#define MyAppName "NOCTURNE"
#define MyAppVersion "0.3.0"
#define MyAppPublisher "NOCTURNE Audio"

[Setup]
AppId={{B14F0E1A-9D3B-4A7E-91E7-0F0C7E4A0C31}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Common Files\VST3\NOCTURNE.vst3
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir=..\dist
OutputBaseFilename=NOCTURNE_Setup_x64
SetupIconFile=nocturne.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
Uninstallable=yes
CloseApplications= no

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\build\NOCTURNE_artefacts\Release\VST3\NOCTURNE.vst3\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
function InitializeSetup(): Boolean;
begin
  if not IsWin64 then
  begin
    MsgBox('NOCTURNE требует 64-битную Windows.', mbError, MB_OK);
    Result := False;
  end
  else
    Result := True;
end;
