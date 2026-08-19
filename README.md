# CrossTerm 0.1.0

CrossTerm is the initial foundation for a cross-platform terminal and connection manager inspired by SecureCRT.

## Current milestone

- Qt 6 / C++20 / CMake
- Windows + Linux project structure
- Dockable session tree
- Tabbed terminal workspace
- Abstract connection interface
- Working local-shell connection using `QProcess`
- Basic keyboard forwarding

> This is intentionally an MVP terminal view, not yet a complete ANSI/xterm emulator. Interactive full-screen applications such as `vim`, `top`, and `nano` will not behave correctly until the terminal engine milestone is implemented.

## Linux build

Install dependencies on Fedora:

```bash
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel
```

Build a Release package and run it:

```bash
chmod +x build_linux.sh
./build_linux.sh
./dist/linux/bin/CrossTerm
```

## Windows build

Install Qt 6, CMake, Ninja, and either MSVC 2022 or MinGW 64-bit. Open the matching Qt command prompt so `cmake`, the selected compiler, and `windeployqt` are on `PATH`.

Build an independently runnable Release package:

```bat
build_windows.bat
dist\windows\bin\CrossTerm.exe
```

`build_windows.bat` runs `windeployqt`, which places the Qt runtime DLLs and platform plugins next to the executable. SSH sessions require the Windows OpenSSH client (`ssh.exe`) to be available on `PATH`.

## Project layout

```text
CrossTerm-0.1.0/
├── CMakeLists.txt
├── README.md
└── src/
    ├── main.cpp
    ├── MainWindow.h
    ├── MainWindow.cpp
    ├── connections/
    │   ├── IConnection.h
    │   ├── LocalShellConnection.h
    │   └── LocalShellConnection.cpp
    └── widgets/
        ├── TerminalWidget.h
        └── TerminalWidget.cpp
```

## Planned milestones

1. ANSI / VT / xterm terminal engine and screen model
2. Saved session profiles and session editor
3. SSH provider
4. Serial provider
5. Telnet provider
6. Logging, reconnect, keepalive, and session options
7. SFTP/SCP and port forwarding
8. Macro and scripting API
