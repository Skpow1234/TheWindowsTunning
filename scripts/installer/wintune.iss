; WinTune optional Inno Setup installer (Phase 21).
; Build portable ZIP first, then:
;   iscc scripts\installer\wintune.iss
;
; Requires Inno Setup 6+. Not run by CI by default — portable ZIP is the
; primary distribution artifact.

#define MyAppName "WinTune"
#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#ifndef MyAppArch
  #define MyAppArch "x64"
#endif
#define MyAppPublisher "WinTune"
#define MyAppURL "https://github.com/Skpow1234/TheWindowsTunning"
#define MyAppExeName "wintune.exe"

#if MyAppArch == "arm64"
  #define MyStageDir "..\..\dist\WinTune-" + MyAppVersion + "-win-arm64"
  #define MyOutputBase "WinTune-" + MyAppVersion + "-win-arm64-setup"
  #define MyArchitecturesAllowed "arm64"
  #define MyArchitecturesInstallIn64BitMode "arm64"
#else
  #define MyStageDir "..\..\dist\WinTune-" + MyAppVersion + "-win-x64"
  #define MyOutputBase "WinTune-" + MyAppVersion + "-win-x64-setup"
  #define MyArchitecturesAllowed "x64compatible"
  #define MyArchitecturesInstallIn64BitMode "x64compatible"
#endif

[Setup]
AppId={{8F3C2A1B-9D4E-4F7A-B2C1-0E6D5A9F8B31}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\WinTune
DefaultGroupName=WinTune
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputDir=..\..\dist
OutputBaseFilename={#MyOutputBase}
SetupIconFile=..\..\resources\wintune.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed={#MyArchitecturesAllowed}
ArchitecturesInstallIn64BitMode={#MyArchitecturesInstallIn64BitMode}
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany=WinTune
VersionInfoDescription=WinTune installer
VersionInfoProductName=WinTune

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "addpath"; Description: "Add WinTune folder to user PATH"; GroupDescription: "Environment:"; Flags: unchecked

[Files]
Source: "{#MyStageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\WinTune CLI"; Filename: "{cmd}"; Parameters: "/k ""{app}\{#MyAppExeName}"""; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\WinTune Doctor"; Filename: "{app}\Run-Doctor.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\WinTune Tray"; Filename: "{app}\Start-Tray.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\Launch WinTune"; Filename: "{app}\Launch-WinTune.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\WinTune"; Filename: "{app}\Launch-WinTune.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\Launch-WinTune.cmd"; Description: "Open WinTune interactive menu"; Flags: nowait postinstall skipifsilent unchecked

[Code]
const
  EnvironmentKey = 'Environment';

procedure EnvAddPath(Path: string);
var
  Paths: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Paths) then
    Paths := '';
  if Pos(';' + Uppercase(Path) + ';', ';' + Uppercase(Paths) + ';') > 0 then
    exit;
  if Paths <> '' then
    Paths := Paths + ';' + Path
  else
    Paths := Path;
  RegWriteExpandStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Paths);
end;

procedure EnvRemovePath(Path: string);
var
  Paths: string;
  P: Integer;
  Left, Right: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Paths) then
    exit;
  P := Pos(';' + Uppercase(Path) + ';', ';' + Uppercase(Paths) + ';');
  if P = 0 then
    exit;
  Left := Copy(Paths, 1, P - 1);
  Right := Copy(Paths, P + Length(Path), MaxInt);
  if (Length(Left) > 0) and (Left[Length(Left)] = ';') then
    Delete(Left, Length(Left), 1);
  if (Length(Right) > 0) and (Right[1] = ';') then
    Delete(Right, 1, 1);
  if (Left <> '') and (Right <> '') then
    Paths := Left + ';' + Right
  else
    Paths := Left + Right;
  RegWriteExpandStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Paths);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    if WizardIsTaskSelected('addpath') then
      EnvAddPath(ExpandConstant('{app}'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    EnvRemovePath(ExpandConstant('{app}'));
end;
