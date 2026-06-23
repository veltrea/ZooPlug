# ZooPlug

**Read this in other languages:** [日本語](README.ja.md)

ZooPlug is an **independent** FileMaker plug-in for Windows / macOS / Linux.
It provides **39 functions** — file and folder operations, shell execution
(cmd / PowerShell), HTTP / FTP downloads, Zip, hashing, dialogs, printers,
processes, hotkeys — under the `zoo_*` namespace.

```
zoo_FileWrite ( Get(TemporaryPath) & "log.txt" ; "hello" )    // → 1
zoo_Hash ( "sha256" ; "abc" )                                 // → ba7816bf...
zoo_DownloadText ( "https://example.com/" )                   // → HTML body
zoo_Shell ( "ipconfig" )                                      // → network info
zoo_powershell ( "Write-Output 'Hello 表予能'" )              // → Hello 表予能 (UTF-8 safe)
```

## Why this exists — the state of upstream MooPlug

ZooPlug grew out of **MooPlug** (2007–2023), the Windows-only FileMaker
plug-in **Adam Dempsey** maintained and gave away for many years. MooPlug
let you call **36 functions** straight from FileMaker calculations — file /
folder, shell, dialog, FTP, hash, Zip, hotkey, process, printer — plus two
undocumented ones (`Moo_Shell` and `Moo_FTPDelete`, for 38 total). It was
embedded in countless FileMaker solutions over that time.

Today, however:

- The distribution site **mooplug.com is gone**. The official documentation
  is only readable through the [Wayback Machine](https://web.archive.org/web/*/mooplug.com).
- No **64-bit build was ever released** before development stopped, so
  MooPlug cannot be loaded by FileMaker Pro 19 and later (which are 64-bit
  only).
- Because MooPlug was binary-only, users have no way to fix it themselves.
  Every OS or FileMaker upgrade tightens the bind of depending on a binary
  that can be neither obtained nor modified.

ZooPlug is built to fill that gap. It is a **clean, independent C++
reimplementation** based on MooPlug's published manual and **behavioural
observation** of MooPlug 0.4.9, but it lives in its own `zoo_*` namespace
so that it never conflicts with upstream MooPlug.

## Sibling product cowPlug — for existing MooPlug solutions

If you have existing FileMaker files that call `Moo_FileWrite( ... )` etc.
and want them to keep working **without rewriting the calculations**, use
**cowPlug** instead.

| Product | Function prefix | Functions | Use case |
|---|---|---|---|
| **ZooPlug** | `zoo_*` | 38 + `zoo_powershell` extension = **39** | Independent namespace. New solutions. Won't collide with upstream MooPlug |
| **cowPlug** | `Moo_*` (strict 0.4.9 parity) | 38 only (no extensions like `zoo_powershell`) | Drop-in replacement for existing MooPlug solutions during upstream's absence |

The argument layouts, return values, and error codes are identical to
ZooPlug; only the prefix differs (and cowPlug omits ZooPlug-original
extensions like `zoo_powershell`). **cowPlug is distributed as a pre-built
binary only** (from Releases) — ZooPlug is the open-source product.

> ⚠️ **If Adam Dempsey resumes publishing MooPlug, cowPlug will be
> withdrawn**, because its `Moo_*` namespace would collide with upstream.
> ZooPlug (the `zoo_*` namespace) is independent and not affected.
> **For new projects, prefer ZooPlug.** cowPlug exists only as a stopgap
> while upstream is dormant.

## Origin of the API design

> **The API design (argument layout, error-code numbering, return-value
> policy, function-area grouping) is Adam Dempsey's, observed from
> MooPlug.** ZooPlug is an **independent implementation** inspired by
> that design; it claims no design originality. Thank you to Adam for
> years of building and freely sharing MooPlug.
>
> If anything here needs correcting, or if Adam would prefer this
> distribution to stop, please open a GitHub Issue. **We will follow
> whatever direction Adam asks for.**

ZooPlug / cowPlug have never had upstream's source code (Adam's source has
never been public). They are built from upstream's published manual
(preserved by the Wayback Machine) together with **behavioural observation**
of MooPlug 0.4.9 running inside FileMaker Pro 11 — the function list and
the registered prototype strings as shown in FileMaker's calculation
editor, the values each function returns, and the error strings reported
by `Moo_ErrorDetail`. All of these are visible to any FileMaker user with
a working copy of MooPlug. **None of upstream's compiled code is carried in.**

## What ZooPlug provides

| Group | Count | Examples |
|---|---|---|
| Meta | 2 | `zoo_Version` / `zoo_ErrorDetail` |
| Shell | 1 | `zoo_Shell` (cmd) |
| **PowerShell (ZooPlug-original)** | **1** | **`zoo_powershell`** (PowerShell 5.1 / 7, UTF-8 safe, works under CLM / WDAC) |
| File | 7 | `zoo_FileExists` / `zoo_FileCopy` / `zoo_FileDelete` / `zoo_FileMove` / `zoo_FileRead` / `zoo_FileWrite` / `zoo_FileInfo` |
| Folder | 6 | `zoo_FolderExists` / `zoo_FolderCopy` / `zoo_FolderCreate` / `zoo_FolderDelete` / `zoo_FolderMove` / `zoo_FolderList` |
| Hash | 1 | `zoo_Hash` (MD5 / SHA-1 / SHA-256 / SHA-512, files supported) |
| Zip | 3 | `zoo_ZipCompress` / `zoo_ZipExtract` / `zoo_ZipList` |
| Net | 5 | `zoo_DownloadText` / `zoo_DownloadFile` / `zoo_FTPDownload` / `zoo_FTPUpload` / `zoo_FTPDelete` |
| Dialog | 3 | `zoo_DialogColour` / `zoo_DialogFile` / `zoo_DialogFolder` |
| Printer | 2 | `zoo_PrinterDefault` / `zoo_PrinterList` |
| Process | 4 | `zoo_ProcessCount` / `zoo_ProcessKill` / `zoo_ProcessList` / `zoo_ProcessRunning` |
| Progress UI | 1 | `zoo_ProgressOptions` |
| Hotkey | 3 | `zoo_HotkeyAdd` / `zoo_HotkeyList` / `zoo_HotkeyRemove` (a key press starts a FileMaker script) |

**The full per-function reference is at
→ [docs/function-reference.md](docs/function-reference.md)**.

The cowPlug function set is the same table with `zoo_` mentally replaced
by `Moo_` and `zoo_powershell` removed — i.e. exactly the 38 functions of
MooPlug 0.4.9.

> **About `zoo_powershell`.** This is a ZooPlug-only addition (not in
> cowPlug). It exists for the cases `zoo_Shell`'s cmd backend can't cover
> well — round-tripping UTF-8 (especially Japanese) cleanly, running under
> Constrained Language Mode behind AppLocker / WDAC, and scripts with
> multi-line, quotes, or `$` interpolation. Design details in
> [`docs/zoo-powershell-design.md`](docs/zoo-powershell-design.md).

## Supported platforms

Upstream MooPlug was **Windows (32-bit) only**. ZooPlug / cowPlug run on:

| Platform | Binary | Tested on |
|---|---|---|
| **Windows 64-bit** | `ZooPlug.fmx64` / `cowPlug.fmx64` | FileMaker Pro 19 |
| **macOS** (Intel + Apple Silicon, universal) | `ZooPlug.fmplugin` / `cowPlug.fmplugin` | macOS 15 (Sequoia) |
| **Linux** (FileMaker Server) | `ZooPlug.fmx` / `cowPlug.fmx` | Ubuntu 22.04 / 24.04 |

> iOS is not supported because the OS sandbox disallows spawning
> processes. On macOS, `zoo_DialogColour` (no modal colour picker
> available) and the `bGlobal` flag of `zoo_HotkeyAdd` (Carbon limitation)
> have platform-specific caveats — see
> [function-reference.md](docs/function-reference.md).

## Installation

1. Quit FileMaker Pro.
2. Copy the plug-in into the Extensions folder:
   - Windows: `ZooPlug.fmx64` → `C:\Program Files\FileMaker\FileMaker Pro 19\Extensions\`
   - macOS: `ZooPlug.fmplugin` → `~/Library/Application Support/FileMaker/Extensions/`
3. Start FileMaker Pro and enable **ZooPlug** under **Preferences →
   Plug-ins**.
4. The `zoo_*` functions become available in calculations.

> For cowPlug, replace `ZooPlug` with `cowPlug` in the steps above.
> **ZooPlug and cowPlug can be enabled at the same time** — their
> namespaces don't collide. However, cowPlug cannot be loaded alongside
> the original MooPlug, because both register the `Moo_*` names.

> **macOS Gatekeeper.** Release binaries are ad-hoc signed. If first
> launch is blocked, strip the quarantine attribute with
> `xattr -dr com.apple.quarantine ZooPlug.fmplugin`.
>
> **FileMaker 19.2+ hotkeys.** Enable the `fmplugin` extended-privilege in
> the security privilege set of the calling file, or `zoo_HotkeyAdd` will
> see error 825 when it tries to fire a script.

## Build from source

The unit tests do not require the FileMaker SDK.

### Unit tests (no SDK, any platform)

```sh
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Eleven test executables run: `shellexec` / `powershellexec` / `mooerror`
/ `fileops` / `hash` / `zipops` / `netops` / `processops` / `printerops`
/ `progressops` / `hotkeyops`.

### Windows → `ZooPlug.fmx64`

```bat
scripts\build-windows-plugin.bat            REM x64 (FileMaker Pro 19)
scripts\build-windows-plugin.bat x86        REM 32-bit (FileMaker Pro 11)
```

Auto-detects Visual Studio 2019 / 2022 / 2026, links the bundled
`FMWrapper.lib`, and produces `build\ZooPlug.fmx64`. Uses Windows SDK
libraries that ship with the OS (`wininet` / `winspool` / `comdlg32` /
`user32` etc.) — no extra SDK needed. (The 32-bit / FM11 build also needs
the FM11 SDK; see the build script.)

### macOS → `ZooPlug.fmplugin` (ad-hoc signed, universal)

```sh
bash scripts/build-and-sign-mac.sh
```

CMake configure → build (arm64 + x86_64) → ad-hoc deep sign
(`codesign --sign - --deep --force --timestamp=none`) → produces
`dist/ZooPlug.fmplugin`. Pass `INSTALL=1` to copy the plug-in into
`~/Library/Application Support/FileMaker/Extensions/`.

### Linux → `ZooPlug.fmx` (FileMaker Server)

```sh
bash scripts/build-linux.sh
```

The Linux `libFMWrapper.so` is **not bundled** in this repository; obtain
it from the [Claris SDK](https://www.claris.com/resources/downloads/) and
place it under `Libraries/linux/<U22|U24>/<x64|arm64>/`, or pass
`-DFMX_LIB=/path/to/libFMWrapper.so` to CMake. Tested with Ubuntu 22.04
and clang 14 with libc++.

## Verification and compatibility

We provide a per-function manual walkthrough in the Data Viewer, plus a
self-test script that runs everything in one pass:

- [docs/function-reference.md](docs/function-reference.md) — user-facing reference for all 39 ZooPlug (`zoo_*`) functions

For cowPlug, the same reference applies with `zoo_` read as `Moo_` and `zoo_powershell` removed — i.e. the 38 functions of MooPlug 0.4.9.

Wherever upstream documentation and the live behaviour disagree, **we
follow the live behaviour**. Examples:

- The error format is `Err_N` (with the underscore). Upstream docs print
  it as `ErrN`, but the real return strings of MooPlug 0.4.9 have the
  underscore.
- `zoo_FileWrite` (and `Moo_FileWrite` in cowPlug) takes **three arguments**
  (`bAppend` only). The four-argument form documented upstream
  (`bAppend ; bOverwrite`) is not what the real calculation editor shows.
- `zoo_FTPDelete` (and `Moo_FTPDelete` in cowPlug) is not in upstream's
  manual, but 0.4.9 registers it in the real external-function list.
- `Moo_PrinterSet` has a documented description but does not appear in the
  registered external-function list, so neither ZooPlug nor cowPlug ships it.

A few minor return-value details (`zoo_Version`'s return string,
`zoo_DialogColour`'s return format, the default separator in
`zoo_ProcessList`, etc.) are noted in the function reference as tentative
and may shift if a future compatibility review reveals a different
upstream behaviour.

## Contact and contributions

Issues are very welcome — "this function differs from upstream", "I don't
understand how to call X", "please add behaviour Y", anything.

- Repository: <https://github.com/veltrea/ZooPlug>

## Credits and licence

- **MooPlug — Adam Dempsey.** The original FileMaker plug-in that ZooPlug
  and cowPlug are based on. **The API design (function names, argument
  layouts, error-code numbering, return-value policy) is entirely Adam's**
  — ZooPlug only renames the namespace to `zoo_*`, cowPlug keeps the
  `Moo_*` namespace verbatim. Thank you for the years of work.
- **[SimplePlugin](http://banks.id.au/filemaker/plugins/simpleplugin/) — Mark Banks.**
  The plug-in skeleton (entry points and the function-registration
  pattern) used by ZooPlug / cowPlug. Used under BSD 3-Clause.
- **[miniz](https://github.com/richgel999/miniz) 3.0.2** — bundled and used
  for the Zip functions (MIT).
- **FMWrapper (Claris FileMaker Plug-In API)** — bundled under `Headers/`
  / `Libraries/`. Redistributed under Claris International Inc.'s licence.
- ZooPlug / cowPlug themselves are released under the **BSD 3-Clause
  License**.

See [License.txt](License.txt) for full details.
