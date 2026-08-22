# CrossTerm 0.5.0

CrossTerm is a Qt 6 desktop terminal and connection manager for Windows and Linux, built with C++20 and CMake. It provides saved connection profiles, tabbed sessions, local shells, SSH sessions, session logging, and ZModem file transfers over SSH.

The current Windows package is the primary tested target.

## Highlights

- Local shell sessions and SSH sessions backed by Windows ConPTY
- Tabbed terminal workspace with a dockable session tree
- Saved session profiles for local shell and SSH connections
- SSH host, port, username, and private-key configuration
- Per-profile fonts, terminal settings, appearance options, and session logging
- ZModem uploads and downloads over SSH using `rz` and `sz`
- Per-profile ZModem download folder for automatic `sz` downloads
- Windows release packaging with Qt runtime deployment

## Quick start on Windows

1. Download or build the package.
2. Run `dist\windows\bin\CrossTerm.exe`.
3. Create or load an SSH profile under **Session**.
4. Connect and use the terminal as usual.

SSH sessions use the Windows OpenSSH client. Ensure `ssh.exe` is available on `PATH`. For key authentication, configure the private-key path in the profile. Password authentication and first-time host-key confirmation must be completed through your SSH configuration before a non-interactive transfer begins.

## ZModem transfers over SSH

CrossTerm packages Windows builds of the `lrzsz` `sz` and `rz` helpers alongside the application. The transfer stream uses a separate raw SSH process so binary protocol data does not pass through the interactive terminal.

### Download from the remote host

Set **ZModem download folder** in the session profile. Then, on the remote shell, run:

```bash
sz filename.zip
```

CrossTerm saves the received file directly to the configured folder using the remote filename.

### Upload to the remote host

On the remote shell, run:

```bash
rz
```

CrossTerm opens a local-file picker, then transfers the selected file to the remote session's working directory.

For binary-transfer verification, compare SHA-256 hashes after a transfer:

```bash
sha256sum filename.zip
```

```powershell
Get-FileHash "C:\path\to\filename.zip" -Algorithm SHA256
```

## Build from source

### Windows

Install Qt 6, CMake, Ninja, and MSVC 2022. The build script locates the supported toolchain, builds a Release configuration, builds `sz.exe` and `rz.exe`, and runs `windeployqt`.

```bat
build_windows.bat
dist\windows\bin\CrossTerm.exe
```

Use the executable in `dist\windows\bin`, not the intermediate binary in `build-windows`.

The build also creates `dist\CrossTerm-0.5.0-windows-x64.zip`. Distribute that ZIP rather than `CrossTerm.exe` alone: it includes the Qt libraries and plugins, app-local Visual C++ runtime DLLs, ZModem helpers, and the project license. Users can extract the ZIP and run `CrossTerm.exe` without installing Qt or Visual Studio runtime dependencies.

### Windows installer

For a conventional Windows installation, install [Inno Setup 6](https://jrsoftware.org/isdl.php) on the build machine, then run:

```bat
build_installer.bat
```

This creates `dist\installer\CrossTerm-0.5.0-windows-x64-setup.exe`. The installer places the complete deployed package in `C:\Program Files\CrossTerm`, creates a Start Menu shortcut, and offers a desktop shortcut. End users do not need Qt or Visual Studio installed.

### Linux

Install the Qt 6 development packages, CMake, and a C++20 compiler. On Fedora:

```bash
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel
chmod +x build_linux.sh
./build_linux.sh
./dist/linux/bin/CrossTerm
```

## Project layout

```text
src/
    connections/  Local shell, SSH, and Windows ConPTY implementations
    dialogs/      Session profile editor
    profiles/     Saved connection-profile storage
    terminal/     ANSI/VT parser and screen model
    transfer/     ZModem transfer orchestration and codec foundation
    widgets/      Terminal UI
third_party/
    lrzsz/        Vendored lrzsz sources used for Windows transfer helpers
    rzsz-main/    Rust-derived ZModem protocol reference implementation
```

## Current limitations

- Windows is the current validated package target; Linux packaging needs equivalent end-to-end validation.
- Serial and Telnet profiles are visible in the UI but are not implemented.
- Port forwarding, SFTP/SCP, macros, and scripting are not implemented.
- The terminal engine supports common shell usage and ANSI output, but it is not yet a complete xterm emulator. Complex full-screen applications may have rendering or input limitations.

## Roadmap

1. Improve terminal emulation compatibility and test coverage.
2. Implement serial and Telnet connections.
3. Add port forwarding and SFTP/SCP workflows.
4. Add reconnect, keepalive, macros, and scripting support.
5. Produce tested Windows and Linux release artifacts.

## Reporting issues

Please include the CrossTerm version, operating system, connection type, steps to reproduce, expected behavior, actual behavior, and relevant sanitized logs. For ZModem reports, include the `crossterm_zmodem.log` entries around the transfer.

## License

CrossTerm is licensed under the GNU General Public License, version 2. See [LICENSE](LICENSE).

The repository includes third-party protocol sources with their own license notices:

- `third_party/lrzsz` is distributed under GPL-2.0.
- `third_party/rzsz-main` is an Apache-2.0 licensed reference implementation.
