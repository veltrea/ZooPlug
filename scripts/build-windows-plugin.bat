@echo off
REM Build the ZooPlug FileMaker plug-in (ZooPlug.fmx64) with MSVC.
REM Links the vendored FMWrapper.lib under Libraries\win64. No CMake needed.
setlocal enabledelayedexpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere not found. Install Visual Studio with the C++ workload.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -find VC\Auxiliary\Build\vcvars64.bat`) do set "VCVARS=%%i"
if not defined VCVARS (
    echo [ERROR] vcvars64.bat not found via vswhere.
    exit /b 1
)
call "%VCVARS%" >nul

cd /d "%~dp0.."
if not exist build mkdir build

REM /LD = DLL, /MT = static CRT (no VC++ redistributable needed at the end user),
REM /utf-8 = read the UTF-8 sources correctly. Output extension is .fmx64.
cl /std:c++17 /utf-8 /EHsc /O2 /MT /nologo /W3 /LD ^
   /I Source /I Headers ^
   Source\ZooPlug.cpp Source\ShellExec.cpp ^
   /Fo:build\ /Fe:build\ZooPlug.fmx64 ^
   /link Libraries\win64\FMWrapper.lib
if errorlevel 1 exit /b 1

echo.
echo === Exported entry point ===
dumpbin /nologo /exports build\ZooPlug.fmx64 | findstr /i "FMExternCallProc"
echo.
echo Built: build\ZooPlug.fmx64
