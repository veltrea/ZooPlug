@echo off
REM Build and run the ZooPlug pure-logic unit tests on Windows with MSVC.
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

echo === BUILD test_shellexec ===
cl /std:c++17 /utf-8 /EHsc /nologo /W4 /I Source ^
    Source\ShellExec.cpp Source\ProcessRun.cpp tests\test_shellexec.cpp ^
    /Fe:tests\test_shellexec.exe
if errorlevel 1 exit /b 1
echo === RUN test_shellexec ===
tests\test_shellexec.exe
if errorlevel 1 set "FAILED=1"

echo.
echo === BUILD test_powershellexec ===
cl /std:c++17 /utf-8 /EHsc /nologo /W4 /I Source ^
    Source\PowerShellExec.cpp Source\ProcessRun.cpp Source\ShellExec.cpp tests\test_powershellexec.cpp ^
    /Fe:tests\test_powershellexec.exe
if errorlevel 1 exit /b 1
echo === RUN test_powershellexec ===
tests\test_powershellexec.exe
if errorlevel 1 set "FAILED=1"

echo.
echo === BUILD test_mooerror ===
cl /std:c++17 /utf-8 /EHsc /nologo /W4 /I Source ^
    Source\MooError.cpp tests\test_mooerror.cpp ^
    /Fe:tests\test_mooerror.exe
if errorlevel 1 exit /b 1
echo === RUN test_mooerror ===
tests\test_mooerror.exe
if errorlevel 1 set "FAILED=1"

echo.
echo === BUILD test_fileops ===
cl /std:c++17 /utf-8 /EHsc /nologo /W4 /I Source ^
    Source\FileOps.cpp Source\ShellExec.cpp Source\ProcessRun.cpp tests\test_fileops.cpp ^
    /Fe:tests\test_fileops.exe
if errorlevel 1 exit /b 1
echo === RUN test_fileops ===
tests\test_fileops.exe
if errorlevel 1 set "FAILED=1"

echo.
echo === BUILD test_hash ===
cl /std:c++17 /utf-8 /EHsc /nologo /W4 /I Source ^
    Source\HashImpl.cpp Source\FileOps.cpp Source\ShellExec.cpp Source\ProcessRun.cpp tests\test_hash.cpp ^
    /Fe:tests\test_hash.exe
if errorlevel 1 exit /b 1
echo === RUN test_hash ===
tests\test_hash.exe
if errorlevel 1 set "FAILED=1"

echo.
echo === BUILD test_zipops ===
cl /std:c++17 /utf-8 /EHsc /nologo /W4 /I Source /I Libraries\miniz ^
    Source\ZipOps.cpp Source\FileOps.cpp Source\ShellExec.cpp Source\ProcessRun.cpp ^
    Libraries\miniz\miniz.c tests\test_zipops.cpp ^
    /Fe:tests\test_zipops.exe
if errorlevel 1 exit /b 1
echo === RUN test_zipops ===
tests\test_zipops.exe
if errorlevel 1 set "FAILED=1"

echo.
echo === BUILD test_netops ===
cl /std:c++17 /utf-8 /EHsc /nologo /W4 /I Source ^
    Source\NetOps.cpp Source\FileOps.cpp Source\ShellExec.cpp Source\ProcessRun.cpp tests\test_netops.cpp ^
    /Fe:tests\test_netops.exe ^
    /link wininet.lib
if errorlevel 1 exit /b 1
echo === RUN test_netops ===
tests\test_netops.exe
if errorlevel 1 set "FAILED=1"

echo.
echo === BUILD test_processops ===
cl /std:c++17 /utf-8 /EHsc /nologo /W4 /I Source ^
    Source\ProcessOps.cpp tests\test_processops.cpp ^
    /Fe:tests\test_processops.exe
if errorlevel 1 exit /b 1
echo === RUN test_processops ===
tests\test_processops.exe
if errorlevel 1 set "FAILED=1"

echo.
echo === BUILD test_printerops ===
cl /std:c++17 /utf-8 /EHsc /nologo /W4 /I Source ^
    Source\PrinterOps.cpp tests\test_printerops.cpp ^
    /Fe:tests\test_printerops.exe ^
    /link winspool.lib
if errorlevel 1 exit /b 1
echo === RUN test_printerops ===
tests\test_printerops.exe
if errorlevel 1 set "FAILED=1"

echo.
echo === BUILD test_progressops ===
cl /std:c++17 /utf-8 /EHsc /nologo /W4 /I Source ^
    Source\ProgressOps.cpp tests\test_progressops.cpp ^
    /Fe:tests\test_progressops.exe
if errorlevel 1 exit /b 1
echo === RUN test_progressops ===
tests\test_progressops.exe
if errorlevel 1 set "FAILED=1"

echo.
echo === BUILD test_hotkeyops ===
cl /std:c++17 /utf-8 /EHsc /nologo /W4 /I Source ^
    Source\HotkeyOps.cpp tests\test_hotkeyops.cpp ^
    /Fe:tests\test_hotkeyops.exe ^
    /link user32.lib
if errorlevel 1 exit /b 1
echo === RUN test_hotkeyops ===
tests\test_hotkeyops.exe
if errorlevel 1 set "FAILED=1"

if defined FAILED ( echo. & echo SOME TESTS FAILED & exit /b 1 )
echo.
echo ALL WINDOWS TESTS PASSED
