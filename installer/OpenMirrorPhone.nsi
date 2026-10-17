; OpenMirrorPhone installer (NSIS 3). Built by package.sh, which passes:
;   /DVERSION=x.y.z  /DDIST=<folder with the app>  /DOUTFILE=<setup.exe path>
; Installs per user (no administrator rights) into %LOCALAPPDATA%\Programs, a folder
; the auto-updater can write to. Settings, history and recordings are never touched.

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user

!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define APP "OpenMirrorPhone"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"
!define REPO_URL "https://github.com/ImElio/openmirrorphone"
!define BONJOUR_URL "https://support.apple.com/kb/DL999"

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "Software\${APP}" "InstallDir"
BrandingText "${APP} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APP} setup"
VIAddVersionKey "LegalCopyright" "GPLv3"

!define MUI_ICON "..\assets\logo.ico"
!define MUI_UNICON "..\assets\logo.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEFINISHPAGE_BITMAP "..\build-gui\welcome.bmp" ; made by package.sh from welcome.svg

!define MUI_WELCOMEPAGE_TEXT "This will install ${APP} ${VERSION} on your computer.$\r$\n$\r$\n${APP} shows the screen of your iPhone, iPad or Mac on this PC. It's completely free.$\r$\n$\r$\nNo administrator rights are needed.$\r$\n$\r$\nClick Next to continue."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP}.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP}"
; The "show readme" checkbox is used for the optional desktop shortcut.
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Create a desktop shortcut"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION CreateDesktopShortcut
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; Ask to close a running copy: its files can't be replaced while it runs.
!macro WAIT_FOR_APP_CLOSED
    check_running:
    nsExec::ExecToStack 'cmd /c tasklist /FI "IMAGENAME eq ${APP}.exe" /NH | find /I "${APP}.exe"'
    Pop $0
    ${If} $0 == 0
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "${APP} is running. Please close it, then click Retry." IDRETRY check_running
        Abort
    ${EndIf}
!macroend

Function CreateDesktopShortcut
    CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\${APP}.exe"
FunctionEnd

Section "Install"
    !insertmacro WAIT_FOR_APP_CLOSED
    SetOutPath "$INSTDIR"
    File /r "${DIST}\*.*"
    Delete "$INSTDIR\*.old" ; leftovers of an interrupted update

    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\${APP}.exe"

    WriteRegStr HKCU "Software\${APP}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${APP}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${APP}.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "${REPO_URL}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" $0

    ; Bonjour is needed to appear in Screen Mirroring. A 32-bit installer sees
    ; SysWOW64 as System32, so look at the real System32.
    ${DisableX64FSRedirection}
    ${IfNot} ${FileExists} "$SYSDIR\dnssd.dll"
        ${EnableX64FSRedirection}
        IfSilent bonjour_done
        MessageBox MB_YESNO|MB_ICONINFORMATION "${APP} needs Apple's Bonjour service to appear in Screen Mirroring, and it isn't installed on this PC.$\r$\n$\r$\nOpen Apple's download page for Bonjour now?" IDNO bonjour_done
        ExecShell "open" "${BONJOUR_URL}"
        bonjour_done:
    ${EndIf}
    ${EnableX64FSRedirection}
SectionEnd

Section "Uninstall"
    !insertmacro WAIT_FOR_APP_CLOSED
    Delete "$SMPROGRAMS\${APP}.lnk"
    Delete "$DESKTOP\${APP}.lnk"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    DeleteRegKey HKCU "Software\${APP}"

    ; Remove the whole folder only when it is clearly ours, so a folder picked by
    ; mistake (say, Documents) never gets wiped.
    ${GetFileName} "$INSTDIR" $0
    ${If} $0 == "${APP}"
    ${AndIf} ${FileExists} "$INSTDIR\${APP}.exe"
        RMDir /r "$INSTDIR"
    ${Else}
        Delete "$INSTDIR\${APP}.exe"
        Delete "$INSTDIR\Uninstall.exe"
        RMDir "$INSTDIR"
    ${EndIf}
SectionEnd
