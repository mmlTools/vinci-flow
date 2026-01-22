; ------------------------------------------------------------------------
; VinciFlow – Windows Installer (NSIS)
; ------------------------------------------------------------------------
; Expects these defines from makensis:
;   /DPRODUCT_NAME
;   /DPRODUCT_VERSION
;   /DPROJECT_ROOT
;   /DCONFIGURATION
;   /DTARGET
;   /DOUTPUT_EXE
; ------------------------------------------------------------------------

Unicode true

!include "MUI2.nsh"
!include "Sections.nsh"

; ------------------------------------------------------------------------
; Compile-time defines (with sane fallbacks)
; ------------------------------------------------------------------------

!ifndef PRODUCT_NAME
  !define PRODUCT_NAME "vinci-flow"
!endif

!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "0.0.0"
!endif

!ifndef PROJECT_ROOT
  !define PROJECT_ROOT "."
!endif

!ifndef CONFIGURATION
  !define CONFIGURATION "RelWithDebInfo"
!endif

!ifndef TARGET
  !define TARGET "x64"
!endif

!ifndef OUTPUT_EXE
  !define OUTPUT_EXE "vinci-flow-setup.exe"
!endif

; Where CMake installed the plugin:
;   ${PROJECT_ROOT}\release\<CONFIGURATION>\vinci-flow\...
!define BUILD_ROOT "${PROJECT_ROOT}\release\${CONFIGURATION}\${PRODUCT_NAME}"

; Installer icon
!ifndef INSTALLER_ICON
  !define INSTALLER_ICON "${PROJECT_ROOT}\installer\resources\vinci-flow.ico"
!endif

; Optional custom welcome/finish bitmap
!define MUI_WELCOMEFINISHPAGE_BITMAP "${PROJECT_ROOT}\installer\resources\vinci-flow-welcome.bmp"

; ------------------------------------------------------------------------
; Basic installer metadata
; ------------------------------------------------------------------------

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "${OUTPUT_EXE}"

RequestExecutionLevel admin

Var OBSDir

; ------------------------------------------------------------------------
; Sections
; ------------------------------------------------------------------------

Section "Core OBS Plugin" SEC_CORE
  SectionIn RO

  ; --- Plugin DLL ---
  SetOutPath "$OBSDir\obs-plugins\64bit"
  File "/oname=vinci-flow.dll" "${BUILD_ROOT}\bin\64bit\vinci-flow.dll"

  ; --- Locale files (future-proof) ---
  SetOutPath "$OBSDir\data\obs-plugins\vinci-flow\locale"
  File /nonfatal /r "${BUILD_ROOT}\data\locale\*.*"
SectionEnd

; ------------------------------------------------------------------------
; MUI pages
; ------------------------------------------------------------------------

!define MUI_ABORTWARNING

; Custom icon for installer + uninstaller
!define MUI_ICON   "${INSTALLER_ICON}"
!define MUI_UNICON "${INSTALLER_ICON}"

!insertmacro MUI_PAGE_WELCOME

; OBS folder selection page
PageEx directory
  DirText "Select the folder where OBS Studio is installed." \
          "The VinciFlow plugin DLL and locale files will be installed into this OBS Studio folder." \
          "Browse..."
  DirVar $OBSDir
PageExEnd

!insertmacro MUI_PAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------------
; Init: default OBS folder
; ------------------------------------------------------------------------

Function .onInit
  ; Default to standard 64-bit OBS install path; user can change it
  StrCpy $OBSDir "$PROGRAMFILES64\obs-studio"
FunctionEnd

; ------------------------------------------------------------------------
; Version info in EXE properties
; ------------------------------------------------------------------------

VIProductVersion  "${PRODUCT_VERSION}.0"
VIAddVersionKey   "ProductName"     "${PRODUCT_NAME}"
VIAddVersionKey   "FileDescription" "VinciFlow plugin installer"
VIAddVersionKey   "CompanyName"     "MML Tech"
VIAddVersionKey   "FileVersion"     "${PRODUCT_VERSION}"
VIAddVersionKey   "LegalCopyright"  "Copyright © MML Tech"
