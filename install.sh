#!/bin/sh
# Installs the latest slkx release from GitHub.
#
# Usage:
#   curl --proto '=https' --tlsv1.2 -sSf \
#     https://raw.githubusercontent.com/Thinato/slkx/main/install.sh | sh
#
# Environment overrides:
#   SLKX_INSTALL_DIR  install location (default: $HOME/.local/bin)
#   SLKX_VERSION      release tag to fetch (default: latest, e.g. v0.1.0-beta)
#   CLAUDE_CONFIG_DIR Claude Code config dir for the agent skill (default: $HOME/.claude)

set -eu

REPO="Thinato/slkx"
INSTALL_DIR="${SLKX_INSTALL_DIR:-$HOME/.local/bin}"
VERSION="${SLKX_VERSION:-latest}"

err() {
    printf 'error: %s\n' "$1" >&2
    exit 1
}

# No wget fallback: slkx itself needs libcurl, so curl is there in practice.
for cmd in uname tar mktemp curl; do
    command -v "$cmd" >/dev/null 2>&1 || err "missing required command: $cmd"
done

os="$(uname -s)"
arch="$(uname -m)"
case "$os/$arch" in
    Linux/x86_64 | Linux/amd64) target="linux-x86_64" ;;
    Darwin/arm64 | Darwin/aarch64) target="macos-arm64" ;;
    *) err "no prebuilt binary for $os $arch -- build from source, see https://github.com/$REPO#build" ;;
esac

# Asset names carry no version, so "latest" is GitHub's own redirect and needs
# no API call.
if [ "$VERSION" = "latest" ]; then
    url="https://github.com/$REPO/releases/latest/download/slkx-$target.tar.gz"
else
    url="https://github.com/$REPO/releases/download/$VERSION/slkx-$target.tar.gz"
fi

echo "downloading $url"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM HUP

curl --proto '=https' --tlsv1.2 -fsSL "$url" -o "$tmp/slkx.tar.gz" ||
    err "download failed (is $VERSION a published release?)"
tar -C "$tmp" -xzf "$tmp/slkx.tar.gz"

mkdir -p "$INSTALL_DIR"
mv "$tmp/slkx" "$INSTALL_DIR/slkx"
chmod +x "$INSTALL_DIR/slkx"
echo "installed: $INSTALL_DIR/slkx"

# The agent skill tells Claude Code that slkx exists and when to use it. Only
# installed where Claude Code is; releases before v0.1.1-beta do not ship it.
claude_dir="${CLAUDE_CONFIG_DIR:-$HOME/.claude}"
if [ -d "$claude_dir" ] && [ -f "$tmp/skills/slkx/SKILL.md" ]; then
    mkdir -p "$claude_dir/skills/slkx"
    cp "$tmp/skills/slkx/SKILL.md" "$claude_dir/skills/slkx/SKILL.md"
    echo "installed: $claude_dir/skills/slkx/SKILL.md"
fi

# The Linux binary links the system libcurl and OpenSSL; a missing one only
# shows up when it runs.
if ! "$INSTALL_DIR/slkx" help >/dev/null 2>&1; then
    echo "warning: slkx is installed but does not run. On Linux it needs"
    echo "libcurl4 and libssl3 (Ubuntu 22.04+, Debian 12+)."
fi

case ":${PATH:-}:" in
    *":$INSTALL_DIR:"*) ;;
    *)
        echo
        echo "warning: $INSTALL_DIR is not on your PATH. Add this to your shell rc:"
        echo "  export PATH=\"$INSTALL_DIR:\$PATH\""
        ;;
esac
