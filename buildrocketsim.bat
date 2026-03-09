@echo off

set "CL_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\bin\Hostx64\x64\cl.exe"
set "LIBEXE=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\bin\Hostx64\x64\lib.exe"

set "WINSDK_INC=C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0"
set "WINSDK_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0"

set "MSVC_INC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\include"
set "MSVC_LIB=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\lib\x64"

set "ROOT=%~dp0"
set "SRC=%ROOT%RocketSim\RocketSim-main"
set "OUT=%ROOT%RocketSimBuild\temp_objs"
set "OUTLIB=%ROOT%RocketSimBuild\RocketSim.lib"

if not exist "%OUT%" mkdir "%OUT%"

set "INCLUDE=%MSVC_INC%;%WINSDK_INC%\um;%WINSDK_INC%\shared;%WINSDK_INC%\ucrt"
set "LIB=%MSVC_LIB%;%WINSDK_LIB%\um\x64;%WINSDK_LIB%\ucrt\x64"

set "CLFLAGS=/nologo /c /MD /EHsc /O2 /std:c++20"

set "CLFLAGS=%CLFLAGS% /I \"%SRC%\" /I \"%SRC%\libsrc\bullet3-3.24\""

echo.
echo ===============================================================
echo  Compiling RocketSim & Bullet sources...
echo ===============================================================

for /R "%SRC%" %%F in (*.cpp) do (
    echo  [CL] %%~pnxF
    "%CL_EXE%" %CLFLAGS% "%%F" /Fo"%OUT%\%%~nxF.obj" || goto :build_fail
)

echo.
echo ===============================================================
echo  Creating RocketSim.lib...
echo ===============================================================

del /F /Q "%OUTLIB%" >nul 2>&1
"%LIBEXE%" /nologo /out:"%OUTLIB%" "%OUT%\*.obj"  || goto :build_fail

rd /S /Q "%OUT%" >nul 2>&1

echo.
echo =========================
echo   RocketSim.lib READY
echo =========================
goto :eof

:build_fail
echo.
echo *** BUILD FAILED ***
exit /b 1
