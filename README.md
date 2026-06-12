# ZooPlug

**Read this in other languages:** [日本語](README.ja.md)

A small FileMaker plug-in that reproduces MooPlug's `moo_shell()` function: run a
one-line shell command from a FileMaker calculation and get its output back as
text.

```
moo_shell ( "echo %USERNAME%" )      // → the current Windows user name
moo_shell ( "echo %COMPUTERNAME%" )  // → the computer name
moo_shell ( "set" )                  // → every environment variable, one per line
moo_shell ( "net config workstation" )
```

The plug-in scaffolding is based on Mark Banks'
[SimplePlugin](http://banks.id.au/filemaker/plugins/simpleplugin/) template.

## The `moo_shell` function

```
moo_shell ( command )
```

- **command** — a one-line command to run in the shell.
- **Returns** — the combined standard output and standard error of the command, as text.

Behaviour:

- **Windows:** the command runs through `cmd.exe /S /C "<command>"`. No console
  window appears.
- **macOS:** the command runs through `/bin/sh -c "<command>"`. (Provided so the
  same calculations and the unit tests work on a Mac; the original MooPlug was
  Windows-only.)
- Line endings are normalised to CR (`\r`), which is FileMaker's internal line
  break, and the trailing newline is trimmed — so `moo_shell ( "echo %USERNAME%" )`
  returns just the name with no empty line after it.
- On Windows the output is decoded from the console's OEM code page (CP932 /
  Shift_JIS on a Japanese system) and returned to FileMaker as Unicode.

> **Security:** `moo_shell` runs whatever you pass it. Never build a command out
> of untrusted input (field values from users, data from the web, …) — that is a
> shell-injection risk. Pass only commands you control.

## Repository layout

```
Source/ShellExec.h      Pure shell-execution logic (no FileMaker dependency)
Source/ShellExec.cpp    Windows (CreateProcess) and POSIX (popen) implementations
Source/ZooPlug.cpp      FileMaker glue: registers moo_shell, calls ShellExec
tests/test_shellexec.cpp Standalone unit test for the pure logic
Headers/FMWrapper/      Claris FileMaker plug-in API headers, v77 (see License.txt)
Libraries/              Bundled FMWrapper link libs: Windows / macOS (Linux: from SDK)
CMakeLists.txt          Builds the test everywhere; the plug-in on Win/Mac/Linux
Info.plist              macOS bundle metadata
```

The shell logic is deliberately separated from the FileMaker API so it can be
built and tested **without** FileMaker or the SDK.

## Building and testing the logic (no SDK required)

```sh
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Or compile the test directly:

```sh
c++ -std=c++17 -I Source Source/ShellExec.cpp tests/test_shellexec.cpp -o test
./test
```

On Windows you can run the same test without CMake via `scripts\test-windows.bat`,
which locates Visual Studio automatically and builds with MSVC.

## Building the plug-in

The headers (v77) **and** the FMWrapper link libraries for Windows and macOS are
bundled under `Libraries/`, so building for those two needs no separate
[Claris SDK](https://www.claris.com/resources/downloads/) download. The Linux
library is not bundled — get `libFMWrapper.so` from the SDK (see the Linux section).

### Windows → `ZooPlug.fmx64`

```sh
cmake -B build -DBUILD_PLUGIN=ON
cmake --build build --config Release
```

Or, without CMake, run `scripts\build-windows-plugin.bat` — it finds Visual
Studio, links the bundled `FMWrapper.lib`, and writes `build\ZooPlug.fmx64`. You
can also open the folder directly in Visual Studio 2019/2022/2026
(File → Open → Folder), which picks up `CMakeLists.txt` automatically.

### macOS → `ZooPlug.fmplugin`

```sh
cmake -B build -DBUILD_PLUGIN=ON
cmake --build build
```

Produces a universal-ready `ZooPlug.fmplugin` bundle linked against the bundled
`FMWrapper.framework`.

### Linux → `ZooPlug.fmx` (FileMaker Server)

The Linux library is **not bundled**. Get `libFMWrapper.so` from the Claris SDK
(`Libraries/Linux/<U22|U24>/<x64|arm64>/libFMWrapper.so`) and either drop it into
this repo under `Libraries/linux/<U22|U24>/<x64|arm64>/` (CMake auto-detects it) or
pass its path with `-DFMX_LIB`:

```sh
cmake -B build -DBUILD_PLUGIN=ON -DFMX_LIB=/path/to/libFMWrapper.so
cmake --build build
```

Claris builds the official sample with **clang + libc++**, so for ABI safety prefer:

```sh
cmake -B build -DBUILD_PLUGIN=ON -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS="-stdlib=libc++"
cmake --build build
```

`scripts/build-linux.sh` automates this once `libFMWrapper.so` is in place. (The
Linux build was verified on Ubuntu 22.04 / clang 14.)

> iOS is intentionally not supported: `moo_shell` cannot launch a process inside
> the iOS sandbox.

## Installing into FileMaker

1. Quit FileMaker Pro.
2. Copy `ZooPlug.fmx64` (Windows) or `ZooPlug.fmplugin` (macOS) into FileMaker's
   **Extensions** folder, or install it from a script with the
   **Install Plug-In File** script step.
3. Start FileMaker Pro and enable **ZooPlug** in
   **Edit → Preferences → Plug-Ins** (Windows) /
   **FileMaker Pro → Settings → Plug-Ins** (macOS).
4. `moo_shell` is now available in the calculation dialog.

## Credits

- [SimplePlugin](http://banks.id.au/filemaker/plugins/simpleplugin/) by Mark Banks — plug-in scaffolding.
- MooPlug — the original `moo_shell` function this reproduces.
- FMWrapper headers © Claris International Inc., redistributed under their bundled license.

See [License.txt](License.txt) for details.
