; ============================================================
;  ETC WindowBuilder 专业版 v12.0  NSIS 安装脚本
;  版权 (c) ET 2024-2026, ET Studio
;  内置 MinGW GCC / CPython / OpenJDK，装完即用
; ============================================================
Unicode true
!define APPNAME   "ETC WindowBuilder"
!define COMPANY   "ET Studio"
!define VERSION   "12.0"
!define EXENAME   "ETC-WindowBuilder.exe"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ETCWindowBuilder"

Name "${APPNAME} 专业版"
OutFile "ETCWindowBuilder-Setup-v12.0.exe"
InstallDir "$PROGRAMFILES64\${COMPANY}\ETCWindowBuilder"
InstallDirRegKey HKLM "${UNINSTKEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUnInstDetails show

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!define MUI_ICON  "..\release\app.ico"
!define MUI_UNICON "..\release\app.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXENAME}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

Section "${APPNAME} 主程序（必选）" SecCore
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "..\release\${EXENAME}"
  File "..\release\app.ico"
  File "..\README.md"
  File "..\LICENSE"
  SetOutPath "$INSTDIR\docs\tutorials"
  File /nonfatal "..\docs\tutorials\*.md"
SectionEnd

Section "C/C++ 工具链 (MinGW-w64 GCC)" SecGCC
  SetOutPath "$INSTDIR\runtime"
  File /r "..\runtime\mingw64"
SectionEnd

Section "Python 3 运行环境 (含 tkinter)" SecPy
  SetOutPath "$INSTDIR\runtime"
  File /r "..\runtime\python"
SectionEnd

Section "Java 开发环境 (OpenJDK 17)" SecJDK
  SetOutPath "$INSTDIR\runtime"
  File /r "..\runtime\jdk"
SectionEnd

Section "开始菜单 / 桌面快捷方式" SecShort
  CreateDirectory "$SMPROGRAMS\${COMPANY}"
  CreateShortcut "$SMPROGRAMS\${COMPANY}\${APPNAME}.lnk" "$INSTDIR\${EXENAME}" "" "$INSTDIR\app.ico"
  CreateShortcut "$SMPROGRAMS\${COMPANY}\卸载 ${APPNAME}.lnk" "$INSTDIR\uninst.exe"
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXENAME}" "" "$INSTDIR\app.ico"
SectionEnd

Section -Post
  WriteUninstaller "$INSTDIR\uninst.exe"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName"     "${APPNAME} 专业版"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher"       "ET"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon"     "$INSTDIR\app.ico"
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninst.exe"'
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section Uninstall
  Delete "$DESKTOP\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${COMPANY}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${COMPANY}\卸载 ${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${COMPANY}"
  RMDir /r "$INSTDIR\runtime"
  RMDir /r "$INSTDIR\docs"
  Delete "$INSTDIR\ETC-WindowBuilder.exe"
  Delete "$INSTDIR\app.ico"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\uninst.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${UNINSTKEY}"
SectionEnd
