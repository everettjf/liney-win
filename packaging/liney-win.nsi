; liney-win NSIS installer.
; Build with tools\make-installer.ps1 (which builds Release first and passes the
; paths below as /D defines). Produces dist\liney-win-Setup.exe.
;
; Defines expected from the build script:
;   APPVERSION  e.g. 0.1.0
;   WINEXE      full path to liney_win.exe
;   CLIEXE      full path to liney.exe
;   ICONFILE    full path to res\liney.ico
;   OUTFILE     full path to the output Setup.exe

Unicode true
!ifndef APPVERSION
  !define APPVERSION "0.2.0"
!endif
!ifndef OUTFILE
  !define OUTFILE "liney-win-Setup.exe"
!endif

Name "liney-win"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\liney-win"
InstallDirRegKey HKCU "Software\liney-win" "InstallDir"
RequestExecutionLevel user      ; per-user install (no admin needed)
SetCompressor /SOLID lzma
!ifdef ICONFILE
  Icon "${ICONFILE}"
  UninstallIcon "${ICONFILE}"
!endif

!include "MUI2.nsh"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\liney-win"

; Close a running instance so files can be replaced (used for in-place updates).
Function .onInit
  nsExec::Exec 'taskkill /IM liney_win.exe /F'
FunctionEnd

Section "liney-win" SecMain
  SetOutPath "$INSTDIR"
!ifdef WINEXE
  File "${WINEXE}"
!endif
!ifdef CLIEXE
  File "${CLIEXE}"
!endif
!ifdef GHOSTTYDLL
  File "${GHOSTTYDLL}"
!endif
!ifdef ICONFILE
  File "${ICONFILE}"
!endif

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\liney-win" "InstallDir" "$INSTDIR"

  ; Start Menu shortcut.
  CreateDirectory "$SMPROGRAMS\liney-win"
  CreateShortCut "$SMPROGRAMS\liney-win\liney-win.lnk" "$INSTDIR\liney_win.exe" "" "$INSTDIR\liney.ico"

  ; Desktop shortcut (created by default).
  CreateShortCut "$DESKTOP\liney-win.lnk" "$INSTDIR\liney_win.exe" "" "$INSTDIR\liney.ico"

  ; Add/Remove Programs entry.
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "liney-win"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${APPVERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "everettjf"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\liney.ico"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\liney_win.exe"
  Delete "$INSTDIR\liney.exe"
  Delete "$INSTDIR\ghostty-vt.dll"
  Delete "$INSTDIR\liney.ico"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\liney-win\liney-win.lnk"
  RMDir "$SMPROGRAMS\liney-win"
  Delete "$DESKTOP\liney-win.lnk"
  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\liney-win"
SectionEnd
