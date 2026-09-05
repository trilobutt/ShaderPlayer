; Inno Setup 6 script for ShaderPlayer.
; Every version number and path is supplied on the ISCC command line via
; preprocessor defines, so this script hardcodes neither. Example:
;   iscc /DAppVersion=1.0.0 /DStageDir=C:\stage /DOutputDir=C:\out ShaderPlayer.iss

#ifndef AppVersion
  #error AppVersion must be passed with /DAppVersion=...
#endif
#ifndef StageDir
  #error StageDir must be passed with /DStageDir=...
#endif
#ifndef OutputDir
  #error OutputDir must be passed with /DOutputDir=...
#endif

[Setup]
AppName=ShaderPlayer
AppVersion={#AppVersion}
AppPublisher=Marc Srour
AppPublisherURL=https://shaderplayer.marcsplained.com
AppId={{7C4E1A2B-9F30-4B6D-8A11-2E5D3C7F9B04}
DefaultDirName={autopf}\ShaderPlayer
PrivilegesRequired=lowest
DefaultGroupName=ShaderPlayer
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=ShaderPlayer-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
LicenseFile={#StageDir}\LICENSE
WizardStyle=modern
UninstallDisplayIcon={app}\ShaderPlayer.exe

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "vc_redist.x64.exe"
Source: "{#StageDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Icons]
; WorkingDir here is load-bearing: AppConfig::shaderDirectory defaults to the
; CWD-relative "shaders", and a shortcut launched from anywhere else only
; works by falling through to the <exe>/shaders fallback.
Name: "{autoprograms}\ShaderPlayer"; Filename: "{app}\ShaderPlayer.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\ShaderPlayer"; Filename: "{app}\ShaderPlayer.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
; No result check: the redistributable elevates itself, and a machine that
; already has a newer runtime returns a non-zero code that is not a failure.
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Visual C++ runtime..."; Flags: waituntilterminated skipifdoesntexist runascurrentuser
Filename: "{app}\ShaderPlayer.exe"; Description: "Launch ShaderPlayer"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; All three are written at runtime beside the exe, so Inno's own file
; tracking (built from [Files] at install time) never covers them.
Type: filesandordirs; Name: "{app}\shader_cache"
Type: filesandordirs; Name: "{app}\config.json"
Type: filesandordirs; Name: "{app}\layouts"
