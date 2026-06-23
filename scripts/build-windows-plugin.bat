@echo off
REM Build the ZooPlug FileMaker plug-in (.fmx / .fmx64) with MSVC.
REM Links the vendored FMWrapper.lib. No CMake needed.
REM
REM Usage:
REM   scripts\build-windows-plugin.bat            REM ZooPlug.fmx64 (zoo_* + extensions, x64, FM19+)
REM   scripts\build-windows-plugin.bat x86        REM ZooPlug.fmx (32-bit, FileMaker Pro 11)
REM   scripts\build-windows-plugin.bat x86 14.16  REM x86 with a specific MSVC toolset
setlocal enabledelayedexpansion

set "TARGET_NAME=ZooPlug"
set "RC_FILE=Source\ZooPlug.rc"

REM --- Arch detection (default: x64) ---
set "ARCH=%~1"
if /i "%ARCH%"=="" set "ARCH=x64"
if /i "%ARCH%"=="64" set "ARCH=x64"
if /i "%ARCH%"=="32" set "ARCH=x86"
if /i "%ARCH%"=="win32" set "ARCH=x86"

REM x64 = current SDK (FM19+ host). x86 = FileMaker 11-era SDK: FM Pro 11's
REM FMWrapper.dll only exports the FM11 API surface, so the 32-bit build MUST
REM link against the FM11 FMWrapper.lib and compile against the FM11 headers,
REM otherwise the import table references symbols missing from FM11's
REM FMWrapper.dll and the plug-in fails to load (silently skipped in Prefs).
REM The FM11 SDK (Libraries\fm11-sdk) is a third-party redistributable from the
REM Claris FileMaker Plug-in SDK; it is NOT bundled in the public repo. Obtain it
REM separately to build the 32-bit / FM11 target.
if /i "%ARCH%"=="x64" (
    set "VCVARS_REL=VC\Auxiliary\Build\vcvars64.bat"
    set "FMWRAPPER_LIB=Libraries\win64\FMWrapper.lib"
    set "FMX_HEADERS=Headers"
    set "OUT_EXT=fmx64"
) else if /i "%ARCH%"=="x86" (
    set "VCVARS_REL=VC\Auxiliary\Build\vcvarsamd64_x86.bat"
    set "FMWRAPPER_LIB=Libraries\fm11-sdk\FMWrapper.lib"
    set "FMX_HEADERS=Libraries\fm11-sdk\Headers"
    REM The FM11 SDK headers use std::auto_ptr (removed in C++17). Re-enable it in
    REM MSVC's STL so the headers compile under /std:c++17 (our own code needs C++17).
    REM FM11_SDK=1 activates the compat shim in ZooPlug.cpp (maps the newer *UniquePtr
    REM and fmx::ptrtype names onto the FM11 SDK's *AutoPtr / FMX_Long).
    set "FMX_DEFS=/D_HAS_AUTO_PTR_ETC=1 /DFM11_SDK=1"
    set "OUT_EXT=fmx"
) else (
    echo [ERROR] Unknown arch: %ARCH% ^(expected x64 ^| x86^)
    exit /b 2
)

echo Target: %TARGET_NAME%  Arch: %ARCH%  Output: build\%TARGET_NAME%.%OUT_EXT%

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere not found. Install Visual Studio with the C++ workload.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -find %VCVARS_REL%`) do set "VCVARS=%%i"
if not defined VCVARS (
    echo [ERROR] %VCVARS_REL% not found via vswhere.
    exit /b 1
)
REM Optional 2nd arg: toolset version override (e.g. 14.16 = VS 2017 v141,
REM 14.00 = VS 2015 v140). Useful when FM Pro 11 rejects PE images produced
REM by the latest MSVC linker (14.x default = VS 2022). Leave empty to use
REM the default toolset for the latest installed VS instance.
set "TOOLSET_VER=%~2"
if defined TOOLSET_VER (
    call "%VCVARS%" -vcvars_ver=%TOOLSET_VER% >nul
    echo Using toolset version: %TOOLSET_VER%
) else (
    call "%VCVARS%" >nul
)

cd /d "%~dp0.."
if not exist build mkdir build

REM /LD = DLL, /MT = static CRT (no VC++ redistributable needed at the end user),
REM /utf-8 = read the UTF-8 sources correctly.
REM
REM 32-bit linker overrides for FM Pro 11 compatibility (FM Pro 11 ships with a
REM 2010-era Win32 plug-in loader that rejects PE images with Subsystem >= 6.00,
REM NX-compatible, or ASLR-relocatable flags). Set:
REM   /SUBSYSTEM:CONSOLE,5.01  -> FM Pro 11's loader expects a CONSOLE-subsystem DLL
REM   /OSVERSION:5.01          -> set the OS version field in the optional header
REM   /DYNAMICBASE:NO          -> clear IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE (ASLR)
REM   /NXCOMPAT:NO             -> clear IMAGE_DLLCHARACTERISTICS_NX_COMPAT
REM   /SAFESEH:NO              -> FM Pro 11's loader may reject DLLs declaring SafeSEH
REM 64-bit FileMaker hosts (Pro 19/Server) accept the modern flags so x64 keeps
REM the default linker behaviour.
if /i "%ARCH%"=="x86" (
    set "FM11_LINK_FLAGS=/SUBSYSTEM:CONSOLE,5.01 /OSVERSION:5.01 /DYNAMICBASE:NO /NXCOMPAT:NO /SAFESEH:NO"
) else (
    set "FM11_LINK_FLAGS="
)

REM Compile the classic FileMaker plug-in resource (STRINGTABLE: options string,
REM function prototypes, name, description). FM Pro 11 reads the plug-in identity
REM from this .rsrc section at scan time; without it the plug-in is silently
REM skipped and never listed in Preferences. rc.exe is on PATH via vcvars.
rc /nologo /fo build\%TARGET_NAME%.res %RC_FILE%
if errorlevel 1 (echo [ERROR] resource compile failed: %RC_FILE% & exit /b 1)

cl /std:c++17 /utf-8 /EHsc /O2 /MT /nologo /W3 /LD %FMX_DEFS% ^
   /I Source /I %FMX_HEADERS% /I Libraries\miniz ^
   Source\ZooPlug.cpp Source\ShellExec.cpp Source\ProcessRun.cpp Source\PowerShellExec.cpp ^
   Source\MooError.cpp Source\FileOps.cpp Source\HashImpl.cpp Source\ZipOps.cpp ^
   Source\NetOps.cpp Source\ProcessOps.cpp Source\PrinterOps.cpp Source\DialogOps.cpp ^
   Source\ProgressOps.cpp Source\HotkeyOps.cpp ^
   Libraries\miniz\miniz.c ^
   /Fo:build\ /Fe:build\%TARGET_NAME%.%OUT_EXT% ^
   /link build\%TARGET_NAME%.res %FMWRAPPER_LIB% wininet.lib winspool.lib comdlg32.lib shell32.lib ole32.lib user32.lib %FM11_LINK_FLAGS%
if errorlevel 1 exit /b 1
echo Built: build\%TARGET_NAME%.%OUT_EXT%
