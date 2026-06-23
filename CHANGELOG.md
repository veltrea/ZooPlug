# Changelog

All notable changes to ZooPlug are recorded here.
This file is maintained by hand and follows [Keep a Changelog](https://keepachangelog.com/)
and [Semantic Versioning](https://semver.org/).

## [1.1.1] — 2026-06-23

### Fixed
- `zoo_Version` now returns the correct ZooPlug version string. The 1.1.0 release
  binaries reported an incorrect value. The function IDs (funcIds) are unchanged, so
  upgrading is a drop-in replacement and existing formulas keep working.

## [1.1.0] — 2026-06-23

Expands ZooPlug from the single shell function to the full MooPlug 0.4.9 function set
(39 functions in total), plus a PowerShell extension.
Argument layouts, return values, and error codes are pinned to MooPlug 0.4.9, so existing
solutions keep working by swapping the plug-in file instead of rewriting formulas.

### Added
- **File (7):** `zoo_FileExists`, `zoo_FileCopy`, `zoo_FileDelete`, `zoo_FileMove`, `zoo_FileRead`, `zoo_FileWrite`, `zoo_FileInfo`.
- **Folder (6):** `zoo_FolderExists`, `zoo_FolderCopy`, `zoo_FolderCreate`, `zoo_FolderDelete`, `zoo_FolderMove`, `zoo_FolderList`.
- **Hash (1):** `zoo_Hash` — MD5 / SHA-1 / SHA-256 / SHA-512 as lowercase hex (self-contained implementation, no OpenSSL).
- **Zip (3):** `zoo_ZipCompress`, `zoo_ZipExtract`, `zoo_ZipList` (bundled miniz; UTF-8 file names).
- **Network (5):** `zoo_DownloadText`, `zoo_DownloadFile`, `zoo_FTPDownload`, `zoo_FTPUpload`, `zoo_FTPDelete` (WinINet on Windows, libcurl on macOS/Linux).
- **Dialog (3):** `zoo_DialogColour`, `zoo_DialogFile`, `zoo_DialogFolder` (FileMaker Pro only; `zoo_DialogColour` is unavailable on macOS).
- **Printer (2):** `zoo_PrinterDefault`, `zoo_PrinterList` (system default; winspool on Windows, CUPS on POSIX).
- **Process (4):** `zoo_ProcessCount`, `zoo_ProcessRunning`, `zoo_ProcessList`, `zoo_ProcessKill`.
- **Progress (1):** `zoo_ProgressOptions` (stores the options and returns success; the progress-dialog UI is not yet wired).
- **Hotkey (3):** `zoo_HotkeyAdd`, `zoo_HotkeyList`, `zoo_HotkeyRemove` — register a global hotkey that launches a FileMaker script (not supported on Linux).
- **Metadata (2):** `zoo_Version`, `zoo_ErrorDetail` (decodes the `zoo_X|Err_N` error strings of every function).
- **`zoo_powershell` (extension):** runs PowerShell scripts and returns UTF-8 output via a temp-file strategy that survives every PowerShell version and language mode (incl. ConstrainedLanguage / WDAC enforcement). ZooPlug-only — not part of MooPlug.

### Notes
- All arguments and return values are UTF-8; on Windows, Japanese "dame-moji" paths (ソ, 表, 予, 能, …) are handled safely.
- Function registration order is fixed — FileMaker assigns funcIds by registration order, so reordering would break existing formulas.

