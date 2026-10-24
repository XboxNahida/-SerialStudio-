Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "Sections.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "nsDialogs.nsh"

!define APP_NAME "串口工作台 · Serial Studio"
!ifdef TEST_ROOT
    !define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\SerialStudio_PackageTest"
!else
    !define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\SerialStudio"
!endif
Name "${APP_NAME} ${APP_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\SerialStudio"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
ManifestDPIAware true
SetCompressor /SOLID lzma
SetCompressorDictSize 32
SetDatablockOptimize on
ShowInstDetails show
ShowUninstDetails show
BrandingText "Serial Studio · Windows 通信调试工具"
VIProductVersion "${APP_NUMERIC_VERSION}.0"
VIAddVersionKey /LANG=2052 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=2052 "FileDescription" "Serial Studio 中文安装程序"
VIAddVersionKey /LANG=2052 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=2052 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=2052 "LegalCopyright" "Serial Studio 项目"
!define MUI_ICON "图标.ico"
!define MUI_UNICON "图标.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "欢迎安装串口工作台"
!define MUI_WELCOMEPAGE_TEXT "将安装 Serial Studio ${APP_VERSION}（Windows 64 位）。$\r$\n$\r$\n支持串口、网络、开发板总线、多系统终端与指令辅助。$\r$\n$\r$\n你可以选择安装目录，以及是否创建快捷方式。安装前请关闭正在运行的串口工作台。"
!insertmacro MUI_PAGE_WELCOME
!define MUI_COMPONENTSPAGE_TEXT_TOP "选择需要创建的快捷方式；取消勾选即可不创建。"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\SerialStudio.exe"
!define MUI_FINISHPAGE_RUN_TEXT "启动串口工作台"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
UninstPage custom un.ToolOptions un.ToolOptionsLeave
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"

Var DesktopDir
Var MenuDir
Var Options
Var ToolsDir
Var RemoveTools
Var ToolsCheckbox

!macro ShellDirectories
    SetShellVarContext current
    !ifdef TEST_ROOT
        StrCpy $DesktopDir "${TEST_ROOT}\Desktop"
        StrCpy $MenuDir "${TEST_ROOT}\StartMenu\Serial Studio"
    !else
        StrCpy $DesktopDir "$DESKTOP"
        StrCpy $MenuDir "$SMPROGRAMS\Serial Studio"
    !endif
!macroend

Section "程序文件（必需）" SEC_APP
    SectionIn RO
    ; 替换前检查当前包的同名文件是否被占用，避免运行中覆盖。
    !include "${CHECK_FILES}"
    IfFileExists "$INSTDIR\卸载.exe" 0 replacement_ready
    ClearErrors
    ExecWait '"$INSTDIR\卸载.exe" /S _?=$INSTDIR' $1
    ${If} ${Errors}
    ${OrIf} $1 != 0
        MessageBox MB_ICONSTOP "无法移除旧版程序文件，已停止更新。请关闭旧程序后重试。" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
replacement_ready:
    SetOutPath "$INSTDIR"
    SetOverwrite on
    File /r "${PAYLOAD}\*"
    WriteUninstaller "$INSTDIR\卸载.exe"
    SetOutPath "$INSTDIR"
    SetRegView 64
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Serial Studio 项目"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\SerialStudio.ico"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\卸载.exe$\"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\卸载.exe$\" /S'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" ${INSTALLED_KIB}
    ; 注册卸载入口后部署工具；失败时仍可卸载本次已写入的程序文件。
    !ifdef TOOL_INSTALL
        InitPluginsDir
        !include "${TOOL_INSTALL}"
        ClearErrors
        ExecWait '"$INSTDIR\SerialStudio.exe" --seed-managed-tools "$PLUGINSDIR\ToolSeed" "$ToolsDir"' $1
        ${If} ${Errors}
        ${OrIf} $1 != 0
            MessageBox MB_ICONSTOP "必备工具部署失败（空间不足、路径冲突或校验异常），已有工具保留。可重新安装、卸载或在设置中指定工具。" /SD IDOK
            SetErrorLevel 1
            Abort
        ${EndIf}
    !endif
SectionEnd

Section "创建桌面快捷方式" SEC_DESKTOP
    CreateDirectory "$DesktopDir"
    CreateShortcut "$DesktopDir\串口工作台.lnk" "$INSTDIR\SerialStudio.exe" "" "$INSTDIR\SerialStudio.ico"
SectionEnd

Section "创建开始菜单快捷方式" SEC_MENU
    CreateDirectory "$MenuDir"
    CreateShortcut "$MenuDir\串口工作台.lnk" "$INSTDIR\SerialStudio.exe" "" "$INSTDIR\SerialStudio.ico"
    CreateShortcut "$MenuDir\卸载串口工作台.lnk" "$INSTDIR\卸载.exe" "" "$INSTDIR\SerialStudio.ico"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_APP} "安装程序、完整 Qt 运行依赖、中文说明、板端参考和许可文本。"
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP} "在当前用户的桌面上创建入口，可取消。"
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_MENU} "在开始菜单中创建启动与卸载入口，可取消。"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "此安装包需要 64 位 Windows 10 或更新版本。"
        Abort
    ${EndIf}
    ${IfNot} ${AtLeastWin10}
        MessageBox MB_ICONSTOP "此安装包需要 Windows 10 或更新版本。"
        Abort
    ${EndIf}
    SetRegView 64
    !insertmacro ShellDirectories
    StrCpy $ToolsDir "$LOCALAPPDATA\SerialStudio\tools"
    !ifdef TEST_ROOT
        StrCpy $INSTDIR "${TEST_ROOT}\App"
        StrCpy $ToolsDir "${TEST_ROOT}\Tools"
    !endif
    ${GetParameters} $Options
    ${GetOptions} $Options "/UPDATEPID=" $2
    ${If} $2 != ""
        System::Call 'kernel32::OpenProcess(i 0x100000, i 0, i r2) p.r3'
        ${If} $3 != 0
            System::Call 'kernel32::WaitForSingleObject(p r3, i 15000) i.r4'
            System::Call 'kernel32::CloseHandle(p r3)'
            ${If} $4 != 0
                MessageBox MB_ICONSTOP "等待串口工作台退出超时，请关闭程序后重试。" /SD IDOK
                SetErrorLevel 1
                Abort
            ${EndIf}
        ${EndIf}
    ${EndIf}
    ${GetOptions} $Options "/SHORTCUTS=" $0
    ${If} $0 == "0"
        !insertmacro UnselectSection ${SEC_DESKTOP}
        !insertmacro UnselectSection ${SEC_MENU}
    ${EndIf}
FunctionEnd

Function .onInstSuccess
    ${IfNot} ${SectionIsSelected} ${SEC_DESKTOP}
        Delete "$DesktopDir\串口工作台.lnk"
    ${EndIf}
    ${IfNot} ${SectionIsSelected} ${SEC_MENU}
        Delete "$MenuDir\串口工作台.lnk"
        Delete "$MenuDir\卸载串口工作台.lnk"
        RMDir "$MenuDir"
    ${EndIf}
FunctionEnd

Function un.onInit
    SetRegView 64
    !insertmacro ShellDirectories
    StrCpy $RemoveTools 0
    StrCpy $ToolsDir "$LOCALAPPDATA\SerialStudio\tools"
    !ifdef TEST_ROOT
        StrCpy $ToolsDir "${TEST_ROOT}\Tools"
    !endif
    ${GetParameters} $Options
    ${GetOptions} $Options "/REMOVETOOLS=" $0
    ${If} $0 == "1"
        StrCpy $RemoveTools 1
    ${EndIf}
FunctionEnd

Function un.ToolOptions
    nsDialogs::Create 1018
    Pop $0
    ${NSD_CreateLabel} 0 0 100% 36u "卸载应用时，可选择是否同时移除受管工具。默认保留，便于以后重新安装。"
    Pop $0
    ${NSD_CreateCheckbox} 0 48u 100% 18u "同时移除 SerialStudio 管理的工具链"
    Pop $ToolsCheckbox
    ${NSD_CreateLabel} 0 76u 100% 55u "只移除安装清单中未被修改的工具文件。自选路径、用户配置、未登记文件及修改过的文件均保留。升级时自动保留工具链。"
    Pop $0
    nsDialogs::Show
FunctionEnd

Function un.ToolOptionsLeave
    ${NSD_GetState} $ToolsCheckbox $RemoveTools
FunctionEnd

Section "Uninstall"
    !include "${CHECK_FILES}"
    ${If} $RemoveTools == 1
        ClearErrors
        ExecWait '"$INSTDIR\SerialStudio.exe" --remove-managed-tools "$ToolsDir"' $1
        ${If} ${Errors}
        ${OrIf} $1 != 0
            MessageBox MB_ICONEXCLAMATION "部分受管工具无法移除，已保留；关闭相关终端后可重新处理。" /SD IDOK
        ${EndIf}
    ${EndIf}
    ClearErrors
    !include "${REMOVE_FILES}"
    ${If} ${Errors}
        MessageBox MB_ICONEXCLAMATION "部分程序文件正在使用，无法完成卸载。请关闭串口工作台后重新运行卸载。" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    Delete "$DesktopDir\串口工作台.lnk"
    Delete "$MenuDir\串口工作台.lnk"
    Delete "$MenuDir\卸载串口工作台.lnk"
    RMDir "$MenuDir"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    Delete "$INSTDIR\卸载.exe"
    !include "${REMOVE_DIRS}"
    RMDir "$INSTDIR"
SectionEnd
