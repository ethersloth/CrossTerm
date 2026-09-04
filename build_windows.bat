@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ============================================================================
rem CrossTerm Windows Build and Packaging Script
rem
rem Output:
rem   dist\windows\bin\CrossTerm.exe
rem   dist\windows\CrossTerm-windows-x64.zip
rem
rem Requirements:
rem   - CMake
rem   - Ninja
rem   - Visual Studio 2022 C++ toolchain
rem   - Qt 6 MSVC x64 kit
rem
rem The script attempts to locate MSVC and Qt automatically when they are not
rem already available in the current environment.
rem ============================================================================

set "APP_NAME=CrossTerm"
set "APP_VERSION=0.6.0"

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "BUILD_DIR=%PROJECT_DIR%\build-windows"
set "PACKAGE_DIR=%PROJECT_DIR%\dist\windows"
set "PACKAGE_BIN_DIR=%PACKAGE_DIR%\bin"
set "ZIP_PATH=%PACKAGE_DIR%\CrossTerm-windows-x64.zip"

set "RC_EXE="
set "MT_EXE="
set "CMAKE_TOOL_FLAGS="

echo.
echo ============================================================================
echo  CrossTerm %APP_VERSION% - Windows Release Build
echo ============================================================================
echo.
echo Project:  %PROJECT_DIR%
echo Build:    %BUILD_DIR%
echo Package:  %PACKAGE_DIR%
echo.

rem ============================================================================
rem Locate build tools
rem ============================================================================

call :ensure_msvc
if errorlevel 1 goto :fail

call :ensure_qt
if errorlevel 1 goto :fail

call :ensure_cmake
if errorlevel 1 goto :fail

call :configure_cmake_tools
if errorlevel 1 goto :fail

rem ============================================================================
rem Configure project
rem ============================================================================

echo.
echo [1/6] Configuring CMake...
echo.

cmake ^
    -S "%PROJECT_DIR%" ^
    -B "%BUILD_DIR%" ^
    -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    %CMAKE_TOOL_FLAGS%

if errorlevel 1 (
    echo.
    echo Error: CMake configuration failed.
    goto :fail
)

rem ============================================================================
rem Build project
rem ============================================================================

echo.
echo [2/6] Building CrossTerm...
echo.

cmake --build "%BUILD_DIR%" --config Release --parallel

if errorlevel 1 (
    echo.
    echo Error: CrossTerm build failed.
    goto :fail
)

rem ============================================================================
rem Ensure CrossTerm is not running before replacing package files
rem ============================================================================

tasklist /FI "IMAGENAME eq CrossTerm.exe" 2>nul | find /I "CrossTerm.exe" >nul

if not errorlevel 1 (
    echo.
    echo Error: CrossTerm.exe is currently running.
    echo Close CrossTerm and run build_windows.bat again.
    goto :fail
)

rem ============================================================================
rem Create clean package directory
rem ============================================================================

echo.
echo [3/6] Creating release package...
echo.

if exist "%PACKAGE_DIR%" (
    echo Removing previous package...
    rmdir /s /q "%PACKAGE_DIR%"

    if exist "%PACKAGE_DIR%" (
        echo Error: Could not remove the previous package directory:
        echo   %PACKAGE_DIR%
        goto :fail
    )
)

mkdir "%PACKAGE_DIR%"

if errorlevel 1 (
    echo Error: Could not create package directory:
    echo   %PACKAGE_DIR%
    goto :fail
)

rem ============================================================================
rem Install project into package directory
rem ============================================================================

cmake --install "%BUILD_DIR%" --prefix "%PACKAGE_DIR%" --config Release

if errorlevel 1 (
    echo.
    echo Error: CMake install step failed.
    goto :fail
)

if not exist "%PACKAGE_BIN_DIR%\CrossTerm.exe" (
    echo.
    echo Error: CrossTerm.exe was not installed into the package.
    echo Expected:
    echo   %PACKAGE_BIN_DIR%\CrossTerm.exe
    goto :fail
)

rem ============================================================================
rem Deploy Qt runtime
rem ============================================================================

echo.
echo [4/6] Deploying Qt runtime...
echo.

windeployqt ^
    --release ^
    --compiler-runtime ^
    "%PACKAGE_BIN_DIR%\CrossTerm.exe"

if errorlevel 1 (
    echo.
    echo Error: windeployqt failed.
    goto :fail
)

call :verify_qt_runtime
if errorlevel 1 goto :fail

rem ============================================================================
rem Deploy Visual C++ runtime
rem ============================================================================

echo.
echo [5/6] Deploying Visual C++ runtime...
echo.

call :deploy_msvc_runtime
if errorlevel 1 goto :fail

rem ============================================================================
rem Create distributable ZIP archive
rem ============================================================================

echo.
echo [6/6] Creating distributable archive...
echo.

if exist "%ZIP_PATH%" (
    del /f /q "%ZIP_PATH%"

    if exist "%ZIP_PATH%" (
        echo Warning: Could not remove the previous ZIP archive:
        echo   %ZIP_PATH%
    )
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Compress-Archive -Path '%PACKAGE_BIN_DIR%\*' -DestinationPath '%ZIP_PATH%' -CompressionLevel Optimal -Force"

if errorlevel 1 (
    echo.
    echo Warning: Failed to create the ZIP archive.
    echo The unpacked package is still available at:
    echo   %PACKAGE_BIN_DIR%
) else (
    echo.
    echo Created distributable archive:
    echo   %ZIP_PATH%
)

rem ============================================================================
rem Success
rem ============================================================================

echo.
echo ============================================================================
echo  Build Complete
echo ============================================================================
echo.
echo Application:
echo   %PACKAGE_BIN_DIR%\CrossTerm.exe
echo.
echo Package folder:
echo   %PACKAGE_BIN_DIR%
echo.

if exist "%ZIP_PATH%" (
    echo ZIP archive:
    echo   %ZIP_PATH%
    echo.
)

echo Do not distribute the standalone executable.
echo Distribute the complete bin folder or the ZIP archive.
echo.
echo Note:
echo   Use the packaged executable above, not:
echo   %BUILD_DIR%\CrossTerm.exe
echo.

endlocal
exit /b 0


rem ============================================================================
rem Helper: Ensure MSVC environment
rem ============================================================================

:ensure_msvc

where cl >nul 2>nul

if errorlevel 1 (
    call :setup_msvc
)

where cl >nul 2>nul

if errorlevel 1 (
    echo.
    echo Error: The MSVC compiler was not found.
    echo.
    echo Install the Visual Studio 2022 workload:
    echo   Desktop development with C++
    echo.
    echo Or run this script from a Visual Studio x64 Native Tools prompt.
    exit /b 1
)

rem VCToolsRedistDir normally comes from vcvars64.bat. If cl.exe was already
rem on PATH but the full Visual Studio environment was not initialized, try
rem loading it now so the runtime DLLs can be packaged later.

if not defined VCToolsRedistDir (
    call :setup_msvc
)

exit /b 0


rem ============================================================================
rem Helper: Locate Visual Studio / initialize vcvars64
rem ============================================================================

:setup_msvc

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
)

if not exist "%VSWHERE%" (
    exit /b 1
)

set "VS_PATH="

for /f "usebackq delims=" %%I in (`
    "%VSWHERE%" -latest -products * ^
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
    -property installationPath
`) do (
    set "VS_PATH=%%I"
)

if not defined VS_PATH (
    exit /b 1
)

if not exist "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" (
    exit /b 1
)

echo Using MSVC toolchain from:
echo   %VS_PATH%

call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

if errorlevel 1 (
    exit /b 1
)

exit /b 0


rem ============================================================================
rem Helper: Ensure Qt / windeployqt
rem ============================================================================

:ensure_qt

where windeployqt >nul 2>nul

if errorlevel 1 (
    call :setup_qt
)

where windeployqt >nul 2>nul

if errorlevel 1 (
    echo.
    echo Error: windeployqt was not found.
    echo.
    echo Set QTDIR to your Qt MSVC x64 kit, for example:
    echo   C:\Qt\6.8.3\msvc2022_64
    echo.
    echo Then run this script again.
    exit /b 1
)

for /f "delims=" %%I in ('where windeployqt 2^>nul') do (
    echo Using windeployqt:
    echo   %%I
    goto :qt_found
)

:qt_found
exit /b 0


rem ============================================================================
rem Helper: Locate Qt automatically
rem ============================================================================

:setup_qt

if defined QTDIR (
    if exist "%QTDIR%\bin\windeployqt.exe" (
        set "PATH=%QTDIR%\bin;%PATH%"
        echo Using Qt from:
        echo   %QTDIR%
        exit /b 0
    )
)

rem Check common Qt installation roots.
rem Directory sorting uses newest-looking version first.
rem ARM kits are ignored because this package targets Windows x64.

for %%R in ("%SystemDrive%\Qt" "C:\Qt") do (
    if exist "%%~R" (
        for /f "delims=" %%V in ('dir /b /ad /o-n "%%~R" 2^>nul') do (
            for /f "delims=" %%K in ('dir /b /ad /o-n "%%~R\%%V\msvc*_64" 2^>nul') do (
                set "QT_KIT=%%K"

                if "!QT_KIT:arm=!"=="!QT_KIT!" (
                    if exist "%%~R\%%V\%%K\bin\windeployqt.exe" (
                        set "QTDIR=%%~R\%%V\%%K"
                        set "PATH=!QTDIR!\bin;!PATH!"

                        echo Using Qt from:
                        echo   !QTDIR!

                        exit /b 0
                    )
                )
            )
        )
    )
)

exit /b 1


rem ============================================================================
rem Helper: Ensure CMake
rem ============================================================================

:ensure_cmake

where cmake >nul 2>nul

if errorlevel 1 (
    echo.
    echo Error: cmake was not found on PATH.
    exit /b 1
)

where ninja >nul 2>nul

if errorlevel 1 (
    echo.
    echo Error: Ninja was not found on PATH.
    echo This build uses the CMake Ninja generator.
    exit /b 1
)

exit /b 0


rem ============================================================================
rem Helper: Locate Windows resource / manifest tools for CMake
rem ============================================================================

:configure_cmake_tools

set "RC_EXE="
set "MT_EXE="
set "CMAKE_TOOL_FLAGS="

for /f "delims=" %%I in ('where rc.exe 2^>nul') do (
    if not defined RC_EXE (
        set "RC_EXE=%%I"
    )
)

for /f "delims=" %%I in ('where mt.exe 2^>nul') do (
    if not defined MT_EXE (
        set "MT_EXE=%%I"
    )
)

if defined RC_EXE (
    if defined MT_EXE (
        rem CMake expects forward slashes in these explicit paths.
        set "RC_CMAKE=!RC_EXE:\=/!"
        set "MT_CMAKE=!MT_EXE:\=/!"

        set CMAKE_TOOL_FLAGS=-DCMAKE_RC_COMPILER="!RC_CMAKE!" -DCMAKE_MT="!MT_CMAKE!"
    )
)

exit /b 0


rem ============================================================================
rem Helper: Verify deployed Qt runtime
rem ============================================================================

:verify_qt_runtime

if not exist "%PACKAGE_BIN_DIR%\Qt6Core.dll" (
    echo.
    echo Error: Qt6Core.dll was not deployed.
    exit /b 1
)

if not exist "%PACKAGE_BIN_DIR%\Qt6Gui.dll" (
    echo.
    echo Error: Qt6Gui.dll was not deployed.
    exit /b 1
)

if not exist "%PACKAGE_BIN_DIR%\Qt6Widgets.dll" (
    echo.
    echo Error: Qt6Widgets.dll was not deployed.
    exit /b 1
)

if not exist "%PACKAGE_BIN_DIR%\platforms\qwindows.dll" (
    echo.
    echo Error: The Qt Windows platform plugin was not deployed.
    echo Expected:
    echo   %PACKAGE_BIN_DIR%\platforms\qwindows.dll
    exit /b 1
)

echo Qt runtime deployment verified.
exit /b 0


rem ============================================================================
rem Helper: Deploy Visual C++ runtime
rem ============================================================================

:deploy_msvc_runtime

if not defined VCToolsRedistDir (
    echo.
    echo Error: Visual C++ redistributable path is not configured.
    echo VCToolsRedistDir was not set by the MSVC environment.
    exit /b 1
)

set "CRT_DIR="

for /d %%D in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do (
    if exist "%%~fD\vcruntime140.dll" (
        set "CRT_DIR=%%~fD"
    )
)

if not defined CRT_DIR (
    echo.
    echo Error: Visual C++ runtime DLLs were not found under:
    echo   %VCToolsRedistDir%x64
    exit /b 1
)

echo Copying Visual C++ runtime from:
echo   %CRT_DIR%

xcopy /y /q "%CRT_DIR%\*.dll" "%PACKAGE_BIN_DIR%\" >nul

if errorlevel 1 (
    echo.
    echo Error: Failed to copy the Visual C++ runtime DLLs.
    exit /b 1
)

if not exist "%PACKAGE_BIN_DIR%\vcruntime140.dll" (
    echo.
    echo Error: vcruntime140.dll was not copied into the package.
    exit /b 1
)

echo Visual C++ runtime deployment verified.
exit /b 0


rem ============================================================================
rem Failure exit
rem ============================================================================

:fail

echo.
echo ============================================================================
echo  BUILD FAILED
echo ============================================================================
echo.

endlocal
exit /b 1