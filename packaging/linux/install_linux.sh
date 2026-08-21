#!/usr/bin/env bash
set -euo pipefail

package_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
install_bin_dir="$HOME/.local/bin"
install_icon_dir="$HOME/.local/share/icons/hicolor/512x512/apps"
install_desktop_dir="$HOME/.local/share/applications"

mkdir -p "$install_bin_dir" "$install_icon_dir" "$install_desktop_dir"

install -m 0755 "$package_dir/bin/CrossTerm" "$install_bin_dir/CrossTerm"
install -m 0644 "$package_dir/share/icons/hicolor/512x512/apps/crossterm.png" "$install_icon_dir/crossterm.png"

cat > "$install_desktop_dir/crossterm.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=CrossTerm
Comment=Cross-platform terminal and SSH connection manager
Exec=$install_bin_dir/CrossTerm
Icon=crossterm
Terminal=false
Categories=System;TerminalEmulator;
StartupWMClass=CrossTerm
EOF

chmod 0644 "$install_desktop_dir/crossterm.desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$install_desktop_dir" >/dev/null 2>&1 || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q "$HOME/.local/share/icons/hicolor" >/dev/null 2>&1 || true
fi

echo "CrossTerm installed to $install_bin_dir/CrossTerm"
echo "Application launcher installed to $install_desktop_dir/crossterm.desktop"