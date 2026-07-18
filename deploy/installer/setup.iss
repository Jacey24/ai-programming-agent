; ──────────────────────────────────────────────────────
; CodePilot Windows Installer Script (Inno Setup 6)
; ──────────────────────────────────────────────────────
; 用法:
;   ISCC.exe /DMyAppVersion=0.2.0 /DStagingDir=build/staging /Odist setup.iss
; ──────────────────────────────────────────────────────

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0-dev"
#endif

#ifndef StagingDir
  #define StagingDir "build\staging"
#endif

#define MyAppName "CodePilot"
#define MyAppPublisher "CodePilot Team"
#define MyAppURL "https://github.com/Jacey24/ai-programming-agent"
#define MyAppExeName "codepilot-shell.exe"

[Setup]
; 签名设置 (后续添加代码签名)
; SignTool=mycustom
AppId={{B8F4A3D2-1E5C-4F9B-A8D6-2C7E3F0A9B1D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}

; 安装路径 — 用户可选
DefaultDirName={autopf}\{#MyAppName}
DisableDirPage=no
DirExistsWarning=auto

; 增量升级
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
UsePreviousTasks=yes
UsePreviousLanguage=yes

; 输出
OutputBaseFilename=CodePilot-Setup-{#MyAppVersion}

; 压缩
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes

; 权限
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog

; 外观
WizardStyle=modern
WizardSizePercent=120,120
WindowVisible=yes
SetupIconFile={#StagingDir}\resources\app.ico

; 卸载
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\codepilot-shell.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; 桌面快捷方式
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; 后端可执行文件
Source: "{#StagingDir}\codepilot-agent-server.exe"; DestDir: "{app}"; Flags: ignoreversion

; 壳启动器
Source: "{#StagingDir}\codepilot-shell.exe"; DestDir: "{app}"; Flags: ignoreversion

; 运行时 DLL（OpenSSL、VC++ 等）
Source: "{#StagingDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion

; 前端静态文件
Source: "{#StagingDir}\web\*"; DestDir: "{app}\web"; Flags: ignoreversion recursesubdirs createallsubdirs

; 占位目录 (运行时数据实际走 APPDATA)
Source: "{#StagingDir}\storage\.gitkeep"; DestDir: "{app}\storage"; Flags: ignoreversion; Check: FileExists(ExpandConstant('{srcexe}'))
Source: "{#StagingDir}\workspace\.gitkeep"; DestDir: "{app}\workspace"; Flags: ignoreversion

[Icons]
; 开始菜单 — 壳启动器
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Comment: "Launch CodePilot"

; 开始菜单 — 在外部浏览器打开
Name: "{autoprograms}\{#MyAppName} (Open in Browser)"; Filename: "http://localhost:8080"; Comment: "Open CodePilot in your default browser"

; 桌面快捷方式 (可选, 用户勾选)
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; 安装完成后启动
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent runascurrentuser

[UninstallRun]
; 卸载前尝试关闭运行中的进程
Filename: "taskkill"; Parameters: "/f /im codepilot-agent-server.exe"; Flags: runhidden; RunOnceId: "KillAgentServer"
Filename: "taskkill"; Parameters: "/f /im codepilot-shell.exe"; Flags: runhidden; RunOnceId: "KillShell"

[Code]
// ──────────────────────────────────────────────────────
// 运行时依赖检测
// ──────────────────────────────────────────────────────

function IsWebView2Available: Boolean;
var
  KeyPath: String;
begin
  KeyPath := 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
  Result := RegKeyExists(HKLM, KeyPath);
  if not Result then
  begin
    KeyPath := 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
    Result := RegKeyExists(HKLM, KeyPath);
  end;
end;

function IsVcRuntimeAvailable: Boolean;
var
  KeyPath: String;
begin
  // VC++ 2015-2022 Redistributable (x64) 检查
  // 通过检查 ucrtbase.dll 对应的注册表键判断
  KeyPath := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64';
  Result := RegKeyExists(HKLM, KeyPath);
end;

function InitializeSetup: Boolean;
var
  MissingMsg: String;
  NeedWarning: Boolean;
  DownloadVC: String;
  DownloadWV: String;
begin
  MissingMsg := '';
  NeedWarning := False;

  if not IsVcRuntimeAvailable then
  begin
    MissingMsg := MissingMsg + '  - Visual C++ Redistributable (x64)' + #13#10;
    NeedWarning := True;
  end;

  if not IsWebView2Available then
  begin
    MissingMsg := MissingMsg + '  - Microsoft Edge WebView2 Runtime' + #13#10;
    NeedWarning := True;
  end;

  if NeedWarning then
  begin
    DownloadVC := 'https://aka.ms/vs/17/release/vc_redist.x64.exe';
    DownloadWV := 'https://go.microsoft.com/fwlink/p/?LinkId=2124703';

    if MsgBox(
      'The following required components are not installed on your system:' + #13#10 + #13#10 +
      MissingMsg + #13#10 +
      'CodePilot will not function correctly without them.' + #13#10 + #13#10 +
      'Download links (will open in browser after install):' + #13#10 +
      '  VC++ Runtime: ' + DownloadVC + #13#10 +
      '  WebView2:     ' + DownloadWV + #13#10 + #13#10 +
      'Do you want to continue with installation anyway?',
      mbConfirmation, MB_YESNO) = IDNO then
    begin
      Result := False;
      Exit;
    end;
  end;

  Result := True;
end;

// ──────────────────────────────────────────────────────
// 卸载时询问是否保留用户数据
// ──────────────────────────────────────────────────────

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  AppDataPath: String;
  KeepData: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    AppDataPath := ExpandConstant('{userappdata}\{#MyAppName}');
    if DirExists(AppDataPath) then
    begin
      KeepData := MsgBox(
        'Your user data at:' + #13#10 +
        AppDataPath + #13#10 + #13#10 +
        'Do you want to keep your sessions, workspaces, and settings?' + #13#10 +
        '(Click No to delete all user data, Yes to keep it.)',
        mbConfirmation, MB_YESNO or MB_DEFBUTTON1);

      if KeepData = IDNO then
      begin
        DelTree(AppDataPath, True, True, True);
      end;
    end;
  end;
end;