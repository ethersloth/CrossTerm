#define MyAppName "CrossTerm"
#define MyAppVersion "0.5.2"
#define MyAppPublisher "ethersloth"
#define MyAppExeName "CrossTerm.exe"

[Setup]
AppId={{C29125ED-F1D8-4B50-8D9F-B84B03A86D43}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\CrossTerm
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\dist\installer
OutputBaseFilename=CrossTerm-0.5.2-windows-x64-setup
SetupIconFile=..\assets\cross_term_logo.ico
UninstallDisplayIcon={app}\bin\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: checkedonce

[Files]
Source: "..\dist\windows\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\CrossTerm"; Filename: "{app}\bin\{#MyAppExeName}"; WorkingDir: "{app}\bin"
Name: "{autodesktop}\CrossTerm"; Filename: "{app}\bin\{#MyAppExeName}"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\{#MyAppExeName}"; Description: "Launch CrossTerm"; Flags: nowait postinstall skipifsilent