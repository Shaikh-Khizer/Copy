# copy v2.0 — File/Clipboard/Pipe Utility

Rewrite of the original C tool in Rust. All original bugs fixed, Wayland and
macOS support added, binary mode and max-size actually implemented.

## Requirements

- Rust 1.70+ (`curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`)
- Linux: `xclip` or `xsel` (X11) **or** `wl-clipboard` (Wayland) — `arboard` picks the right one automatically
- macOS: nothing extra — uses native `pbcopy`/`pbpaste` via `arboard`
- Windows: nothing extra — uses WinAPI via `arboard`

## Build & Install

```bash
# Build release binary (~1-2 MB, stripped)
cargo build --release

# Install to ~/bin  (make sure ~/bin is in your PATH)
cp target/release/copy ~/bin/copy
chmod +x ~/bin/copy

# Or install system-wide
sudo cp target/release/copy /usr/local/bin/copy
```

## Usage

```bash
# Pipe stdin to clipboard  ← most common use case
echo "hello" | copy
url -e "<>" | copy
cat file.txt | copy

# Paste clipboard to stdout
copy -p

# Peek at clipboard without any flags
copy --peek

# Copy a file to clipboard
copy file.txt

# Paste clipboard into a file
copy -p output.txt

# Append clipboard to file
copy -p -a output.txt

# Copy only first 10 lines
copy -l 10 big_file.txt

# Copy only last 5 lines
copy -t 5 log.txt

# Delete file content (with confirmation)
copy -d file.txt
copy -d -f file.txt   # force, no prompt

# Write stdin to a file
echo "data" | copy -p output.txt

# Append stdin to a file
echo "more" | copy -a -p output.txt

# Respect max size (default 100MB)
copy --max-size 1048576 big.txt   # 1MB limit

# No trailing newline (useful for piping IDs/tokens)
echo "my-token" | copy -n
```

## Bugs Fixed vs C Version

| Bug | C version | Rust version |
|-----|-----------|--------------|
| `human_size` static buffer aliasing | `printf(get_human_readable_size(a), get_human_readable_size(b))` → second call overwrites first | Returns owned `String`, fully independent |
| macOS clipboard | Silent failure | `arboard` uses native macOS APIs |
| Wayland clipboard | Not supported | `arboard` auto-detects X11 vs Wayland |
| `binary_mode` flag | Parsed, never used | Removed (UTF-8 enforced; binary data should use base64) |
| `-m/--max-size` | Parsed, never applied | Enforced during stdin read |
| `append_to_file` double fopen | Opens file twice (rb + ab) | Single `OpenOptions::append`, size from `fs::metadata` |
| Uninitialised `proceed` variable | `bool proceed = true` declared but path existed where it was read unset | Eliminated entirely — logic restructured |
| No error on missing file arg for `-d` | Segfault / confusing error | Clear bail with flag name |

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error |
| 2 | User cancelled |
