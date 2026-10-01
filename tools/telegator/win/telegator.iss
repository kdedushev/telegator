; Установщик Telegator для Windows (Inno Setup 6). Собирает workflow
; "Telegator Windows": ReleasePath — папка Release-сборки, рядом с
; Telegator.exe уже лежит telegator.json с настройками панели.
#define MyAppName "Telegator"
#define MyAppExeName "Telegator.exe"
; core/version.h: AppId
#define MyAppId "06803E50-7D88-4F0B-88B0-F2BF753C3CB3"

[Setup]
AppId={{{#MyAppId}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppName}
DefaultDirName={userappdata}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableWelcomePage=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputPath}
OutputBaseFilename=Telegator-setup
SetupIconFile={#SourcePath}..\..\..\Telegram\Resources\art\icon256.ico
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
CloseApplications=force
WizardStyle=modern

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#ReleasePath}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleasePath}\telegator.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleasePath}\modules\*"; DestDir: "{app}\modules"; Flags: ignoreversion recursesubdirs skipifsourcedoesntexist

[Icons]
; Ярлык «Telegator\Telegator» в меню «Пуск» — его ищет клиент для уведомлений
; (windows_app_user_model_id.cpp, checkInstalled).
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
