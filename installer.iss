; Inno Setup script: builds GuitarChordPlayer-Setup.exe from the deployed files in dist\.
; Installs per user (no admin rights) so the built-in updater can replace the files later.
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{6C2E4D1A-7B1F-4E6B-9F0C-2A7D5C3B8E41}
AppName=Guitar Chord Player
AppVersion={#AppVersion}
AppPublisher=Guitar Chord Player
AppPublisherURL=https://github.com/bahalouati/GuitarChordPlayer
DefaultDirName={localappdata}\Programs\Guitar Chord Player
DefaultGroupName=Guitar Chord Player
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=.
OutputBaseFilename=GuitarChordPlayer-Setup
SetupIconFile=resources\app.ico
UninstallDisplayIcon={app}\GuitarChordPlayer.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Guitar Chord Player"; Filename: "{app}\GuitarChordPlayer.exe"
Name: "{group}\Uninstall Guitar Chord Player"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Guitar Chord Player"; Filename: "{app}\GuitarChordPlayer.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\GuitarChordPlayer.exe"; Description: "{cm:LaunchProgram,Guitar Chord Player}"; Flags: nowait postinstall skipifsilent
