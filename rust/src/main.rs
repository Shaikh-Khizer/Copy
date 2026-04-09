// copy v2.2.0 — File/Clipboard/Pipe Utility
// Target: Kali Linux (X11 + Wayland)
//
// Clipboard backend priority:
//   Write: wl-copy → xclip → xsel → ~/.local/share/copy/clipboard (file fallback)
//   Read:  wl-paste → xclip → xsel → ~/.local/share/copy/clipboard (file fallback)
//
// File fallback ensures the tool works in any terminal, SSH session,
// or environment where $DISPLAY is unavailable or xclip cannot connect.

use std::fs::{self, OpenOptions};
use std::io::{self, IsTerminal, Read, Write};
use std::path::PathBuf;
use std::process::{self, Command, Stdio};

use anyhow::{bail, Context, Result};
use clap::{ArgGroup, Parser};

// ── Constants ─────────────────────────────────────────────────────────────────

const VERSION: &str = env!("CARGO_PKG_VERSION");
const DEFAULT_MAX_BYTES: u64 = 100 * 1024 * 1024; // 100 MB

// ── Clipboard file fallback path ──────────────────────────────────────────────

fn clipboard_file_path() -> PathBuf {
    let base = std::env::var("HOME").unwrap_or_else(|_| "/tmp".into());
    let dir = PathBuf::from(base).join(".local/share/copy");
    fs::create_dir_all(&dir).ok();
    dir.join("clipboard")
}

// ── Clipboard backend ─────────────────────────────────────────────────────────

fn clipboard_write(text: &str) -> Result<()> {
    // ── Wayland ──────────────────────────────────────────────────────────────
    if std::env::var("WAYLAND_DISPLAY").is_ok() {
        if try_write_cmd(&["wl-copy"], text) {
            return Ok(());
        }
    }

    // ── X11: xclip ───────────────────────────────────────────────────────────
    if std::env::var("DISPLAY").is_ok() {
        if try_write_cmd(&["xclip", "-selection", "clipboard"], text) {
            return Ok(());
        }
        // ── X11: xsel ────────────────────────────────────────────────────────
        if try_write_cmd(&["xsel", "--clipboard", "--input"], text) {
            return Ok(());
        }
    }

    // ── File fallback (always works, any terminal/SSH) ────────────────────────
    let path = clipboard_file_path();
    fs::write(&path, text)
        .with_context(|| format!("Failed to write clipboard file {}", path.display()))?;
    eprintln!("⚠ No display server — saved to clipboard file (~/.local/share/copy/clipboard)");
    Ok(())
}

fn clipboard_read() -> Result<String> {
    // ── Wayland ──────────────────────────────────────────────────────────────
    if std::env::var("WAYLAND_DISPLAY").is_ok() {
        if let Some(s) = try_read_cmd(&["wl-paste", "--no-newline"]) {
            return Ok(s);
        }
    }

    // ── X11: xclip ───────────────────────────────────────────────────────────
    if std::env::var("DISPLAY").is_ok() {
        if let Some(s) = try_read_cmd(&["xclip", "-selection", "clipboard", "-o"]) {
            return Ok(s);
        }
        // ── X11: xsel ────────────────────────────────────────────────────────
        if let Some(s) = try_read_cmd(&["xsel", "--clipboard", "--output"]) {
            return Ok(s);
        }
    }

    // ── File fallback ─────────────────────────────────────────────────────────
    let path = clipboard_file_path();
    if path.exists() {
        let content = fs::read_to_string(&path)
            .with_context(|| format!("Failed to read clipboard file {}", path.display()))?;
        eprintln!("⚠ No display server — reading from clipboard file (~/.local/share/copy/clipboard)");
        return Ok(content);
    }

    bail!(
        "Clipboard is empty.\n\
         If you expected X11 clipboard, check:\n\
         echo $DISPLAY          (should be :0 or :0.0)\n\
         which xclip            (install: sudo apt install xclip)\n\
         xclip -o               (test xclip directly)"
    )
}

// ── Backend helpers ───────────────────────────────────────────────────────────

fn try_write_cmd(cmd: &[&str], text: &str) -> bool {
    let (prog, args) = match cmd.split_first() {
        Some(x) => x,
        None => return false,
    };
    let child = Command::new(prog)
        .args(args)
        .stdin(Stdio::piped())
        .stderr(Stdio::null())
        .spawn();
    if let Ok(mut child) = child {
        if let Some(mut stdin) = child.stdin.take() {
            let _ = stdin.write_all(text.as_bytes());
        }
        return child.wait().map(|s| s.success()).unwrap_or(false);
    }
    false
}

fn try_read_cmd(cmd: &[&str]) -> Option<String> {
    let (prog, args) = cmd.split_first()?;
    let out = Command::new(prog)
        .args(args)
        .stderr(Stdio::null())
        .output()
        .ok()?;
    if out.status.success() && !out.stdout.is_empty() {
        Some(String::from_utf8_lossy(&out.stdout).into_owned())
    } else {
        None
    }
}

// ── CLI definition ────────────────────────────────────────────────────────────

#[derive(Parser, Debug)]
#[command(
    name = "copy",
    version = VERSION,
    about = "copy v2.2 — File/Clipboard/Pipe Utility (Kali Linux)",
    long_about = "\
copy v2.2 — File/Clipboard/Pipe Utility
========================================
Kali Linux: X11 (xclip/xsel) + Wayland (wl-clipboard)

PIPE TO CLIPBOARD:
  echo 'hello'       | copy
  url -e '<script>'  | copy
  cat /etc/hosts     | copy

PASTE CLIPBOARD:
  copy -p                 → stdout
  copy -p out.txt         → write to file
  copy -p -a out.txt      → append to file

COPY A FILE:
  copy file.txt

DELETE FILE CONTENT:
  copy -d file.txt
  copy -d -f file.txt     (force, no prompt)

EXIT CODES:  0 success  |  1 error  |  2 user cancelled"
)]
#[command(group(ArgGroup::new("mode").args(["paste", "delete"]).multiple(false)))]
struct Args {
    /// File to read from / write to / delete
    file: Option<String>,

    /// Paste clipboard → stdout (or FILE if given)
    #[arg(short = 'p', long)]
    paste: bool,

    /// Delete all content from FILE
    #[arg(short = 'd', long)]
    delete: bool,

    /// Append instead of overwrite
    #[arg(short = 'a', long)]
    append: bool,

    /// Write to stdout instead of clipboard
    #[arg(short = 'o', long)]
    stdout: bool,

    /// Skip all confirmation prompts
    #[arg(short = 'f', long)]
    force: bool,

    /// Strip trailing newline before copying
    #[arg(short = 'n', long)]
    no_newline: bool,

    /// Copy only the first N lines
    #[arg(short = 'l', long, value_name = "N")]
    lines: Option<usize>,

    /// Copy only the last N lines
    #[arg(short = 't', long, value_name = "N")]
    tail: Option<usize>,

    /// Max input size in bytes (default: 100 MB)
    #[arg(short = 'm', long, value_name = "BYTES", default_value_t = DEFAULT_MAX_BYTES)]
    max_size: u64,

    /// Show clipboard contents, no writes (dry run)
    #[arg(long)]
    peek: bool,
}

// ── Entry point ───────────────────────────────────────────────────────────────

fn main() {
    let args = Args::parse();
    match run(args) {
        Ok(code) => process::exit(code),
        Err(e) => {
            eprintln!("✗ {e:#}");
            process::exit(1);
        }
    }
}

fn run(args: Args) -> Result<i32> {
    if args.peek {
        print!("{}", clipboard_read()?);
        return Ok(0);
    }
    if args.delete {
        return delete_file_content(require_file(&args.file, "-d/--delete")?, args.force);
    }
    if args.paste {
        return paste_mode(&args);
    }
    if !io::stdin().is_terminal() {
        return stdin_mode(&args);
    }
    if let Some(ref path) = args.file {
        return copy_file_mode(path, &args);
    }
    eprintln!("No input. Use --help for usage.");
    Ok(1)
}

// ── Modes ─────────────────────────────────────────────────────────────────────

fn paste_mode(args: &Args) -> Result<i32> {
    let text = clipboard_read()?;
    if text.is_empty() {
        println!("Clipboard is empty.");
        return Ok(0);
    }
    if args.file.is_none() || args.stdout {
        print!("{text}");
        return Ok(0);
    }
    let path = args.file.as_deref().unwrap();
    if args.append {
        append_to_path(path, &text)?;
        println!("✓ Appended {} to '{path}'", human_size(text.len() as u64));
    } else {
        if !args.force && !confirm_overwrite(path)? {
            println!("Operation cancelled.");
            return Ok(2);
        }
        fs::write(path, &text).with_context(|| format!("Failed to write '{path}'"))?;
        println!("✓ Pasted {} to '{path}'", human_size(text.len() as u64));
    }
    Ok(0)
}

fn stdin_mode(args: &Args) -> Result<i32> {
    let mut input = read_stdin(args.max_size)?;
    if args.no_newline && input.ends_with('\n') {
        input.pop();
        if input.ends_with('\r') { input.pop(); }
    }
    let content = apply_line_filters(&input, args.lines, args.tail);

    if args.stdout {
        print!("{content}");
        return Ok(0);
    }
    if let Some(ref path) = args.file {
        if args.append {
            append_to_path(path, &content)?;
            println!("✓ Appended {} to '{path}'", human_size(content.len() as u64));
        } else {
            if !args.force && !confirm_overwrite(path)? {
                println!("Operation cancelled.");
                return Ok(2);
            }
            fs::write(path, &content).with_context(|| format!("Failed to write '{path}'"))?;
            println!("✓ Written {} to '{path}'", human_size(content.len() as u64));
        }
        return Ok(0);
    }

    // THE FIX: xclip/wl-copy run as separate processes → clipboard persists after we exit
    clipboard_write(&content)?;
    println!("✓ Copied {} from stdin to clipboard", human_size(content.len() as u64));
    Ok(0)
}

fn copy_file_mode(path: &str, args: &Args) -> Result<i32> {
    let meta = fs::metadata(path).with_context(|| format!("File '{path}' not found"))?;
    if !meta.is_file() { bail!("'{path}' is not a regular file"); }

    let size = meta.len();
    if size == 0 && !args.force {
        if !confirm("File is empty. Copy anyway?", false)? {
            println!("Operation cancelled."); return Ok(2);
        }
    }
    if size > args.max_size && !args.force {
        if !confirm(&format!("File is large ({}). Continue?", human_size(size)), false)? {
            println!("Operation cancelled."); return Ok(2);
        }
    }

    let raw = fs::read_to_string(path).with_context(|| format!("Failed to read '{path}'"))?;
    let content = apply_line_filters(&raw, args.lines, args.tail);

    if args.stdout { print!("{content}"); return Ok(0); }

    clipboard_write(&content)?;
    println!("✓ Copied {} from '{path}' to clipboard", human_size(content.len() as u64));
    Ok(0)
}

fn delete_file_content(path: &str, force: bool) -> Result<i32> {
    let meta = fs::metadata(path).with_context(|| format!("File '{path}' not found"))?;
    if !meta.is_file() { bail!("'{path}' is not a regular file"); }
    let size = meta.len();
    if size == 0 { println!("File '{path}' is already empty."); return Ok(0); }
    if !force {
        println!("Warning: will erase all content in '{path}' ({}).", human_size(size));
        if !confirm("Continue?", false)? { println!("Operation cancelled."); return Ok(2); }
    }
    OpenOptions::new().write(true).truncate(true).open(path)
        .with_context(|| format!("Failed to truncate '{path}'"))?;
    println!("✓ Deleted content of '{path}' (freed {})", human_size(size));
    Ok(0)
}

// ── Helpers ───────────────────────────────────────────────────────────────────

fn read_stdin(max_bytes: u64) -> Result<String> {
    let mut buf = Vec::new();
    io::stdin().read_to_end(&mut buf).context("Failed to read stdin")?;
    if buf.len() as u64 > max_bytes {
        bail!("Input too large ({} > {})", human_size(buf.len() as u64), human_size(max_bytes));
    }
    String::from_utf8(buf).context("stdin is not valid UTF-8")
}

fn apply_line_filters(content: &str, lines: Option<usize>, tail: Option<usize>) -> String {
    if let Some(n) = lines {
        return content.lines().take(n).collect::<Vec<_>>().join("\n");
    }
    if let Some(n) = tail {
        let all: Vec<&str> = content.lines().collect();
        return all[all.len().saturating_sub(n)..].join("\n");
    }
    content.to_owned()
}

fn append_to_path(path: &str, content: &str) -> Result<()> {
    // Strip trailing newlines from content — xclip/wl-paste always add one
    let trimmed = content.trim_end_matches(|c| c == '\n' || c == '\r');

    let mut file = OpenOptions::new().create(true).append(true).open(path)
        .with_context(|| format!("Cannot open '{path}' for appending"))?;

    // Read last byte of existing file to check if it already ends with \n
    // This avoids a blank line between entries regardless of what was written before
    let needs_newline = {
        let existing = fs::metadata(path).map(|m| m.len()).unwrap_or(0);
        if existing == 0 {
            false
        } else {
            use std::io::{Read, Seek, SeekFrom};
            let mut f = fs::File::open(path).context("Cannot read existing file")?;
            f.seek(SeekFrom::End(-1)).ok();
            let mut last = [0u8; 1];
            f.read_exact(&mut last).ok();
            last[0] != b'\n'
        }
    };

    if needs_newline { file.write_all(b"\n").context("Write error")?; }
    file.write_all(trimmed.as_bytes()).with_context(|| format!("Cannot append to '{path}'"))
}

fn confirm(prompt: &str, default_yes: bool) -> Result<bool> {
    print!("{prompt} {}: ", if default_yes { "[Y/n]" } else { "[y/N]" });
    io::stdout().flush()?;
    let mut line = String::new();
    io::stdin().read_line(&mut line)?;
    Ok(match line.trim().to_lowercase().as_str() {
        "" => default_yes,
        s if s.starts_with('y') => true,
        _ => false,
    })
}

fn confirm_overwrite(path: &str) -> Result<bool> {
    match fs::metadata(path) {
        Err(_) => Ok(true),
        Ok(m) if m.len() == 0 => Ok(true),
        Ok(m) => {
            println!("Warning: '{path}' already exists ({}).", human_size(m.len()));
            confirm("Overwrite?", false)
        }
    }
}

fn require_file<'a>(file: &'a Option<String>, flag: &str) -> Result<&'a str> {
    file.as_deref().ok_or_else(|| anyhow::anyhow!("{flag} requires a FILE argument"))
}

fn human_size(bytes: u64) -> String {
    const UNITS: &[&str] = &["B", "KB", "MB", "GB", "TB"];
    let mut size = bytes as f64;
    let mut unit = 0;
    while size >= 1024.0 && unit < UNITS.len() - 1 { size /= 1024.0; unit += 1; }
    format!("{size:.1} {}", UNITS[unit])
}

// ── Tests ─────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn human_size_units() {
        assert_eq!(human_size(0),                  "0.0 B");
        assert_eq!(human_size(1024),               "1.0 KB");
        assert_eq!(human_size(1024 * 1024),        "1.0 MB");
        assert_eq!(human_size(1024 * 1024 * 1024), "1.0 GB");
    }

    #[test]
    fn human_size_no_aliasing() {
        let a = human_size(1024);
        let b = human_size(1024 * 1024);
        assert_eq!(a, "1.0 KB");
        assert_eq!(b, "1.0 MB");
    }

    #[test]
    fn line_filter_head()     { assert_eq!(apply_line_filters("a\nb\nc\nd", Some(2), None), "a\nb"); }
    #[test]
    fn line_filter_tail()     { assert_eq!(apply_line_filters("a\nb\nc\nd", None, Some(2)), "c\nd"); }
    #[test]
    fn line_filter_none()     { assert_eq!(apply_line_filters("hello\nworld", None, None), "hello\nworld"); }
    #[test]
    fn line_filter_overflow() { assert_eq!(apply_line_filters("a\nb", None, Some(99)), "a\nb"); }
}
