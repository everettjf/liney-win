; Liney NSIS installer.
; Build with tools\make-installer.ps1 (which builds Release first and passes the
; paths below as /D defines). Produces dist\liney-setup.exe.
;
; Defines expected from the build script:
;   APPVERSION  e.g. 0.1.0
;   WINEXE      full path to Liney.exe (GUI + built-in notify CLI)
;   ICONFILE    full path to res\liney.ico
;   OUTFILE     full path to the output Setup.exe

Unicode true
!ifndef APPVERSION
  !define APPVERSION "0.5.9"
!endif
!ifndef OUTFILE
  !define OUTFILE "liney-Setup.exe"
!endif

Name "Liney"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\Liney"
InstallDirRegKey HKCU "Software\liney" "InstallDir"
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

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\liney"

; Close a running instance so files can be replaced (used for in-place updates).
Function .onInit
  nsExec::Exec 'taskkill /IM Liney.exe /F'
  nsExec::Exec 'taskkill /IM liney_win.exe /F'
FunctionEnd

Section "Liney" SecMain
  SetOutPath "$INSTDIR"
!ifdef WINEXE
  File "${WINEXE}"
!endif
!ifdef GHOSTTYDLL
  File "${GHOSTTYDLL}"
!endif
!ifdef BINDIR
  File /nonfatal "${BINDIR}\msvcp140*.dll"
  File /nonfatal "${BINDIR}\vcruntime140*.dll"
!endif
!ifdef ICONFILE
  File "${ICONFILE}"
!endif

  ; Remove binaries from older-named installs (GUI liney_win.exe + the separate
  ; liney.exe CLI, now merged into Liney.exe) so upgraders don't keep stale ones.
  Delete "$INSTDIR\liney_win.exe"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\liney" "InstallDir" "$INSTDIR"

  ; Clean up shortcuts from older installs so upgraders don't get duplicates.
  Delete "$DESKTOP\liney-win.lnk"
  Delete "$DESKTOP\liney.lnk"
  Delete "$SMPROGRAMS\liney-win\liney-win.lnk"
  RMDir  "$SMPROGRAMS\liney-win"
  Delete "$SMPROGRAMS\liney\liney.lnk"

  ; Start Menu shortcut.
  CreateDirectory "$SMPROGRAMS\liney"
  CreateShortCut "$SMPROGRAMS\liney\Liney.lnk" "$INSTDIR\Liney.exe" "" "$INSTDIR\liney.ico"

  ; Desktop shortcut (created by default).
  CreateShortCut "$DESKTOP\Liney.lnk" "$INSTDIR\Liney.exe" "" "$INSTDIR\liney.ico"

  ; Add/Remove Programs entry.
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "Liney"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${APPVERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "everettjf"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\liney.ico"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\Liney.exe"
  Delete "$INSTDIR\liney_win.exe"
  Delete "$INSTDIR\ghostty-vt.dll"
  Delete "$INSTDIR\msvcp140*.dll"
  Delete "$INSTDIR\vcruntime140*.dll"
  Delete "$INSTDIR\liney.ico"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\liney\Liney.lnk"
  Delete "$SMPROGRAMS\liney\liney.lnk"
  RMDir "$SMPROGRAMS\liney"
  Delete "$DESKTOP\Liney.lnk"
  Delete "$DESKTOP\liney.lnk"
  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\liney"
SectionEnd
