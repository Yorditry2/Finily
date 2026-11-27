Unicode true
ManifestDPIAware true
RequestExecutionLevel user
AllowSkipFiles off

!include "MUI2.nsh"
!include "x64.nsh"

Name "Finily"
OutFile "${SETUP_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Finily"

!define APP_STAGE "$INSTDIR\app.new"
!define APP_BACKUP "$INSTDIR\app.previous"
!define MUI_ICON "finily.ico"
!define MUI_UNICON "finily.ico"

!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "Instalación completada"
!define MUI_FINISHPAGE_TEXT "Finily está listo. Puedes abrirlo desde el menú Inicio."
!define MUI_FINISHPAGE_BUTTON "Finalizar"
!define MUI_FINISHPAGE_RUN "$INSTDIR\app\finily.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir Finily ahora"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Spanish"

!macro DefineMaintenanceChecks Prefix
Function ${Prefix}LockMaintenance
    ; El identificador se mantiene abierto hasta que sale este proceso.
    System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Local\FinilyMaintenance") p.r0 ?e'
    Pop $1
    ${If} $0 == 0
    ${OrIf} $1 == 183
        MessageBox MB_ICONEXCLAMATION "Cierra el otro instalador o desinstalador de Finily e inténtalo de nuevo." /SD IDOK
        SetErrorLevel 2
        Quit
    ${EndIf}
FunctionEnd

Function ${Prefix}WaitForFinily
    StrCpy $2 0
    check:
    IfFileExists "$INSTDIR\app\finily.exe" 0 done
    ; Comprobar el acceso de escritura sin modificar el ejecutable.
    System::Call 'kernel32::CreateFileW(w "$INSTDIR\app\finily.exe", i 0x40000000, i 7, p 0, i 3, i 0, p 0) p.r0'
    ${If} $0 == -1
        ${If} $2 < 24
            IntOp $2 $2 + 1
            Sleep 250
            Goto check
        ${EndIf}
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Los archivos de Finily están en uso o bloqueados. Guarda tu trabajo, cierra Finily y pulsa Reintentar, o cancela." /SD IDCANCEL IDRETRY check
        SetErrorLevel 2
        Quit
    ${EndIf}
    System::Call 'kernel32::CloseHandle(p r0)'
    done:
FunctionEnd
!macroend

!insertmacro DefineMaintenanceChecks ""
!insertmacro DefineMaintenanceChecks "un."

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "Finily requiere Windows de 64 bits."
        Abort
    ${EndIf}
    Call LockMaintenance
    Call WaitForFinily
FunctionEnd

Function un.onInit
    Call un.LockMaintenance
    Call un.WaitForFinily
FunctionEnd

Section "Finily"
    SetOutPath "$INSTDIR"
    ; Recuperar la versión anterior si una actualización se interrumpió durante el cambio.
    IfFileExists "${APP_BACKUP}\*.*" 0 prepare
    IfFileExists "$INSTDIR\app\*.*" discardBackup
    ClearErrors
    Rename "${APP_BACKUP}" "$INSTDIR\app"
    IfErrors failed
    Call WaitForFinily
    Goto prepare

    discardBackup:
    ClearErrors
    RMDir /r "${APP_BACKUP}"
    IfErrors failed

    prepare:
    RMDir /r "${APP_STAGE}"
    IfFileExists "${APP_STAGE}\*.*" failed
    ClearErrors
    SetOutPath "${APP_STAGE}"
    IfErrors failed
    File /r "${PAYLOAD_DIR}\*"
    IfErrors failed
    SetOutPath "$INSTDIR"
    Call WaitForFinily
    ClearErrors
    IfFileExists "$INSTDIR\app\*.*" 0 activate
    Rename "$INSTDIR\app" "${APP_BACKUP}"
    IfErrors failed

    activate:
    Rename "${APP_STAGE}" "$INSTDIR\app"
    IfErrors restore
    SetOutPath "$INSTDIR\app"
    WriteUninstaller "$INSTDIR\Desinstalar.exe"
    CreateShortcut "$SMPROGRAMS\Finily.lnk" "$INSTDIR\app\finily.exe"

    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily" "DisplayName" "Finily"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily" "DisplayVersion" "${FINILY_VERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily" "UninstallString" '"$INSTDIR\Desinstalar.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily" "DisplayIcon" "$INSTDIR\app\finily.exe"
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily" "NoRepair" 1
    IfErrors registrationFailed
    RMDir /r "${APP_BACKUP}"
    Goto done

    restore:
    IfFileExists "${APP_BACKUP}\*.*" 0 failed
    Rename "${APP_BACKUP}" "$INSTDIR\app"

    failed:
    SetOutPath "$INSTDIR"
    RMDir /r "${APP_STAGE}"
    MessageBox MB_ICONSTOP "No se pudo completar la instalación de Finily. Revisa el espacio libre y los permisos, y vuelve a ejecutar el instalador." /SD IDOK
    SetErrorLevel 2
    Abort

    registrationFailed:
    MessageBox MB_ICONSTOP "Finily se copió. Vuelve a ejecutar el instalador para completar el registro y el acceso directo." /SD IDOK
    SetErrorLevel 2
    Abort

    done:
SectionEnd

Section "Uninstall"
    Call un.WaitForFinily
    SetOutPath "$TEMP"
    RMDir /r "$INSTDIR\app"
    IfFileExists "$INSTDIR\app\*.*" failed
    RMDir /r "${APP_STAGE}"
    RMDir /r "${APP_BACKUP}"
    Delete "$SMPROGRAMS\Finily.lnk"
    Delete "$INSTDIR\Desinstalar.exe"
    RMDir "$INSTDIR"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Finily"
    Goto done

    failed:
    MessageBox MB_ICONSTOP "No se pudo desinstalar Finily. Cierra la aplicación y vuelve a intentarlo." /SD IDOK
    SetErrorLevel 2
    Abort

    done:
SectionEnd
