; ---------------------------------------------------------------------------
; BackAndForth 0.7.1 Windows installer (Inno Setup 6).
;
; Build it with:
;     powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
;
; That script compiles bafc.exe first, then runs ISCC.exe on this file. The
; result is installer\Output\BackAndForth-0.7.1-Setup.exe.
;
; The installer adds the install directory to PATH (per-user by default, or
; system-wide when run elevated), so `baf` and `bafc` work in a fresh shell.
; ---------------------------------------------------------------------------

#define AppName "BackAndForth"
#define AppVersion "0.7.1"
#define AppPublisher "BackAndForth project"
#define AppURL "https://github.com/backandforth/backandforth"

[Setup]
AppId={{8F1B6C34-2B4E-4C7A-9E51-BAF07000A001}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=Output
OutputBaseFilename={#AppName}-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog commandline
PrivilegesRequired=lowest
; Tells Explorer that PATH changed, so new shells pick it up without a reboot.
ChangesEnvironment=yes
ChangesAssociations=yes
UninstallDisplayIcon={app}\bin\bafc.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Types]
Name: "full"; Description: "Everything (compiler, runtimes, editor, examples, docs)"
Name: "compact"; Description: "Compiler and runtimes only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "core"; Description: "Compiler, driver scripts and runtimes"; Types: full compact custom; Flags: fixed
Name: "editor"; Description: "BackAndForth Studio (browser editor)"; Types: full
Name: "examples"; Description: "Example programs"; Types: full
Name: "docs"; Description: "Documentation"; Types: full
Name: "osdev"; Description: "Freestanding i386 support (bootable images)"; Types: full

[Tasks]
Name: "addtopath"; Description: "Add BackAndForth to the PATH environment variable"; GroupDescription: "Integration:"
Name: "associate"; Description: "Associate .baf files with BackAndForth Studio"; GroupDescription: "Integration:"; Components: editor
Name: "desktopicon"; Description: "Create a desktop shortcut for BackAndForth Studio"; GroupDescription: "Integration:"; Components: editor; Flags: unchecked

[Files]
; --- compiler and driver ---
Source: "..\build\bafc.exe"; DestDir: "{app}\bin"; Flags: ignoreversion; Components: core
Source: "..\windows\baf.cmd";  DestDir: "{app}\bin"; Flags: ignoreversion; Components: core
Source: "..\windows\bafb.cmd"; DestDir: "{app}\bin"; Flags: ignoreversion; Components: core
Source: "..\windows\baf.ps1";  DestDir: "{app}\bin"; Flags: ignoreversion; Components: core
Source: "..\windows\bafb.ps1"; DestDir: "{app}\bin"; Flags: ignoreversion; Components: core

; --- runtimes ---
Source: "..\runtime\windows.ll"; DestDir: "{app}\runtime"; Flags: ignoreversion; Components: core
Source: "..\runtime\posix.ll";   DestDir: "{app}\runtime"; Flags: ignoreversion; Components: core
Source: "..\runtime\i386-abi.ll";  DestDir: "{app}\runtime"; Flags: ignoreversion; Components: osdev
Source: "..\runtime\i386-vga.ll";  DestDir: "{app}\runtime"; Flags: ignoreversion; Components: osdev
Source: "..\runtime\i386-core.c";  DestDir: "{app}\runtime"; Flags: ignoreversion; Components: osdev
Source: "..\runtime\i386-disk.c";  DestDir: "{app}\runtime"; Flags: ignoreversion; Components: osdev
Source: "..\arch\i386\boot.S";     DestDir: "{app}\arch\i386"; Flags: ignoreversion; Components: osdev
Source: "..\arch\i386\debugcon.S"; DestDir: "{app}\arch\i386"; Flags: ignoreversion; Components: osdev
Source: "..\arch\i386\linker.ld";  DestDir: "{app}\arch\i386"; Flags: ignoreversion; Components: osdev

; --- editor, examples, docs ---
Source: "..\tools\editor\index.html"; DestDir: "{app}\editor"; Flags: ignoreversion; Components: editor
Source: "..\examples\*"; DestDir: "{app}\examples"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: examples
Source: "..\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: docs
Source: "..\README.md";    DestDir: "{app}"; Flags: ignoreversion; Components: docs
Source: "..\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion; Components: docs
Source: "..\LICENSE";      DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\BackAndForth Studio"; Filename: "{app}\editor\index.html"; Components: editor
Name: "{group}\Examples"; Filename: "{app}\examples"; Components: examples
Name: "{group}\Documentation"; Filename: "{app}\docs"; Components: docs
Name: "{group}\Command prompt with BackAndForth"; Filename: "{cmd}"; Parameters: "/K ""set PATH={app}\bin;%PATH%"" "; WorkingDir: "{userdocs}"
Name: "{autodesktop}\BackAndForth Studio"; Filename: "{app}\editor\index.html"; Tasks: desktopicon

[Registry]
; PATH. HKA resolves to HKLM when elevated and HKCU otherwise, which is what
; makes the same script work for both per-user and machine-wide installs.
Root: HKA; Subkey: "{code:GetEnvironmentKey}"; ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}\bin"; Tasks: addtopath; Check: NeedsAddPath(ExpandConstant('{app}\bin'))

; .baf association, pointed at the editor.
Root: HKA; Subkey: "Software\Classes\.baf"; ValueType: string; ValueName: ""; ValueData: "BackAndForth.Source"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\BackAndForth.Source"; ValueType: string; ValueName: ""; ValueData: "BackAndForth source file"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\BackAndForth.Source\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\editor\index.html"""; Tasks: associate

[Run]
Filename: "{app}\editor\index.html"; Description: "Open BackAndForth Studio"; Flags: postinstall shellexec nowait skipifsilent; Components: editor
Filename: "{app}\docs\LANGUAGE.md"; Description: "Read the language reference"; Flags: postinstall shellexec nowait skipifsilent unchecked; Components: docs

[Code]
function GetEnvironmentKey(Param: string): string;
begin
  if IsAdminInstallMode then
    Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else
    Result := 'Environment';
end;

{ True when the directory is not already on PATH, so repeated installs do not
  append it over and over. }
function NeedsAddPath(Dir: string): Boolean;
var
  Existing: string;
  Root: Integer;
begin
  if IsAdminInstallMode then
    Root := HKEY_LOCAL_MACHINE
  else
    Root := HKEY_CURRENT_USER;

  if not RegQueryStringValue(Root, GetEnvironmentKey(''), 'Path', Existing) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Existing) + ';') = 0;
end;

{ Removes our directory from PATH on uninstall, leaving the rest untouched. }
procedure RemoveFromPath(Dir: string);
var
  Existing, Rebuilt, Part: string;
  Root, Position: Integer;
begin
  if IsAdminInstallMode then
    Root := HKEY_LOCAL_MACHINE
  else
    Root := HKEY_CURRENT_USER;

  if not RegQueryStringValue(Root, GetEnvironmentKey(''), 'Path', Existing) then
    exit;

  Rebuilt := '';
  Existing := Existing + ';';
  repeat
    Position := Pos(';', Existing);
    Part := Copy(Existing, 1, Position - 1);
    Delete(Existing, 1, Position);
    if (Part <> '') and (Uppercase(Part) <> Uppercase(Dir)) then
    begin
      if Rebuilt <> '' then
        Rebuilt := Rebuilt + ';';
      Rebuilt := Rebuilt + Part;
    end;
  until Existing = '';

  RegWriteExpandStringValue(Root, GetEnvironmentKey(''), 'Path', Rebuilt);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}\bin'));
end;
