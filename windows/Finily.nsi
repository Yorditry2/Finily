Unicode true
ManifestDPIAware true
RequestExecutionLevel user

!include "MUI2.nsh"
!include "x64.nsh"

Name "Finily"
OutFile "${SETUP_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Finily"

!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Spanish"

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "Finily requiere Windows de 64 bits."
        Abort
    ${EndIf}
FunctionEnd

Section "Finily"
    SetOutPath "$INSTDIR\app"
    File /r "${PAYLOAD_DIR}\*"
    WriteUninstaller "$INSTDIR\Desinstalar.exe"
    CreateShortcut "$SMPROGRAMS\Finily.lnk" "$INSTDIR\app\finily.exe"

    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily" "DisplayName" "Finily"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily" "UninstallString" '"$INSTDIR\Desinstalar.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily" "DisplayIcon" "$INSTDIR\app\finily.exe"
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily" "NoRepair" 1
SectionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\Finily.lnk"
    Delete "$INSTDIR\Desinstalar.exe"
    RMDir /r "$INSTDIR\app"
    RMDir "$INSTDIR"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily"
SectionEnd
