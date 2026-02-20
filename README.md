# Copy v1.1.0
### File / Clipboard  (Written in C)

A lightweight and powerful CLI utility written in C for copying, pasting, deleting, and manipulating file or clipboard content directly from the terminal.

---

## 📌 Features

- Copy file content to clipboard
- Paste clipboard content to file or stdout
- Append or overwrite files
- Read from stdin (pipe support)
- Binary mode support
- Copy first N lines or last N lines
- Set maximum file size limit
- Safe operations with confirmation
- Clean exit codes

---

## 📦 Installation & Compilation Guide

### 1️⃣ Requirements

- GCC compiler
- Linux (X11 / Wayland clipboard support depending on implementation)
- Standard C libraries

## Options :
| Option          | Description                                    |
| --------------- | ---------------------------------------------- |
| `-c, --copy`    | Copy file/content to clipboard (default)       |
| `-p, --paste`   | Paste clipboard to file (or stdout if no file) |
| `-d, --delete`  | Delete file content                            |
| `-a, --append`  | Append to file instead of overwriting          |
| `-s, --stdin`   | Read from stdin (pipe input)                   |
| `-o, --stdout`  | Output to stdout                               |
| `-v, --version` | Show version                                   |

## Additional Options:
| Option             | Description                                 |
| ------------------ | ------------------------------------------- |
| `-f, --force`      | Force operation without confirmation        |
| `-n, --no-newline` | Don’t add newline when reading from stdin   |
| `-b, --binary`     | Treat content as binary (preserve newlines) |
| `-l, --lines N`    | Copy only first N lines                     |
| `-t, --tail N`     | Copy only last N lines                      |
| `-m, --max-size N` | Maximum size in bytes (default: 100MB)      |

## Installation process: 
```bash
git clone https://github.com/Shaikh-Khizer/Copy.git
```

### Check GCC version:

```bash
gcc --version
```

### Compilation
```bash
gcc -o copy main.c
```
### Move to gloal path (optional):
```bash
sudo mv copy /usr/local/bin/
copy -h
```


