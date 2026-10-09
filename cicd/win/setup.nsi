##	- Purpose: NSIS script for the Windows setup exe. It installs the release zip's
##	  files where install.ps1 puts them, with the same Start menu shortcut and
##	  PATH entry, so either one updates or removes what the other installed. It
##	  also adds an uninstaller and an entry in Settings, Apps.
##	- For this account only. No elevation, nothing machine-wide.
##	- Built by pack-setup.bash, which passes these:
##	  VERSION   the release version, as in source/meson.build
##	  VIVERSION the same as four numbers, for the version resource
##	  PAYLOAD   the unpacked zip's top folder
##	  ICON      the app's .ico
##	  OUTFILE   where the setup exe goes
##	- Design: project/design_docs/20260930-145641_windows_exe_packing.md

##	Copyright (c) 2026 t00mietum
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

## 64-bit, like the app. A 32-bit one would have its registry and folders
## redirected for no gain.
Target amd64-unicode
ManifestDPIAware true
ManifestSupportedOS all
RequestExecutionLevel user
SetCompressor /SOLID lzma
## A file that can't be written stops the install rather than leaving a hole.
AllowSkipFiles off

!define APP_NAME  "Nemo Anywhere"
!define EXE_NAME  "nemo-anywhere"
## Same key name install.ps1 looks for.
!define ARP_KEY   "Software\Microsoft\Windows\CurrentVersion\Uninstall\${EXE_NAME}"
!define PUBLISHER "t00mietum"
!define HOME_URL  "https://github.com/yottacore/nemo-anywhere"

Name "${APP_NAME}"
OutFile "${OUTFILE}"
BrandingText "${APP_NAME} ${VERSION}"
## Set again in .onInit, so a /D on the command line can't move it.
InstallDir "$LOCALAPPDATA\Programs\${APP_NAME}"
ShowInstDetails show
ShowUninstDetails show

!include MUI2.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!include WinMessages.nsh

!define MUI_ICON   "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "Install ${APP_NAME} ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This installs ${APP_NAME} for this account only, into:$\r$\n$\r$\n$INSTDIR$\r$\n$\r$\nIt adds a Start menu entry and puts that folder on your PATH. An install already there, from this setup or from install.ps1, is replaced.$\r$\n$\r$\nSettings are left alone.$\r$\n$\r$\nClose every ${APP_NAME} window before going on."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE_NAME}.exe"
!define MUI_FINISHPAGE_TEXT "${APP_NAME} is installed. Start it from the Start menu, or type ${EXE_NAME} in a new terminal.$\r$\n$\r$\nTo remove it, use Settings, Apps."
!insertmacro MUI_PAGE_FINISH

!define MUI_UNCONFIRMPAGE_TEXT_TOP "This removes ${APP_NAME}, its Start menu entry and its PATH entry. Settings in %APPDATA%\${EXE_NAME} are left in place."
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${VIVERSION}"
VIFileVersion    "${VIVERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName"      "${APP_NAME}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "CompanyName"      "${PUBLISHER}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription"  "${APP_NAME} setup"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion"      "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion"   "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright"   "Copyright (c) 2026 t00mietum. GPL-2.0-only."
VIAddVersionKey /LANG=${LANG_ENGLISH} "OriginalFilename" "${EXE_NAME}-${VERSION}-windows-x86_64-setup.exe"

Var Staging
Var Backup
Var PathIn
Var PathOut
Var PathDir
Var PathDropped
Var PathState


#------------------------------------------------------------------------------
# Shared by the installer and the uninstaller

## Each function is needed twice, once with the un. prefix NSIS wants inside an
## uninstaller.
!macro fShared un

## The leaf name is checked before anything is moved or removed, the same guard
## install.ps1 has. A folder by any other name never reaches RMDir /r.
Function ${un}fGuardDir
	${GetFileName} "$INSTDIR" $0
	${If} $0 != "${APP_NAME}"
		MessageBox MB_ICONSTOP "Refusing to touch a folder not named '${APP_NAME}': $INSTDIR" /SD IDOK
		Abort
	${EndIf}
FunctionEnd

## Rename is all or nothing: if anything in the folder is open, nothing moves.
## A copy that is closing takes a moment, and the session bus an older build
## started runs from the folder and outlives its window by a few seconds, so
## give it 10 before asking.
Function ${un}fMoveAside
	StrCpy $1 0
	${Do}
		ClearErrors
		Rename "$INSTDIR" "$Backup"
		${IfNot} ${Errors}
			Return
		${EndIf}
		${If} $1 == 0
			DetailPrint "Waiting for programs running from $INSTDIR to finish"
		${EndIf}
		IntOp $1 $1 + 1
		${If} $1 >= 10
			MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Something still has this folder open:$\r$\n$\r$\n$INSTDIR$\r$\n$\r$\nClose every ${APP_NAME} window, and any window sitting in that folder, then click Retry." /SD IDCANCEL IDRETRY retry
			## Only the installer stages anything.
			${If} $Staging != ""
				RMDir /r "$Staging"
			${EndIf}
			Abort "Nothing changed: $INSTDIR is in use."
		retry:
			StrCpy $1 1
		${EndIf}
		Sleep 1000
	${Loop}
FunctionEnd

## Reads the user PATH unexpanded into $PathIn. $PathState says what was found:
## ok, absent, or skip when it can't be edited safely. NSIS strings stop at
## NSIS_MAX_STRLEN. A longer value reads as an error, the same as no value, so
## the value names are listed to tell the two apart, and one that is there is
## left alone rather than written back cut short.
Function ${un}fPathRead
	StrCpy $PathState "absent"
	ClearErrors
	ReadRegStr $PathIn HKCU "Environment" "Path"
	${IfNot} ${Errors}
		StrCpy $PathState "ok"
		Return
	${EndIf}
	StrCpy $PathIn ""
	StrCpy $0 0
	${Do}
		ClearErrors
		EnumRegValue $1 HKCU "Environment" $0
		${If} ${Errors}
		${OrIf} $1 == ""
			Return
		${EndIf}
		${If} $1 == "Path"
			StrCpy $PathState "skip"
			Return
		${EndIf}
		IntOp $0 $0 + 1
	${Loop}
FunctionEnd

## Drops a trailing backslash, or several, off $9.
Function ${un}fTrimSlash
	${Do}
		StrCpy $8 "$9" 1 -1
		${If} $8 != "\"
			Return
		${EndIf}
		StrCpy $9 "$9" -1
	${Loop}
FunctionEnd

## $PathIn with every entry naming $PathDir taken out, into $PathOut, and how
## many went into $PathDropped. Same rules as install.ps1: compared without
## case or trailing backslashes, and empty entries kept, a trailing one
## included, so the value comes back as it was found.
Function ${un}fPathFilter
	StrCpy $9 "$PathDir"
	Call ${un}fTrimSlash
	StrCpy $7 "$9"
	StrCpy $PathOut ""
	StrCpy $PathDropped 0
	StrLen $6 "$PathIn"
	StrCpy $5 0
	StrCpy $4 ""
	StrCpy $3 1
	${Do}
		${If} $5 < $6
			StrCpy $2 "$PathIn" 1 $5
		${Else}
			StrCpy $2 ""
		${EndIf}
		${If} $5 >= $6
		${OrIf} $2 == ";"
			StrCpy $9 "$4"
			Call ${un}fTrimSlash
			${If} $4 != ""
			${AndIf} $9 == $7
				IntOp $PathDropped $PathDropped + 1
			${ElseIf} $3 == 1
				StrCpy $PathOut "$4"
				StrCpy $3 0
			${Else}
				StrCpy $PathOut "$PathOut;$4"
			${EndIf}
			${If} $5 >= $6
				${Break}
			${EndIf}
			StrCpy $4 ""
		${Else}
			StrCpy $4 "$4$2"
		${EndIf}
		IntOp $5 $5 + 1
	${Loop}
FunctionEnd

## An expandable string, as install.ps1 writes it, so %VARS% in other entries
## keep working. Then tell running programs, or a new terminal keeps the old
## PATH until the next sign-in.
Function ${un}fPathWrite
	WriteRegExpandStr HKCU "Environment" "Path" "$PathOut"
	SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000
FunctionEnd

!macroend

!insertmacro fShared ""
!insertmacro fShared "un."


#------------------------------------------------------------------------------
# Install

Function .onInit
	SetShellVarContext current
	StrCpy $INSTDIR "$LOCALAPPDATA\Programs\${APP_NAME}"
FunctionEnd

Section "Install"
	Call fGuardDir
	System::Call 'kernel32::GetCurrentProcessId() i .r0'
	StrCpy $Staging "$INSTDIR.new.$0"
	StrCpy $Backup  "$INSTDIR.old.$0"
	RMDir /r "$Staging"
	RMDir /r "$Backup"

	## Unpacked beside the install first, so a full disk or a failed write
	## never leaves nothing installed. The swap after is two renames on one
	## volume.
	SetOutPath "$Staging"
	File /r "${PAYLOAD}/*"
	## SetOutPath is also the working folder, which would keep the staging
	## folder open and the rename below would fail.
	SetOutPath "$TEMP"

	${If} ${FileExists} "$INSTDIR\*.*"
		ClearErrors
		Call fMoveAside
	${EndIf}
	ClearErrors
	Rename "$Staging" "$INSTDIR"
	${If} ${Errors}
		Rename "$Backup" "$INSTDIR"
		RMDir /r "$Staging"
		Abort "Could not put the new files in $INSTDIR."
	${EndIf}
	RMDir /r "$Backup"
	${If} ${FileExists} "$Backup\*.*"
		DetailPrint "Could not remove $Backup - delete it by hand."
	${EndIf}
	DetailPrint "Installed in $INSTDIR"

	WriteUninstaller "$INSTDIR\uninstall.exe"

	## The working folder of the shortcut is the output folder at the time.
	SetOutPath "$INSTDIR"
	CreateDirectory "$SMPROGRAMS"
	CreateShortcut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\${EXE_NAME}.exe" "" "$INSTDIR\${EXE_NAME}.exe" 0 SW_SHOWNORMAL "" "${APP_NAME}"
	DetailPrint "Start menu entry: $SMPROGRAMS\${APP_NAME}.lnk"

	StrCpy $PathDir "$INSTDIR"
	Call fPathRead
	${If} $PathState == "absent"
		StrCpy $PathOut "$INSTDIR"
		Call fPathWrite
		DetailPrint "Added $INSTDIR to the user PATH"
	${ElseIf} $PathState == "ok"
		## Room for ';' and the folder, or it is left alone like a long one.
		StrLen $0 "$PathIn"
		StrLen $1 "$INSTDIR"
		IntOp $0 $0 + $1
		IntOp $0 $0 + 1
		${If} $0 >= ${NSIS_MAX_STRLEN}
			StrCpy $PathState "skip"
		${EndIf}
	${EndIf}
	${If} $PathState == "ok"
		Call fPathFilter
		${If} $PathDropped > 0
			DetailPrint "$INSTDIR was already on the user PATH"
		${ElseIf} $PathIn == ""
			StrCpy $PathOut "$INSTDIR"
			Call fPathWrite
			DetailPrint "Added $INSTDIR to the user PATH"
		${Else}
			## A trailing ';' stays the last thing in the value.
			StrCpy $0 "$PathIn" 1 -1
			${If} $0 == ";"
				StrCpy $1 "$PathIn" -1
				StrCpy $PathOut "$1;$INSTDIR;"
			${Else}
				StrCpy $PathOut "$PathIn;$INSTDIR"
			${EndIf}
			Call fPathWrite
			DetailPrint "Added $INSTDIR to the user PATH"
		${EndIf}
	${ElseIf} $PathState == "skip"
		DetailPrint "The user PATH was left alone: it is too long to change safely here."
		MessageBox MB_ICONINFORMATION "Your user PATH is too long for this setup to change safely, so it was left alone. To start ${APP_NAME} by name from a terminal, add this folder to it:$\r$\n$\r$\n$INSTDIR" /SD IDOK
	${EndIf}

	${GetSize} "$INSTDIR" "/S=0K /G=1" $0 $1 $2
	WriteRegStr   HKCU "${ARP_KEY}" "DisplayName"          "${APP_NAME}"
	WriteRegStr   HKCU "${ARP_KEY}" "DisplayVersion"       "${VERSION}"
	WriteRegStr   HKCU "${ARP_KEY}" "Publisher"            "${PUBLISHER}"
	WriteRegStr   HKCU "${ARP_KEY}" "DisplayIcon"          "$INSTDIR\${EXE_NAME}.exe"
	WriteRegStr   HKCU "${ARP_KEY}" "InstallLocation"      "$INSTDIR"
	WriteRegStr   HKCU "${ARP_KEY}" "URLInfoAbout"         "${HOME_URL}"
	WriteRegStr   HKCU "${ARP_KEY}" "UninstallString"      '"$INSTDIR\uninstall.exe"'
	WriteRegStr   HKCU "${ARP_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
	WriteRegDWORD HKCU "${ARP_KEY}" "NoModify"             1
	WriteRegDWORD HKCU "${ARP_KEY}" "NoRepair"             1
	WriteRegDWORD HKCU "${ARP_KEY}" "EstimatedSize"        $0
SectionEnd


#------------------------------------------------------------------------------
# Uninstall

Function un.onInit
	SetShellVarContext current
FunctionEnd

Section "Uninstall"
	SetOutPath "$TEMP"
	Call un.fGuardDir
	System::Call 'kernel32::GetCurrentProcessId() i .r0'
	StrCpy $Backup "$INSTDIR.old.$0"
	RMDir /r "$Backup"

	## Moved aside first, so a folder something has open is left whole, with
	## its shortcut and PATH entry, rather than half removed.
	${If} ${FileExists} "$INSTDIR\*.*"
		Call un.fMoveAside
		RMDir /r "$Backup"
		${If} ${FileExists} "$Backup\*.*"
			DetailPrint "Could not remove $Backup - delete it by hand."
		${Else}
			DetailPrint "Removed $INSTDIR"
		${EndIf}
	${EndIf}

	Delete "$SMPROGRAMS\${APP_NAME}.lnk"

	StrCpy $PathDir "$INSTDIR"
	Call un.fPathRead
	${If} $PathState == "ok"
		Call un.fPathFilter
		${If} $PathDropped > 0
			Call un.fPathWrite
			DetailPrint "Removed $INSTDIR from the user PATH"
		${EndIf}
	${ElseIf} $PathState == "skip"
		DetailPrint "The user PATH was left alone: it is too long to change safely here."
		MessageBox MB_ICONINFORMATION "Your user PATH is too long for this uninstaller to change safely. If it still lists this folder, take it out by hand:$\r$\n$\r$\n$INSTDIR" /SD IDOK
	${EndIf}

	DeleteRegKey HKCU "${ARP_KEY}"
SectionEnd
