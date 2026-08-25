@echo off
setlocal EnableExtensions

set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "ISCC_EXE="

call "%PROJECT_DIR%\build_windows.bat"
if errorlevel 1 exit /b 1

for /f "delims=" %%I in ('where ISCC.exe 2^>nul') do (
    set "ISCC_EXE=%%I"
    goto :found_iscc
)
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if exist "%LocalAppData%\Programs\Inno Setup 6\ISCC.exe" set "ISCC_EXE=%LocalAppData%\Programs\Inno Setup 6\ISCC.exe"

:found_iscc
if not defined ISCC_EXE (
    echo Error: Inno Setup 6 was not found.
    echo Install Inno Setup from https://jrsoftware.org/isdl.php and run this script again.
    exit /b 1
)

"%ISCC_EXE%" "%PROJECT_DIR%\installer\CrossTerm.iss"
if errorlevel 1 exit /b 1

echo.
echo Installer complete: %PROJECT_DIR%\dist\installer\CrossTerm-0.5.2-windows-x64-setup.exe
endlocal