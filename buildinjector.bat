@echo off
set "CompilerPath=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\bin\Hostx64\x64\cl.exe"
set "LibPath=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\bin\Hostx64\x64\lib.exe"
set "WindowsSDKInclude=C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0"
set "WindowsSDKLib=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0"
set "MSVCInclude=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\include"
set "MSVCLib=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\lib\x64"

echo Compiling injector.cpp...
"%CompilerPath%" /I"%WindowsSDKInclude%\um" /I"%WindowsSDKInclude%\shared" /I"%WindowsSDKInclude%\ucrt" /I"%MSVCInclude%" injector.cpp /Fe:injector.exe /link /LIBPATH:"%WindowsSDKLib%\um\x64" /LIBPATH:"%WindowsSDKLib%\ucrt\x64" /LIBPATH:"%MSVCLib%" kernel32.lib user32.lib

if %ERRORLEVEL% EQU 0 (
    echo Build successful! injector.exe created.
) else (
    echo Build failed!
)

pause
