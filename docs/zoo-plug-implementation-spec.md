# ZooPlug full-function reimplementation — design spec

**Read this in other languages:** [日本語](zoo-plug-implementation-spec.ja.md)

> Design spec for reproducing MooPlug 0.4.9 (all 38 functions) in ZooPlug.
> The primary references are upstream's published manual (preserved by
> the Wayback Machine) and behavioural observation of MooPlug 0.4.9
> running inside FileMaker Pro 11. The existing implementation
> (`Source/ZooPlug.cpp` / `Source/ShellExec.*`) is the design that the
> remaining functions follow and extend.

---

## 1. Goal and scope

- **Goal**: reproduce MooPlug 0.4.9's external functions so they are
  **compatible at the signature, return-value, and error-code level** as
  seen from a FileMaker calculation.
- **What "compatible" means here**: the specification fixed by upstream's
  public manual and by cross-checking MooPlug 0.4.9 running inside
  FileMaker Pro 11 (the list of registered functions and their prototype
  strings as shown in the calculation editor, the values each function
  returns, and the error strings reported by `zoo_ErrorDetail`). When the
  manual disagrees with the observed behaviour, **the observed behaviour
  is authoritative.**
- **Target functions**: the **38 functions** the real plug-in registers
  (36 documented + `zoo_Shell` + `zoo_FTPDelete`). `Moo_PrinterSet` is
  documented upstream but not in the registered function list, so ZooPlug **does not ship it**.
- **Existing assets**: `zoo_Shell` is already implemented. This spec
  defines the work to add the remaining 37 functions.

### Out of scope (explicit)

- `Moo_PrinterSet` (not registered in the real plug-in).
- Functions that existed in older MooPlug versions but disappeared by
  0.4.9 (e.g. `zoo_FolderInfo`).
- iOS (like `zoo_Shell`, no process launching or GUI is possible inside
  the iOS sandbox).

---

## 2. Design principles (carried over from the existing ZooPlug)

1. **Pure logic is separated from FileMaker glue.** Just as `zoo_Shell`
   splits into `ShellExec.h/cpp` (FMWrapper-independent) plus
   `ZooPlug.cpp` (the glue), every function follows the same split.
   The pure logic can be unit-tested without FileMaker, the
   `tsx`-equivalent path: `c++ -I Source ... && ./test`.
2. **The prototype string is the source of truth.** Functions are
   registered with the exact prototype strings as seen in real 0.4.9.
   The existing `RegisterFunction` / `NumberOfParameters` understands
   `{ ; optional }` notation, so **getting the prototype right** is
   usually enough to make the required / optional argument counts
   correct automatically (with a few manual adjustments — see §5.4).
3. **Make it cross-platform from the start, when possible.** Functions
   that can run on `std::filesystem` / libcurl / miniz get a single
   implementation shared across Win / Mac / Linux; only the
   platform-specific functions use `#ifdef`.
4. **Errors are returned as MooPlug-format strings.** A function returns
   `Moo_<name>|Err_N` as its reply text (§5.1). `zoo_ErrorDetail`
   converts that string into the description.
5. **Stateful functions (Hotkey, ProgressOptions) must assume concurrent
   server calls.** Global state is mutex-protected.

---

## 3. Architecture

```
ZooPlug.fmplugin / .fmx64 / .fmx
└─ ZooPlug.cpp              FileMaker glue (registration / dispatch / argument extraction / error shaping)
   ├─ MooError.h/cpp        Code → description table, Err-string generation, zoo_ErrorDetail
   ├─ Pure logic layer (FMWrapper-independent, unit-testable)
   │   ├─ ShellExec.*       zoo_Shell (already implemented)
   │   ├─ FileOps.*         File / Folder 13 functions (std::filesystem)
   │   ├─ HashImpl.*        zoo_Hash (MD5 / SHA1 / SHA256 / SHA512 bundled implementations)
   │   ├─ ZipOps.*          Zip 3 functions (miniz wrapper)
   │   ├─ NetOps.*          Download 2 + FTP 3 (libcurl wrapper)
   │   └─ SysInfo.*         Logic parts shared by Process / Printer
   └─ Platform layer (split by #ifdef, GUI / OS-specific)
       ├─ Dialogs_win.* / Dialogs_mac.mm    Dialog 3 functions
       ├─ Printer_win.* / Printer_mac.*     Printer 2 functions
       ├─ Process_win.* / Process_mac.*     Process 4 functions
       ├─ Progress_win.* / Progress_mac.*   ProgressOptions + progress dialog
       └─ Hotkey_win.* / Hotkey_mac.*       Hotkey 3 functions (+ Idle dispatch)
```

- **Prefer single-file / small dependencies** to keep distribution
  light. libcurl is the one library where the static / dynamic decision
  has to be made on a per-OS basis (§8).
- **The platform layer covers the GUI- and StartScript-dependent
  functions only.** Data processing is shared across platforms.

---

## 4. Function categorisation and implementation plan (all 38)

The 38 functions split into three tiers by difficulty, dependencies, and
platform specificity. **Start with Tier A** — fastest progress and
lightest dependencies.

### Tier A — shared implementations (`std::filesystem` / bundled libraries; identical code on Win / Mac / Linux)

| Function | How | Difficulty | Notes |
|---|---|---|---|
| zoo_Version | Return a string constant | easy | Version meta-function; returns ZooPlug's own version string. No arguments. |
| zoo_ErrorDetail | Error-string → description map | easy | The full table is documented. §5.1 |
| zoo_FileExists | `fs::exists` | easy | |
| zoo_FileCopy | `fs::copy_file` | easy | bOverwrite / bProgress. Progress hooks into Tier C. |
| zoo_FileDelete | `fs::remove` | easy | |
| zoo_FileMove | `fs::rename` (cross-device falls back to copy + remove) | easy | |
| zoo_FileRead | Read text, return it | easy | |
| zoo_FileWrite | Write or append (**three arguments in real 0.4.9: bAppend only**) | easy | The docs' `bOverwrite` isn't in the real plug-in. §5.4 |
| zoo_FileInfo | size / created / modified = `std::filesystem`, version = Windows only | medium | version uses `#ifdef _WIN32` (`GetFileVersionInfo`) |
| zoo_FolderCopy | `fs::copy` (recursive) | easy | |
| zoo_FolderCreate | `fs::create_directories` | easy | |
| zoo_FolderDelete | `fs::remove_all` | easy | |
| zoo_FolderExists | `fs::is_directory` | easy | |
| zoo_FolderList | `directory_iterator` + glob (default `sPattern` `*.*`) | medium | sSeparator-joined |
| zoo_FolderMove | `fs::rename` | easy | |
| zoo_Hash | Bundled MD5 / SHA implementations (public domain) | medium | `sHash` picks the algorithm, `bFile` switches file / string |
| zoo_ZipCompress | miniz `mz_zip_writer_*` | medium | Password is classic Zip 2.0 (a miniz extension) — can defer |
| zoo_ZipExtract | miniz `mz_zip_reader_*` | medium | **Single file only** (matches upstream) |
| zoo_ZipList | miniz `mz_zip_reader_get_filename` | easy | |
| zoo_DownloadText | libcurl GET → return the body | medium | Plus one-time cost of integrating libcurl |
| zoo_DownloadFile | libcurl GET → save to temp / local → return the path | medium | bProgress hooks into Tier C |
| zoo_FTPDownload | libcurl FTP RETR | medium | bProgress hooks into Tier C |
| zoo_FTPUpload | libcurl FTP STOR | medium | |
| zoo_FTPDelete | libcurl FTP DELE (undocumented function) | easy | Piggybacks on NetOps |

### Tier B — platform-specific (Windows first; macOS in parallel if you want)

| Function | Windows | macOS | Difficulty |
|---|---|---|---|
| zoo_DialogColour | `ChooseColor` | `NSColorPanel` | medium |
| zoo_DialogFile | `GetOpenFileName` / `GetSaveFileName` | `NSOpenPanel` / `NSSavePanel` | medium |
| zoo_DialogFolder | `SHBrowseForFolder` / IFileDialog | `NSOpenPanel` (dir) | medium |
| zoo_PrinterDefault | `GetDefaultPrinter` / `SetDefaultPrinter` | CUPS `cupsGetDefault` / `cupsSetDefault` | medium |
| zoo_PrinterList | `EnumPrinters` | CUPS `cupsGetDests` | medium |
| zoo_ProcessCount | `CreateToolhelp32Snapshot` | `sysctl` / `libproc` | medium |
| zoo_ProcessList | same | same | medium |
| zoo_ProcessRunning | same | same | medium |
| zoo_ProcessKill | `OpenProcess` + `TerminateProcess` | `kill(2)` | medium |

> **GUI threading note**: plug-in functions run synchronously on
> FileMaker's calculation thread. In Pro that thread is effectively the
> main UI thread, so a modal dialog can be shown there — but macOS
> AppKit has the "main-thread-only" constraint, so be careful with
> `NSApp` state (consider `performSelectorOnMainThread` equivalents).
> Dialog / Printer / Process **don't make sense on Server / WebDirect**,
> so do not set the `kServerCompatible` flag for them.

### Tier C — async / resident; hardest (the Idle / StartScript discipline is mandatory)

| Function | What's hard | Required machinery |
|---|---|---|
| zoo_ProgressOptions + progress dialog | Show a separate UI during long-running work, reflect Download / FTP progress, persist state across calls | Progress window (Windows: custom dialog / Mac: NSProgressIndicator), progress callback, mutex-protected global state |
| zoo_HotkeyAdd / List / Remove | Register a global hotkey; on press, **start a FileMaker script**. Stay resident; call StartScript from a separate thread's message loop | Windows: hidden window + `RegisterHotKey` + message pump. The press is **pushed onto a completion queue**, and `kFMXT_Idle` (main thread) calls `FMX_StartScript`. Copy `cStartScript` / `cCurrentEnv` in Init (rule 3). 9th character `Y` (Idle enabled) in the options string is required |

> **Hotkey is the only seriously resident structure in this project.**
> Queue the press and call `FMX_StartScript` from `kFMXT_Idle` on the
> main thread. `FMX_StartScript` is a queued start (not
> immediate); FM 19.2+ requires the `fmplugin` extended privilege
> (without it, the call is rejected with error 825).

---

## 5. Cross-cutting concerns

### 5.1 Error-code compatibility (most important)

- MooPlug **returns errors as reply text** of the form `Moo_<name>|Err_N`
  (not as a numeric error). The real plug-in's form is **`Err_N` (with
  the underscore)**.
- Implementation: on every failure path, return
  `MakeError("zoo_FileCopy", 3)` → `"zoo_FileCopy|Err_3"` via
  `reply.SetAsText`.
- `zoo_ErrorDetail(sError)`: convert e.g. `"zoo_FileCopy|Err_3"` into
  `"Source file does not exist."`. Implemented as
  `std::unordered_map<std::string, std::string>`. **All entries come
  from upstream's Error Descriptions tables plus the descriptions
  returned by the real `zoo_ErrorDetail`** (covering 0.4.9's upper
  bounds: `FTPUpload Err_16`, `ZipExtract Err_12`, etc.).
- Note: FileMaker's `Get(LastExternalErrorDetail)` integration
  (`kPluginErrResult1..8`) is **optional** — MooPlug doesn't do it.
  For compatibility, the reply-text approach alone is sufficient.

### 5.2 Character encoding

> **Mind the Windows console encoding before you start.**
> cmd's output encoding varies per command, CP932 has 0x5C "danger
> bytes", `std::filesystem` mishandles Japanese paths, `_popen` has to
> be avoided — get any of these wrong and a Japanese environment will
> bite immediately.

- Argument extraction goes through a shared `TextAsUTF8` helper. Returns
  use `AssignWithLength(..., kEncoding_UTF8)`.
- **File paths on Windows**: `std::filesystem::path(utf8str)` is
  **wrong** (ANSI = CP932 interpretation drops bytes). Use `fs::u8path`
  or go through UTF-16. Required for Japanese-path support.
- **Shell-exec (zoo_Shell)**: not `_popen` / `system` but `CreateProcessW`
  + pipe to capture raw bytes, then decode as the system's OEM code
  page (`CP_OEMCP`: 932 on a Japanese system, 437 in the US, 850 in
  Western Europe, etc.) and convert to UTF-8. The launcher uses
  `cmd.exe /S /C "<command>"` — the `/S` flag strips just the outer
  pair of quotes so a one-liner with its own quoting survives unchanged.
  `stderr` is folded into the same pipe (`2>&1`-equivalent), `stdin`
  is redirected to `NUL`, and the window is hidden with
  `CREATE_NO_WINDOW`. Encoding is locale-adaptive, not hard-coded
  to CP932.
- **Paths and filenames returned by Download / FTP / Zip** hit the same
  CP932 trap, so do path operations in UTF-16.

### 5.3 Temp folders and paths

- For Download / Zip's "temp folder": `fs::temp_directory_path()`.
- Returned paths use the OS-native separator (Windows: `\`). MooPlug
  was Windows-only, so `\` separators are what callers expect on
  Windows.

### 5.4 Function registration (prototype → arg counts, automatically)

- Use the existing `RegisterFunction(prototype, func, description)`
  unchanged, registering all 38 prototype strings **exactly as they
  appear in real 0.4.9**.
- `NumberOfParameters` parses `{ }` to derive min / max. **Cases that
  need manual override**:
  - `zoo_DialogColour( bFull)` — no `{ }`, so `bFull` would be required;
    but in upstream it's optional (Required: No). Set min=0 manually.
  - No-argument signatures like `zoo_Version` need min=max=0.
- **Registration order is fixed** (no inserting in the middle). This
  prevents funcId drift on existing calculations. New functions are
  appended to the end.
- Flags: `kDisplayInAllDialogs | kFutureCompatible` by default; Tier B / C
  GUI-bound functions **must not** carry `kServerCompatible`.

### 5.5 Dispatch tidy-up

- Currently `zoo_Shell` is registered directly as a single function.
  With 38 of them, refactor to a **table-driven approach**:
  `{prototype, function-pointer, description, min-override}`. `Init`
  walks the table to register everything.

---

## 6. Platform strategy

| Platform | Target tiers | Notes |
|---|---|---|
| **Windows** (primary) | A + B + C all | MooPlug's native environment. Verify on a Windows test box + FM Pro 11 / 19 |
| **macOS** | A all + B (Dialog / Printer / Process) + C (optional) | Verify on a macOS machine. Hotkey / Progress on macOS is expensive — low priority |
| **Linux** (FileMaker Server) | A excluding GUI (File / Folder / Hash / Zip / Download / FTP / Version / ErrorDetail) | Dialog / Printer / Process / Hotkey don't fit Server. Only Tier A non-GUI gets `kServerCompatible` |

> Recommended: **finish all 38 on Windows → port Tier A + B to macOS →
> Linux gets Tier A non-GUI**. Finishing one platform first is faster
> than chasing cross-platform parity on every function.

---

## 7. Dependencies

| Purpose | Library | Form | License | Notes |
|---|---|---|---|---|
| HTTP / FTP | **libcurl** | static linking preferred | MIT-family | Download 2 + FTP 3. Windows uses schannel; macOS uses Secure Transport / LibreSSL |
| Zip | **miniz** | single file bundled | MIT | Compress / Extract / List. Password is separate |
| Hash | **Bundled public-domain implementations** (sha2.c / md5, etc.) | source bundled | PD / MIT | Avoid an OpenSSL dependency |
| Character conversion | Standard (`<codecvt>` substitute or OS APIs) | — | — | Windows UTF-8 ↔ UTF-16 |

- **Avoid OpenSSL** (heavy to distribute, painful to sign). Single-file
  hash implementations are enough.
- libcurl alone is large. Windows: vcpkg / prebuilt static. macOS:
  link the system libcurl. Linux: system libcurl is also an option.
- Integrate into CMake via `find_package` / `FetchContent`, bundling
  under `Libraries/` (same pattern as FMWrapper).

---

## 8. Testing strategy (carrying the existing approach forward)

1. **Pure-logic unit tests** (no FileMaker needed). Add `tests/test_*.cpp`
   per function group and drive them with `ctest`. Most of FileOps /
   HashImpl / ZipOps / NetOps is verifiable here.
2. **NetOps against local servers**: drive Download / FTP against
   Python's `http.server` / a vsftpd in a Linux VM (no need to occupy
   the main machine — use remote resources).
3. **Platform layer (Dialog / Printer / Process / Hotkey) on real
   hardware**: verify on a Windows test box (FM Pro 11 / 19) and a macOS machine. For
   Hotkey, the "press → script start" round trip needs the real
   FileMaker.
4. **Cross-checking compatibility**: evaluate the same calculation on
   MooPlug 0.4.9 (loaded into a Windows test box's Extensions folder) and on
   ZooPlug, and compare return values and error strings. The
   `MooPlug.fp7` sample provides ready-made calls.
5. **A FileMaker sample file**: build a `.fmp12` with one call per
   function (equivalent to `MooPlug.fp7`).

---

## 9. Implementation phases (milestones)

**M0 — foundation**
Table-driven registration, `MooError` (Err-string generation +
ErrorDetail map), shared helpers (UTF-8, temp paths, UTF-16 on Windows),
test harness expansion. `zoo_Version` / `zoo_ErrorDetail` are finished
in this phase.

**M1 — Tier A (23 data-handling functions)**
File 7 → Folder 6 → Hash 1 → Zip 3 → Download 2 → FTP 3. Each group:
pure logic + glue + unit tests. Cross-platform from the start.
**At the end of M1, 25 of 38 are complete (M0 included).**

**M2 — Tier B (9 Windows GUI / system functions)**
Dialog 3 → Printer 2 → Process 4. Native Windows implementation,
verify on a Windows test box.

**M3 — Tier C (4 difficult features)**
ProgressOptions + progress dialog → Download / FTP `bProgress`
wiring → Hotkey 3 (Idle / StartScript resident). Getting the
Idle / StartScript threading discipline right is mandatory here.

**M4 — distribution, macOS expansion, sample**
Ad-hoc signing (deep, unified bundle), macOS Tier A + B port, sample
`.fmp12`, MooPlug cross-check pass.

---

## 10. Effort estimate

Assumes the existing ZooPlug scaffolding (registration, logic
separation, CMake, `zoo_Shell`) is reused. Per-function effort is
"pure logic + glue + unit test + one-platform verification". Numbers
are **focused-work hours**.

### Windows-first, all 38 functions

| Phase | Content | Hours |
|---|---|---|
| M0 | Foundation (table-driven registration, MooError, helpers, Version, ErrorDetail) | 8–12 |
| M1-File | File 7 | 4–6 |
| M1-Folder | Folder 6 | 3–5 |
| M1-Hash | Hash (bundled implementation integration) | 2–4 |
| M1-Zip | Zip 3 (miniz integration, password excluded) | 5–8 |
| M1-Net | Download 2 + FTP 3 (includes first-time libcurl integration) | 7–11 |
| M2-Dialog | Dialog 3 (Windows) | 5–8 |
| M2-Printer | Printer 2 (Windows) | 3–5 |
| M2-Process | Process 4 (Windows) | 4–6 |
| M3-Progress | ProgressOptions + progress UI + `bProgress` wiring | 6–10 |
| M3-Hotkey | Hotkey 3 (resident + Idle + StartScript) | 10–16 |
| cross-cutting | Library bundling and CMake cross-build hardening | 4–8 |
| cross-cutting | Ad-hoc signing and packaging | 3–5 |
| cross-cutting | Sample `.fmp12` and MooPlug cross-check | 4–6 |
| **Total** | | **68–110** |

→ **roughly 90 hours (≈ 12 working days of focused work).**

### Cross-platform extension (macOS Tier A + B, Linux Tier A non-GUI)

| Addition | Hours |
|---|---|
| macOS: Dialog / Printer / Process port | 13–20 |
| macOS: ProgressOptions (optional) | 6–10 |
| Linux: Tier A non-GUI build and verification (logic is shared so mostly verification) | 4–8 |
| Additional cross-platform verification pass | 4–8 |
| **Additional total** | **27–46** |

→ Full cross-platform support comes to **roughly 120–150 hours**.

### Quick reference by scope

| Scope | Functions | Hours | What you get |
|---|---|---|---|
| **Minimal**: M0 + M1 (data handling only) | 25 | **30–45** | File / Folder / Hash / Zip / Download / FTP / Version / ErrorDetail. Cross-platform. No GUI, no resident |
| **Practical**: + M2 (Windows GUI / system) | 34 | **45–70** | Above + Dialog / Printer / Process. Only Hotkey / Progress missing |
| **Full (Windows)**: + M3 | 38 | **70–110** | All 38 on Windows |
| **Full cross-platform** | 38 | **120–150** | + macOS / Linux extension |

### Estimate assumptions and risks

- **Two items dominate**: ① Hotkey (resident + StartScript thread
  discipline; tripping over the threading traps multiplies the cost) ②
  libcurl static linking per OS (one-time but heavy). Spike both early
  to tighten the estimate.
- Zip **password protection** (classic Zip 2.0 cipher) is outside
  miniz's standard build. +3–5 h if needed; out of scope if not.
- `zoo_FileInfo`'s **version retrieval is Windows-only**
  (`GetFileVersionInfo`). On Mac / Linux it's unsupported or returns a
  substitute value.
- The numbers above are hours to "works". The work of
  **exactly matching the fine MooPlug behaviour** (boundary error
  codes, return values on empty inputs, default separators) is
  absorbed by the compatibility cross-check pass (M4).

---

## 11. Open questions (decide these up front to avoid drift)

1. **Scope**: pick one of the quick-reference rows above (Minimal /
   Practical / Full Windows / Full cross-platform).
2. **Platform priorities**: Windows-first then cross-port (recommended)
   or all three OSes from the start?
3. **Zip password**: support it (upstream does; required if
   compatibility is the priority).
4. **Error integration**: reply-text scheme only, or also
   `Get(LastExternalErrorDetail)`?
5. **libcurl strategy**: bundle statically (distribution is easy, build
   is heavy) or link the system libcurl (distribution gains a
   dependency)?
6. **Hotkey priority**: it's hard, and even MooPlug added it late.
   Choose whether to defer it or drop it.
