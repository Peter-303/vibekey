; ============================================================
; VibeKey 安装包脚本（Inno Setup 6）
;
; PRD 6.6 要求的 7 页安装向导：
;   1. 欢迎页
;   2. 隐私协议（默认不勾选，不勾则"下一步"置灰）
;   3. 安装说明
;   4. 安装路径（默认 %LocalAppData%\Programs\VibeKey，允许自定义，
;      路径含中文/空格需兼容）
;   5. 附加任务（开机自启 / 桌面快捷方式 / 开始菜单快捷方式）
;   6. 安装中（释放文件、写启动项、注册卸载入口；**不安装任何第三方驱动**）
;   7. 完成页（"启动 VibeKey"默认勾选）
;
; USB 方案已定 TinyUSB CDC，Windows 10/11 自带 usbser.sys 驱动，
; 本安装包**不携带任何第三方驱动，也不申请驱动签名**。
; 卸载时干净移除程序文件、注册表启动项与快捷方式；
; 用户映射配置（%AppData%\VibeKey\mappings.json）默认保留，
; 卸载完成时弹窗询问是否一并删除。
; ============================================================

#define AppName "VibeKey"
#define AppVersion "0.1.0"
#define AppPublisher "萤狸科技"
#define AppUrl "https://github.com/yingli-tech/vibekey"
#define ExeName "VibeKey.exe"
#define SrcDir "..\\host\\publish\\win-x64"
#define OutDir "output"

[Setup]
AppId={{8F3A2C1E-5B7D-4E6F-9A1B-2C3D4E5F6A7B}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}
AppUpdatesURL={#AppUrl}
DefaultDirName={localappdata}\Programs\VibeKey
DefaultGroupName={#AppName}
DisableProgramGroupPage=no
AllowNoIcons=yes
OutputDir={#OutDir}
OutputBaseFilename=VibeKey-Setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
WizardStyle=modern
WizardSizePercent=110
SetupIconFile=
UninstallDisplayIcon={app}\{#ExeName}
UninstallDisplayName={#AppName}
CreateUninstallRegKey=yes

; 语言：winget 用户作用域装的 Inno Setup 不带 ChineseSimplified.isl，
; 用默认英文 + 自定义 Messages 覆盖所有界面文字（这样也更可控）。
[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Messages]
en.WelcomeLabel1=Welcome to the VibeKey Setup Wizard
en.WelcomeLabel2=This will install VibeKey on your computer.%n%nVibeKey is a custom hardware keyboard for Vibe Coding:%npress a key to trigger a code snippet, a command, or voice input.
en.ClickNext=Click Next to continue, or Cancel to exit Setup.
en.ButtonNext=&Next >
en.ButtonBack=< &Back
en.ButtonFinish=&Finish
en.ButtonCancel=Cancel
en.LicenseLabel=Please read the following Privacy Policy before continuing.
en.LicenseLabel3=Please read the following Privacy Policy. You must accept the terms before continuing with the installation.
en.LicenseNotAccepted=I &do not accept
en.LicenseAccepted=I &accept
en.SelectDirLabel3=Select the folder in which VibeKey should be installed, then click Next.
en.SelectDirBrowseLabel=To continue, click Next. If you would like to select a different folder, click Browse.
en.FinishedHeadingLabel=Completing the VibeKey Setup Wizard
en.FinishedLabel=Setup has finished installing VibeKey on your computer.
en.FinishedRestartLabel=To complete the installation, Setup must restart your computer. Would you like to restart now?
en.UninstallAppFullTitle=Uninstall VibeKey
en.ConfirmUninstall=Are you sure you want to completely remove VibeKey and all of its components?
en.UninstallOnlyOnWin64=This installation can only be uninstalled on 64-bit Windows.
en.NotOnThisPlatform=This application will not run on this version of Windows.

[Types]
Name: "full"; Description: "完整安装"
Name: "minimal"; Description: "最小安装"
Name: "custom"; Description: "自定义安装"; Flags: iscustom

[Components]
Name: "main"; Description: "VibeKey 主程序"; Types: full minimal custom; Flags: fixed

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务："
Name: "startmenu"; Description: "创建开始菜单快捷方式"; GroupDescription: "附加任务："
Name: "autostart"; Description: "开机自动启动 VibeKey"; GroupDescription: "附加任务："

[Files]
Source: "{#SrcDir}\{#ExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\VibeKey.Protocol.xml"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#ExeName}"; Tasks: desktopicon
Name: "{group}\{#AppName}"; Filename: "{app}\{#ExeName}"; Tasks: startmenu
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"; Tasks: startmenu

[Registry]
; 开机自启写 HKCU Run（PRD 6.5）。卸载时由 [UninstallRun] 删除。
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "VibeKey"; \
    ValueData: """{app}\{#ExeName}"" --minimized"; \
    Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#ExeName}"; Description: "启动 {#AppName}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 卸载时干净移除程序文件（PRD 6.6）
Type: filesandordirs; Name: "{app}"

[Code]
// 隐私协议文本（PRD 6.6：说明上位机收集哪些信息、ASR 数据流向）
const
  PRIVACY_TEXT =
    'VibeKey 隐私协议' + #13#10#13#10 +
    '1. 上位机（本应用）收集的信息' + #13#10 +
    '   - 设备状态：连接状态、IP、固件版本、WiFi 信号强度（仅用于界面显示）' + #13#10 +
    '   - 按键事件：按键字符仅在本地映射执行，默认不上传到任何服务器' + #13#10 +
    '   - 语音文本：语音转写结果仅在本地使用（注入光标 / 显示在界面上），' +
    '默认不离开本机' + #13#10#13#10 +
    '2. ASR 服务数据流向' + #13#10 +
    '   语音识别的处理方是**下位机**（ESP32-S3 硬件本体），不是本应用。' + #13#10 +
    '   按下语音键时，硬件直接把音频流发送到第三方 ASR 服务商' +
    '（默认腾讯云，备用火山引擎）。' + #13#10 +
    '   你的语音数据会离开你的电脑，到达第三方 ASR 服务商的服务器。' + #13#10 +
    '   本应用只接收转写后的文字，不接收、不存储原始音频。' + #13#10#13#10 +
    '3. 你需要知道的几件事' + #13#10 +
    '   - WiFi 密码、ASR API Key 由本应用下发并存储在硬件的 NVS 里，' +
    '不会通过调试串口明文打印' + #13#10 +
    '   - 配网、映射配置存储在你的 PC 上（%AppData%\VibeKey\），' +
    '不会同步到我们的服务器' + #13#10 +
    '   - 使用 ASR 功能即表示你同意第三方 ASR 服务商的隐私政策' + #13#10#13#10 +
    '如果你不同意，请取消安装。';

procedure InitializeWizard;
begin
  WizardForm.LicenseAcceptedRadio.Checked := False;
  WizardForm.LicenseAcceptedRadio.Enabled := False;
  WizardForm.LicenseNotAcceptedRadio.Checked := True;
  WizardForm.LicenseNotAcceptedRadio.Enabled := False;

  // 把许可页改造成"隐私协议 + 必须勾选才能继续"
  WizardForm.LicenseLabel1.Caption := '请阅读并同意以下隐私协议：';
  WizardForm.LicenseMemo.Lines.Clear;
  WizardForm.LicenseMemo.Lines.Add(PRIVACY_TEXT);
  WizardForm.LicenseMemo.ScrollBars := ssVertical;

  WizardForm.LicenseAcceptedRadio.Caption := '我已阅读并同意隐私协议';
  WizardForm.LicenseAcceptedRadio.Enabled := True;
  WizardForm.LicenseAcceptedRadio.Checked := False;

  WizardForm.LicenseNotAcceptedRadio.Caption := '我不同意';
  WizardForm.LicenseNotAcceptedRadio.Enabled := True;
  WizardForm.LicenseNotAcceptedRadio.Checked := True;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  // 隐私协议页：必须勾选"我已阅读并同意"才能继续（PRD 6.6）
  if CurPageID = wpLicense then
  begin
    if not WizardForm.LicenseAcceptedRadio.Checked then
    begin
      MsgBox('请先阅读并勾选「我已阅读并同意隐私协议」，才能继续安装。',
             mbInformation, MB_OK);
      Result := False;
    end;
  end;
end;

function InitializeUninstall: Boolean;
begin
  Result := True;
  if MsgBox('是否同时删除你的按键映射配置（%AppData%\VibeKey\mappings.json）？' + #13#10#13#10 +
            '选择「是」会一并删除；选择「否」会保留，方便你下次重装后直接恢复。',
            mbConfirmation, MB_YESNO) = IDYES then
  begin
    DelTree(ExpandConstant('{userappdata}\VibeKey'), True, True, True);
  end;
end;
