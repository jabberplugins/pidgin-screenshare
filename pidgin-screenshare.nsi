!include LogicLib.nsh
!include FileFunc.nsh

Var pidgin_install_dir

Function SelectPidginDir
    StrCpy $pidgin_install_dir "$ProgramFiles(x86)\Pidgin"
    nsDialogs::SelectFolderDialog "Please select the directory where Pidgin.exe is located (by default its C:\Program Files (x86)*)" $pidgin_install_dir
    Pop $pidgin_install_dir
FunctionEnd

Function ExtractPlugin
    File "/oname=$PLUGINSDIR\pidgin-screenshare.dll" "pidgin-screenshare.dll"
FunctionEnd

Function CopyPlugin
    CopyFiles "$PLUGINSDIR\pidgin-screenshare.dll" "$pidgin_install_dir\plugins\pidgin-screenshare.dll"
FunctionEnd

Function SetupAssets
    SetShellVarContext all

    ExpandEnvStrings $0 %APPDATA%
    StrCpy $1 "$0\pidgin-ssotr"

    CreateDirectory "$1"

    File "/oname=$1\pidgin-screenshare.png" "pidgin-screenshare.png"
    File "/oname=$1\libdeflate.dll" "libdeflate.dll"
    File "/oname=$1\libotr.dll" "libotr.dll"
FunctionEnd

Function .onInit
    Call SelectPidginDir
    Call ExtractPlugin
    Call CopyPlugin
    Call SetupAssets
FunctionEnd

Section
    ; No files to install, just a placeholder
SectionEnd
