; Chromatic Sampler installer (Inno Setup 6)
; Build: ISCC.exe installer\DragSampler.iss  -> installer\Output\ChromaticSampler-Setup-1.0.0.exe

#define AppName "Chromatic Sampler"
#define AppVersion "1.0.0"
#define Publisher "Seleven Audio"
#define PublisherURL "https://selevenaudio.com"
#define Artefacts "..\build\ChromaticSampler_artefacts\Release"

[Setup]
AppId={{3F7A9C21-8B4E-4D6A-A1C3-2E9B7D4F6C08}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL={#PublisherURL}
AppSupportURL={#PublisherURL}
DefaultDirName={autopf}\{#Publisher}\{#AppName}
DefaultGroupName={#Publisher}
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=ChromaticSampler-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName={#AppName}

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Components: standalone; Flags: unchecked

[Dirs]
Name: "{commoncf64}\VST3"

[Files]
Source: "{#Artefacts}\VST3\Chromatic Sampler.vst3\*"; DestDir: "{commoncf64}\VST3\Chromatic Sampler.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\Standalone\Chromatic Sampler.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Chromatic Sampler.exe"; Components: standalone
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\Chromatic Sampler.exe"; Components: standalone; Tasks: desktopicon

[Run]
Filename: "{app}\Chromatic Sampler.exe"; Description: "Launch {#AppName}"; Components: standalone; Flags: nowait postinstall skipifsilent unchecked
