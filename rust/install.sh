#!/usr/bin/env bash
# install.sh — build and install copy tool
set -e

echo "── Building copy v2.0 (release) ──"
cargo build --release

BINARY="target/release/copy"
INSTALL_DIR="${1:-$HOME/bin}"

if [ ! -d "$INSTALL_DIR" ]; then
    mkdir -p "$INSTALL_DIR"
    echo "Created $INSTALL_DIR"
fi

cp "$BINARY" "$INSTALL_DIR/copy"
chmod +x "$INSTALL_DIR/copy"

echo "✓ Installed to $INSTALL_DIR/copy"
echo ""

# Check if install dir is in PATH
if [[ ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
    echo "⚠ $INSTALL_DIR is not in your PATH."
    echo "  Add this to your ~/.bashrc or ~/.zshrc:"
    echo "    export PATH=\"\$HOME/bin:\$PATH\""
fi

echo ""
echo "Test it:"
echo "  echo 'hello world' | copy"
echo "  copy -p"
