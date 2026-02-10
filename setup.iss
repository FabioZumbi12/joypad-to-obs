; Script do Inno Setup para o plugin Nightbot para OBS Studio
; Para compilar com uma configuração diferente, use: /DBuildConfig=Release
#ifndef BuildConfig
  #define BuildConfig "RelWithDebInfo"
#endif

#pragma message "Inno Setup - Compiling with BuildConfig: " + BuildConfig

; Desenvolvido por FabioZumbi12

#ifndef PluginName
#define PluginName "joypad-to-obs"
#endif

#ifndef AppName
#define AppName "Joypad to OBS"
#endif

[Setup]
#ifndef AppVersion
#define AppVersion "1.0.0"
#endif

AppId={{25C8D9A9-7F49-45D4-9B0E-6B67C0F7A8F4}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=FabioZumbi12
AppPublisherURL=https://example.com/joypad-to-obs
AppSupportURL=https://example.com/joypad-to-obs
AppUpdatesURL=https://example.com/joypad-to-obs
DefaultDirName={reg:HKLM\SOFTWARE\OBS Studio,InstallPath|{autopf}\obs-studio}
OutputDir=release\{#BuildConfig}
OutputBaseFilename={#PluginName}-{#AppVersion}-windows-x64-setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
DirExistsWarning=no
UninstallFilesDir={app}\{#PluginName}-uninstaller
UninstallDisplayName={#PluginName}-uninstaller
SetupIconFile=img\game.ico
UninstallDisplayIcon={uninstallexe}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[CustomMessages]
; Portuguese
brazilianportuguese.Installation=Instalação do {#AppName}
brazilianportuguese.Uninstallation=Desinstalação do {#AppName}
brazilianportuguese.LaunchOBS=Iniciar OBS Studio agora
brazilianportuguese.RemoveConfig=Deseja também remover os arquivos de configuração adicionais?
brazilianportuguese.OBSNotFound=A instalação do OBS Studio não foi encontrada automaticamente.#13#10Você poderá selecionar a pasta manualmente.

; English
english.Installation={#AppName} Setup
english.Uninstallation={#AppName} Uninstall
english.LaunchOBS=Launch OBS Studio now
english.RemoveConfig=Do you also want to remove the additional configuration files?
english.OBSNotFound=OBS Studio installation not found automatically.#13#10You will be able to select the folder manually.

[Files]
; Plugin DLL
Source: "build_x64\{#BuildConfig}\{#PluginName}.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
; Locale files
Source: "data\locale\*.ini"; DestDir: "{app}\data\obs-plugins\{#PluginName}\locale"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
Filename: "{app}\bin\64bit\obs64.exe"; Description: "{cm:LaunchOBS}"; Flags: nowait postinstall skipifsilent

[Code]
var
  ObsPath: string;

function InitializeSetup(): Boolean;
begin
  if RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SOFTWARE\OBS Studio', '', ObsPath) then
  begin
  end
  else
  begin
    MsgBox(CustomMessage('OBSNotFound'), mbInformation, MB_OK);
  end;

  Result := True;
end;

procedure InitializeWizard();
begin
  if ObsPath <> '' then
    WizardForm.DirEdit.Text := ObsPath;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  TargetDir: string;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    TargetDir := ExpandConstant('{userappdata}\obs-studio\plugin_config\{#PluginName}');

    if DirExists(TargetDir) then
    begin
      if MsgBox(
        CustomMessage('RemoveConfig') + #13#10 + TargetDir,
        mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      begin
        DelTree(TargetDir, True, True, True);
      end;
    end;
  end;
end;