@echo off
REM Build and run the ShellExec unit test on Windows with MSVC.
REM No FileMaker SDK needed - just a C++ compiler. VS is located via vswhere.
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

REM This script lives in the repo's scripts\ folder; move to the repo root.
cd /d "%~dp0.."

cl /std:c++17 /utf-8 /EHsc /nologo /W4 /I Source Source\ShellExec.cpp tests\test_shellexec.cpp /Fe:tests\test_shellexec.exe
if errorlevel 1 exit /b 1

echo === RUN ===
tests\test_shellexec.exe
