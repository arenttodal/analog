; ANDOR Analog: the Windows installer (Inno Setup 6). 64-bit, VST3 + standalone.
;   iscc /DAppVersion=1.0.0 /DArtefacts=<...\ANDOR_Analog_artefacts\Release> /DNotices=<THIRD_PARTY_NOTICES.txt> /DOutDir=<dir> ANDOR-Analog.iss
; Signing (Authenticode) is done after compiling, on the produced setup and on the bundled
; binaries before compiling, when a certificate is available (see docs/RELEASE_CANDIDATE.md).
; AppId never changes: it is how Windows recognises an upgrade.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef OutDir
  #define OutDir "."
#endif

[Setup]
AppId={{BC7C8BA2-7277-5A74-9EE8-31F30D4B841D}
AppName=ANDOR Analog
AppVersion={#AppVersion}
AppPublisher=[PUBLISHER]
DefaultDirName={autopf}\ANDOR Analog
DefaultGroupName=ANDOR Analog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
LicenseFile=..\common\EULA-DRAFT.txt
OutputDir={#OutDir}
OutputBaseFilename=ANDOR-Analog-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
UninstallDisplayName=ANDOR Analog
DisableProgramGroupPage=yes

[Types]
Name: "full"; Description: "VST3 and standalone"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Ableton Live, Cubase, REAPER, Bitwig, Studio One, FL Studio)"; Types: full custom
Name: "app"; Description: "Standalone application"; Types: full custom

[Files]
Source: "{#Artefacts}\VST3\ANDOR Analog.vst3\*"; DestDir: "{commoncf64}\VST3\ANDOR Analog.vst3"; Components: vst3; Flags: recursesubdirs ignoreversion
Source: "{#Artefacts}\Standalone\ANDOR Analog.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#Notices}"; DestDir: "{app}"; DestName: "THIRD_PARTY_NOTICES.txt"; Flags: ignoreversion
Source: "..\common\EULA-DRAFT.txt"; DestDir: "{app}"; DestName: "EULA.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\ANDOR Analog"; Filename: "{app}\ANDOR Analog.exe"; Components: app
Name: "{group}\Uninstall ANDOR Analog"; Filename: "{uninstallexe}"

[UninstallDelete]
; Only what the installer put there. The musician's presets and recordings (Documents\ANDOR Analog) are never touched.
Type: filesandordirs; Name: "{commoncf64}\VST3\ANDOR Analog.vst3"
