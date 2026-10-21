; qlitemd md_viewer installer.
; Build with ISCC and define StageDir, RepoRoot, BuildVersion, BuildArch and
; OutputDir, for example:
;   ISCC.exe /DStageDir=... /DRepoRoot=... /DBuildVersion=0.0.1 ^
;     /DBuildArch=x64 /DOutputDir=... installer\md_viewer.iss

#ifndef StageDir
  #error StageDir is required
#endif
#ifndef RepoRoot
  #error RepoRoot is required
#endif
#ifndef BuildVersion
  #define BuildVersion "0.0.1"
#endif
#ifndef BuildArch
  #define BuildArch "x64"
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif

#define AppName "qlitemd Markdown Viewer"
#define AppPublisher "qlitemd"
#define AppURL "https://github.com/qyslamd/qlitemd"

[Setup]
AppId={{C25CB79D-C7E6-44A0-84F0-35EECBC144E1}
AppName={#AppName}
AppVersion={#BuildVersion}
AppVerName={#AppName} {#BuildVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\qlitemd
DefaultGroupName=qlitemd
DisableProgramGroupPage=yes
LicenseFile={#StageDir}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=qlitemd-md_viewer-{#BuildVersion}-{#BuildArch}-setup
SetupIconFile={#RepoRoot}\examples\md_viewer\resources\icon.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0
PrivilegesRequired=admin
UsePreviousAppDir=yes
AppMutex=md_viewerMutex
UninstallDisplayIcon={app}\md_viewer.exe
VersionInfoVersion={#BuildVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName}
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#BuildVersion}
VersionInfoCopyright=MIT

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; \
  GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "{#StageDir}\bin\*"; DestDir: "{app}"; \
  Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\NOTICE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\README.zh-CN.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoRoot}\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"; \
  Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\md_viewer.exe"; \
  WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\md_viewer.exe"; \
  WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\md_viewer.exe"; Description: "Launch {#AppName}"; \
  Flags: nowait postinstall skipifsilent
