#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#ifdef _WIN32
    #include <windows.h>
    #include <conio.h>  // For getch() on Windows
    #define PATH_SEPARATOR '\\'
    #define IS_WINDOWS 1
#else
    #include <sys/wait.h>
    #include <dirent.h>
    #include <termios.h>  // For terminal control
    #define PATH_SEPARATOR '/'
    #define IS_WINDOWS 0
#endif

// Configuration
#define MAX_PATH_LENGTH 4096
#define MAX_FILE_SIZE (100 * 1024 * 1024) // 100MB limit
#define BUFFER_SIZE 65536
#define VERSION "1.1.0"

// Function prototypes
void print_help();
bool get_user_confirmation(const char *prompt, bool default_no);
char* get_human_readable_size(off_t bytes);

// Clipboard functions (platform-specific)
#ifdef _WIN32
bool copy_to_clipboard_win(const char *text) {
    if (!OpenClipboard(NULL)) {
        fprintf(stderr, "Failed to open clipboard\n");
        return false;
    }
    
    EmptyClipboard();
    
    size_t len = strlen(text) + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    
    char *ptr = (char*)GlobalLock(hMem);
    strcpy(ptr, text);
    GlobalUnlock(hMem);
    
    SetClipboardData(CF_TEXT, hMem);
    CloseClipboard();
    GlobalFree(hMem);
    
    return true;
}

char* paste_from_clipboard_win() {
    if (!OpenClipboard(NULL)) {
        fprintf(stderr, "Failed to open clipboard\n");
        return NULL;
    }
    
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) {
        CloseClipboard();
        return NULL;
    }
    
    char *ptr = (char*)GlobalLock(hData);
    if (!ptr) {
        CloseClipboard();
        return NULL;
    }
    
    size_t len = strlen(ptr);
    char *result = malloc(len + 1);
    if (result) {
        strcpy(result, ptr);
    }
    
    GlobalUnlock(hData);
    CloseClipboard();
    
    return result;
}
#else
bool copy_to_clipboard_unix(const char *text) {
    // Try xclip first
    FILE *proc = popen("xclip -selection clipboard 2>/dev/null", "w");
    if (proc) {
        fwrite(text, 1, strlen(text), proc);
        int status = pclose(proc);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return true;
        }
    }
    
    // Try xsel as fallback
    proc = popen("xsel --clipboard --input 2>/dev/null", "w");
    if (proc) {
        fwrite(text, 1, strlen(text), proc);
        int status = pclose(proc);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return true;
        }
    }
    
    return false;
}

char* paste_from_clipboard_unix() {
    char *result = NULL;
    size_t size = 0;
    FILE *proc;
    
    // Try xclip first
    proc = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (!proc) {
        // Try xsel as fallback
        proc = popen("xsel --clipboard --output 2>/dev/null", "r");
    }
    
    if (proc) {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), proc)) {
            size_t len = strlen(buffer);
            result = realloc(result, size + len + 1);
            if (!result) {
                pclose(proc);
                return NULL;
            }
            strcpy(result + size, buffer);
            size += len;
        }
        pclose(proc);
    }
    
    return result;
}
#endif

// Cross-platform clipboard wrappers
bool copy_to_clipboard(const char *text) {
    if (!text) return false;
    
#ifdef _WIN32
    return copy_to_clipboard_win(text);
#else
    return copy_to_clipboard_unix(text);
#endif
}

char* paste_from_clipboard() {
#ifdef _WIN32
    return paste_from_clipboard_win();
#else
    return paste_from_clipboard_unix();
#endif
}

// User confirmation utility
bool get_user_confirmation(const char *prompt, bool default_no) {
    printf("%s [%s/%s]: ", prompt, default_no ? "y" : "Y", default_no ? "N" : "n");
    fflush(stdout);
    
    char response[256];
    if (!fgets(response, sizeof(response), stdin)) {
        return false;  // EOF or error
    }
    
    // Trim newline
    response[strcspn(response, "\n")] = 0;
    
    if (strlen(response) == 0) {
        // User just pressed Enter - use default
        return !default_no;
    }
    
    // Check for yes/no
    char first_char = tolower(response[0]);
    return (first_char == 'y');
}

// File utilities
bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

bool is_regular_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

bool is_file_empty(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return st.st_size == 0;
}

off_t get_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return st.st_size;
}

char* get_human_readable_size(off_t bytes) {
    static char buffer[32];
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = bytes;
    
    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    
    snprintf(buffer, sizeof(buffer), "%.1f %s", size, units[unit]);
    return buffer;
}

char* read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Error opening file '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size > MAX_FILE_SIZE) {
        fprintf(stderr, "File too large: %s (max: %s)\n", 
                get_human_readable_size(size), 
                get_human_readable_size(MAX_FILE_SIZE));
        fclose(file);
        return NULL;
    }
    
    char *content = malloc(size + 1);
    if (!content) {
        fclose(file);
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }
    
    size_t bytes_read = fread(content, 1, size, file);
    content[bytes_read] = '\0';
    fclose(file);
    
    return content;
}

bool write_to_file(const char *path, const char *content, bool overwrite, bool force) {
    bool file_exists_already = file_exists(path);
    
    if (file_exists_already && !overwrite && !force) {
        off_t size = get_file_size(path);
        if (size == 0) {
            printf("Note: File '%s' exists but is empty. Overwriting.\n", path);
        } else {
            printf("Warning: File '%s' already exists (%s).\n", 
                   path, get_human_readable_size(size));
            if (!get_user_confirmation("Do you want to overwrite it?", true)) {
                printf("Operation cancelled.\n");
                return false;
            }
        }
    }
    
    // Create directory if it doesn't exist
    char dir_path[MAX_PATH_LENGTH];
    strncpy(dir_path, path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    
    // Extract directory
    char *last_sep = strrchr(dir_path, PATH_SEPARATOR);
    if (last_sep) {
        *last_sep = '\0';
        
        // Create directory if needed
#ifdef _WIN32
        if (strlen(dir_path) > 0) {
            CreateDirectoryA(dir_path, NULL);
        }
#else
        if (strlen(dir_path) > 0) {
            mkdir(dir_path, 0755);
        }
#endif
    }
    
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "Error creating file '%s': %s\n", path, strerror(errno));
        return false;
    }
    
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, file);
    fclose(file);
    
    return written == len;
}

bool append_to_file(const char *path, const char *content) {
    FILE *file = fopen(path, "ab");
    if (!file) {
        fprintf(stderr, "Error opening file '%s': %s\n", path, strerror(errno));
        return false;
    }
    
    // Check if file already has content (size > 0)
    bool file_has_content = false;
    long current_size = 0;
    
    // Get current file size
    FILE *temp = fopen(path, "rb");
    if (temp) {
        fseek(temp, 0, SEEK_END);
        current_size = ftell(temp);
        fclose(temp);
        file_has_content = (current_size > 0);
    }
    
    size_t total_written = 0;
    
    // If file has content, add a newline first
    if (file_has_content) {
        const char *newline = "\n";
        size_t written = fwrite(newline, 1, 1, file);
        if (written != 1) {
            fclose(file);
            return false;
        }
        total_written += written;
    }
    
    // Now append the actual content
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, file);
    total_written += written;
    
    fclose(file);
    
    return written == len;
}

bool delete_file_content(const char *path, bool force) {
    if (!file_exists(path)) {
        fprintf(stderr, "File '%s' does not exist\n", path);
        return false;
    }
    
    if (!is_regular_file(path)) {
        fprintf(stderr, "'%s' is not a regular file\n", path);
        return false;
    }
    
    off_t size = get_file_size(path);
    if (size == 0) {
        printf("File '%s' is already empty.\n", path);
        return true;
    }
    
    if (!force) {
        printf("Warning: This will delete all content from '%s' (%s).\n", 
               path, get_human_readable_size(size));
        if (!get_user_confirmation("Do you want to continue?", true)) {
            printf("Operation cancelled.\n");
            return false;
        }
    }
    
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "Error opening file: %s\n", strerror(errno));
        return false;
    }
    fclose(file);
    
    printf("All content successfully deleted from '%s'\n", path);
    printf("Bytes freed: %s\n", get_human_readable_size(size));
    return true;
}

// Pipe/STDIN handling
char* read_from_stdin() {
    char *buffer = NULL;
    size_t size = 0;
    size_t capacity = 0;
    char chunk[BUFFER_SIZE];
    
    while (!feof(stdin)) {
        size_t bytes = fread(chunk, 1, BUFFER_SIZE, stdin);
        if (bytes > 0) {
            if (size + bytes + 1 > capacity) {
                capacity = (size + bytes + 1) * 2;
                buffer = realloc(buffer, capacity);
                if (!buffer) {
                    fprintf(stderr, "Memory allocation failed\n");
                    return NULL;
                }
            }
            memcpy(buffer + size, chunk, bytes);
            size += bytes;
        }
    }
    
    if (buffer) {
        buffer[size] = '\0';
    }
    
    return buffer;
}

// String utilities
char* trim_whitespace(char *str) {
    if (!str) return NULL;
    
    // Trim leading space
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    // Trim trailing space
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    *(end + 1) = 0;
    return str;
}

// Help text
void print_help() {
    printf("Copy v%s - File/Clipboard/Pipe Utility\n", VERSION);
    printf("===========================================\n\n");
    printf("Usage: copy [OPTIONS] [FILE]\n\n");
    printf("Operations:\n");
    printf("  -c, --copy           Copy file/content to clipboard (default)\n");
    printf("  -p, --paste          Paste clipboard to file (or stdout if no file)\n");
    printf("  -d, --delete         Delete file content\n");
    printf("  -a, --append         Append to file instead of overwriting\n");
    printf("  -s, --stdin          Read from stdin (pipe)\n");
    printf("  -o, --stdout         Output to stdout\n");
    printf("  -v, --version        Show version\n\n");
    
    printf("Options:\n");
    printf("  -f, --force          Force operation without confirmation\n");
    printf("  -n, --no-newline     Don't add newline when reading from stdin\n");
    printf("  -b, --binary         Treat content as binary (preserve newlines)\n");
    printf("  -l, --lines N        Copy only first N lines\n");
    printf("  -t, --tail N         Copy only last N lines\n");
    printf("  -m, --max-size N     Maximum size in bytes (default: 100MB)\n\n");
    
    printf("Exit Codes:\n");
    printf("  0 - Success\n");
    printf("  1 - Error\n");
    printf("  2 - User cancelled\n");
}

// New features
char* get_first_n_lines(const char *content, int n) {
    if (n <= 0 || !content) return NULL;
    
    char *result = malloc(strlen(content) + 1);
    if (!result) return NULL;
    
    char *dest = result;
    const char *src = content;
    int lines = 0;
    
    while (*src && lines < n) {
        *dest = *src;
        if (*src == '\n') lines++;
        dest++;
        src++;
    }
    *dest = '\0';
    
    // Trim if we stopped in the middle of a line
    if (lines == n && *(dest-1) != '\n') {
        // Find the last newline
        char *last_newline = strrchr(result, '\n');
        if (last_newline) {
            *(last_newline + 1) = '\0';
        }
    }
    
    return result;
}

char* get_last_n_lines(const char *content, int n) {
    if (n <= 0 || !content) return NULL;
    
    const char *end = content + strlen(content);
    const char *start = end;
    int lines = 0;
    
    // Go backwards to find start of last n lines
    while (start > content) {
        start--;
        if (*start == '\n') {
            lines++;
            if (lines == n) {
                start++; // Move past newline
                break;
            }
        }
    }
    
    // If we didn't find enough newlines, start from beginning
    if (lines < n) {
        start = content;
    }
    
    char *result = malloc(end - start + 1);
    if (!result) return NULL;
    
    strcpy(result, start);
    return result;
}

// Main function with improved error handling
int main(int argc, char *argv[]) {
    bool copy_mode = true;      // Default: copy to clipboard
    bool paste_mode = false;
    bool delete_mode = false;
    bool append_mode = false;
    bool stdin_mode = false;
    bool stdout_mode = false;
    bool force_mode = false;
    bool no_newline = false;
    bool binary_mode = false;
    int lines_limit = 0;
    int tail_lines = 0;
    const char *filename = NULL;
    
    // Check if running in interactive mode
    bool is_interactive = isatty(fileno(stdin));
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--copy") == 0) {
                copy_mode = true;
                paste_mode = false;
                delete_mode = false;
            } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--paste") == 0) {
                paste_mode = true;
                copy_mode = false;
                delete_mode = false;
            } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--delete") == 0) {
                delete_mode = true;
                copy_mode = false;
                paste_mode = false;
            } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--append") == 0) {
                append_mode = true;
            } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--stdin") == 0) {
                stdin_mode = true;
            } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--stdout") == 0) {
                stdout_mode = true;
            } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
                force_mode = true;
            } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--no-newline") == 0) {
                no_newline = true;
            } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--binary") == 0) {
                binary_mode = true;
            } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
                printf("Copy v%s\n", VERSION);
                return 0;
            } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                print_help();
                return 0;
            } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--lines") == 0) {
                if (i + 1 < argc) {
                    lines_limit = atoi(argv[++i]);
                }
            } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--tail") == 0) {
                if (i + 1 < argc) {
                    tail_lines = atoi(argv[++i]);
                }
            } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--max-size") == 0) {
                if (i + 1 < argc) {
                    // Could implement dynamic max size here
                    i++;
                }
            }
        } else if (!filename) {
            filename = argv[i];
        }
    }
    
    // Handle pipe/STDIN input
    if (stdin_mode || (!is_interactive && argc == 1)) {
        char *input = read_from_stdin();
        if (!input) {
            fprintf(stderr, "Failed to read from stdin\n");
            return 1;
        }
        
        if (no_newline) {
            char *trimmed = trim_whitespace(input);
            // Remove trailing newlines
            size_t len = strlen(trimmed);
            while (len > 0 && (trimmed[len-1] == '\n' || trimmed[len-1] == '\r')) {
                trimmed[--len] = '\0';
            }
        }
        
        if (stdout_mode) {
            printf("%s", input);
            free(input);
            return 0;
        } else if (filename && paste_mode) {
            // Check if file exists and needs confirmation
            bool file_exists_already = file_exists(filename);
            bool proceed = true;
            
            if (file_exists_already && !append_mode && !force_mode) {
                off_t size = get_file_size(filename);
                if (size == 0) {
                    printf("Note: File '%s' exists but is empty. Proceeding.\n", filename);
                } else {
                    printf("Warning: File '%s' already exists (%s).\n", 
                           filename, get_human_readable_size(size));
                    if (!get_user_confirmation("Do you want to overwrite it?", true)) {
                        printf("Operation cancelled.\n");
                        free(input);
                        return 2;  // User cancelled
                    }
                }
            }
            
            // Write stdin to file
            if (append_mode) {
                if (append_to_file(filename, input)) {
                    printf("Appended %ld bytes to '%s'\n", (long)strlen(input), filename);
                    free(input);
                    return 0;
                } else {
                    free(input);
                    return 1;
                }
            } else {
                if (write_to_file(filename, input, false, force_mode)) {
                    printf("Written %ld bytes to '%s'\n", (long)strlen(input), filename);
                    free(input);
                    return 0;
                } else {
                    free(input);
                    return 1;
                }
            }
        } else {
            // Copy stdin to clipboard
            if (copy_to_clipboard(input)) {
                printf("✓ Copied %ld characters from stdin to clipboard\n", 
                       (long)strlen(input));
                free(input);
                return 0;
            } else {
                fprintf(stderr, "✗ Failed to copy to clipboard\n");
                free(input);
                return 1;
            }
        }
    }
    
    // Handle delete mode
    if (delete_mode) {
        if (!filename) {
            fprintf(stderr, "Error: File name required for delete operation\n");
            return 1;
        }
        return delete_file_content(filename, force_mode) ? 0 : 1;
    }
    
    // Handle paste mode
    if (paste_mode) {
        // Get clipboard content
        char *clipboard = paste_from_clipboard();
        if (!clipboard) {
            fprintf(stderr, "Clipboard is empty or inaccessible\n");
            return 1;
        }
        
        size_t clip_len = strlen(clipboard);
        if (clip_len == 0) {
            printf("Clipboard is empty. Nothing to paste.\n");
            free(clipboard);
            return 0;
        }
        
        // If no filename is provided OR stdout mode is enabled, output to stdout
        if (!filename || stdout_mode) {
            printf("%s", clipboard);
            free(clipboard);
            return 0;
        }
        
        // Otherwise, paste to file
        // Check if file exists and needs confirmation
        bool file_exists_already = file_exists(filename);
        bool proceed = true;
        
        if (file_exists_already && !append_mode && !force_mode) {
            off_t size = get_file_size(filename);
            if (size == 0) {
                printf("Note: File '%s' exists but is empty. Proceeding.\n", filename);
            } else {
                printf("Warning: File '%s' already exists (%s).\n", 
                       filename, get_human_readable_size(size));
                if (!get_user_confirmation("Do you want to overwrite it?", true)) {
                    printf("Operation cancelled.\n");
                    free(clipboard);
                    return 2;  // User cancelled
                }
            }
        }
        
        if (append_mode) {
            if (append_to_file(filename, clipboard)) {
                printf("✓ Appended %ld bytes from clipboard to '%s'\n", 
                       (long)clip_len, filename);
                free(clipboard);
                return 0;
            } else {
                free(clipboard);
                return 1;
            }
        } else {
            if (write_to_file(filename, clipboard, false, force_mode)) {
                printf("✓ Pasted %ld bytes from clipboard to '%s'\n", 
                       (long)clip_len, filename);
                free(clipboard);
                return 0;
            } else {
                free(clipboard);
                return 1;
            }
        }
    }
    
    // Handle copy mode (default)
    if (copy_mode) {
        if (!filename) {
            // If no filename but we have stdin data, process it
            if (!is_interactive) {
                char *input = read_from_stdin();
                if (input) {
                    bool success = copy_to_clipboard(input);
                    if (success) {
                        printf("✓ Copied %ld characters from stdin to clipboard\n", 
                               (long)strlen(input));
                    } else {
                        fprintf(stderr, "✗ Failed to copy to clipboard\n");
                    }
                    free(input);
                    return success ? 0 : 1;
                }
            }
            
            fprintf(stderr, "Error: File name or input required for copy operation\n");
            fprintf(stderr, "Use 'copycat -h' for help\n");
            return 1;
        }
        
        if (!file_exists(filename)) {
            fprintf(stderr, "Error: File '%s' does not exist\n", filename);
            return 1;
        }
        
        if (!is_regular_file(filename)) {
            fprintf(stderr, "Error: '%s' is not a regular file\n", filename);
            return 1;
        }
        
        // Check file size for large files
        off_t file_size = get_file_size(filename);
        if (file_size == 0 && !force_mode) {
            printf("Warning: File '%s' is empty.\n", filename);
            if (!get_user_confirmation("Do you want to copy empty content?", true)) {
                printf("Operation cancelled.\n");
                return 2;  // User cancelled
            }
        } else if (file_size > MAX_FILE_SIZE && !force_mode) {
            printf("Warning: File is large (%s).\n", get_human_readable_size(file_size));
            if (!get_user_confirmation("Do you want to continue?", true)) {
                printf("Operation cancelled.\n");
                return 2;  // User cancelled
            }
        }
        
        char *content = read_file(filename);
        if (!content) {
            return 1;
        }
        
        // Apply line limits if specified
        char *processed_content = content;
        if (lines_limit > 0) {
            char *limited = get_first_n_lines(content, lines_limit);
            if (limited) {
                processed_content = limited;
                free(content);
                content = processed_content;
            }
        } else if (tail_lines > 0) {
            char *limited = get_last_n_lines(content, tail_lines);
            if (limited) {
                processed_content = limited;
                free(content);
                content = processed_content;
            }
        }
        
        if (stdout_mode) {
            printf("%s", processed_content);
            free(processed_content);
            return 0;
        } else {
            if (copy_to_clipboard(processed_content)) {
                printf("✓ Copied %ld characters from '%s' to clipboard\n", 
                       (long)strlen(processed_content), filename);
                free(processed_content);
                return 0;
            } else {
                fprintf(stderr, "✗ Failed to copy to clipboard\n");
                fprintf(stderr, "You may need to install clipboard utilities:\n");
#ifdef _WIN32
                fprintf(stderr, "  Windows: Built-in clipboard should work\n");
#elif __APPLE__
                fprintf(stderr, "  macOS: Built-in clipboard should work\n");
#else
                fprintf(stderr, "  Linux: Install 'xclip' or 'xsel':\n");
                fprintf(stderr, "    sudo apt-get install xclip\n");
                fprintf(stderr, "    sudo apt-get install xsel\n");
#endif
                free(processed_content);
                return 1;
            }
        }
    }
    
    // Default: show help
    print_help();
    return 0;
}