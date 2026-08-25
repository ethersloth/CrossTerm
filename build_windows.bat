@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "APP_VERSION=0.5.3"
set "BUILD_DIR=%PROJECT_DIR%\build-windows"
set "PACKAGE_DIR=%PROJECT_DIR%\dist\windows"
set "PACKAGE_ARCHIVE=%PROJECT_DIR%\dist\CrossTerm-%APP_VERSION%-windows-x64.zip"
set "RC_EXE="
set "MT_EXE="
set "CMAKE_TOOL_FLAGS="

where cl >nul 2>nul || call :setup_msvc
where windeployqt >nul 2>nul || call :setup_qt

where cmake >nul 2>nul || (
    echo Error: cmake was not found on PATH.
    exit /b 1
)

where cl >nul 2>nul || (
    echo Error: the MSVC compiler was not found and could not be auto-detected.
    echo Install the "Desktop development with C++" workload, or run this script
    echo from a Visual Studio x64 Native Tools command prompt.
    exit /b 1
)

where windeployqt >nul 2>nul || (
    echo Error: windeployqt was not found and could not be auto-detected.
    echo Set QTDIR to your Qt kit ^(e.g. C:\Qt\6.8.3\msvc2022_64^) and run this script again.
    exit /b 1
)

for /f "delims=" %%I in ('where rc 2^>nul') do (
    set "RC_EXE=%%I"
    goto :found_rc
)
:found_rc

for /f "delims=" %%I in ('where mt 2^>nul') do (
    set "MT_EXE=%%I"
    goto :found_mt
)
:found_mt

rem Delayed expansion keeps "(x86)" in the tool paths from closing this block early.
rem CMake rejects backslashes in these values, so hand it forward slashes.
if defined RC_EXE if defined MT_EXE (
    set CMAKE_TOOL_FLAGS=-DCMAKE_RC_COMPILER="!RC_EXE:\=/!" -DCMAKE_MT="!MT_EXE:\=/!"
)

cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release %CMAKE_TOOL_FLAGS%
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 exit /b 1

tasklist /FI "IMAGENAME eq CrossTerm.exe" 2>nul | find /I "CrossTerm.exe" >nul && (
    echo Error: CrossTerm.exe is currently running.
    echo Close the app and run build_windows.bat again so packaging can update files.
    exit /b 1
)

if exist "%PACKAGE_DIR%" rmdir /s /q "%PACKAGE_DIR%"
mkdir "%PACKAGE_DIR%"

cmake --install "%BUILD_DIR%" --prefix "%PACKAGE_DIR%" --config Release
if errorlevel 1 exit /b 1

windeployqt --release --compiler-runtime "%PACKAGE_DIR%\bin\CrossTerm.exe"
if errorlevel 1 exit /b 1

call :deploy_msvc_runtime
if errorlevel 1 exit /b 1

if exist "%PACKAGE_ARCHIVE%" del /q "%PACKAGE_ARCHIVE%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%PACKAGE_DIR%\*' -DestinationPath '%PACKAGE_ARCHIVE%' -CompressionLevel Optimal"
if errorlevel 1 exit /b 1

echo.
echo Release package complete: %PACKAGE_DIR%\bin\CrossTerm.exe
echo Release archive complete: %PACKAGE_ARCHIVE%
echo Note: Use the packaged exe above, not %BUILD_DIR%\CrossTerm.exe.
endlocal
exit /b 0

:deploy_msvc_runtime
if not defined VCToolsRedistDir (
    echo Error: Visual C++ redistributable path was not configured.
    exit /b 1
)
set "CRT_DIR="
for /d %%D in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do (
    if exist "%%~fD\vcruntime140.dll" set "CRT_DIR=%%~fD"
)
if not defined CRT_DIR (
    echo Error: Visual C++ runtime DLLs were not found under %VCToolsRedistDir%x64.
    exit /b 1
)
xcopy /y /q "%CRT_DIR%\*.dll" "%PACKAGE_DIR%\bin\" >nul
if errorlevel 1 (
    echo Error: Failed to copy Visual C++ runtime DLLs.
    exit /b 1
)
exit /b 0

:setup_msvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :eof
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%I"
if not defined VS_PATH goto :eof
if not exist "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" goto :eof
echo Using MSVC toolchain from %VS_PATH%
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
goto :eof

:setup_qt
if defined QTDIR if exist "%QTDIR%\bin\windeployqt.exe" (
    set "PATH=%QTDIR%\bin;%PATH%"
    echo Using Qt from %QTDIR%
    goto :eof
)
rem Newest version first; skip arm64 kits since this script targets x64.
for %%R in ("%SystemDrive%\Qt" "C:\Qt") do (
    for /f "delims=" %%V in ('dir /b /ad /o-n "%%~R" 2^>nul') do (
        for /f "delims=" %%K in ('dir /b /ad /o-n "%%~R\%%V\msvc*_64" 2^>nul') do (
            set "QT_KIT=%%K"
            if "!QT_KIT:arm=!"=="!QT_KIT!" (
                if exist "%%~R\%%V\%%K\bin\windeployqt.exe" (
                    set "PATH=%%~R\%%V\%%K\bin;!PATH!"
                    echo Using Qt from %%~R\%%V\%%K
                    goto :eof
                )
            )
        )
    )
)
goto :eof