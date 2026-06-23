# 変更履歴

ZooPlug の主な変更点をまとめています。
このファイルは手書きで管理し、[Keep a Changelog](https://keepachangelog.com/ja/)
と [セマンティック バージョニング](https://semver.org/lang/ja/) に従います。

## [1.1.1] — 2026-06-23

### Fixed
- `zoo_Version` が正しい ZooPlug のバージョン文字列を返すように修正。1.1.0 のリリース
  バイナリは誤った値を返していました。関数 ID（funcId）は不変なので、差し替えるだけで
  アップグレードでき、既存の計算式はそのまま動作します。

## [1.1.0] — 2026-06-23

シェル実行の 1 関数のみだった ZooPlug を、MooPlug 0.4.9 の全関数セット（合計 39 関数）
へ拡張します。あわせて PowerShell 拡張を追加。引数構成・戻り値・エラーコードは
MooPlug 0.4.9 に固定してあり、計算式を書き換えずプラグインファイルの差し替えだけで
既存ソリューションが動き続けます。

### Added
- **File（7）:** `zoo_FileExists`, `zoo_FileCopy`, `zoo_FileDelete`, `zoo_FileMove`, `zoo_FileRead`, `zoo_FileWrite`, `zoo_FileInfo`
- **Folder（6）:** `zoo_FolderExists`, `zoo_FolderCopy`, `zoo_FolderCreate`, `zoo_FolderDelete`, `zoo_FolderMove`, `zoo_FolderList`
- **Hash（1）:** `zoo_Hash` — MD5 / SHA-1 / SHA-256 / SHA-512 を小文字 hex で返す（OpenSSL 不使用の同梱実装）
- **Zip（3）:** `zoo_ZipCompress`, `zoo_ZipExtract`, `zoo_ZipList`（miniz 同梱・日本語ファイル名対応）
- **Network（5）:** `zoo_DownloadText`, `zoo_DownloadFile`, `zoo_FTPDownload`, `zoo_FTPUpload`, `zoo_FTPDelete`（Windows は WinINet、macOS/Linux は libcurl）
- **Dialog（3）:** `zoo_DialogColour`, `zoo_DialogFile`, `zoo_DialogFolder`（FileMaker Pro 専用。`zoo_DialogColour` は macOS 非対応）
- **Printer（2）:** `zoo_PrinterDefault`, `zoo_PrinterList`（システム既定の取得/設定。Windows は winspool、POSIX は CUPS）
- **Process（4）:** `zoo_ProcessCount`, `zoo_ProcessRunning`, `zoo_ProcessList`, `zoo_ProcessKill`
- **進捗 UI（1）:** `zoo_ProgressOptions`（オプションを保存して成功を返すのみ。進捗ダイアログ UI 本体は未配線）
- **Hotkey（3）:** `zoo_HotkeyAdd`, `zoo_HotkeyList`, `zoo_HotkeyRemove` — グローバルホットキーを登録し、押下で FileMaker スクリプトを起動（Linux 非対応）
- **メタ（2）:** `zoo_Version`, `zoo_ErrorDetail`（全関数の `zoo_X|Err_N` エラー文字列を説明文に変換）
- **`zoo_powershell`（拡張）:** PowerShell スクリプトを実行し UTF-8 で出力を返す。一時ファイル方式により全 PowerShell 版・全言語モード（ConstrainedLanguage / WDAC enforce 下を含む）で文字化けしない。ZooPlug 独自で MooPlug には無い

### Notes
- 引数・戻り値はすべて UTF-8。Windows では日本語の「ダメ文字」パス（ソ・表・予・能 等）も安全に通します。
- 関数の登録順は不変（FileMaker は登録順に funcId を振るため、並べ替えは既存計算式を壊します）。

