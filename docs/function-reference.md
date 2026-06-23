# ZooPlug Function Reference

**Read this in other languages:** [日本語](function-reference.ja.md)

A user-facing reference for all **39 functions** ZooPlug exposes — the 38
functions derived from MooPlug 0.4.9 plus ZooPlug's own `zoo_powershell`.

- This document is the **reference you open to write calculations**
  day-to-day on ZooPlug (the spec was pinned down through observation of
  MooPlug 0.4.9 and live cross-checking).
- The behaviour described here is backed by observation and unit tests.
  Anywhere the upstream documentation disagrees with observed behaviour,
  this reference follows the observed behaviour and notes the discrepancy.
- **The registration order is fixed forever**: FileMaker assigns each
  external function an internal funcId based on its registration order, so
  inserting a function in the middle would break existing calculations. The
  order itself is part of the API.

---

## Contents

| Group | Functions |
|---|---|
| ZooPlug-original | [zoo_Shell](#zoo_shell) / [zoo_powershell](#zoo_powershell) |
| Meta | [zoo_Version](#zoo_version) / [zoo_ErrorDetail](#zoo_errordetail) |
| File (7) | [zoo_FileExists](#zoo_fileexists) / [zoo_FileCopy](#zoo_filecopy) / [zoo_FileDelete](#zoo_filedelete) / [zoo_FileMove](#zoo_filemove) / [zoo_FileRead](#zoo_fileread) / [zoo_FileWrite](#zoo_filewrite) / [zoo_FileInfo](#zoo_fileinfo) |
| Folder (6) | [zoo_FolderExists](#zoo_folderexists) / [zoo_FolderCopy](#zoo_foldercopy) / [zoo_FolderCreate](#zoo_foldercreate) / [zoo_FolderDelete](#zoo_folderdelete) / [zoo_FolderMove](#zoo_foldermove) / [zoo_FolderList](#zoo_folderlist) |
| Hash (1) | [zoo_Hash](#zoo_hash) |
| Zip (3) | [zoo_ZipCompress](#zoo_zipcompress) / [zoo_ZipExtract](#zoo_zipextract) / [zoo_ZipList](#zoo_ziplist) |
| Net = Download + FTP (5) | [zoo_DownloadText](#zoo_downloadtext) / [zoo_DownloadFile](#zoo_downloadfile) / [zoo_FTPDownload](#zoo_ftpdownload) / [zoo_FTPUpload](#zoo_ftpupload) / [zoo_FTPDelete](#zoo_ftpdelete) |
| Dialog (3) | [zoo_DialogColour](#zoo_dialogcolour) / [zoo_DialogFile](#zoo_dialogfile) / [zoo_DialogFolder](#zoo_dialogfolder) |
| Printer (2) | [zoo_PrinterDefault](#zoo_printerdefault) / [zoo_PrinterList](#zoo_printerlist) |
| Process (4) | [zoo_ProcessCount](#zoo_processcount) / [zoo_ProcessKill](#zoo_processkill) / [zoo_ProcessList](#zoo_processlist) / [zoo_ProcessRunning](#zoo_processrunning) |
| Progress UI (1) | [zoo_ProgressOptions](#zoo_progressoptions) |
| Hotkey (3) | [zoo_HotkeyAdd](#zoo_hotkeyadd) / [zoo_HotkeyList](#zoo_hotkeylist) / [zoo_HotkeyRemove](#zoo_hotkeyremove) |

---

## Shared conventions

### Return-value policy

- **Success / boolean** = the number `1` or `0`. (0.4.9 returns numbers for
  booleans rather than the strings `"True"` / `"False"`, so ZooPlug does the
  same.) `Get(AsText(...))` gives `"1"`, and
  `Get(AsNumber(...))` gives `1`.
- **Value retrieval** (paths, text, hashes, etc.) = text.
- **Failure** = text of the form `Moo_<function>|Err_N` (the **underscore**
  in `Err_N` is the real 0.4.9 format — the upstream docs' `ErrN` without
  underscore is a typo).

### Standard error-handling pattern

```
Let ( [
    result = zoo_FileCopy ( "/tmp/a.txt" ; "/tmp/b.txt" )
];
    If ( result = 1 ;
         "ok" ;
         "failed: " & zoo_ErrorDetail ( result )
    )
)
```

`zoo_ErrorDetail` returns a human-readable description for every error code
from all 38 functions.

### Reading the signatures

- Arguments wrapped in `{ }` are optional (they appear greyed out in the
  FileMaker calculation editor).
- `;` is the argument separator.
- Type prefixes follow MooPlug convention: `s` = String, `b` = Boolean.

### Character encoding and paths

- All inputs and outputs are UTF-8. From FileMaker they are just normal
  Unicode text.
- On Windows, paths containing CP932 "dangerous" characters (the well-known
  「ソ・表・予・能」cases, where the second byte is `0x5C`) are handled
  safely — all paths go through `std::filesystem::u8path`.

### Platform support at a glance

| Group | Windows | macOS | Linux Server |
|---|:---:|:---:|:---:|
| Meta / File / Folder / Hash / Zip / Net | ✅ | ✅ | ✅ |
| Dialog | ✅ | ✅ (DialogColour unsupported) | stub (Err_2) |
| Printer | ✅ (winspool) | ✅ (CUPS) | ✅ (CUPS) |
| Process | ✅ | ✅ | ✅ |
| ProgressOptions | ✅ (state only) | ✅ | ✅ |
| Hotkey | ✅ | ✅ (bGlobal ignored) | not supported (Err_3) |
| zoo_Shell / zoo_powershell | ✅ | ✅ (sh / pwsh) | ✅ |

---

# ZooPlug-original functions

## `zoo_Shell`

```
zoo_Shell ( command )
```

Run a one-liner in the shell and return its combined standard output and
standard error as text. ZooPlug's reproduction of the upstream `zoo_Shell`.

| Argument | Type | Required | Description |
|---|---|---|---|
| `command` | String | yes | Command to run |

**Returns**: the output text, with the trailing newline trimmed and line
endings normalised to CR. An empty string if no argument is given.

**Implementation**:
- Windows: `cmd.exe /S /C "<command>"` (the `/S` flag strips just the outer
  pair of quotes and passes everything in between to cmd as a single
  command line, so a one-liner with its own quoting survives unchanged).
- macOS / Linux: `/bin/sh -c "<command>"`.
- On Windows, the captured output is decoded as the system's OEM code
  page (`CP_OEMCP`: 932 on a Japanese system, 437 in the US, 850 in
  Western Europe, etc.) and converted to UTF-8. Encoding is locale-
  adaptive, not hard-coded to CP932.
- `stderr` is folded into the same pipe as `stdout` (`2>&1` equivalent);
  child `stdin` is redirected to `NUL` / `/dev/null` so the process
  never blocks on input. The window is hidden with `CREATE_NO_WINDOW`.

**Examples**:
```
zoo_Shell ( "echo %USERNAME%" )                  // current user
zoo_Shell ( "dir C:\Windows\System32 /b" )       // file list (CP932-safe Japanese names)
zoo_Shell ( "ipconfig" )                         // network config
```

> **Security**: the string is passed to the shell verbatim. Don't build
> commands from untrusted input (fields the user can type into, data fetched
> from the web, etc.) — this is shell injection. Use it only with commands
> you control.

---

## `zoo_powershell`

```
zoo_powershell ( command { ; bCore } )
```

Run a PowerShell script and return its combined stdout and stderr as
**UTF-8 text**. ZooPlug-original — there is no upstream equivalent. Think of
it as the PowerShell counterpart to `zoo_Shell`'s cmd.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `command` | String | yes | | PowerShell script (multi-line is fine; quotes and `$` survive verbatim) |
| `bCore` | Boolean | no | False | True → PowerShell 7 (`pwsh`); False → Windows PowerShell 5.1. macOS / Linux always use `pwsh`. |

**Returns**: the script output (UTF-8, CR-normalised, trailing newline
trimmed).

**Implementation strategy** (see `docs/zoo-powershell-design.md` §18):
- The script is written to a temp `.ps1` **with a UTF-8 BOM**, then passed
  via `-File` (which is what keeps 5.1 from misreading Japanese).
- The script writes its output to a file with `Out-File -Encoding utf8`,
  and the plug-in reads that file back. This sidesteps the well-known
  problem where a PowerShell process's piped stdout is governed by
  `[Console]::OutputEncoding` (which is CP932 by default, on every version
  including pwsh 7).
- Verified to round-trip UTF-8 cleanly through all PowerShell versions
  (5.1 through 7.5) and both language modes (FullLanguage and
  ConstrainedLanguage), including under WDAC enforcement.

**Examples**:
```
zoo_powershell ( "Write-Output 'Hello 表予能'" )
                                                  // → Hello 表予能

zoo_powershell ( "Get-Process | Where-Object {$_.WorkingSet -gt 100MB} | Select-Object Name, Id" )

zoo_powershell ( "Write-Output ('v' + $PSVersionTable.PSVersion.Major)" )         // → v5
zoo_powershell ( "Write-Output ('v' + $PSVersionTable.PSVersion.Major)" ; True )  // → v7
```

> **AppLocker / WDAC environments**: the generated script is placed in
> `%PROGRAMDATA%\ZooPlug\scripts\`. A locked-down deployment only needs the
> administrator to allow `%OSDRIVE%\ProgramData\ZooPlug\*` with a single
> rule (see `docs/zoo-powershell-design.md` §19).

---

# Meta functions

## `zoo_Version`

```
zoo_Version
```

Return ZooPlug's version string. No arguments (the signature has no
parentheses at all).

**Returns**: `"ZooPlug 1.1.1"`.

**Example**:
```
zoo_Version    // → "ZooPlug 1.1.1"
```

> **Note**: ZooPlug returns its own version string `"ZooPlug 1.1.1"`.

---

## `zoo_ErrorDetail`

```
zoo_ErrorDetail ( sError )
```

Convert a `Moo_<function>|Err_N` error string into a human-readable
description. You can pass another Moo function's return value straight in.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sError` | String | yes | Error string returned by another Moo function |

**Returns**: the description (text). Returns an empty string for an
unrecognised code.

**Examples**:
```
zoo_ErrorDetail ( "zoo_FileCopy|Err_3" )          // → "Source file does not exist."
zoo_ErrorDetail ( "zoo_DownloadFile|Err_4" )      // → "File download cancelled by user."
zoo_ErrorDetail ( zoo_FileCopy ( "/no" ; "/x" ) ) // → description on failure, empty on success
```

The full error-description table was built from observation and live
cross-checking of MooPlug 0.4.9 and covers all 38 functions.

---

# File functions (7)

## `zoo_FileExists`

```
zoo_FileExists ( sFile )
```

Check whether a file exists.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sFile` | String | yes | The file path to check |

**Returns**: `1` if it exists, `0` if not, or `zoo_FileExists|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | Empty input |

**Examples**:
```
zoo_FileExists ( "/tmp/foo.txt" )                // → 1 or 0
zoo_FileExists ( "C:\\Users\\me\\Desktop\\x.txt" )
```

---

## `zoo_FileCopy`

```
zoo_FileCopy ( sSource ; sDest {; bOverwrite ; bProgress } )
```

Copy a file.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sSource` | String | yes | | Source path |
| `sDest` | String | yes | | Destination file path |
| `bOverwrite` | Boolean | no | False | Overwrite if the destination exists |
| `bProgress` | Boolean | no | False | Progress dialog (not yet wired up; ignored) |

**Returns**: `1` on success, or `zoo_FileCopy|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | sSource is empty |
| `Err_3` | sSource does not exist |
| `Err_4` | sDest is empty |
| `Err_5` | sDest already exists (overwrite not requested) |
| `Err_6` | Copy failed |
| `Err_7` | Could not delete the existing destination (when overwriting) |

**Examples**:
```
zoo_FileCopy ( "/tmp/a.txt" ; "/tmp/b.txt" )                     // Err_5 if b.txt already exists
zoo_FileCopy ( "/tmp/a.txt" ; "/tmp/b.txt" ; True )              // overwrite allowed
```

---

## `zoo_FileDelete`

```
zoo_FileDelete ( sFile )
```

Delete a file.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sFile` | String | yes | The file to delete |

**Returns**: `1` on success, or `zoo_FileDelete|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | Empty input |
| `Err_3` | Delete failed |
| `Err_4` | File does not exist |

```
zoo_FileDelete ( "/tmp/old.txt" )
```

---

## `zoo_FileMove`

```
zoo_FileMove ( sSource ; sDest {; bOverwrite } )
```

Move a file (a same-drive move is a rename; across drives it falls back to
copy + delete).

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sSource` | String | yes | | Source |
| `sDest` | String | yes | | Destination |
| `bOverwrite` | Boolean | no | False | Overwrite the destination |

**Returns**: `1` on success, or `zoo_FileMove|Err_N`.

**Errors**: same numbering as `zoo_FileCopy` (Err_1 through Err_7). `Err_7`
here is "Error deleting destination file."

```
zoo_FileMove ( "/tmp/a.txt" ; "/tmp/sub/a.txt" ; True )
```

---

## `zoo_FileRead`

```
zoo_FileRead ( sFile )
```

Read a text file and return its contents.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sFile` | String | yes | The file to read |

**Returns**: the file contents as text, or `zoo_FileRead|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | Empty input |
| `Err_3` | Open failed (no such file, permission denied, …) |
| `Err_5` | Read failed |

**Decoding behaviour**:
- A leading UTF-8 BOM is stripped.
- If the bytes are valid UTF-8 they're returned as-is. If not, **on Windows**
  the file is decoded as the system's ANSI code page (`CP_ACP`: 932 on a
  Japanese system, 1252 in the US / Western Europe, 949 in Korea, etc.)
  and converted to UTF-8. The decoder is locale-adaptive, not hard-coded
  to CP932. Line endings are normalised to CR.

```
zoo_FileRead ( "/tmp/note.txt" )
```

---

## `zoo_FileWrite`

```
zoo_FileWrite ( sFile ; sText {; bAppend } )
```

Write text to a file.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sFile` | String | yes | | Destination |
| `sText` | String | yes | | Text to write (UTF-8) |
| `bAppend` | Boolean | no | False | True → append; False → create new (Err_3 if the file already exists) |

**Returns**: `1` on success, or `zoo_FileWrite|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | Empty input |
| `Err_3` | File already exists (and bAppend is false) |
| `Err_4` | Open failed |
| `Err_5` | Write failed |

**Behaviour**:
- Line endings are converted to the OS native form (CRLF on Windows, LF
  elsewhere).
- Upstream MooPlug's documentation lists four arguments (`bAppend` and
  `bOverwrite`), but the real 0.4.9 accepts **three** (`bAppend`
  only). ZooPlug matches the observed behaviour.

```
zoo_FileWrite ( "/tmp/log.txt" ; "hello¶world" )          // create
zoo_FileWrite ( "/tmp/log.txt" ; "more¶" ; True )         // append
```

---

## `zoo_FileInfo`

```
zoo_FileInfo ( sFile ; sInfo {; sOptions } )
```

Get or set a piece of file information. `sInfo` selects the property,
`sOptions` chooses a format or holds the new value.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sFile` | String | yes | The file |
| `sInfo` | String | yes | `"size"` / `"version"` / `"created"` / `"modified"` |
| `sOptions` | String/TimeStamp | no | Format selector or new value (see below) |

**Behaviour per `sInfo`**:

| sInfo | Default return | What `sOptions` controls |
|---|---|---|
| `size` | Human-readable string (`"532 bytes"`, `"1.50 MB"`, …) | `"bytes"` → the raw byte count as a number |
| `version` | `"%d.%d.%d.%d"` (Windows VERSIONINFO only) | — |
| `created` | TimeStamp value | Pass a TimeStamp to **set** the creation time (Windows/Mac only) |
| `modified` | TimeStamp value | Pass a TimeStamp to **set** the modification time |

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | Empty input |
| `Err_3` | File does not exist |
| `Err_6` | Retrieval failed (version is always Err_6 off Windows) |
| `Err_7` | Set failed (Linux can't set creation time, so it returns Err_7) |

```
zoo_FileInfo ( "/tmp/big.bin" ; "size" )               // → "1.50 MB" (human is the default)
zoo_FileInfo ( "/tmp/big.bin" ; "size" ; "bytes" )     // → 1572864
zoo_FileInfo ( "C:\\app.exe" ; "version" )             // → "1.2.3.4"
zoo_FileInfo ( "/tmp/x" ; "modified" )                 // → TimeStamp
zoo_FileInfo ( "/tmp/x" ; "modified" ; GetAsTimestamp ( "2026/06/18 10:00" ) )  // → 1 (set)
```

> **Note**: whether the size default is `human` vs. `bytes`, and whether
> `created` / `modified` return TimeStamp vs. Date, is not yet fully
> confirmed against upstream. Subject to change as compatibility coverage
> is filled in.

---

# Folder functions (6)

## `zoo_FolderExists`

```
zoo_FolderExists ( sFolder )
```

Check whether a folder exists.

**Returns**: `1` / `0` / `zoo_FolderExists|Err_N`.

**Errors**: `Err_1` (arg count), `Err_2` (empty).

```
zoo_FolderExists ( "/tmp/sub" )
```

---

## `zoo_FolderCopy`

```
zoo_FolderCopy ( sSource ; sDest )
```

Copy a folder **recursively**.

**Returns**: `1` on success, or `zoo_FolderCopy|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` – `Err_5` | arg count / empty / source missing / destination exists / other failure |
| `Err_6` | Copy failed (the real 0.4.9 returns the form `"Err_6.<n> (<hex>)"` for partial failure; ZooPlug returns the canonical `Err_6`) |

```
zoo_FolderCopy ( "/tmp/src" ; "/tmp/dst" )
```

---

## `zoo_FolderCreate`

```
zoo_FolderCreate ( sFolder )
```

Create a folder. **Intermediate folders are created too** (equivalent to
`mkdir -p`).

**Returns**: `1` on success, or `zoo_FolderCreate|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | Empty |
| `Err_3` | Already exists |
| `Err_4` | Create failed |

```
zoo_FolderCreate ( "/tmp/zoo/sub/sub2" )    // creates every level
```

---

## `zoo_FolderDelete`

```
zoo_FolderDelete ( sFolder )
```

Delete a folder **and everything in it** (equivalent to `rm -rf`).

**Returns**: `1` on success, or `zoo_FolderDelete|Err_N`.

**Errors**: `Err_1` (arg count) / `Err_2` (empty) / `Err_3` (missing) /
`Err_4` (delete failed).

> **Warning**: this deletes the folder's contents recursively. A bad path
> can wipe important files.

```
zoo_FolderDelete ( "/tmp/zoo" )
```

---

## `zoo_FolderMove`

```
zoo_FolderMove ( sSource ; sDest )
```

Move a folder (a same-drive move is a rename; across drives it falls back
to copy + delete).

**Returns**: `1` on success, or `zoo_FolderMove|Err_N`.

**Errors**: `Err_1` – `Err_6`. `Err_6` is "Error moving folder."

```
zoo_FolderMove ( "/tmp/src" ; "/tmp/archive/src" )
```

---

## `zoo_FolderList`

```
zoo_FolderList ( sFolder {; sPattern ; sSeparator } )
```

List the **files** immediately under `sFolder` (no subfolders, no recursion).

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sFolder` | String | yes | | Folder to list |
| `sPattern` | String | no | `*.*` | Windows FindFirstFile-style wildcard |
| `sSeparator` | String | no | `|` | Separator |

**Returns**: a separator-joined list of filenames, or `zoo_FolderList|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | Empty |
| `Err_3` | No matching files |
| `Err_4` | Folder does not exist |
| `Err_5` | Unknown error |

**Patterns**:
- `*.*` matches everything. `*` matches any run; `?` matches one character.
- ASCII is case-insensitive.

```
zoo_FolderList ( "/tmp/src" )                       // → "a.txt|b.csv|c.png"
zoo_FolderList ( "/tmp/src" ; "*.csv" )             // → "b.csv"
zoo_FolderList ( "/tmp/src" ; "*.*" ; "¶" )         // newline-separated
```

---

# Hash function (1)

## `zoo_Hash`

```
zoo_Hash ( sHash ; sText {; bFile } )
```

Return an MD5 / SHA-1 / SHA-256 / SHA-512 hash as **lowercase hex**.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sHash` | String | yes | | Algorithm: `"md5"` / `"sha1"` / `"sha256"` / `"sha512"` (case-insensitive) |
| `sText` | String | yes | | Input string (or a file path if `bFile=True`) |
| `bFile` | Boolean | no | False | True → treat sText as a file path and hash the file |

**Returns**: the hex string, or `zoo_Hash|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | Bad algorithm |
| `Err_3` | Empty input |
| `Err_4` | (bFile=True) file not found |
| `Err_5` | Read failed |

**Implementation**:
- A self-contained implementation is bundled (no OpenSSL). Verified against
  the NIST/RFC test vectors.

```
zoo_Hash ( "md5" ; "" )           // → d41d8cd98f00b204e9800998ecf8427e
zoo_Hash ( "sha1" ; "abc" )       // → a9993e364706816aba3e25717850c26c9cd0d89d
zoo_Hash ( "sha256" ; "abc" )     // → ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
zoo_Hash ( "sha256" ; "/tmp/big.bin" ; True )    // file mode
```

---

# Zip functions (3)

## `zoo_ZipCompress`

```
zoo_ZipCompress ( sPath {; bTemp ; bOverwrite ; bFolderName ; sPassword } )
```

Zip a file or folder. Pointing at an existing Zip **appends** to it (with a
rebuild strategy that supports replacing an existing entry by the same name).

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sPath` | String | yes | | What to compress (a file or a folder) |
| `bTemp` | Boolean/String | no | False | False → next to the input. True → temp folder. A string → output path or filename. |
| `bOverwrite` | Boolean | no | False | Replace a same-named entry inside the existing Zip |
| `bFolderName` | Boolean | no | True | When compressing a folder, prefix entries with the folder name |
| `sPassword` | String | no | | **Not supported** (any non-empty value returns Err_5) |

**Returns**: the path of the created/updated Zip, or `zoo_ZipCompress|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | Empty input |
| `Err_3` | Input does not exist |
| `Err_4` | A same-named entry already exists in the Zip (overwrite not allowed) |
| `Err_5` | Create failed (also returned when a password was requested) |
| `Err_6` | Output folder does not exist |

```
zoo_ZipCompress ( "/tmp/a.txt" )                              // → "/tmp/a.zip"
zoo_ZipCompress ( "/tmp/src" )                                // compress a folder
zoo_ZipCompress ( "/tmp/a.txt" ; True )                       // → temp folder
zoo_ZipCompress ( "/tmp/a.txt" ; "/out/combined.zip" )        // explicit path (appends if it exists)
zoo_ZipCompress ( "/tmp/a.txt" ; "/out/combined.zip" ; True ) // replace entry if it already exists
```

> **miniz 3.0.2 is bundled** (MIT). UTF-8 entry names work, including
> Japanese filenames.

---

## `zoo_ZipExtract`

```
zoo_ZipExtract ( sFile {; bTemp ; bOverwrite } )
```

Extract just the **first file** in the Zip. (Matches the upstream behaviour
— upstream's docs have said "a future version" will do more, but the real
0.4.9 still only extracts one.)

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sFile` | String | yes | | Zip file |
| `bTemp` | Boolean | no | False | True → temp folder, False → next to the Zip |
| `bOverwrite` | Boolean | no | False | Overwrite an existing destination |

**Returns**: the extracted file's path, or `zoo_ZipExtract|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | Empty input |
| `Err_3` | Zip not found |
| `Err_4` | Open failed |
| `Err_5` | Empty Zip |
| `Err_6` | Extract failed |
| `Err_8` | Destination already exists (overwrite not allowed) |
| `Err_12` | Password-protected entry (not supported) |

**Safety**:
- Output goes to the entry's basename — directory components are stripped.
  That makes **zip-slip attacks impossible**.

```
zoo_ZipExtract ( "/tmp/a.zip" )
zoo_ZipExtract ( "/tmp/a.zip" ; True ; True )
```

---

## `zoo_ZipList`

```
zoo_ZipList ( sZip {; sPattern ; sSeparator } )
```

List the file entries inside a Zip.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sZip` | String | yes | | Zip file |
| `sPattern` | String | no | `*.*` | Wildcard |
| `sSeparator` | String | no | `|` | Separator |

**Returns**: a separator-joined list (in storage order), or
`zoo_ZipList|Err_N`.

**Errors**: `Err_1` – `Err_5`.

```
zoo_ZipList ( "/tmp/combo.zip" )                       // → "a.txt|b.csv|src/sub/c.png"
zoo_ZipList ( "/tmp/combo.zip" ; "*.txt" ; "¶" )
```

---

# Net functions (5) = Download 2 + FTP 3

> **Implementation**: on Windows ZooPlug uses **WinINet** (already shipped
> with the OS, no extra dependency); on POSIX it uses the system
> **libcurl**.

## `zoo_DownloadText`

```
zoo_DownloadText ( sURL )
```

Fetch the body of an HTTP(S) URL and return it as text.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sURL` | String | yes | A URL beginning with http:// or https:// |

**Returns**: the body text, or `zoo_DownloadText|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | Invalid input URL |
| `Err_3` | Download failed (4xx/5xx responses are failures too) |

```
zoo_DownloadText ( "https://example.com/api/text" )
```

---

## `zoo_DownloadFile`

```
zoo_DownloadFile ( sURL {; sLocal ; bProgress } )
```

Download a file from an HTTP(S) URL.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sURL` | String | yes | | URL |
| `sLocal` | String | no | temp folder | Save location: a folder, a full file path, or omitted = the temp folder |
| `bProgress` | Boolean | no | False | Progress dialog (not yet wired up) |

**Returns**: the saved path, or `zoo_DownloadFile|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` / `Err_2` / `Err_3` | arg count / invalid URL / download failed |
| `Err_4` | Cancelled by user |
| `Err_5` | Local save folder does not exist |

```
zoo_DownloadFile ( "https://example.com/big.zip" )                            // → temp folder
zoo_DownloadFile ( "https://example.com/big.zip" ; "/Users/me/Downloads" )    // → a folder
zoo_DownloadFile ( "https://example.com/big.zip" ; "/tmp/x.zip" )             // → a file
```

---

## `zoo_FTPDownload`

```
zoo_FTPDownload ( sServer ; sUser ; sPass ; sRemotePath {; sLocalFile ; bProgress } )
```

Download a file via FTP.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sServer` | String | yes | | Hostname (`ftp://` prefix and `:port` are accepted) |
| `sUser` | String | yes | | Username |
| `sPass` | String | yes | | Password |
| `sRemotePath` | String | yes | | Remote path |
| `sLocalFile` | String | no | temp folder | Save location |
| `bProgress` | Boolean | no | False | Not yet wired up |

**Returns**: the saved path, or `zoo_FTPDownload|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_3` | Invalid sServer |
| `Err_4` | Invalid sUser |
| `Err_5` | Invalid sPass |
| `Err_6` | Could not open the internet connection |
| `Err_7` | Could not open the FTP connection |
| `Err_8` | Same-named local file already exists |
| `Err_10` | Remote file not found |
| `Err_11` | Download failed |
| `Err_13` | Could not open the local file |

```
zoo_FTPDownload ( "ftp.example.com" ; "user" ; "pw" ; "/pub/data.csv" )
```

---

## `zoo_FTPUpload`

```
zoo_FTPUpload ( sServer ; sUser ; sPass ; sLocalFile ; sRemotePath {; bOverwrite ; bProgress } )
```

Upload a file via FTP.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sServer` / `sUser` / `sPass` | String | yes | | Connection info |
| `sLocalFile` | String | yes | | Local file to upload |
| `sRemotePath` | String | yes | | Remote path |
| `bOverwrite` | Boolean | no | False | Overwrite a same-named remote file |
| `bProgress` | Boolean | no | False | Not yet wired up |

**Returns**: `1` on success, or `zoo_FTPUpload|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_3` / `Err_4` / `Err_5` | bad server / user / pass |
| `Err_6` / `Err_7` | connection failures (internet / ftp) |
| `Err_8` | Local source not found |
| `Err_11` | Same-named remote file exists (overwrite not allowed) |
| `Err_12` | Could not open local file |
| `Err_16` | Unknown upload failure |

```
zoo_FTPUpload ( "ftp.example.com" ; "user" ; "pw" ; "/tmp/x.txt" ; "/upload/x.txt" ; True )
```

---

## `zoo_FTPDelete`

```
zoo_FTPDelete ( sServer ; sUser ; sPass ; sRemotePath )
```

Delete a remote file via FTP. (Added in 0.4.9; **undocumented** by the
upstream project.)

| Argument | Type | Required | Description |
|---|---|---|---|
| Connection info | | yes | server / user / pass |
| `sRemotePath` | String | yes | Remote path to delete |

**Returns**: `1` on success, or `zoo_FTPDelete|Err_N`.

**Errors**: `Err_3` / `Err_4` / `Err_5` (inputs), `Err_6` / `Err_7`
(connection), `Err_8` (delete failed).

> **Note**: ErrorDetail returns no description for these error codes (only
> the codes themselves are observable).

```
zoo_FTPDelete ( "ftp.example.com" ; "user" ; "pw" ; "/upload/old.txt" )
```

---

# Dialog functions (3)

> These are **modal GUIs**, so they don't make sense on Server / WebDirect.
> Plug-in functions run synchronously on FileMaker's calculation thread,
> which in Pro is effectively the main UI thread, so a modal dialog can be
> shown there (the macOS backend marshals to the main thread internally).

## `zoo_DialogColour`

```
zoo_DialogColour ( bFull)        // ← yes, the missing space matches the upstream prototype
```

Open a colour picker and return the chosen colour.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `bFull` | Boolean | no | False | True → full picker (the Windows `CC_FULLOPEN` flag) |

**Returns**: a **six-digit uppercase hex** string `"RRGGBB"`, or
`zoo_DialogColour|Err_2` on cancel.

```
zoo_DialogColour                  // → "FF8800" etc.
zoo_DialogColour ( True )         // full picker
```

> **Note**: which colour form upstream actually returns (`"RRGGBB"` /
> `"R,G,B"` decimal / `#RRGGBB`) has not been fully confirmed against
> upstream. ZooPlug currently returns 6-digit upper-case hex `"RRGGBB"`.
>
> **macOS limitation**: macOS has no equivalent of Windows' modal
> `ChooseColor`, so this is unsupported on macOS = always `Err_2`.

---

## `zoo_DialogFile`

```
zoo_DialogFile ( {bOpen ; sTitle ; sDefault } )
```

Open a file picker. All arguments are optional.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `bOpen` | Boolean | no | False | True → open dialog, False → save dialog |
| `sTitle` | String | no | | Dialog title |
| `sDefault` | String | no | | Default filename |

**Returns**: the chosen path, or `zoo_DialogFile|Err_2` on cancel.

```
zoo_DialogFile ( True ; "Choose a file" ; "" )
zoo_DialogFile ( False ; "Save as" ; "output.csv" )
```

---

## `zoo_DialogFolder`

```
zoo_DialogFolder ( { sTitle ; bNewFolder } )
```

Folder picker.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sTitle` | String | no | | Title |
| `bNewFolder` | Boolean | no | False | True → show the "new folder" button |

**Returns**: the chosen path, or an error.

**Errors**:
| Code | Description |
|---|---|
| `Err_2` | Cancelled |
| `Err_3` | Could not retrieve the chosen path |

```
zoo_DialogFolder ( "Choose a destination" ; True )
```

---

# Printer functions (2)

> These do **not** affect FileMaker's printing. They get and set the
> **system default printer**. Not meaningful on anything but Pro.

## `zoo_PrinterDefault`

```
zoo_PrinterDefault ( { sPrinter } )
```

No argument (or empty) → return the system default printer's name. Non-empty
argument → set it as the default and return `1`.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sPrinter` | String | no | Printer name to set as default |

**Returns**: the name (text) when getting, `1` when setting, or
`zoo_PrinterDefault|Err_N`.

**Errors**: `Err_2` (retrieve failed) / `Err_3` (set failed, including
"no such printer").

**Implementation**:
- Windows: `GetDefaultPrinterW` / `SetDefaultPrinterW`.
- POSIX: CUPS `cupsGetNamedDest` / `cupsSetDests`.

```
zoo_PrinterDefault                            // get
zoo_PrinterDefault ( "Canon PIXUS" )          // set → 1
```

---

## `zoo_PrinterList`

```
zoo_PrinterList ( { sSeparator } )
```

List the installed printers.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sSeparator` | String | no | `|` | Separator |

**Returns**: a separator-joined list, or `zoo_PrinterList|Err_N`.

**Errors**: `Err_2` (retrieve failed) / `Err_3` (no printers installed).

```
zoo_PrinterList                       // → "Canon PIXUS|Brother MFC|Microsoft Print to PDF"
zoo_PrinterList ( "¶" )               // newline-separated
```

---

# Process functions (4)

## `zoo_ProcessCount`

```
zoo_ProcessCount ( sProcess )
```

Count running processes. An empty `sProcess` returns the total; otherwise
returns the number that match.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sProcess` | String | yes | Process name (may be empty) |

**Returns**: a number.

**Name matching**:
- Case-insensitive.
- The Windows `.exe` extension is absorbed on either side (so `"notepad"`
  and `"notepad.exe"` match).

```
zoo_ProcessCount ( "" )                // total
zoo_ProcessCount ( "FileMaker Pro" )   // matches only
```

---

## `zoo_ProcessRunning`

```
zoo_ProcessRunning ( sProcess )
```

Return `1` if a matching process is running, `0` if not.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sProcess` | String | yes | Process name |

**Returns**: `1` / `0` / `zoo_ProcessRunning|Err_N`.

**Errors**: `Err_2` (empty input — "Invalid input process.")

```
zoo_ProcessRunning ( "FileMaker Pro" )
```

---

## `zoo_ProcessList`

```
zoo_ProcessList ( { sSeparator } )
```

List running process names.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sSeparator` | String | no | `|` | Separator |

**Returns**: a separator-joined list.

> **Note**: the upstream default separator hasn't been fully confirmed.
> ZooPlug uses `|` (pipe).

```
zoo_ProcessList
zoo_ProcessList ( "¶" )
```

---

## `zoo_ProcessKill`

```
zoo_ProcessKill ( sProcess )
```

Terminate **all** matching processes.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sProcess` | String | yes | Process name |

**Returns**: `1` on success, or `zoo_ProcessKill|Err_N`.

**Errors**: `Err_2` (empty, no match, or terminate failed — "Error
terminating process.")

**Implementation**: Windows = `OpenProcess + TerminateProcess`; POSIX =
`kill(SIGKILL)`.

> **Warning**: don't aim this at FileMaker Pro itself or at important
> system processes.

```
zoo_ProcessKill ( "notepad" )
```

---

# Progress dialog (1)

## `zoo_ProgressOptions`

```
zoo_ProgressOptions ( sTitle {; sCaption ; bCancel } )
```

Set up the options for the progress dialog used by Download / FTP when
`bProgress=True`.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sTitle` | String | yes | | Dialog title |
| `sCaption` | String | no | | Caption text |
| `bCancel` | Boolean | no | False | Show a cancel button |

**Returns**: `1` on success, or `zoo_ProgressOptions|Err_1`.

> **Current limitation**: ZooPlug stores the options and returns `1`, but
> **the progress dialog UI itself isn't wired up yet** (slated for after
> Tier C). Download/FTP's `bProgress=True` is accepted and ignored for now.

```
zoo_ProgressOptions ( "Downloading" ; "Please wait" ; True )
```

---

# Hotkey functions (3)

> These **stay resident** to receive global hotkeys, and a key press
> **starts a FileMaker script**. The implementation uses the standard
> pattern: a fire queue is filled from the host's event loop, and
> `kFMXT_Idle` drains it and calls `FMX_StartScript` on the main thread.
>
> **FM 19.2+ requirement**: the verification file's privilege set must
> have the **`fmplugin` extended privilege** enabled. Without it, the
> script start is rejected with **error 825**.

## `zoo_HotkeyAdd`

```
zoo_HotkeyAdd ( sHotkey ; sFile ; sScript {; sParam ; bAlt ; bControl ; bShift ; bGlobal } )
```

Register a hotkey that starts `sScript` in `sFile` when pressed.

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sHotkey` | String | yes | | Key name (see below) |
| `sFile` | String | yes | | Name of the file the script lives in (passing `Get(FileName)` is the robust choice) |
| `sScript` | String | yes | | Script to start |
| `sParam` | String | no | | Script parameter (received via `Get(ScriptParameter)`) |
| `bAlt` | Boolean | no | False | Alt modifier |
| `bControl` | Boolean | no | False | Ctrl modifier |
| `bShift` | Boolean | no | False | Shift modifier |
| `bGlobal` | Boolean | no | False | True → fires even when FileMaker isn't the active window |

**Returns**: `1` on success, or `zoo_HotkeyAdd|Err_N`.

**Supported keys**: `A`–`Z`, `0`–`9`, `F1`–`F12`, `SPACE`, `ESC` (also
`ESCAPE`), `END`, `HOME`, `UP`, `DOWN`, `LEFT`, `RIGHT`. Case-insensitive,
leading/trailing whitespace allowed.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | Unknown key |
| `Err_3` | Could not create the hotkey window |
| `Err_4` | Already registered (same canonical key) |
| `Err_5` | Registration rejected by the OS |

> **macOS limitation**: `bGlobal` is ignored and the hotkey is **always**
> global, because Carbon's `RegisterEventHotKey` always fires
> system-wide. Limiting to the foreground app is not yet supported.

```
zoo_HotkeyAdd ( "F8" ; Get ( FileName ) ; "OnHotkey" ; "param-test" ; False ; True ; False ; True )
                                          // Ctrl+F8 (global) calls OnHotkey ( "param-test" )
```

---

## `zoo_HotkeyList`

```
zoo_HotkeyList ( { sSeparator } )
```

List the registered hotkeys (formatted like `"CTRL+SHIFT+A"`).

| Argument | Type | Required | Default | Description |
|---|---|---|---|---|
| `sSeparator` | String | no | `|` | Separator |

**Returns**: a separator-joined list, or `zoo_HotkeyList|Err_N`.

**Errors**: `Err_2` (none registered) / `Err_3` (retrieve failed).

```
zoo_HotkeyList                       // → "CTRL+F8|ALT+SHIFT+A"
```

---

## `zoo_HotkeyRemove`

```
zoo_HotkeyRemove ( sHotkey )
```

Unregister a hotkey. **Only the key name is needed** (no modifiers) because
the registry is keyed by canonical key name.

| Argument | Type | Required | Description |
|---|---|---|---|
| `sHotkey` | String | yes | Key name to unregister |

**Returns**: `1` on success, or `zoo_HotkeyRemove|Err_N`.

**Errors**:
| Code | Description |
|---|---|
| `Err_1` | Invalid number of arguments |
| `Err_2` | None registered (zero so far) |
| `Err_3` | Unknown key |
| `Err_4` | No such hotkey |
| `Err_5` | Unregistration failed |

```
zoo_HotkeyRemove ( "F8" )
```

---

## Related documents

- Design / Tier breakdown / time estimates → [`zoo-plug-implementation-spec.md`](zoo-plug-implementation-spec.md)
- `zoo_powershell` design details (temp-file approach, AppLocker / WDAC live tests) → [`zoo-powershell-design.md`](zoo-powershell-design.md)
