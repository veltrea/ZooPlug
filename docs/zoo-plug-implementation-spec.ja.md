# ZooPlug 全関数再現 — 実装設計書

**他の言語で読む:** [English](zoo-plug-implementation-spec.md)

> MooPlug 0.4.9（全 38 関数）を ZooPlug で再現するための設計仕様。
> 仕様の根拠は本家公式マニュアル（Wayback Machine 保存版）と、FileMaker Pro 11 で
> 動かす MooPlug 0.4.9 の挙動観察。既存実装（`Source/ZooPlug.cpp` / `Source/ShellExec.*`）
> の設計をそのまま踏襲・拡張する。

---

## 1. 目的とスコープ

- **目的**: MooPlug 0.4.9 の外部関数を、FileMaker の計算式から見て**シグネチャ・戻り値・エラーコードまで互換**に再現する。
- **互換の基準**: 本家公式マニュアルと、FileMaker Pro 11 で動く MooPlug 0.4.9 の挙動観察（計算式エディタが見せる外部関数の登録一覧とプロトタイプ文字列、各関数の戻り値、`zoo_ErrorDetail` が返すエラー文字列）。**ドキュメントと挙動が食い違う場合は実機の挙動を正**とする。
- **対象関数**: 実機登録される **38 関数**（公開 36 + `zoo_Shell` + `zoo_FTPDelete`）。`Moo_PrinterSet` は本家ドキュメントにはあるが登録関数一覧に出ないため**作らない**。
- **既存資産**: `zoo_Shell` は実装済み。本設計はそれ以外の 37 関数を追加する作業を定義する。

### スコープ外（明示）

- `Moo_PrinterSet`（実機未登録）。
- MooPlug の旧版固有関数（`zoo_FolderInfo` 等、0.4.9 で消滅したもの）。
- iOS（zoo_Shell 同様、プロセス起動・GUI が不可）。

---

## 2. 設計原則（既存 ZooPlug の踏襲）

1. **純粋ロジックと FileMaker グルーの分離。** `zoo_Shell` が `ShellExec.h/cpp`（FMWrapper 非依存）と `ZooPlug.cpp`（グルー）に分かれているのと同じ構造を全関数に適用する。ロジックは `tsx` 相当＝ `c++ -I Source ... && ./test` で FileMaker なしに単体テストする。
2. **プロトタイプ文字列が真実の源。** 関数は 0.4.9 実機リストの文字列で登録する。既存の `RegisterFunction` / `NumberOfParameters` が `{ ; optional }` 記法を解釈するので、**プロトタイプを正しく書けば必要/省略可能引数の数は自動算出される**（数点だけ手動調整、§5.4）。
3. **クロスプラットフォーム可能なものは最初からそうする。** `std::filesystem` / libcurl / miniz で済む関数は Win/Mac/Linux 共通実装にし、プラットフォーム固有 API が要る関数だけ `#ifdef` で分ける。
4. **エラーは MooPlug 形式の文字列で返す。** 戻り値テキストに `Moo_関数名|Err_N` を返す（§5.1）。`zoo_ErrorDetail` がその文字列を説明文に変換する。
5. **状態を持つ関数（Hotkey / ProgressOptions）はサーバーの並行呼び出しを想定**し、グローバル状態を mutex 保護する。

---

## 3. アーキテクチャ

```
ZooPlug.fmplugin / .fmx64 / .fmx
└─ ZooPlug.cpp              FileMaker グルー（登録・ディスパッチ・引数取り出し・エラー整形）
   ├─ MooError.h/cpp        エラーコード→説明文テーブル、Err 文字列生成、zoo_ErrorDetail
   ├─ 純粋ロジック層（FMWrapper 非依存・単体テスト可能）
   │   ├─ ShellExec.*       zoo_Shell（実装済み）
   │   ├─ FileOps.*         File/Folder 13 関数（std::filesystem）
   │   ├─ HashImpl.*        zoo_Hash（MD5/SHA1/SHA256/SHA512 を同梱実装）
   │   ├─ ZipOps.*          Zip 3 関数（miniz ラッパ）
   │   ├─ NetOps.*          Download 2 + FTP 3 関数（libcurl ラッパ）
   │   └─ SysInfo.*         一部 Process/Printer のロジック寄り部分
   └─ プラットフォーム層（#ifdef で分岐・GUI/OS 固有）
       ├─ Dialogs_win.* / Dialogs_mac.mm    Dialog 3 関数
       ├─ Printer_win.* / Printer_mac.*     Printer 2 関数
       ├─ Process_win.* / Process_mac.*     Process 4 関数
       ├─ Progress_win.* / Progress_mac.*   ProgressOptions + 進捗ダイアログ
       └─ Hotkey_win.* / Hotkey_mac.*       Hotkey 3 関数（+ Idle ディスパッチ）
```

- **依存ライブラリは単一ファイル/小規模を優先**（配布を軽くするため）。libcurl のみ動的/静的リンクを選ぶ（§8）。
- **GUI・StartScript を伴う層だけがプラットフォーム依存**。データ処理系は共通。

---

## 4. 関数の分類と実装方針（全 38 関数）

実装難度・依存・プラットフォーム性で 3 ティアに分ける。**Tier A から着手**（成果が早く、依存も軽い）。

### Tier A — 共通実装（`std::filesystem` / 同梱ライブラリ。Win/Mac/Linux 同一コード）

| 関数 | 実装手段 | 難度 | 備考 |
|---|---|---|---|
| zoo_Version | 文字列定数を返す | 易 | バージョンを返すメタ関数。ZooPlug 自身のバージョン文字列を返す。引数なし |
| zoo_ErrorDetail | エラー文字列→説明のマップ | 易 | 全テーブルは資料にあり。§5.1 |
| zoo_FileExists | `fs::exists` | 易 | |
| zoo_FileCopy | `fs::copy_file` | 易 | bOverwrite / bProgress。進捗は Tier C 連携 |
| zoo_FileDelete | `fs::remove` | 易 | |
| zoo_FileMove | `fs::rename`（跨デバイスは copy+remove） | 易 | |
| zoo_FileRead | テキスト読み込み→返す | 易 | |
| zoo_FileWrite | 書き込み/追記（**実機は 3 引数: bAppend のみ**） | 易 | bOverwrite は実機に無い（4 番目の引数は無視される）。§5.4 |
| zoo_FileInfo | size/created/modified=`std::filesystem`、version=Win 専用 | 中 | version は `#ifdef _WIN32`（GetFileVersionInfo） |
| zoo_FolderCopy | `fs::copy`（recursive） | 易 | |
| zoo_FolderCreate | `fs::create_directories` | 易 | |
| zoo_FolderDelete | `fs::remove_all` | 易 | |
| zoo_FolderExists | `fs::is_directory` | 易 | |
| zoo_FolderList | `directory_iterator` + glob（sPattern `*.*`） | 中 | sSeparator 区切り |
| zoo_FolderMove | `fs::rename` | 易 | |
| zoo_Hash | 同梱 MD5/SHA 実装（public domain） | 中 | sHash で algo 選択、bFile でファイル/文字列 |
| zoo_ZipCompress | miniz `mz_zip_writer_*` | 中 | password は classic Zip 2.0（miniz 拡張）= 後回し可 |
| zoo_ZipExtract | miniz `mz_zip_reader_*` | 中 | **単一ファイルのみ**（本家仕様どおり） |
| zoo_ZipList | miniz `mz_zip_reader_get_filename` | 易 | |
| zoo_DownloadText | libcurl GET → 本文返す | 中 | 初回 libcurl 統合に +工数 |
| zoo_DownloadFile | libcurl GET → temp/local 保存→パス返す | 中 | bProgress は Tier C 連携 |
| zoo_FTPDownload | libcurl FTP RETR | 中 | bProgress は Tier C 連携 |
| zoo_FTPUpload | libcurl FTP STOR | 中 | |
| zoo_FTPDelete | libcurl FTP DELE（未公開関数） | 易 | NetOps に相乗り |

### Tier B — プラットフォーム固有（Windows 優先、macOS は任意で並行）

| 関数 | Windows | macOS | 難度 |
|---|---|---|---|
| zoo_DialogColour | `ChooseColor` | `NSColorPanel` | 中 |
| zoo_DialogFile | `GetOpenFileName`/`GetSaveFileName` | `NSOpenPanel`/`NSSavePanel` | 中 |
| zoo_DialogFolder | `SHBrowseForFolder` / IFileDialog | `NSOpenPanel`(dir) | 中 |
| zoo_PrinterDefault | `GetDefaultPrinter`/`SetDefaultPrinter` | CUPS `cupsGetDefault`/`cupsSetDefault` | 中 |
| zoo_PrinterList | `EnumPrinters` | CUPS `cupsGetDests` | 中 |
| zoo_ProcessCount | `CreateToolhelp32Snapshot` | `sysctl`/`libproc` | 中 |
| zoo_ProcessList | 同上 | 同上 | 中 |
| zoo_ProcessRunning | 同上 | 同上 | 中 |
| zoo_ProcessKill | `OpenProcess`+`TerminateProcess` | `kill(2)` | 中 |

> **GUI ダイアログのスレッド注意**: プラグイン関数は FileMaker 計算スレッドで同期実行される。Pro ではこれが実質メイン UI スレッドなのでモーダルダイアログは出せるが、macOS の AppKit は「メインスレッドでのみ」制約があるため、`NSApp` の状態に注意（`performSelectorOnMainThread` 相当を検討）。Server/WebDirect では Dialog/Printer/Process 系は**意味をなさない**ので `kServerCompatible` フラグを外す。

### Tier C — 非同期・常駐が要る難関（Idle/StartScript のスレッド規律が必須）

| 関数 | 何が難しいか | 必要な仕組み |
|---|---|---|
| zoo_ProgressOptions + 進捗ダイアログ | 長時間処理中に別 UI を出し、Download/FTP の進捗を反映。状態を次の呼び出しに持ち越す | 進捗ウィンドウ（Win: 自前ダイアログ / Mac: NSProgressIndicator）、進捗コールバック、グローバル状態の mutex 保護 |
| zoo_HotkeyAdd / List / Remove | グローバルホットキー登録 → 押下で**FileMaker スクリプトを起動**。常駐し、別スレッドのメッセージループから StartScript を呼ぶ | Win: 隠しウィンドウ + `RegisterHotKey` + メッセージポンプ。押下を**完了キューに積み**、`kFMXT_Idle`（メインスレッド）で `FMX_StartScript` を呼ぶ。Init で `cStartScript`/`cCurrentEnv` をコピー（鉄則 3）。オプション文字列 9 文字目 `Y`（Idle 有効）が必須 |

> **Hotkey はこのプロジェクト唯一の本格的な常駐構成。** `FMX_StartScript` はキュー投入（即時実行ではない）、FM19.2+ は `fmplugin` 拡張アクセス権が必要（無効だとエラー 825）。

---

## 5. 横断的な仕組み

### 5.1 エラーコード互換（最重要）

- MooPlug は**エラー時に戻り値テキストとして `Moo_関数名|Err_N` を返す**（数値エラーではない）。実機形式は **`Err_N`（アンダースコアあり）**。
- 実装: 各関数の失敗パスで `MakeError("zoo_FileCopy", 3)` → `"zoo_FileCopy|Err_3"` を `reply.SetAsText` で返す。
- `zoo_ErrorDetail(sError)`: 受け取った `"zoo_FileCopy|Err_3"` を説明文 `"Source file does not exist."` に変換。実装は `std::unordered_map<std::string,std::string>`。**全エントリは本家 Error Descriptions と実機照合から生成**（0.4.9 実機の上限まで網羅: FTPUpload Err_16、ZipExtract Err_12 等）。
- 補足: FileMaker の `Get(LastExternalErrorDetail)` 連携（`kPluginErrResult1..8`）は MooPlug にはない挙動なので**任意**。互換重視なら戻り値テキスト方式のみで十分。

### 5.2 文字エンコーディング

> **着手前に Windows コンソールの文字エンコーディングに注意。** cmd の出力エンコーディングはコマンドごとに違う・
> CP932 の 0x5C ダメ文字・`std::filesystem` の日本語パス・`_popen` 回避など、ここを甘く見ると日本語環境で必ず詰まる。

- 引数取り出しは既存 `TextAsUTF8` を共通ヘルパー化。戻りは `AssignWithLength(..., kEncoding_UTF8)`。
- **Windows のファイルパス**: `std::filesystem::path(utf8str)` は **誤り**（ANSI=CP932 解釈で欠落）。`fs::u8path` か UTF-16 変換経由で作る。日本語パス対応に必須。
- **shell-exec 系（zoo_Shell）**: `_popen`/`system` でなく `CreateProcessW` + パイプで生バイト捕捉 → システムの OEM コードページ (`CP_OEMCP`：日本=932、米=437、西欧=850 など、ロケール依存で動的に決まる) で復号して UTF-8 に変換。起動は `cmd.exe /S /C "<command>"` を使う — `/S` は外側の引用符を 1 組だけ剥がし、中身を 1 コマンドとしてそのまま渡すので、クォートを含むワンライナーをユーザーが書いたまま実行できる。`stderr` は `stdout` と同じパイプにまとめ（`2>&1` 相当）、`stdin` は `NUL` に向け、コンソール窓は `CREATE_NO_WINDOW` で出さない。CP932 固定ではないので欧米ロケールでも文字化けせず動く。
- **Download/FTP/Zip が返すパス・ファイル名**も同じ CP932 ダメ文字の罠を踏むので、パス操作は UTF-16 上で行う。

### 5.3 一時フォルダ・パスの扱い

- Download/Zip が使う「テンポラリフォルダ」: `fs::temp_directory_path()`。
- 戻り値のパス区切りは OS ネイティブ（Win は `\`）。MooPlug は Windows 専用だったので Win では `\` 区切りが期待値。

### 5.4 関数登録（プロトタイプ→引数数の自動算出）

- 既存 `RegisterFunction(prototype, func, description)` をそのまま使い、**38 個分のプロトタイプを 0.4.9 実機リスト文字列で**登録する。
- `NumberOfParameters` が `{ }` を解釈して min/max を出す。**手動調整が要る例外**:
  - `zoo_DialogColour( bFull)` — `{}` が無く bFull が必須扱いになるが、本家は省略可（Required: No）。min=0 に手動指定。
  - 引数なし（`zoo_Version`）は min=max=0。
- **登録順は固定**（途中に関数を挿入しない）。既存計算式の funcId ズレを防ぐ。新規追加は末尾に。
- フラグ: `kDisplayInAllDialogs | kFutureCompatible` を基本に、Tier B/C の GUI 系は `kServerCompatible` を**付けない**。

### 5.5 ディスパッチの整理

- 現状は `zoo_Shell` 1 個を直接登録。38 個になるので、`{プロトタイプ, 関数ポインタ, 説明, min上書き}` の**テーブル駆動**に小リファクタする。`Init` でテーブルを舐めて登録。

---

## 6. プラットフォーム戦略

| プラットフォーム | 対象ティア | 備考 |
|---|---|---|
| **Windows**（第一目標） | A + B + C 全部 | MooPlug 本来の環境。Windows 実機 + FM11/19 で検証 |
| **macOS** | A 全部 + B（Dialog/Printer/Process）+ C（任意） | macOS 実機で検証。Hotkey/Progress の Mac 対応は工数大、優先度低 |
| **Linux**（FileMaker Server） | A のうち GUI 無し（File/Folder/Hash/Zip/Download/FTP/Version/ErrorDetail）| Server には Dialog/Printer/Process/Hotkey は不適。`kServerCompatible` を付けるのは A の非 GUI のみ |

> 推奨: **Windows で全 38 関数を完成 → macOS は Tier A+B → Linux は Tier A 非 GUI**、の順。クロス対応を全関数に広げるより、まず 1 プラットフォーム完結が早い。

---

## 7. 依存ライブラリ

| 用途 | ライブラリ | 形態 | ライセンス | 備考 |
|---|---|---|---|---|
| HTTP/FTP | **libcurl** | 静的リンク推奨 | MIT 系 | Download 2 + FTP 3。Win は schannel、Mac は Secure Transport/LibreSSL |
| Zip | **miniz** | 単一ファイル同梱 | MIT | Compress/Extract/List。password は別途 |
| ハッシュ | **同梱 public-domain 実装**（sha2.c / md5 等） | ソース同梱 | PD/MIT | OpenSSL 依存を避ける |
| 文字変換 | 標準（`<codecvt>` 代替の自前 or OS API） | — | — | Win の UTF-8↔UTF-16 |

- **OpenSSL は避ける**（配布が重く署名も面倒）。ハッシュは単一ファイル実装で足りる。
- libcurl だけは規模が大きい。Windows は vcpkg/プリビルド静的、macOS はシステム libcurl リンク、Linux はシステム libcurl を選択肢に。
- CMake に `find_package`/`FetchContent` で取り込み、`Libraries/` 同梱方針（既存の FMWrapper と同じ流儀）。

---

## 8. テスト戦略（既存方針の踏襲）

1. **純粋ロジックの単体テスト**（FileMaker 不要）。`tests/test_*.cpp` を関数群ごとに追加し、`ctest` で回す。FileOps/HashImpl/ZipOps/NetOps はここで大半を検証。
2. **NetOps はローカルサーバで**: Python の `http.server` / Linux VM の vsftpd 等を相手に Download/FTP を回す（メインマシン占有しない＝リモート活用）。
3. **プラットフォーム層（Dialog/Printer/Process/Hotkey）は実機**: Windows 実機（FM11/19）・macOS 実機で確認。Hotkey は「押下→指定スクリプト起動」を実機で。
4. **互換性照合**: 同じ計算式を MooPlug 0.4.9（Windows 実機の Extensions に配置済み）と ZooPlug で実行し、戻り値・エラー文字列を突き合わせる。`MooPlug.fp7` のサンプルスクリプトが各関数の実例。
5. **FileMaker サンプルファイル**: 全関数の呼び出し例を入れた `.fmp12` を作る（MooPlug.fp7 相当）。

---

## 9. 実装フェーズ（マイルストーン）

**M0 — 基盤整備**
登録のテーブル駆動化、`MooError`（Err 文字列 + ErrorDetail マップ）、共通ヘルパー（UTF-8、temp パス、Win の UTF-16 変換）、テストハーネス拡張。→ ここで `zoo_Version` / `zoo_ErrorDetail` も完成。

**M1 — Tier A（データ処理系 23 関数）**
File 7 → Folder 6 → Hash 1 → Zip 3 → Download 2 → FTP 3。各群ごとに純粋ロジック + グルー + 単体テスト。クロスプラットフォームで一気に通る。**ここで 38 中 25 関数（M0 含む）が完成**。

**M2 — Tier B（Windows GUI/システム 9 関数）**
Dialog 3 → Printer 2 → Process 4。Windows ネイティブで実装、Windows 実機で確認。

**M3 — Tier C（難関 4 機能）**
ProgressOptions + 進捗ダイアログ → Download/FTP の bProgress 結線 → Hotkey 3（Idle/StartScript 常駐）。Idle/StartScript のスレッド規律を正しく押さえることが必須。

**M4 — 配布・macOS 展開・サンプル**
ad-hoc 署名（deep, unified bundle）、macOS の Tier A+B 移植、サンプル `.fmp12`、MooPlug との互換照合パス。

---

## 10. 工数見積もり

前提: 既存 ZooPlug の足場（登録・ロジック分離・CMake・zoo_Shell）を再利用。1 関数あたり「純粋ロジック + グルー + 単体テスト + 1 プラットフォーム検証」込み。数値は**集中作業の時間**（h）。

### Windows 優先・全 38 関数

| フェーズ | 内容 | 工数(h) |
|---|---|---|
| M0 | 基盤（登録テーブル化・MooError・ヘルパー・Version・ErrorDetail） | 8–12 |
| M1-File | File 7 関数 | 4–6 |
| M1-Folder | Folder 6 関数 | 3–5 |
| M1-Hash | Hash（同梱実装統合） | 2–4 |
| M1-Zip | Zip 3（miniz 統合、password 除く） | 5–8 |
| M1-Net | Download 2 + FTP 3（libcurl 初回統合込み） | 7–11 |
| M2-Dialog | Dialog 3（Windows） | 5–8 |
| M2-Printer | Printer 2（Windows） | 3–5 |
| M2-Process | Process 4（Windows） | 4–6 |
| M3-Progress | ProgressOptions + 進捗 UI + bProgress 結線 | 6–10 |
| M3-Hotkey | Hotkey 3（常駐 + Idle + StartScript） | 10–16 |
| 横断 | ライブラリ同梱・CMake クロスビルド整備 | 4–8 |
| 横断 | ad-hoc 署名・パッケージング | 3–5 |
| 横断 | サンプル .fmp12・MooPlug 互換照合 | 4–6 |
| **合計** | | **68–110** |

→ **概算 90 時間（集中作業 ≒ 12 営業日相当）**。

### クロスプラットフォーム追加（macOS Tier A+B、Linux Tier A 非 GUI）

| 追加分 | 工数(h) |
|---|---|
| macOS: Dialog/Printer/Process 移植 | 13–20 |
| macOS: ProgressOptions（任意） | 6–10 |
| Linux: Tier A 非 GUI のビルド・検証（ロジック共通なので主に検証） | 4–8 |
| 追加クロス検証パス | 4–8 |
| **追加合計** | **27–46** |

→ フルクロス対応で **総計およそ 120–150 時間**。

### スコープ別の早見

| スコープ | 関数数 | 概算(h) | 何が手に入るか |
|---|---|---|---|
| **最小**: M0 + M1（データ処理系のみ） | 25 | **30–45** | File/Folder/Hash/Zip/Download/FTP/Version/ErrorDetail。Win/Mac/Linux 共通。GUI・常駐なし |
| **実用**: + M2（Win GUI/システム） | 34 | **45–70** | 上記 + Dialog/Printer/Process。Hotkey/進捗のみ未対応 |
| **完全(Win)**: + M3 | 38 | **70–110** | Windows で全 38 関数 |
| **フルクロス** | 38 | **120–150** | + macOS/Linux 展開 |

### 見積もりの前提・リスク

- **重い 2 点が全体を左右**: ① Hotkey（常駐 + StartScript のスレッド規律。スレッド周りの罠を踏むと数倍化）② libcurl の各 OS 静的リンク整備（初回が重い）。この 2 つを早めに技術検証（spike）すると見積もり精度が上がる。
- Zip の **password 保護**（classic Zip 2.0 暗号）は miniz 標準外。必要なら +3–5h。不要ならスコープ外。
- `zoo_FileInfo` の **version 取得は Windows 専用**（GetFileVersionInfo）。Mac/Linux では未対応 or 代替値。
- 上記は「動作する」までの工数。**MooPlug の細かな挙動の完全一致**（エラー番号の境界条件、空入力時の戻り、区切り文字のデフォルト等）の作り込みは互換照合パス（M4）で吸収。

---

## 11. 未決定事項（着手前に決めるとブレない）

1. **スコープ**: 上記早見の「最小／実用／完全(Win)／フルクロス」のどれを目標にするか。
2. **プラットフォーム優先順位**: Windows 完結 → 横展開で良いか（推奨）、最初から 3 OS 同時か。
3. **Zip password**: 対応するか（本家は対応。互換重視なら要）。
4. **エラー連携**: 戻り値テキスト方式のみで良いか、`Get(LastExternalErrorDetail)` も併設するか。
5. **libcurl の同梱方針**: 静的同梱（配布は楽・ビルド重）か、システム libcurl リンク（配布で依存）か。
6. **Hotkey の優先度**: 難関かつ MooPlug でも後期追加。最後に回す/落とす判断。
