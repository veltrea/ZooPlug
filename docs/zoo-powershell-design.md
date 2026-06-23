# `zoo_powershell` — design notes

**Read this in other languages:** [日本語](zoo-powershell-design.ja.md)

> Design notes for adding a PowerShell-running function `zoo_powershell`
> to ZooPlug. It is a **separate function that pairs with `zoo_Shell`**
> (which uses cmd.exe / CP932). Why a single shared function cannot
> work was settled during this project's testing — cmd and PowerShell
> don't agree on default output encoding, how the process is launched,
> or how arguments are quoted, so baking a single behaviour into one
> function will always break for one of them.
>
> Related: `Source/ShellExec.*` (the zoo_Shell implementation) and
> `docs/zoo-plug-implementation-spec.md`.

> 🟢 **The final design is §18 (the temp-file approach).** The matrix
> in §17 showed that the original plan from §5 / §6
> (`-EncodedCommand` + forcing `[Console]::OutputEncoding`, plus base64)
> **dies in Constrained Language Mode**, so it was demoted. **Read §18.**
> §1–§16 are the history of how the design got there (with two
> assumptions overturned by live measurement).

---

## 1. Why split this off from `zoo_Shell`

The decisive reasons not to reuse `zoo_Shell` for PowerShell:

| Axis | zoo_Shell (cmd) | zoo_powershell (PowerShell) |
|---|---|---|
| Backend | `cmd.exe` (console code page is its native form) | `powershell.exe` (5.1) / `pwsh.exe` (7+) (Unicode is the native form) |
| **Default output encoding** | **decoded as CP932 (OEM CP)** | **decoded as UTF-8** (PowerShell is Unicode-native; pwsh 7's default is UTF-8) |
| How the command is passed | `cmd /S /C "<command>"` | `-EncodedCommand <base64(UTF-16LE)>` (avoids quote hell) |
| Quoting / escaping | `^` escape, `%VAR%` expansion | backtick, `$var`, `@'...'@` |
| Compatibility purpose | **Reproduce MooPlug 0.4.9's `zoo_Shell`** | A ZooPlug original (does not exist in MooPlug) |

> ⚠️ **Correction (from the live tests in §16)**: this table originally
> said "the output defaults are opposites (cmd = CP932 / PowerShell =
> UTF-8)", but **the measurement overturned that**. **The captured
> stdout of cmd, of WinPS 5.1, and of pwsh 7 is all CP932** — pwsh 7's
> "utf8 by default" applies to file APIs (`Out-File` etc.), but the
> stdout that goes down a pipe is governed by `[Console]::OutputEncoding`
> (which is CP932 on a Japanese machine). So the real reason these
> need to be separate functions isn't "different default stdout
> encoding" — it's **how the process is launched, the quoting rules,
> the compatibility purpose, and which versions / installation forms
> can be launched at all**. `zoo_powershell` has to **force UTF-8 (or
> base64 the output)** regardless of which PowerShell. Details and
> measured values are in §16.

---

## 2. Signature

> ⚠️ **Shipped signature (final, registered in `ZooPlug.cpp`):**
> ```
> zoo_PowerShell ( command { ; bCore } )
> ```
> The `sEncoding` parameter listed below is **historical**. It was
> dropped on the way to §18 because the temp-file approach pipes
> through `Out-File -Encoding utf8` unconditionally — output is always
> UTF-8, so a knob to switch decoders has no job to do. Read the rest
> of this section as design history.

```
zoo_powershell ( command {; bCore ; sEncoding } )
```

- **command** (String, required) — the PowerShell script or one-liner to
  run (UTF-8).
- **bCore** (Boolean, optional, default False) — True → **PowerShell 7+
  (`pwsh.exe`)**, False → **Windows PowerShell 5.1
  (`powershell.exe`)**. 5.1 is bundled with Windows, so it's the
  default.
- **sEncoding** (String, optional, default `"utf8"`) — override the
  output decoding. `"utf8"` (default) / `"cp932"` (= `"ansi"`) /
  `"oem"`. Usually leave it alone. *(Dropped in the shipped build — see
  the warning above.)*

> Follow MooPlug's notation conventions (lowercase; optional in
> `{ }`). The existing registration parser (`NumberOfParameters`)
> understands `{ ; ... }`, so registering with this prototype string
> derives the required / optional counts automatically.

Return value: text combining stdout and stderr (UTF-8). Line endings are
CR-normalised (same as zoo_Shell).

---

## 3. Default behaviour (when no flags are given)

`zoo_powershell ( command )` does the following:

1. Start **Windows PowerShell 5.1 (`powershell.exe`)**.
2. Launch flags:
   `-NoProfile -NonInteractive -ExecutionPolicy Bypass -OutputFormat Text`
   - `-NoProfile`: don't load the user profile (faster, reproducible).
   - `-NonInteractive`: don't stop at interactive prompts.
   - `-ExecutionPolicy Bypass`: don't get blocked by the script execution
     policy.
   - `-OutputFormat Text`: plain text (not CLIXML).
3. The command is passed via **`-EncodedCommand` (base64 UTF-16LE)** (§5).
4. The output is **forced to UTF-8** and decoded as UTF-8 (§6).
5. stdout and stderr are bundled into a single pipe (§7).
6. Line endings are normalised to CR; the trailing newline is trimmed.

---

## 4. Implementation strategy (separate pure logic, share `CreateProcessW`)

Follows the existing ZooPlug design.

```
Source/
├─ ProcessRun.h/.cpp     ★new, shared. A low-level "launch a command line via
│                          CreateProcessW + pipe and capture raw bytes" helper.
│                          Shared between zoo_Shell and zoo_powershell.
├─ ShellExec.*           zoo_Shell (existing). Can be refactored to use
│                          ProcessRun (optional).
├─ PowerShellExec.h/.cpp ★new. RunPowerShell(command_utf8, opts) — independent
│                          of FMWrapper, unit-testable.
│                          - wrap the command (prepend UTF-8 forcing script)
│                          - UTF-16LE → base64 to build -EncodedCommand
│                          - pick the host (powershell / pwsh)
│                          - decode the output by sEncoding + newline normalise
└─ ZooPlug.cpp           glue. Registers zoo_powershell, calls PowerShellExec.
```

- **`ProcessRun`** factors out zoo_Shell's Windows implementation
  (`CreateProcessW` + anonymous pipe + raw-byte read) into a shared
  helper. Both `RunShellCommand` and `RunPowerShell` then just "build a
  command-line string and pass it to ProcessRun".
- **The pure-logic layer** (`PowerShellExec`) does not depend on
  FMWrapper, so it can be unit-tested in `tests/test_powershellexec.cpp`
  without FileMaker (verify base64 generation, wrapping text, etc.).
- The base64 + UTF-16LE conversion uses the existing
  `MultiByteToWideChar` helper plus a small base64 encoder (written
  in-house or bundled).

---

## 5. How the command is passed — `-EncodedCommand` is the core of the design

This is the **only clean way to safely pass an arbitrary script** to
PowerShell, and the heart of this function.

- Arbitrary text from a FileMaker field can contain quotes, `$`,
  newlines, `;`, backticks, anything. Embedding that raw inside
  `-Command "..."` falls straight into **PowerShell's quote hell**,
  which is a different system from cmd's `^` / `%` — and going through
  cmd first means double-escaping.
- **`-EncodedCommand` passes the script as a single token, UTF-16LE →
  base64.** No quoting, no escaping. PowerShell provides this
  officially "to avoid quoting issues". With it:
  - Quote / escape bugs are eliminated by construction.
  - Multi-line scripts (with newlines) pass through as-is.
  - The command itself never loses encoding (it's in UTF-16, so
    characters outside CP932 survive).

### Build steps (inside PowerShellExec)

```
wrapped = "$OutputEncoding=[Console]::OutputEncoding=[Text.Encoding]::UTF8\n" + <user command(UTF-8→UTF-16)>
b64     = Base64( UTF16LE_bytes(wrapped) )
cmdline = "<host> -NoProfile -NonInteractive -ExecutionPolicy Bypass -OutputFormat Text -EncodedCommand " + b64
```

- The first line **pins the output encoding to UTF-8** (§6). It's part
  of the base64 itself, so there's no separate "pre-command" step.
- `<host>` = `bCore ? "pwsh.exe" : "powershell.exe"`.

---

## 6. Output encoding — force UTF-8, decode as UTF-8 (the opposite of zoo_Shell)

PowerShell's captured-output encoding **varies by version and
configuration**. From Microsoft Learn (`about_character_encoding` etc.),
the raw defaults are:

- **Windows PowerShell 5.1 (out of the box)**:
  `[Console]::OutputEncoding` is **OEM CP (= CP932 on Japanese
  Windows)`, and `$OutputEncoding` (used when piping to native processes)
  is **US-ASCII**. **Not UTF-8.** And cmdlets disagree with each other
  (`Set-Content` → ANSI / CP932, `Export-Csv` → ASCII, `Out-File` →
  UTF-16LE).
- **PowerShell 7+ (pwsh)**: a breaking change made **utf8NoBOM the
  default — but only for `Out-File` and similar file APIs**. **stdout
  going to a pipe is separate**, and `[Console]::OutputEncoding` (= CP932
  on a Japanese machine) controls it.

→ **"PowerShell → UTF-8" is incorrect, and not only for 5.1 — pwsh 7's
stdout was also CP932 in our captures (proven in §16)**. So you can't
trust the host regardless of version. **Force UTF-8**, or **base64 the
output** (§16). That is why `zoo_powershell` *forces* UTF-8:

1. **Sender side (force PS)**: at the start of the wrapper:
   `$OutputEncoding = [Console]::OutputEncoding = [System.Text.Encoding]::UTF8`
   → PowerShell itself writes UTF-8 bytes on stdout.
2. **Receiver side (decode in the plug-in)**: default `sEncoding="utf8"`
   decodes UTF-8 → UTF-16 → UTF-8 (returned to FileMaker).
3. **Belt and suspenders**: for cases where the environment still
   garbles things or you're calling a legacy native exe through PS,
   `sEncoding` can switch to `cp932` / `oem` (no auto-detection — be
   explicit).

> This is the **mirror image of zoo_Shell's "decode as CP932"**.
> The fact that "shell execution" has opposite defaults is exactly why
> these can't be a single function. It bakes the conclusion
> ("cmd = CP932, PowerShell = Unicode; they are different things") into
> the implementation.

---

## 7. stderr handling

- Like zoo_Shell, **redirect the process's stderr handle to the same
  pipe as stdout** and capture both together.
- Caveat: PowerShell's **errors are objects (`ErrorRecord`)** on a
  stream different from native stderr. To reliably surface them as
  text, put `$ErrorActionPreference='Continue'` in the wrapper, and
  optionally encourage users to add `2>&1` (error stream → success
  stream) at the end of their command.
- The chosen default: bundle OS-level stderr into the pipe (reliably
  captures native stderr) AND include
  `$ErrorActionPreference='Continue'` in the wrapper. The formatting
  difference for PS exceptions is documented as part of the spec.

---

## 8. Parameter details

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| command | String | Yes | — | PowerShell script / one-liner (UTF-8, multi-line allowed) |
| bCore | Boolean | No | False | True = pwsh (7+) / False = powershell (5.1) |
| sEncoding | String | No | `utf8` | Output decoding: `utf8` / `cp932` (=`ansi`) / `oem` |

- **bCore=True but pwsh isn't found**: launch fails → empty string (§10).
  In future, an explicit error is worth considering.
- **sEncoding has an invalid value**: fall back to `utf8` (or explicit
  error — to be decided, §13).

---

## 9. macOS / Linux

- PowerShell (`pwsh`) is cross-platform but **not installed by default**.
- Implementation: launch `pwsh` either via `/bin/sh` or directly.
  Output is natively UTF-8, so just decode it as UTF-8.
- If `pwsh` isn't there, launch fails → empty string. **macOS / Linux
  are effectively "if you've installed pwsh yourself"**, and that's
  documented.
- Unlike zoo_Shell, which uses `/bin/sh` on Mac / Linux (the system
  shell), zoo_powershell does not use a system shell.

---

## 10. Error / return contract

- **Success**: UTF-8 text combining stdout and stderr (PS's own error
  messages can be mixed in here).
- **Process launch failure** (no host, etc.): **return an empty string**
  (symmetric with zoo_Shell).
- The MooPlug-format `function|Err_N` **isn't used** (zoo_Shell doesn't
  use it either — ZooPlug's shell-family functions return raw output or
  empty, uniformly).
- If we eventually need detailed error information, an option is to
  add a separate `zoo_powershell_lasterror()` helper (§13).

---

## 11. Security

- Like zoo_Shell, this is **arbitrary code execution**.
  `-ExecutionPolicy Bypass` means scripts run.
- **`-EncodedCommand` is not sanitisation.** It prevents quoting
  accidents only; whatever you pass runs. Don't build commands from
  untrusted input (user-typed fields, web-fetched values).
- The documentation (README / function description) carries the same
  shell-injection warning as zoo_Shell.

---

## 12. Side-by-side with zoo_Shell (the case for separate functions, summarised)

| | zoo_Shell | zoo_powershell |
|---|---|---|
| Backend | cmd.exe | powershell.exe / pwsh.exe |
| Default output decoding | **CP932 (OEM)** | **UTF-8** |
| How the command is passed | `cmd /S /C "..."` | `-EncodedCommand base64(UTF-16LE)` |
| Quoting safety | `/S /C` strips one outer pair | base64 — **no escaping required** |
| Compatibility | Reproduces MooPlug 0.4.9 | ZooPlug original |
| mac / linux | `/bin/sh` (standard) | `pwsh` (optionally installed) |
| Main garbling cause | Calling a UTF-8-emitting tool → garbled | Calling a CP932-emitting legacy exe through PS → garbled (→ handle with `sEncoding`) |

**Conclusion**: opposite default encodings, different quoting
mechanisms, different compatibility goals. **Merging them isn't
possible; splitting them is correct.**

---

## 13. Testing strategy

1. **Pure-logic unit tests** (no FileMaker needed): verify wrapper-text
   generation, UTF-16LE conversion, and base64 (`tests/test_powershellexec.cpp`,
   `ctest`).
2. **A Windows 11 Pro test machine** (FileMaker Pro 11 / 19, or a test exe):
   - Japanese round-trip: does `zoo_powershell("'表予能'")` return UTF-8
     `E8 A1 A8 E4 BA 88 E8 83 BD`? (Verify in hex, the same way
     zoo_Shell was verified.)
   - Multi-line scripts and scripts containing `$` or quotes survive
     `-EncodedCommand` unchanged.
   - `bCore=True` (pwsh 7) and False (5.1) produce identical output.
   - Calling a CP932-emitting native exe through PS, can `sEncoding="cp932"`
     recover correctly?
3. Side-by-side with zoo_Shell: **the same Japanese round-trips
   correctly through both** (verifies the CP932 path and the UTF-8 path
   simultaneously).

---

## 14. Open questions (decide before starting)

1. **Error contract**: empty string on launch failure (symmetric with
   zoo_Shell), or introduce `zoo_powershell|Err_N`?
2. **Invalid sEncoding**: fall back to utf8 or explicit error?
3. **stderr**: is "OS stderr bundling + `$ErrorActionPreference=Continue`"
   enough, or auto-append `2>&1`?
4. **bCore default**: 5.1 (bundled, reliable) — is that fine? Are there
   deployments that want pwsh 7 as the default?
5. **Timeout**: since this blocks the calculation thread, add an
   upper-bound timeout argument? (zoo_Shell has the same problem.)
6. **Function name**: `zoo_powershell` final? (The `zoo_` prefix
   instead of `moo_` makes it explicit that this is a ZooPlug-original
   function.)
7. **Also add a generic `zoo_exec`?** (See §15 — the industry pattern
   would suggest it.)

---

## 15. Appendix: industry implementation patterns and ZooPlug's choice (research backup)

A survey of how other plug-ins / language runtimes handle "cmd vs.
PowerShell" (2026-06-12, web research + counter-evidence checks).
**Conclusion: the industry mainstream isn't "separate functions" — it's
"one generic execution function + encoding as an argument".** But for
ZooPlug's situation (zoo_Shell is frozen for MooPlug compatibility),
adding a separate `zoo_powershell` is correct.

### What other implementations actually do

| Implementation | Approach | Encoding | Source |
|---|---|---|---|
| **MBS Plugin** | **Generic `Shell.Execute(path-to-binary)`**. The user supplies cmd.exe / powershell.exe / sh themselves. No per-shell function. | `Shell.ReadOutputText(shell; encoding)` **specifies it at read time** (UTF-8 / ANSI / Native…, internally iconv, CP932 / Shift_JIS supported, v11.2+ auto-detects UTF-16 BOM) | mbsplugins.eu/component_Shell.shtml etc. |
| **BaseElements** | **Single `BE_ExecuteSystemCommand`**. Uses `Poco::Process::launch`, returns **raw bytes as-is** (no decoding) | **No argument**. Leaves it to the OS native encoding (Japanese = CP932) → **garbles for UTF-8-emitting tools** | github.com/GoyaPtyLtd/BaseElements-Plugin |
| **Python subprocess** | `shell=` and `encoding=` are **separate arguments** | Caller specifies e.g. `encoding='cp932'` | docs.python.org/3/library/subprocess |
| **Node child_process / execa** | `shell` (bool / path) and `encoding` (default utf8) are **arguments** | Same | nodejs.org/api/child_process |
| **Rust std::process / Go os/exec** | No "shell" argument. Spawn `cmd` / `sh` yourself; encoding handled in the I/O layer | Caller-managed | doc.rust-lang.org / pkg.go.dev/os/exec |

→ **Dominant pattern = "shell and encoding as separate arguments on a
generic exec"** (Python / Node / execa / MBS). "A separate function per
shell" is the minority.

### What this analysis corrected

- "Other people split cmd and PowerShell into **separate plug-ins /
  separate functions**" — **the direction is right, but it isn't the
  most common pattern.** The actual mainstream is **the MBS style:
  generic exec + encoding specified at read time**. BaseElements is a
  single function and **doesn't even decode** (so it garbles UTF-8
  tools).
- "PowerShell defaults to UTF-8" applies **only to pwsh 7 and to
  pre-configured 5.1**. Out-of-the-box 5.1 is OEM / ASCII (§6).
- But the core insight — "**no single hard-coded default can handle
  both cmd and PowerShell correctly**" — is **supported by both live
  measurements and Microsoft's official documentation** (cmd = CP932 /
  pwsh 7 = UTF-8, with different launch and quoting too).

### ZooPlug's design decision (given the evidence)

1. **zoo_Shell stays frozen** (MooPlug 0.4.9 compatibility:
   cmd / CP932 / fixed arguments). Adding an argument here breaks
   compatibility → **leave it alone**.
2. **Add `zoo_powershell` as a separate function** (this document).
   Default UTF-8, `-EncodedCommand`. Pairs with zoo_Shell.
   - One notch sturdier than BaseElements' "raw bytes" approach (UTF-8
     is explicitly forced + decoded).
3. **Future option: add a generic `zoo_exec ( program ; args {; sEncoding ; sStdin } )`**
   (MBS / Python style).
   - Arbitrary executable + encoding argument. The most flexible and
     industry-standard choice.
   - zoo_Shell / zoo_powershell stay as "thin wrappers for the common
     combinations" on top of `zoo_exec`.
   - If we go this way, the `ProcessRun` from §4 becomes the body of
     `zoo_exec` (the design lines up straight through).

> Summary: **"split or merge" isn't a binary choice.** The mainstream is
> "generic + encoding argument". ZooPlug adds
> "zoo_Shell (frozen) + zoo_powershell (pair)" while planning a generic
> `zoo_exec` as the underlying layer — the right of both worlds.

---

## 16. The decisive measurement matrix (overturned two assumptions in the design)

> On a Windows 11 Pro test machine (Japanese Windows 11 / ja-JP), launched three hosts with the
> same `CreateProcessW + CREATE_NO_WINDOW + pipe` that the plug-in uses
> and **observed raw bytes in hex** (2026-06-12). The empirical proof
> that logic-only design crumbles on a version matrix.

The three "PowerShells" co-existing on the same machine:

- **Windows PowerShell 5.1** (`powershell.exe`, .NET Framework, in-box,
  frozen)
- **pwsh 7.5.4 (MSI version)** (`C:\Program Files\PowerShell\7\pwsh.exe`)
- **pwsh 7.5.4 (MSIX / Store alias)** (`...\WindowsApps\pwsh.exe`)

Test string: 表予能 (UTF-8 `E8 A1 A8 E4 BA 88 E8 83 BD` / CP932
`95 5C 97 5C 94 5C`).

| Host | (a) do nothing | (b) force `[Console]::OutputEncoding=UTF8` | (c) base64 the output |
|---|---|---|---|
| WinPS 5.1 | **CP932** (`95 5C 97 5C 94 5C`) | UTF-8 ✓ | ASCII ✓ |
| pwsh 7.5 MSI | **CP932** (`95 5C 97 5C 94 5C`) | UTF-8 ✓ | ASCII ✓ |
| pwsh 7.5 MSIX | **`<CreateProcessW FAILED gle=15612>`** | can't launch | can't launch |

### Overturned assumption ①: "pwsh 7 defaults to UTF-8" does not apply to captured stdout

- **Even pwsh 7 MSI's stdout going down a pipe was CP932** (`95 5C…`).
  The `utf8NoBOM` default applies to **`Out-File`-style file APIs**;
  **the console / stdout is governed by `[Console]::OutputEncoding`
  (CP932 on a Japanese machine)**.
- → "cmd = CP932 / pwsh = UTF-8 — opposite defaults", which was part of
  the rationale for separating the functions, **is wrong for the
  capture path** (both are CP932). The real difference is **how they
  launch, how they quote, and which versions / installation forms can
  be launched at all**, not the default character set of stdout.
- → **`zoo_powershell` can't assume "PS → UTF-8". UTF-8 has to be
  forced regardless of version, or the output base64-encoded** (next
  section).

### Overturned assumption ②: MSIX / Store pwsh can't be launched with `CreateProcessW`

- The `pwsh.exe` under `WindowsApps` is an **app execution alias (a
  0-byte reparse point)**, designed to be invoked through a shell.
  **The plug-in's `CreateProcessW(path)` can't launch it** (`gle=15612`).
- → If the user **only has the Store / MSIX pwsh**, zoo_powershell
  **fails at the launch stage, before encoding is even relevant**.
- Mitigation: **resolve the real path** (`C:\Program Files\PowerShell\7\pwsh.exe`
  etc.) and launch that; if not found, **return an explicit error**.
  A naive design of "hit the alias path when `bCore=True`" is not
  workable.

### What survived and how to harden

- **Forcing `[Console]::OutputEncoding=UTF8` worked uniformly on 5.1
  and pwsh 7 MSI** (even without a console). → As long as the launch
  succeeds, UTF-8 forcing is effective. The concern that "it
  might break without a console" does not hold on 5.1 / MSI.
- **Base64-ing the output (c) reduced everything to ASCII across all
  hosts — the most robust path, free of any encoding intermediate**
  (but it doesn't save you from a failed launch). Symmetric with
  input-side `-EncodedCommand`, the **output base64** approach
  completely sidesteps console / stream encoding differences.
- **Revised mandatory requirements for `zoo_powershell`**:
  1. **Resolve the launch host**: probe `powershell.exe` (5.1) /
     resolved real `pwsh.exe`. Avoid the MSIX alias. Missing → explicit
     error.
  2. **Pin the output encoding**: force UTF-8 **plus** (for the most
     robust path) base64 the output and decode it plug-in side.
  3. **Verify before claiming support for a version / installation
     form**: don't claim "all PowerShell supported" from a single
     successful cell (5.1).

### Honest verification status

| Cell | Status |
|---|---|
| WinPS 5.1 / FullLanguage | ✅ Verified (forced UTF-8 and base64 both work) |
| pwsh 7.5 MSI / FullLanguage | ✅ Verified (same; **raw stdout is CP932**) |
| pwsh 7.5 MSIX (Store) | ⛔ **CreateProcessW can't launch (gle=15612)** |
| Constrained Language Mode / WDAC / AppLocker | ❓ Not verified (`-EncodedCommand` could be banned. Needs live machine) |
| pwsh 6.x / 7.0–7.4 / ARM64 / dotnet global tool / portable zip | ❓ Not verified |

> Lesson: **"logically correct" easily falls apart on a version matrix.**
> Live measurement overturned two assumptions (pwsh's stdout was CP932,
> MSIX won't launch). Claims about PowerShell support need
> **per-cell measurements**, not "based on understanding".

---

## 17. Full version × encoding × Constrained Language Mode measurement (2026-06-12)

> Installed **seven hosts** on the test machine side by side (5.1 / 6.2.7 / 7.0.3 /
> 7.2.6 / 7.4.6 / 7.5 MSI / 7.5 MSIX) and measured raw bytes in hex
> through a generic launcher `runraw.exe` (`CreateProcessW +
> CREATE_NO_WINDOW + pipe`). Test string 表予能 (UTF-8 `E8A1A8E4BA88E883BD` /
> CP932 `955C975C945C`).

### A. host × output-control method (FullLanguage) — behaviour is uniform across versions

| Method | 5.1 | 6.2.7 | 7.0.3 | 7.2.6 | 7.4.6 | 7.5 MSI | 7.5 MSIX |
|---|---|---|---|---|---|---|---|
| (a) no control | **CP932** | CP932 | CP932 | CP932 | CP932 | CP932 | can't launch |
| (b) `[Console]::OutputEncoding=UTF8` | UTF-8 | UTF-8 | UTF-8 | UTF-8 | UTF-8 | UTF-8 | can't launch |
| (c) base64 the output | base64 | base64 | base64 | base64 | base64 | base64 | can't launch |
| (d) only `$OutputEncoding=UTF8` | **CP932** | CP932 | CP932 | CP932 | CP932 | CP932 | can't launch |

**What was learned:**

1. **Every PowerShell (5.1–7.5) defaults to writing CP932 on its
   captured stdout.** pwsh 7 is no exception. "pwsh = UTF-8" is
   completely wrong for the capture path (and is independent of
   version).
2. **`$OutputEncoding` does not control captured stdout** (it stays
   CP932). **Only `[Console]::OutputEncoding` does**. → `$OutputEncoding=…`
   can be removed from the wrapper (harmless but pointless). Use
   `[Console]::OutputEncoding`.
3. (b) and (c) work on every version in FullLanguage. MSIX **never
   launches** (gle 15612) in any row.

### B. Constrained Language Mode (5.1 / 7.4.6) — **this is where the design lived or died**

| Method | 5.1 (CLM) | 7.4.6 (CLM) |
|---|---|---|
| Raw output | CP932 (it does run) | CP932 (it does run) |
| (b) force `[Console]::OutputEncoding=UTF8` | **PS-ERROR** ("cannot set property") → stays CP932 | **PS-ERROR** (InvalidOperation) → stays CP932 |
| (c) base64 (`[Convert]` / `[Text.Encoding]`) | **PS-ERROR exit=1** (type access forbidden) | **PS-ERROR exit=1** |
| (d) `Out-File -Encoding utf8` → read the file | **UTF-8 ✓** (with a leading BOM `EFBBBF`) | **UTF-8 ✓** (no BOM) |

**What was learned (decisively):**

- **CLM kills both (b) and (c).** `[Console]::OutputEncoding=…`,
  `[Convert]` / `[Text.Encoding]` are all **method / property accesses
  on .NET types**, which CLM forbids → exception. Both of the design's
  two main ideas are eliminated at once.
- **What survives is cmdlets only.** `... | Out-File -Encoding utf8 <file>`
  (cmdlets are allowed even under CLM) writes UTF-8 to a file and
  **the plug-in reads that file**. This works on every version, in
  FullLanguage and CLM.
- **5.1's `Out-File -Encoding utf8` adds a BOM** (7.x doesn't), so the
  plug-in has to strip a leading BOM.

### C. Command passing (7.4.6)

- `-Command` and `-EncodedCommand` both work fine in FullLanguage,
  produce UTF-8 ✓.
- The `__PSLockdownPolicy=4` environment variable **did not actually
  trigger CLM** on this build (output stays UTF-8). → Real-world CLM
  is imposed by **WDAC / AppLocker script enforcement**.
  `__PSLockdownPolicy` is insufficient on current Windows. **Under
  WDAC script enforcement, `-Command` / `-EncodedCommand` could be
  rejected outright**, **but this requires policy deployment to
  measure** (a known unknown).

### D. PowerShell as a relay for a native exe (7.4.6)

- `cmd /c echo 表` (cmd emits CP932) relayed via pwsh → by default
  `95 5C` (CP932 passthrough); with `[Console]::OutputEncoding=UTF8`
  forced, `E8 A1 A8` (correctly UTF-8). In FullLanguage, forcing also
  fixes the relay path.

### Revised, hardened design (after the matrix)

1. **Resolve the launch host**: probe `powershell.exe` (5.1) / resolved
   real `pwsh.exe` (`C:\Program Files\PowerShell\7\` etc.). The MSIX
   alias **cannot be launched with `CreateProcessW`** → find the real
   binary or return an explicit error.
2. **Pin the encoding in two layers**:
   - **Default (fast, FullLanguage)**: force
     `[Console]::OutputEncoding=UTF8` + decode UTF-8 (works on every
     version).
   - **CLM-safe mode (universal)**: have the user command's output
     written to a file via `Out-File -Encoding utf8 <temp>` (a cmdlet),
     and **the plug-in reads that temp file**. **This is the only path
     that works on every version under FullLanguage and CLM.** Don't
     forget to **strip the 5.1 BOM**.
   - → For distribution, **make "go through a file (the `Out-File`
     cmdlet)" the default** — that's the most robust. Console forcing
     / base64 die under CLM, so they're not safe to assume if
     lockdown environments are possible.
3. **Unverified gaps**: WDAC script enforcement (might reject
   `-Command` / `-EncodedCommand`), pwsh 6.0 / 7.1 / 7.3, ARM64, dotnet
   global tool. Don't claim support before measuring each cell.

### Verification matrix (updated)

| Cell | Status |
|---|---|
| 5.1 / 6.2 / 7.0 / 7.2 / 7.4 / 7.5 MSI × FullLanguage | ✅ Every version defaults to CP932, (b) and (c) work, only MSIX fails to launch |
| 5.1 / 7.4 × ConstrainedLanguage | ✅ Measured: **(b) and (c) die, only the `Out-File` cmdlet path survives (5.1 has BOM)** |
| WDAC / AppLocker script enforcement | ❓ Not verified (needs policy deployment; `-EncodedCommand` could be rejected) |
| pwsh 6.0 / 7.1 / 7.3 / ARM64 / dotnet tool | ❓ Not verified |

> Conclusion: **the two main ideas of the logic-driven design were
> wiped out in CLM.** The only path that works universally across
> versions and language modes is "**write to a file with the `Out-File
> -Encoding utf8` cmdlet, and have the plug-in read it**" (plus the
> 5.1 BOM strip). This confirmed that the logic-driven assumption is
> insufficient under CLM.

---

## 18. Final architecture — the temp-file approach (verified; this is the main design)

> The `-EncodedCommand` + `[Console]::OutputEncoding`-forcing approach
> from §5 / §6 **dies in CLM** (§17 B), so it was demoted. **This
> chapter is the official main design for `zoo_powershell`.** Verified
> on 2026-06-12 across all 6 live versions × FullLanguage / CLM.

### The mechanism

1. The plug-in writes the user script to a **temp .ps1 (UTF-8 **with
   BOM**)**.
   - The wrapper ends with the result piped to
     **`... | Out-File -Encoding utf8 -FilePath <out.txt>`** (cmdlet).
   - If you want stderr too:
     `<user command> 2>&1 | Out-File ...`.
2. Launch `<host> -NoProfile -NonInteractive -ExecutionPolicy Bypass -File <temp.ps1>`
   via `CreateProcessW + CREATE_NO_WINDOW + pipe` (existing
   `ProcessRun`).
3. The plug-in **reads out.txt** (UTF-8). **Strip the BOM that 5.1
   adds**, normalise line endings to CR, return.
4. temp.ps1 / out.txt get unique names (for concurrent calls); both
   are deleted afterwards.

### Why this melts every constraint (verification mapping)

| Old constraint | How the temp-file approach handles it | Verified |
|---|---|---|
| Every version defaults to CP932 on stdout | **Don't use stdout.** Output goes to a file (`Out-File -Encoding utf8`) | All versions UTF-8 in §18 table |
| Forcing `[Console]` raises in CLM | **Don't use it.** No .NET type access anywhere | UTF-8 ✓ under CLM |
| base64 (.NET) raises in CLM | **Don't use it** | Same |
| Quote hell with `-Command` | **Pass via `-File`.** No quoting / escaping required | Multi-line, Japanese literals all pass through |
| .ps1 input garbling | **Write with UTF-8 BOM** (for 5.1) | Verified vs. no-BOM control |

### Empirical data (same machine as §17)

```
BOM    / Full :  5.1=UTF-8(+BOM)  6.2=UTF-8  7.0=UTF-8  7.2=UTF-8  7.4=UTF-8  7.5MSI=UTF-8
BOM    / CLM  :  5.1=UTF-8(+BOM)  7.4=UTF-8
NO-BOM / Full :  5.1=GARBLED(!)   7.4=UTF-8     ← .ps1 must have a BOM (for 5.1)
```

### Implementation must-haves (checklist)

- [ ] Write the temp .ps1 as **UTF-8 with BOM** (so 5.1 doesn't misread
      literals as CP932).
- [ ] Send output through **`Out-File -Encoding utf8`** (cmdlet =
      CLM-safe). **Do not use** `[Console]` / `[Convert]` /
      `[Text.Encoding]`.
- [ ] **Strip the leading BOM (`EF BB BF`)** from the read out.txt
      (5.1 inserts it).
- [ ] **Default host is 5.1 (`powershell.exe`)** (guaranteed to launch).
      For pwsh, **resolve the real path** — never use the MSIX alias.
- [ ] **Unique names** for temp / out + **cleanup afterwards**. Pick a
      writable temp directory.
- [ ] Use `2>&1` to mix stderr into the file (or write a separate
      out.err and concatenate).
- [ ] Residual guard: temp scripts can be rejected under WDAC /
      AppLocker enforcement → **fail with an explicit error** ("lockdown
      environment not supported").

### Remaining gaps (not yet verified)

- **WDAC / AppLocker script enforcement**: temp-path .ps1 is rather
  likely to be rejected. Treat as "this is supposed to fail" with an
  explicit-error design. Live measurement needs separate policy
  deployment.
- pwsh 6.0 / 7.1 / 7.3 / ARM64 / dotnet tool (likely to work in
  FullLanguage, but unmeasured).

> Summary: **on the encoding and language-mode axes, the temp-file
> approach effectively removes the constraints.** Trying to be clever
> at the API level (`[Console]` forcing, base64) all died in CLM;
> "**just do everything via a file**" sliced through it. What remains is
> launch resolution (absorbed by the 5.1 floor) and the WDAC ceiling
> (explicit error).

---

## 19. AppLocker measurement — pinning down the generated-path rule (verified with Test-AppLockerPolicy)

> Measured on a Windows 11 Pro test machine (2026-06-12). Used
> `Test-AppLockerPolicy` (**the official command that evaluates a
> policy without launching** = the same evaluation logic AppLocker
> uses) to ask "can a standard user execute the .ps1 at each path?"
> Policy = the default script rules (allow Windows / Program Files /
> Administrators, default-deny everywhere else) + **allow
> `%OSDRIVE%\ProgramData\ZooPlug\*`**.

| Path | Standard user | Administrator |
|---|---|---|
| `C:\ProgramData\ZooPlug\…` (the allow-listed target) | **Allowed** (rule="ZooPlug allowed") | Allowed |
| `C:\ProgramData\NotAllowed\…` (a name that doesn't look temp-y) | **DeniedByDefault** | Allowed (Administrators exception) |
| `%TEMP%\…` | **DeniedByDefault** | Allowed |
| `C:\Users\Public\…` | **DeniedByDefault** | Allowed |
| `C:\Windows\Temp\…` | Allowed (`%WINDIR%` default rule, but standard users can't write here → useless) | Allowed |
| `C:\Program Files\…` | Allowed (default rule, but can't write at runtime) | Allowed |

### Confirmed conclusions

1. **The concern was real**: `%TEMP%` / `Public` / any random
   ProgramData subfolder are **DeniedByDefault (cannot execute)** for
   standard users under AppLocker script enforcement. **A design that
   writes temp scripts to `%TEMP%` will die for sure in a lockdown
   environment.**
2. **"A non-temp-looking name" does not solve it (confirmed)**:
   `ProgramData\NotAllowed`, which doesn't look temp-y, is still
   **DeniedByDefault**. The decision is **allow-list, not a heuristic
   on path names**. Renaming doesn't help.
3. **The correct rule was empirically pinned down**: **write to the
   fixed path `%PROGRAMDATA%\ZooPlug\scripts\` and have the
   administrator allow-list it with a single rule** — then it's
   **Allowed** even for a standard user. The lever isn't "the name";
   it's "**a fixed path that can be allow-listed + one administrator
   rule**".

### → Generated-path rule (confirmed, mandatory for the implementation)

```
ZooPlug always places generated scripts / output files under
   %PROGRAMDATA%\ZooPlug\scripts\
Never %TEMP% / %LOCALAPPDATA% / Public.
For administrators of lockdown environments, document this single rule:
   Add  %OSDRIVE%\ProgramData\ZooPlug\*  to AppLocker / WDAC script allow rules.
```

- `C:\ProgramData` is writable by standard users (default ACL) and is a
  **stable fixed path** — it can be allow-listed.
- In non-lockdown environments (the majority), `ProgramData\ZooPlug` is
  writable and executable as normal — no extra config needed.
- In lockdown environments, the single rule above opens the door. If
  the rule is missing and the path is fully blocked, the plug-in
  returns an explicit error.

### Honest limits of this measurement

- `Test-AppLockerPolicy` **authoritatively evaluates the policy
  logic** (edition-independent). → The strategy "fixed ProgramData
  path + allow rule" is **confirmed correct**.
- But **runtime enforcement on Win11 Pro itself is not yet measured**
  (non-interactive session isolation prevents launching as a standard user =
  `0xC0000142 DLL init failed` / window-station unavailable). Real
  enforcement happens on AppLocker for Enterprise / Education or on
  WDAC; the **judgement logic there is identical to the evaluation
  above**, so the strategy holds.
- WDAC script enforcement (mostly forces CLM, where §18's temp-file
  approach already survives) is a separate axis — needs policy
  deployment.

---

## 20. AppLocker runtime enforcement measurement — pinned "Win11 Pro doesn't enforce" on real hardware (2026-06-12)

> Took the "real runtime enforcement" caveat from §19 and measured it
> for real on a Windows 11 **Pro** test machine. The
> session-0 / window-station problem (`0xC0000142`) that blocked it
> previously was solved by **launching a pre-created standard user
> account via Task Scheduler** (scheduled tasks can launch with a
> standard user's token + a non-interactive window-station). Harness is
> a verification harness (marker-based,
> try / finally guarantees revert).

### Method (marker-based)

1. Grant `SeBatchLogonRight` to a standard user account via `secedit`
   (`schtasks /ru /rp` does **not** auto-grant it).
2. Place test .ps1 files in an allowed path
   (`C:\ProgramData\ZooPlug\scripts\`), a denied path
   (`C:\ProgramData\NotAllowed\`), and `C:\Users\Public\…`. Each .ps1
   first writes a marker (`C:\Users\Public\zoo_<tag>.txt`, including
   the LanguageMode).
3. Launch each .ps1 as a **scheduled task under the standard user account**
   (`/rl LIMITED` = standard token) → presence of the marker = ran;
   absence = AppLocker blocked.
4. Test sequence: no policy (SANITY) → Script enforcement Enabled
   (DEPLOY) → repeat, with the same harness.

### Measurement results (decisive)

| Phase | allowed (ProgramData\ZooPlug) | notallowed (ProgramData\NotAllowed) | public |
|---|---|---|---|
| no policy (SANITY) | RAN / FullLanguage | RAN / FullLanguage | RAN / FullLanguage |
| **Script `EnforcementMode="Enabled"`** | **RAN / FullLanguage** | **RAN / FullLanguage** (should have been blocked) | **RAN / FullLanguage** |
| AppLocker events | — | **none** (no 8003 / 8004 / 8007) | — |

- Effective policy is confirmed as `Type="Script" EnforcementMode="Enabled"`,
  `AppIDSvc=Running`. **Even so**, scripts on denied paths run in
  FullLanguage, and AppLocker raises **zero** block / audit events.

### Confirmed conclusions

1. **Windows 11 Pro does not enforce AppLocker script enforcement at
   runtime** (proven on hardware). You can set the policy to "Enabled"
   and the service runs, but **no judgement, no blocking, no events
   happen**. This empirically confirms Microsoft's official position
   that "AppLocker enforcement is Enterprise / Education only".
2. So **AppLocker is not a threat for ZooPlug on Pro** (temp .ps1 runs
   on any path). The `%PROGRAMDATA%\ZooPlug\*` allow-list from §19 is
   **necessary and correct for Enterprise / Education** (Test-AppLockerPolicy
   evaluation is edition-independent, so §19 is settled). On Pro, "it
   doesn't hurt; it has no effect".
3. **What actually locks down Pro is WDAC (CI / UMCI).** With kernel-mode
   CI (HVCI) on as the Win11 default but user-mode CI (script
   enforcement) off, WDAC's script enforcement isn't engaged either.
   Whether §18's temp-file approach survives under real WDAC
   enforcement is **the next ceiling** (planned for §21).

### Side benefits (lessons from the harness)

- `schtasks /create /ru <user> /rp <pass>` **does not grant
  `SeBatchLogonRight`** (only warns; tasks don't run = `0x41303`).
  Grant it explicitly via `secedit /export → edit lines → /configure`
  (the INF must be **UTF-16LE + BOM**). REVERT restores the original
  SID list.
- The "can't launch a standard user from a non-interactive session" issue is
  resolved by **launching via Task Scheduler** (`CreateProcessWithLogonW`
  via a small launcher exe fails with `0xC0000142`).

---

## 21. Independent re-verification of CLM survival (self-imposed ConstrainedLanguage, safe path) (2026-06-12)

> §20 established "Pro doesn't enforce AppLocker; the real lock is
> WDAC". Deploying real WDAC carries real risk (it needs a reboot),
> so the first step was an **independent re-verification of
> §18's CLM survival on a no-reboot safe path**. Used
> a verification harness, run as a standard user account via Task
> Scheduler.

### How to induce CLM (an important measurement note)

- **`__PSLockdownPolicy=4` (env var) did NOT induce CLM** on this build
  (Win11 Pro, 5.1 / pwsh 7.5.4) — setting it on the process env
  left the mode as `FullLanguage`. The old PS test hook is not
  reliable.
- Instead, **self-downgrade inside the temp .ps1 at the top** with
  `$ExecutionContext.SessionState.LanguageMode = 'ConstrainedLanguage'`,
  then run the §18 body. This is a safe way to **faithfully reproduce
  CLM's language constraints** (downgrade is always allowed; unsetting
  isn't).

### Measurement results (5.1 and pwsh 7 both round-trip UTF-8 intact under CLM)

| Host | mode under CLM | hex of `表予能` | BOM |
|---|---|---|---|
| 5.1 (`powershell.exe`) | **ConstrainedLanguage** | `E8 A1 A8 E4 BA 88 E8 83 BD` ✓ | 5.1's BOM stripped ✓ |
| pwsh 7 (`pwsh.exe`) | **ConstrainedLanguage** | `E8 A1 A8 E4 BA 88 E8 83 BD` ✓ | no BOM ✓ |

- The §18 body (`& { <user> } 2>&1 | Out-File -FilePath … -Encoding utf8`
  → read → strip BOM) is built only from **CLM-legal constructs**
  (cmdlets / string concatenation / `$ExecutionContext` property
  reference), so it raises no CLM exceptions and Japanese round-trips
  through UTF-8 fully. `Out-File -Width 8192` covers the line-wrap
  pitfall.

### Conclusions and limits

1. **§18 — temp-file approach + `Out-File -Encoding utf8` + BOM strip
   — survives CLM** (5.1 / pwsh 7, as a standard user, verified). If
   WDAC forces CLM, §18 is **very likely to pass** based on this
   independent corroboration.
2. **Limits (honestly)**: self-imposed CLM reproduces CLM's *language
   constraints*, but does not reproduce WDAC-induced CLM's **file
   trust layer** (whether unsigned .ps1 is allowed; how
   dot-sourcing / `& {}` is trust-judged). That can only be verified
   with the **real WDAC deployment (reboot required)** that §20
   deferred. → Once the user accepts the risk, deploy
   a policy based on `AllowMicrosoft` with UMCI enabled and measure
   live (planned for §22).

---

## 22. Real WDAC deployment measurement — pinned "§18 passes under enforcement" on hardware (the final ceiling) (2026-06-13)

> The **WDAC-induced CLM file-trust layer** that §21 left open was
> tested by deploying a real WDAC policy on a Windows 11 Pro test machine and
> rebooting. Used a verification harness (preflight →
> build_deploy → setup_failsafe → to_enforce → measure → revert →
> verify). **Staged, safety-netted execution** with a confirmed full
> clean recovery.

### Why this was safe to do (verified in preflight)

- Policy carries **option 10 "Boot Audit on Failure" + 9 "Advanced Boot
  Options"** (boot doesn't die even if some boot-critical code is
  blocked).
- A **failsafe via a startup SYSTEM task** is armed (after 12 minutes,
  if not disarmed, `CiTool --remove-policy` + reboot).
- The measurement / recovery scripts are **fully CLM-safe** (zero .NET
  method / static calls; only cmdlets and operators), so they complete
  even under enforcement (the orchestrator itself is in CLM).

### Method

Deploy a WDAC policy with `AllowMicrosoft` as the base, **UMCI enabled
+ Script Enforcement enabled** (option 11 removed) via CiTool. Stage 1
= Audit (option 3 present) → reboot → measure. Stage 2 = Enforce
(option 3 removed at the same GUID) → reboot → measure. Run §18's
temp.ps1 (unsigned, no self-imposed CLM) via a standard user account in Task
Scheduler, and verify the observed LanguageMode and the Japanese UTF-8
round-trip (`表予能` = `E8 A1 A8 E4 BA 88 E8 83 BD`).

### Measurement results

| Host | Audit (UMCI=1) | **Enforce (UMCI=2)** | UTF-8 round-trip |
|---|---|---|---|
| 5.1 (`powershell.exe`) | FullLanguage (not CLM in Audit) | **ConstrainedLanguage** | ✓ (with BOM strip) |
| pwsh 7 (`pwsh.exe`) | ConstrainedLanguage (CLM even in Audit) | **ConstrainedLanguage** | ✓ (no BOM) |

- **Under enforce, unsigned temp.ps1 was NOT blocked — it ran in CLM**
  (both hosts). The Japanese round-trip through `Out-File -Encoding utf8`
  is completely intact. The orchestrator itself (also unsigned .ps1)
  ran in ConstrainedLanguage, and the CLM-safe implementation
  completed.
- CodeIntegrity logs many **3077 (enforce blocked)** events, but the
  targets are **unsigned DLLs / exes**.
  **PowerShell scripts (.ps1) are not blocked by 3077; they are
  CLM-ised** — this is the essence of WDAC's script enforcement.
- After revert, **UMCI=0 fully recovered**.

### Confirmed conclusions (final backing for §18)

1. **Windows 11 Pro DOES enforce WDAC at runtime** (in contrast with
   AppLocker = §20). The real lockdown on Pro is, as expected, WDAC.
2. **Under real WDAC enforcement, §18's temp-file approach passes**:
   unsigned .ps1 is not blocked, 5.1 and pwsh 7 both run in CLM, and
   the UTF-8 round-trip via `Out-File -Encoding utf8` is uninjured.
   §21's open item (the file trust layer) is **resolved — passed**.
3. So ZooPlug's `zoo_powershell` **meets both the AppLocker ceiling
   (§19 / §20) and the WDAC ceiling (§22) with §18 + §19**:
   - §18's temp.ps1 must stay **strictly CLM-safe** (no `[Console]` /
     `[Convert]` / `[Text.Encoding]` / `Add-Type` ever in the
     generated script — and the §5 / §6 old approach's rejection is
     vindicated here again).
   - §19's `%PROGRAMDATA%\ZooPlug\*` allow is for
     **AppLocker (Enterprise / Education) path enforcement**. WDAC
     uses trust rather than path to put scripts into CLM — separate
     layer, but the two coexist.
4. **Implementation implication**: the temp.ps1 that `PowerShellExec`
   generates is composed only of cmdlets and string operators, with no
   .NET type access whatsoever (test under CLM is mandatory). The
   host choice (5.1 / pwsh 7) makes no behavioural difference under
   enforce (both CLM).

### Remaining work (honestly)

- This test used a WDAC policy based on `AllowMicrosoft`. Real
  enterprise policies (signed-required, ISG, Managed Installer, etc.)
  can be stricter, but the core of script enforcement — ".ps1 runs in
  CLM" — is the same, so §18 is very likely to pass (further testing
  is optional).
- A WDAC policy that also uses an **AppLocker-style explicit .ps1
  deny** could block ZooPlug; in that case, the §10 "fail with an
  explicit error" contract is followed.
