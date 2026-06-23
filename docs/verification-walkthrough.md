# ZooPlug verification walkthrough — checking the 38 MooPlug-compat functions in FileMaker Pro 19

> The shipped ZooPlug build registers **39 functions** (the 38 MooPlug-compat
> functions plus the ZooPlug-original `zoo_PowerShell`). This walkthrough
> focuses on the 38 MooPlug-compat surface; `zoo_PowerShell` has its own
> verification track in [`docs/zoo-powershell-design.md`](zoo-powershell-design.md)
> §17 / §18.

**Read this in other languages:** [日本語](verification-walkthrough.ja.md)

Copy-and-paste walkthrough for the **live verification** of the plug-in.
The plug-in is `dist/ZooPlug.fmplugin` (ad-hoc signed) on macOS, and
`build/ZooPlug.fmx64` (produced by `scripts\build-windows-plugin.bat`) on
Windows.

## 0. Prerequisites

- **macOS**: drop the distribution `.fmplugin` into
  `~/Library/Application Support/FileMaker/Extensions/`. Clear the
  quarantine attribute:
  ```
  xattr -dr com.apple.quarantine ZooPlug.fmplugin
  ```
- **Windows**: drop `ZooPlug.fmx64` into
  `C:\Program Files\FileMaker\FileMaker Pro 19\Extensions\` (quit
  FileMaker first to avoid DLL locking).
- Launch FileMaker Pro 19 → Preferences > Plug-Ins and confirm that
  **ZooPlug is enabled**.
- **Important (FM 19.2+)**: enable the **`fmplugin` extended privilege**
  on the verification file's privilege set. Without it, hotkey-driven
  script starts are rejected with **error 825**.

## 1. Evaluate each function in the Data Viewer (the quickest path)

Open Tools > Data Viewer (Advanced Tools must be enabled) and paste each
expression below into the Watch tab. Functions appear in registration
order — if any of them returns `zoo_XXX|Err_N`, cross-check with
[Source/MooError.cpp](../Source/MooError.cpp).

### 1.1 Meta / error lookup

```
zoo_Version
// expected: "ZooPlug 1.1.1"  (ZooPlug returns its own version string)

zoo_ErrorDetail("zoo_FileCopy|Err_3")
// expected: "Source file does not exist."

zoo_ErrorDetail("zoo_DownloadFile|Err_4")
// expected: "File download cancelled by user."  ※follows the observed wording
```

### 1.2 File (7)

```
zoo_FileExists(Get(TemporaryPath) & "no_such.txt")     // expected: 0
zoo_FileWrite(Get(TemporaryPath) & "zoo_test.txt"; "こんにちは 表予能")  // expected: 1
zoo_FileExists(Get(TemporaryPath) & "zoo_test.txt")    // expected: 1
zoo_FileRead(Get(TemporaryPath) & "zoo_test.txt")      // expected: "こんにちは 表予能"
zoo_FileInfo(Get(TemporaryPath) & "zoo_test.txt"; "size")   // expected: human string (e.g. "21 bytes")
zoo_FileInfo(Get(TemporaryPath) & "zoo_test.txt"; "size"; "bytes")  // expected: 21
zoo_FileCopy(Get(TemporaryPath) & "zoo_test.txt"; Get(TemporaryPath) & "zoo_test2.txt"; True)  // expected: 1
zoo_FileMove(Get(TemporaryPath) & "zoo_test2.txt"; Get(TemporaryPath) & "zoo_moved.txt")  // expected: 1
zoo_FileDelete(Get(TemporaryPath) & "zoo_moved.txt")   // expected: 1
```

### 1.3 Folder (6)

```
zoo_FolderCreate(Get(TemporaryPath) & "zoo_dir/sub")   // expected: 1 (intermediates are created too)
zoo_FolderExists(Get(TemporaryPath) & "zoo_dir/sub")   // expected: 1
zoo_FileWrite(Get(TemporaryPath) & "zoo_dir/a.txt"; "a")
zoo_FileWrite(Get(TemporaryPath) & "zoo_dir/b.csv"; "b")
zoo_FolderList(Get(TemporaryPath) & "zoo_dir")           // expected: "a.txt|b.csv" (order may vary)
zoo_FolderList(Get(TemporaryPath) & "zoo_dir"; "*.csv")  // expected: "b.csv"
zoo_FolderCopy(Get(TemporaryPath) & "zoo_dir"; Get(TemporaryPath) & "zoo_dir2")
zoo_FolderMove(Get(TemporaryPath) & "zoo_dir2"; Get(TemporaryPath) & "zoo_dir3")
zoo_FolderDelete(Get(TemporaryPath) & "zoo_dir")        // expected: 1
zoo_FolderDelete(Get(TemporaryPath) & "zoo_dir3")       // expected: 1
```

### 1.4 Hash (1) — verified against the NIST / RFC test vectors

```
zoo_Hash("md5"; "")        // expected: d41d8cd98f00b204e9800998ecf8427e
zoo_Hash("sha1"; "abc")    // expected: a9993e364706816aba3e25717850c26c9cd0d89d
zoo_Hash("sha256"; "abc")  // expected: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
```

### 1.5 Zip (3)

```
zoo_FileWrite(Get(TemporaryPath) & "z.txt"; "zoo zip")
zoo_ZipCompress(Get(TemporaryPath) & "z.txt")          // expected: path (e.g. "...z.zip")
zoo_ZipList(Get(TemporaryPath) & "z.zip")              // expected: "z.txt"
zoo_ZipExtract(Get(TemporaryPath) & "z.zip"; True)     // expected: path of the extracted file in temp
```

### 1.6 Net = Download + FTP (5)

**HTTP** (test against any public URL; pick one with anonymous access):
```
zoo_DownloadText("https://example.com/")           // expected: HTML text
zoo_DownloadFile("https://example.com/")           // expected: local path
```

**FTP** (requires e.g. vsftpd; see `zoo-plug-implementation-spec.md` §8.
Skip if you don't have an FTP server handy):
```
zoo_FTPUpload("ftp.example.com"; "user"; "pw"; Get(TemporaryPath) & "z.txt"; "/upload/z.txt")
zoo_FTPDownload("ftp.example.com"; "user"; "pw"; "/upload/z.txt"; Get(TemporaryPath) & "ftp_dl.txt")
zoo_FTPDelete("ftp.example.com"; "user"; "pw"; "/upload/z.txt")
```

### 1.7 Dialog (3) — modal GUI

```
zoo_DialogColour                  // pick a colour and OK → expected: "RRGGBB" hex (e.g. "FF0000"). Cancel → Err_2
zoo_DialogFile(True; "Choose"; "")  // open / expected: a path. Cancel → Err_2
zoo_DialogFolder("Choose"; True)    // shows the new-folder button / expected: a path
```

### 1.8 Printer (2)

```
zoo_PrinterList                    // expected: "PrinterA|PrinterB|..."
zoo_PrinterDefault                 // expected: name of the default printer
zoo_PrinterDefault("PrinterA")     // expected: 1. Note: setting is destructive — watch what you change
```

### 1.9 Process (4)

```
zoo_ProcessCount("")                       // expected: total process count
zoo_ProcessCount("FileMaker Pro")          // expected: >= 1 (Windows absorbs .exe)
zoo_ProcessRunning("FileMaker Pro")        // expected: 1
zoo_ProcessList                            // expected: "..|..|.." form
// zoo_ProcessKill is destructive. If you want to try it, use a throwaway process like notepad.
```

### 1.10 ProgressOptions (1)

```
zoo_ProgressOptions("Downloading"; "Please wait"; True)    // expected: 1
// (The state is stored. The actual progress UI for Download / FTP is not yet wired up.)
```

### 1.11 Hotkey (3) — the round-trip test for Idle / StartScript

The **`fmplugin` extended privilege must be enabled** (otherwise you get
Err 825).

1. Create a notification script `OnHotkey` in the verification file:
   - `Show Custom Dialog [ "Hotkey fired: " & Get(ScriptParameter) ]`
2. In the Data Viewer:
   ```
   zoo_HotkeyAdd("F8"; Get(FileName); "OnHotkey"; "param-test"; False; True; False; True)
   // expected: 1
   zoo_HotkeyList                                  // expected: "CTRL+F8"
   ```
3. **Press Ctrl+F8 on the keyboard** → within a few seconds `OnHotkey`
   should run and the dialog should show `param-test`.
   - Note: it's not immediate execution but a queued start
     (`FMX_StartScript` queues the script rather than running it inline).
     It may not fire until a currently-running script finishes.
4. Unregister:
   ```
   zoo_HotkeyRemove("F8")                          // expected: 1
   zoo_HotkeyList                                  // expected: zoo_HotkeyList|Err_2
   ```

## 2. Self-test script — run every function in one go and write the results to a file

A way to run the equivalent of `scripts\ZooPlug_SelfTest` as a single
FileMaker script — **no Data Viewer needed**.

```
# Set up variables
Set Variable [ $tmp ; Get(TemporaryPath) ]
Set Variable [ $out ; $tmp & "zoo_selftest.txt" ]

# Call each function and join the results with newlines
Set Variable [ $log ;
    "version="   & zoo_Version & ¶ &
    "hash="      & zoo_Hash("sha256"; "abc") & ¶ &
    "exists0="   & zoo_FileExists($tmp & "no_such.txt") & ¶ &
    "write="     & zoo_FileWrite($tmp & "z.txt"; "ok 表予能") & ¶ &
    "read="      & zoo_FileRead($tmp & "z.txt") & ¶ &
    "zipc="      & zoo_ZipCompress($tmp & "z.txt") & ¶ &
    "ziplist="   & zoo_ZipList($tmp & "z.zip") & ¶ &
    "proccnt="   & zoo_ProcessCount("FileMaker Pro") & ¶ &
    "printers="  & zoo_PrinterList & ¶ &
    "default="   & zoo_PrinterDefault & ¶ &
    "errdet="    & zoo_ErrorDetail("zoo_FileCopy|Err_3")
]

# Save to file
Set Variable [ $rc ; zoo_FileWrite($out ; $log) ]
Show Custom Dialog [ "Wrote " & $out & " (rc=" & $rc & ")" ]
```

After running, read the file back with
`zoo_FileRead($tmp & "zoo_selftest.txt")` and eyeball it.

## 3. Cross-checking against MooPlug 0.4.9 (requires FileMaker Pro 11 + MooPlug 0.4.9)

A real 0.4.9 is loaded into Pro 11 Extensions on the test machine.
Evaluate the same calculations on Pro 11 (MooPlug 0.4.9) and on Pro 19
(ZooPlug) and compare. Items still pending from the **TODO-compat** list:

- `zoo_Version` (the return string)
- `zoo_FileInfo` size default when `sOptions` is omitted (human / bytes)
- `zoo_FileExists` return type (use `GetAsText` to see whether it's a
  number or a string)
- `zoo_DialogColour` return form (`"RRGGBB"` hex vs. `"R,G,B"`)
- `zoo_PrinterDefault` with an empty-string argument
- `zoo_ProcessList` default separator

If you find a discrepancy, close the corresponding TODO-compat entry and
adjust the ZooPlug implementation to match real 0.4.9 if needed.

## 4. Known limitations (macOS)

- **`zoo_DialogColour`**: macOS has no modal equivalent of Windows'
  `ChooseColor`, so this is unsupported on macOS — always returns
  `Err_2`. Windows is the primary target.
- **`zoo_HotkeyAdd`'s `bGlobal`**: macOS's `RegisterEventHotKey` always
  fires system-wide, so `bGlobal` is ignored (limiting to the foreground
  app would require an `.mm` extension that uses NSWorkspace).

## 5. Debugging a failure

1. If `zoo_XXX|Err_N` comes back, pass it through `zoo_ErrorDetail` to
   get the description.
2. If that doesn't clear it up, follow the matching entry in
   [Source/MooError.cpp](../Source/MooError.cpp) and the `return N;`
   sites in the pure-logic files under [Source/](../Source/)
   (`FileOps.cpp`, `NetOps.cpp`, etc.).
3. For platform-specific behaviour, check the `#ifdef _WIN32` /
   `__APPLE__` branches.
