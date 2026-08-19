@echo off
setlocal EnableExtensions

set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%build-windows"
set "PACKAGE_DIR=%PROJECT_DIR%dist\windows"

where cmake >nul 2>nul || (
    echo Error: cmake was not found on PATH.
    exit /b 1
)

where windeployqt >nul 2>nul || (
    echo Error: windeployqt was not found on PATH.
    echo Open a Qt command prompt matching your compiler and run this script again.
    exit /b 1
)

cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 exit /b 1

if exist "%PACKAGE_DIR%" rmdir /s /q "%PACKAGE_DIR%"
mkdir "%PACKAGE_DIR%"

cmake --install "%BUILD_DIR%" --prefix "%PACKAGE_DIR%" --config Release
if errorlevel 1 exit /b 1

windeployqt --release --compiler-runtime "%PACKAGE_DIR%\bin\CrossTerm.exe"
if errorlevel 1 exit /b 1

echo.
echo Release package complete: %PACKAGE_DIR%\bin\CrossTerm.exe
endlocal