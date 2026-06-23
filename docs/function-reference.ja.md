# ZooPlug 関数リファレンス（日本語版）

**他の言語で読む:** [English](function-reference.md)

ZooPlug が提供する全 **39 関数**（MooPlug 0.4.9 由来の 38 関数 + ZooPlug 独自の `zoo_powershell`）の利用者向けリファレンスです。

- 本ファイルは **ZooPlug を使う人が日常開いて式を書くためのリファレンス** です（仕様は MooPlug 0.4.9 の挙動観察と実機照合で固定済み）。
- 実装の挙動は実機照合と単体テストで裏取りしてあります。本家ドキュメントとの食い違いは「注意」として明記しています。
- 関数の **登録順は不変厳守**（FileMaker の funcId が登録順に振られるため、途中への挿入は既存計算式を壊す）。順番自体が API の一部です。

---

## 目次

| 区分 | 関数 |
|---|---|
| ZooPlug 独自 | [zoo_Shell](#zoo_shell) ／ [zoo_powershell](#zoo_powershell) |
| メタ | [zoo_Version](#zoo_version) ／ [zoo_ErrorDetail](#zoo_errordetail) |
| File（7） | [zoo_FileExists](#zoo_fileexists) ／ [zoo_FileCopy](#zoo_filecopy) ／ [zoo_FileDelete](#zoo_filedelete) ／ [zoo_FileMove](#zoo_filemove) ／ [zoo_FileRead](#zoo_fileread) ／ [zoo_FileWrite](#zoo_filewrite) ／ [zoo_FileInfo](#zoo_fileinfo) |
| Folder（6） | [zoo_FolderExists](#zoo_folderexists) ／ [zoo_FolderCopy](#zoo_foldercopy) ／ [zoo_FolderCreate](#zoo_foldercreate) ／ [zoo_FolderDelete](#zoo_folderdelete) ／ [zoo_FolderMove](#zoo_foldermove) ／ [zoo_FolderList](#zoo_folderlist) |
| Hash（1） | [zoo_Hash](#zoo_hash) |
| Zip（3） | [zoo_ZipCompress](#zoo_zipcompress) ／ [zoo_ZipExtract](#zoo_zipextract) ／ [zoo_ZipList](#zoo_ziplist) |
| Net = Download + FTP（5） | [zoo_DownloadText](#zoo_downloadtext) ／ [zoo_DownloadFile](#zoo_downloadfile) ／ [zoo_FTPDownload](#zoo_ftpdownload) ／ [zoo_FTPUpload](#zoo_ftpupload) ／ [zoo_FTPDelete](#zoo_ftpdelete) |
| Dialog（3） | [zoo_DialogColour](#zoo_dialogcolour) ／ [zoo_DialogFile](#zoo_dialogfile) ／ [zoo_DialogFolder](#zoo_dialogfolder) |
| Printer（2） | [zoo_PrinterDefault](#zoo_printerdefault) ／ [zoo_PrinterList](#zoo_printerlist) |
| Process（4） | [zoo_ProcessCount](#zoo_processcount) ／ [zoo_ProcessKill](#zoo_processkill) ／ [zoo_ProcessList](#zoo_processlist) ／ [zoo_ProcessRunning](#zoo_processrunning) |
| 進捗 UI（1） | [zoo_ProgressOptions](#zoo_progressoptions) |
| Hotkey（3） | [zoo_HotkeyAdd](#zoo_hotkeyadd) ／ [zoo_HotkeyList](#zoo_hotkeylist) ／ [zoo_HotkeyRemove](#zoo_hotkeyremove) |

---

## 共通の決まりごと

### 戻り値の方針

- **成功/真偽** = 数値 `1` / `0`（MooPlug 0.4.9 が真偽を `"True"`/`"False"` 文字列ではなく数値で返す、という挙動観察に合わせて ZooPlug も数値を返す設計）。`Get(AsText(...))` で取り出すと `"1"`、`Get(AsNumber(...))` で `1`。
- **値の取得**（パス・テキスト・ハッシュなど）= テキスト。
- **失敗** = テキストで `Moo_関数名|Err_N`（`Err_` の **アンダースコア付き** が実機形式。ドキュメントの `ErrN` 表記は誤記）。

### エラー処理の定形

```
Let ( [
    result = zoo_FileCopy ( "/tmp/a.txt" ; "/tmp/b.txt" )
];
    If ( result = 1 ;
         "ok" ;
         "失敗: " & zoo_ErrorDetail ( result )
    )
)
```

`zoo_ErrorDetail` は全 38 関数のエラー説明文を返します。

### シグネチャの読み方

- `{ }` で囲まれた引数は省略可能（FileMaker 計算式エディタでも灰色表示）。
- `;` が引数の区切り。
- 型表記は本家 MooPlug の慣習に準拠（`s` = String、`b` = Boolean）。

### 文字エンコーディング・パス

- すべての引数・戻り値は UTF-8。FileMaker 側からは普通の Unicode テキストとして扱えます。
- Windows で日本語パス（特に「ソ・表・予・能」等の CP932 ダメ文字）を含むパスも安全に通します（`std::filesystem::u8path` 経由でパス生成）。

### 対応プラットフォーム早見

| 区分 | Windows | macOS | Linux Server |
|---|:---:|:---:|:---:|
| メタ / File / Folder / Hash / Zip / Net | ✅ | ✅ | ✅ |
| Dialog | ✅ | ✅（DialogColour 除く） | スタブ（Err_2） |
| Printer | ✅（winspool） | ✅（CUPS） | ✅（CUPS） |
| Process | ✅ | ✅ | ✅ |
| ProgressOptions | ✅（状態保持のみ） | ✅ | ✅ |
| Hotkey | ✅ | ✅（bGlobal は無視） | 非対応（Err_3） |
| zoo_Shell / zoo_powershell | ✅ | ✅（sh / pwsh） | ✅ |

---

# ZooPlug 独自関数

## `zoo_Shell`

```
zoo_Shell ( command )
```

シェルでワンライナーを実行し、標準出力＋標準エラーをテキストで返します。MooPlug 本家の `zoo_Shell` の再現。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| `command` | String | はい | 実行するコマンド |

**戻り値**: 出力テキスト（末尾改行は除去、改行は CR に正規化）。引数が無い場合は空文字列。

**実装**:
- Windows: `cmd.exe /S /C "<command>"`（`/S` は外側の引用符を 1 組だけ
  剥がし、中身を 1 つのコマンドラインとしてそのまま cmd に渡すフラグ。
  クォートを含むワンライナーをユーザーが書いたまま実行できる）
- macOS / Linux: `/bin/sh -c "<command>"`
- Windows では出力をシステムの OEM コードページ (`CP_OEMCP`：日本=932、
  米=437、西欧=850 など、ロケール依存で動的に決まる) として復号し
  UTF-8 に変換。CP932 固定ではないので、欧米ロケールでも文字化けせず動く。
- `stderr` は `stdout` と同じパイプにまとめる（`2>&1` 相当）。子プロセスの
  `stdin` は `NUL` / `/dev/null` に向けて入力待ちを防ぎ、コンソール窓は
  `CREATE_NO_WINDOW` で出さない。

**例**:
```
zoo_Shell ( "echo %USERNAME%" )                  // 現在のユーザー名
zoo_Shell ( "dir C:\Windows\System32 /b" )       // System32 のファイル一覧（CP932 が漢字パスも通る）
zoo_Shell ( "ipconfig" )                         // ネットワーク設定
```

> **セキュリティ**: 渡した文字列はそのままシェルが解釈します。信頼できない入力からコマンドを組み立てない（シェルインジェクション対策）。

---

## `zoo_powershell`

```
zoo_powershell ( command { ; bCore } )
```

PowerShell スクリプトを実行し、標準出力＋標準エラーを **UTF-8 テキスト**で返します。ZooPlug 独自で本家にはありません。`zoo_Shell` の cmd 版に対する PowerShell 版です。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `command` | String | はい | | PowerShell スクリプト（複数行可・クオート/`$` も素通り） |
| `bCore` | Boolean | いいえ | False | True で PowerShell 7（pwsh）、False で Windows PowerShell 5.1。Mac/Linux は常に pwsh |

**戻り値**: スクリプトの出力（UTF-8、CR 正規化、末尾改行除去）。

**実装方針**（`docs/zoo-powershell-design.ja.md` §18）:
- スクリプトを **UTF-8 BOM 付きの一時 `.ps1`** に書き出し、`-File` で渡す（5.1 でも日本語が壊れない）。
- 出力は `Out-File -Encoding utf8` でファイルに書かせて読み戻す（パイプ stdout が CP932 になる問題を回避）。
- 全 PowerShell 版（5.1〜7.5）と全言語モード（FullLanguage / ConstrainedLanguage、WDAC enforce 下含む）で UTF-8 往復が壊れない。

**例**:
```
zoo_powershell ( "Write-Output 'Hello 表予能'" )
                                                  // → Hello 表予能

zoo_powershell ( "Get-Process | Where-Object {$_.WorkingSet -gt 100MB} | Select-Object Name, Id" )

zoo_powershell ( "Write-Output ('v' + $PSVersionTable.PSVersion.Major)" )         // → v5
zoo_powershell ( "Write-Output ('v' + $PSVersionTable.PSVersion.Major)" ; True )  // → v7
```

> **AppLocker / WDAC 環境**: 生成スクリプトは `%PROGRAMDATA%\ZooPlug\scripts\` に置きます。ロックダウン配布では管理者が `%OSDRIVE%\ProgramData\ZooPlug\*` を 1 行許可登録すれば動きます（`docs/zoo-powershell-design.ja.md` §19）。

---

# メタ関数

## `zoo_Version`

```
zoo_Version
```

ZooPlug のバージョン文字列を返します。引数なし（括弧自体が無いシグネチャ）。

**戻り値**: `"ZooPlug 1.1.1"`。

**例**:
```
zoo_Version    // → "ZooPlug 1.1.1"
```

> **注意**: ZooPlug は独自のバージョン文字列 `"ZooPlug 1.1.1"` を返します。

---

## `zoo_ErrorDetail`

```
zoo_ErrorDetail ( sError )
```

`zoo_X|Err_N` 形式のエラー文字列を、人間が読める説明文に変換します。エラーを返した別の Moo 関数の戻り値をそのまま渡せます。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| `sError` | String | はい | 別 Moo 関数が返したエラー文字列 |

**戻り値**: 説明文（テキスト）。未知のエラー文字列を渡した場合は空文字列。

**例**:
```
zoo_ErrorDetail ( "zoo_FileCopy|Err_3" )          // → "Source file does not exist."
zoo_ErrorDetail ( "zoo_DownloadFile|Err_4" )      // → "File download cancelled by user."
zoo_ErrorDetail ( zoo_FileCopy ( "/no" ; "/x" ) ) // → 失敗時のみ説明文、成功時は空
```

エラー説明表は MooPlug 0.4.9 の挙動観察と実機照合で作成してあり、全 38 関数を網羅しています。

---

# File 関数（7）

## `zoo_FileExists`

```
zoo_FileExists ( sFile )
```

ファイルの存在を確認します。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| `sFile` | String | はい | 確認するファイルのパス |

**戻り値**: 存在すれば `1`、無ければ `0`、エラーなら `zoo_FileExists|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | 入力ファイルパスが空 |

**例**:
```
zoo_FileExists ( "/tmp/foo.txt" )                // → 1 or 0
zoo_FileExists ( "C:\\Users\\me\\Desktop\\x.txt" )
```

---

## `zoo_FileCopy`

```
zoo_FileCopy ( sSource ; sDest {; bOverwrite ; bProgress } )
```

ファイルをコピーします。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sSource` | String | はい | | コピー元 |
| `sDest` | String | はい | | コピー先（ファイルパス） |
| `bOverwrite` | Boolean | いいえ | False | True で既存を上書き |
| `bProgress` | Boolean | いいえ | False | 進捗ダイアログ（現状未配線・無視） |

**戻り値**: 成功で `1`、失敗で `zoo_FileCopy|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | sSource が空 |
| `Err_3` | sSource が存在しない |
| `Err_4` | sDest が空 |
| `Err_5` | sDest が既存（上書き不可） |
| `Err_6` | コピー失敗 |
| `Err_7` | 既存 dest の削除失敗（上書き時） |

**例**:
```
zoo_FileCopy ( "/tmp/a.txt" ; "/tmp/b.txt" )                     // 既存なら Err_5
zoo_FileCopy ( "/tmp/a.txt" ; "/tmp/b.txt" ; True )              // 上書き有
```

---

## `zoo_FileDelete`

```
zoo_FileDelete ( sFile )
```

ファイルを削除します。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| `sFile` | String | はい | 削除するファイル |

**戻り値**: 成功で `1`、失敗で `zoo_FileDelete|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | 入力が空 |
| `Err_3` | 削除失敗 |
| `Err_4` | ファイルが存在しない |

**例**:
```
zoo_FileDelete ( "/tmp/old.txt" )
```

---

## `zoo_FileMove`

```
zoo_FileMove ( sSource ; sDest {; bOverwrite } )
```

ファイルを移動します（同一ドライブ内なら rename、跨ぐ場合は copy+delete）。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sSource` | String | はい | | 元 |
| `sDest` | String | はい | | 先 |
| `bOverwrite` | Boolean | いいえ | False | 既存上書き |

**戻り値**: 成功で `1`、失敗で `zoo_FileMove|Err_N`。

**エラー**: `zoo_FileCopy` と同じ並び（Err_1〜Err_7。Err_7 は "Error deleting destination file."）。

**例**:
```
zoo_FileMove ( "/tmp/a.txt" ; "/tmp/sub/a.txt" ; True )
```

---

## `zoo_FileRead`

```
zoo_FileRead ( sFile )
```

テキストファイルを読み込んで内容を返します。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| `sFile` | String | はい | 読み込むファイル |

**戻り値**: ファイル内容のテキスト、または `zoo_FileRead|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | 入力が空 |
| `Err_3` | オープン失敗（存在しない・権限なし等） |
| `Err_5` | 読み込み失敗 |

**実装の挙動**:
- UTF-8 BOM 付きなら除去。
- UTF-8 として妥当ならそのまま、不正なら **Windows ではシステムの ANSI コードページ
  (`CP_ACP`：日本=932、米/西欧=1252、韓=949 など、ロケール依存で動的に決まる) として復号**
  してから UTF-8 に正規化（CR 改行統一）。CP932 固定ではないので欧米ロケールでも動く。

**例**:
```
zoo_FileRead ( "/tmp/note.txt" )
```

---

## `zoo_FileWrite`

```
zoo_FileWrite ( sFile ; sText {; bAppend } )
```

テキストをファイルへ書き込みます。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sFile` | String | はい | | 書き込み先 |
| `sText` | String | はい | | 書き込むテキスト（UTF-8 のまま） |
| `bAppend` | Boolean | いいえ | False | True で追記、False で新規（既存なら Err_3） |

**戻り値**: 成功で `1`、失敗で `zoo_FileWrite|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | 入力が空 |
| `Err_3` | 既存（追記でない） |
| `Err_4` | オープン失敗 |
| `Err_5` | 書き込み失敗 |

**実装の挙動**:
- 改行は OS ネイティブに変換（Windows = CRLF、Mac/Linux = LF）。
- 本家 MooPlug ドキュメントは 4 引数（`bAppend ; bOverwrite`）を謳いますが、実機で観察される挙動は **3 引数（`bAppend` のみ）**。ZooPlug は実機の観察結果に準拠。

**例**:
```
zoo_FileWrite ( "/tmp/log.txt" ; "hello¶world" )          // 新規
zoo_FileWrite ( "/tmp/log.txt" ; "more¶" ; True )         // 追記
```

---

## `zoo_FileInfo`

```
zoo_FileInfo ( sFile ; sInfo {; sOptions } )
```

ファイル情報の取得・設定を行います。`sInfo` で項目、`sOptions` で書式または設定値を指定します。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| `sFile` | String | はい | 対象ファイル |
| `sInfo` | String | はい | `"size"` / `"version"` / `"created"` / `"modified"` |
| `sOptions` | String/TimeStamp | いいえ | 書式または設定値（後述） |

**`sInfo` 別の挙動**:

| sInfo | 既定の戻り | sOptions の役割 |
|---|---|---|
| `size` | 人間が読める書式の文字列（例 `"532 bytes"` / `"1.50 MB"`） | `"bytes"` を渡すと整数のバイト数 |
| `version` | `"%d.%d.%d.%d"`（Win 専用 VERSIONINFO） | — |
| `created` | TimeStamp 値 | TimeStamp を渡すと作成日時の **設定**（Win/Mac のみ） |
| `modified` | TimeStamp 値 | TimeStamp を渡すと更新日時の **設定** |

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | 入力が空 |
| `Err_3` | ファイル無し |
| `Err_6` | 情報取得失敗（version は Windows 以外で常に Err_6） |
| `Err_7` | 設定失敗（Linux は作成日時設定不可 = Err_7） |

**例**:
```
zoo_FileInfo ( "/tmp/big.bin" ; "size" )               // → "1.50 MB"（既定 = human）
zoo_FileInfo ( "/tmp/big.bin" ; "size" ; "bytes" )     // → 1572864
zoo_FileInfo ( "C:\\app.exe" ; "version" )             // → "1.2.3.4"
zoo_FileInfo ( "/tmp/x" ; "modified" )                 // → TimeStamp
zoo_FileInfo ( "/tmp/x" ; "modified" ; GetAsTimestamp ( "2026/06/18 10:00" ) )  // → 1（設定）
```

> **注意**: size の既定が human か bytes か、created/modified の戻り型が TimeStamp か Date かは、本家との完全互換確認が未了です。将来の互換調査で変わる可能性があります。

---

# Folder 関数（6）

## `zoo_FolderExists`

```
zoo_FolderExists ( sFolder )
```

フォルダの存在を確認します。

**戻り値**: 存在 `1` / 無 `0` / エラー `zoo_FolderExists|Err_N`。

**エラー**: `Err_1`（引数数）、`Err_2`（空）。

```
zoo_FolderExists ( "/tmp/sub" )
```

---

## `zoo_FolderCopy`

```
zoo_FolderCopy ( sSource ; sDest )
```

フォルダを **再帰的に**コピーします。

**戻り値**: 成功 `1` / 失敗 `zoo_FolderCopy|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1`〜`Err_5` | 引数数 / 空 / 無し / 既存 / その他失敗系 |
| `Err_6` | コピー失敗（部分失敗時、実機は `"Err_6.<n> (<hex>)"` 形式の詳細を返すが、ZooPlug は正規形 `Err_6`） |

```
zoo_FolderCopy ( "/tmp/src" ; "/tmp/dst" )
```

---

## `zoo_FolderCreate`

```
zoo_FolderCreate ( sFolder )
```

フォルダを作成します。**中間フォルダも合わせて作る**（`mkdir -p` 相当）。

**戻り値**: 成功 `1` / 失敗 `zoo_FolderCreate|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | 空 |
| `Err_3` | 既存 |
| `Err_4` | 作成失敗 |

```
zoo_FolderCreate ( "/tmp/zoo/sub/sub2" )    // 全層作成
```

---

## `zoo_FolderDelete`

```
zoo_FolderDelete ( sFolder )
```

フォルダを **中身ごと**削除します（`rm -rf` 相当）。

**戻り値**: 成功 `1` / 失敗 `zoo_FolderDelete|Err_N`。

**エラー**: `Err_1`（引数数）/ `Err_2`（空）/ `Err_3`（無し）/ `Err_4`（削除失敗）。

> **注意**: 中身ごと再帰削除します。誤指定で重要ファイルを消さないこと。

```
zoo_FolderDelete ( "/tmp/zoo" )
```

---

## `zoo_FolderMove`

```
zoo_FolderMove ( sSource ; sDest )
```

フォルダを移動します（同一ドライブなら rename、跨ぐ場合は copy + delete）。

**戻り値**: 成功 `1` / 失敗 `zoo_FolderMove|Err_N`。

**エラー**: `Err_1`〜`Err_6`。`Err_6` は "Error moving folder."。

```
zoo_FolderMove ( "/tmp/src" ; "/tmp/archive/src" )
```

---

## `zoo_FolderList`

```
zoo_FolderList ( sFolder {; sPattern ; sSeparator } )
```

フォルダ **直下の「ファイル」名** を一覧します（サブフォルダは除く、再帰しない）。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sFolder` | String | はい | | 対象フォルダ |
| `sPattern` | String | いいえ | `*.*` | Windows FindFirstFile 風ワイルドカード |
| `sSeparator` | String | いいえ | `|` | 区切り文字 |

**戻り値**: 区切り連結のファイル名一覧、または `zoo_FolderList|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | 空 |
| `Err_3` | 該当ファイル無し |
| `Err_4` | フォルダ無し |
| `Err_5` | 不明なエラー |

**パターン**:
- `*.*` = すべて。`*` = 任意列、`?` = 任意 1 文字。
- ASCII は大小無視。

```
zoo_FolderList ( "/tmp/src" )                       // → "a.txt|b.csv|c.png"
zoo_FolderList ( "/tmp/src" ; "*.csv" )             // → "b.csv"
zoo_FolderList ( "/tmp/src" ; "*.*" ; "¶" )         // 改行区切り
```

---

# Hash 関数（1）

## `zoo_Hash`

```
zoo_Hash ( sHash ; sText {; bFile } )
```

MD5 / SHA-1 / SHA-256 / SHA-512 のハッシュを **小文字 hex** で返します。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sHash` | String | はい | | アルゴリズム名 `"md5"` / `"sha1"` / `"sha256"` / `"sha512"`（大小無視） |
| `sText` | String | はい | | 入力文字列（または `bFile=True` のときファイルパス） |
| `bFile` | Boolean | いいえ | False | True で sText をファイルパスとして読みハッシュ |

**戻り値**: 16 進文字列、または `zoo_Hash|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | アルゴリズム不正 |
| `Err_3` | 入力が空 |
| `Err_4` | （bFile=True 時）ファイル無し |
| `Err_5` | 読み込み失敗 |

**実装**:
- 同梱の self-contained 実装（OpenSSL 不使用）。NIST/RFC ベクタで検証済み。

```
zoo_Hash ( "md5" ; "" )           // → d41d8cd98f00b204e9800998ecf8427e
zoo_Hash ( "sha1" ; "abc" )       // → a9993e364706816aba3e25717850c26c9cd0d89d
zoo_Hash ( "sha256" ; "abc" )     // → ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
zoo_Hash ( "sha256" ; "/tmp/big.bin" ; True )    // ファイル
```

---

# Zip 関数（3）

## `zoo_ZipCompress`

```
zoo_ZipCompress ( sPath {; bTemp ; bOverwrite ; bFolderName ; sPassword } )
```

ファイル/フォルダを Zip にします。既存 Zip を指すと **追加（リビルド方式で同名置換も可）** になります。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sPath` | String | はい | | 圧縮対象（ファイル or フォルダ） |
| `bTemp` | Boolean/String | いいえ | False | False=入力と同フォルダ / True=テンポラリ / 文字列=出力パス or ファイル名 |
| `bOverwrite` | Boolean | いいえ | False | 既存 Zip 内の同名エントリ置換 |
| `bFolderName` | Boolean | いいえ | True | フォルダ圧縮時に先頭にフォルダ名を付与 |
| `sPassword` | String | いいえ | | **未対応**（指定すると Err_5） |

**戻り値**: 作成/更新した Zip のパス、または `zoo_ZipCompress|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | 入力が空 |
| `Err_3` | 入力が存在しない |
| `Err_4` | Zip 内に同名既存（上書き不可） |
| `Err_5` | 作成失敗（パスワード指定含む） |
| `Err_6` | 出力フォルダが存在しない |

```
zoo_ZipCompress ( "/tmp/a.txt" )                              // → "/tmp/a.zip"
zoo_ZipCompress ( "/tmp/src" )                                // フォルダ圧縮
zoo_ZipCompress ( "/tmp/a.txt" ; True )                       // テンポラリへ
zoo_ZipCompress ( "/tmp/a.txt" ; "/out/combined.zip" )        // 明示パス（追加可）
zoo_ZipCompress ( "/tmp/a.txt" ; "/out/combined.zip" ; True ) // 既存同名上書き
```

> **miniz 3.0.2 を同梱**しています（MIT）。日本語ファイル名も UTF-8 で扱えます。

---

## `zoo_ZipExtract`

```
zoo_ZipExtract ( sFile {; bTemp ; bOverwrite } )
```

Zip 内の **最初の 1 ファイルだけ**を展開します（本家仕様に準拠。複数展開は本家でも `"a future version"` のまま）。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sFile` | String | はい | | Zip ファイル |
| `bTemp` | Boolean | いいえ | False | True でテンポラリへ、False で Zip と同フォルダ |
| `bOverwrite` | Boolean | いいえ | False | 既存上書き |

**戻り値**: 展開先パス、または `zoo_ZipExtract|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | 入力が空 |
| `Err_3` | Zip が無い |
| `Err_4` | オープン失敗 |
| `Err_5` | 空 Zip |
| `Err_6` | 展開失敗 |
| `Err_8` | 展開先に同名既存（上書き不可） |
| `Err_12` | パスワード付き Zip（未対応） |

**実装の安全性**:
- パスは除いて basename だけで展開するため、**zip-slip 攻撃が不可能**。

```
zoo_ZipExtract ( "/tmp/a.zip" )
zoo_ZipExtract ( "/tmp/a.zip" ; True ; True )
```

---

## `zoo_ZipList`

```
zoo_ZipList ( sZip {; sPattern ; sSeparator } )
```

Zip の内容（ファイルエントリのみ）を一覧します。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sZip` | String | はい | | Zip ファイル |
| `sPattern` | String | いいえ | `*.*` | ワイルドカード |
| `sSeparator` | String | いいえ | `|` | 区切り |

**戻り値**: 格納順のエントリ一覧、または `zoo_ZipList|Err_N`。

**エラー**: `Err_1`〜`Err_5`。

```
zoo_ZipList ( "/tmp/combo.zip" )                       // → "a.txt|b.csv|src/sub/c.png"
zoo_ZipList ( "/tmp/combo.zip" ; "*.txt" ; "¶" )
```

---

# Net 関数（5）= Download 2 + FTP 3

> **実装**: ZooPlug は Windows で OS 同梱の **WinINet（外部依存ゼロ）**、POSIX で **システム libcurl** を使います。

## `zoo_DownloadText`

```
zoo_DownloadText ( sURL )
```

HTTP(S) URL の本文を取得してテキストで返します。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| `sURL` | String | はい | http:// または https:// で始まる URL |

**戻り値**: 本文テキスト、または `zoo_DownloadText|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | 入力 URL 不正 |
| `Err_3` | ダウンロード失敗（HTTP 4xx/5xx も含む） |

```
zoo_DownloadText ( "https://example.com/api/text" )
```

---

## `zoo_DownloadFile`

```
zoo_DownloadFile ( sURL {; sLocal ; bProgress } )
```

HTTP(S) URL からファイルをダウンロードし、ローカルへ保存。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sURL` | String | はい | | URL |
| `sLocal` | String | いいえ | テンポラリ | 保存先（フォルダ / ファイルパス / 省略=テンポラリ） |
| `bProgress` | Boolean | いいえ | False | 進捗ダイアログ（現状未配線） |

**戻り値**: 保存先パス、または `zoo_DownloadFile|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1`/`Err_2`/`Err_3` | 引数数 / URL 不正 / ダウンロード失敗 |
| `Err_4` | ユーザーキャンセル |
| `Err_5` | ローカル保存先フォルダ無し |

```
zoo_DownloadFile ( "https://example.com/big.zip" )                            // テンポラリへ
zoo_DownloadFile ( "https://example.com/big.zip" ; "/Users/me/Downloads" )    // フォルダ指定
zoo_DownloadFile ( "https://example.com/big.zip" ; "/tmp/x.zip" )             // ファイル指定
```

---

## `zoo_FTPDownload`

```
zoo_FTPDownload ( sServer ; sUser ; sPass ; sRemotePath {; sLocalFile ; bProgress } )
```

FTP でファイルをダウンロード。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sServer` | String | はい | | ホスト名（`ftp://` 接頭辞・`:port` 指定可） |
| `sUser` | String | はい | | ユーザー名 |
| `sPass` | String | はい | | パスワード |
| `sRemotePath` | String | はい | | リモートパス |
| `sLocalFile` | String | いいえ | テンポラリ | 保存先 |
| `bProgress` | Boolean | いいえ | False | 未配線 |

**戻り値**: 保存先パス、または `zoo_FTPDownload|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_3` | server 入力不正 |
| `Err_4` | user 入力不正 |
| `Err_5` | password 入力不正 |
| `Err_6` | インターネット接続失敗 |
| `Err_7` | FTP 接続失敗 |
| `Err_8` | ローカル同名既存 |
| `Err_10` | リモートにファイル無し |
| `Err_11` | ダウンロード失敗 |
| `Err_13` | ローカルファイル開けない |

```
zoo_FTPDownload ( "ftp.example.com" ; "user" ; "pw" ; "/pub/data.csv" )
```

---

## `zoo_FTPUpload`

```
zoo_FTPUpload ( sServer ; sUser ; sPass ; sLocalFile ; sRemotePath {; bOverwrite ; bProgress } )
```

FTP でファイルをアップロード。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sServer`/`sUser`/`sPass` | String | はい | | 接続情報 |
| `sLocalFile` | String | はい | | アップロードするローカルファイル |
| `sRemotePath` | String | はい | | リモートパス |
| `bOverwrite` | Boolean | いいえ | False | リモート同名上書き |
| `bProgress` | Boolean | いいえ | False | 未配線 |

**戻り値**: 成功 `1` / 失敗 `zoo_FTPUpload|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_3`/`Err_4`/`Err_5` | server/user/pass 入力不正 |
| `Err_6`/`Err_7` | 接続失敗（internet / ftp） |
| `Err_8` | ローカル source 無し |
| `Err_11` | リモートに同名既存（上書き不可） |
| `Err_12` | ローカル open 失敗 |
| `Err_16` | 不明なアップロード失敗 |

```
zoo_FTPUpload ( "ftp.example.com" ; "user" ; "pw" ; "/tmp/x.txt" ; "/upload/x.txt" ; True )
```

---

## `zoo_FTPDelete`

```
zoo_FTPDelete ( sServer ; sUser ; sPass ; sRemotePath )
```

FTP でリモートファイルを削除します（**0.4.9 で追加・本家公式ドキュメントには無い**未公開関数）。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| 接続情報 | | はい | server / user / pass |
| `sRemotePath` | String | はい | 削除するリモートパス |

**戻り値**: 成功 `1` / 失敗 `zoo_FTPDelete|Err_N`。

**エラー**: `Err_3`/`Err_4`/`Err_5`（入力）、`Err_6`/`Err_7`（接続）、`Err_8`（削除失敗）。

> **注意**: これらのエラーコードは ErrorDetail で説明が引けません（戻り値だけが観測されます）。

```
zoo_FTPDelete ( "ftp.example.com" ; "user" ; "pw" ; "/upload/old.txt" )
```

---

# Dialog 関数（3）

> **GUI モーダル**のため Server/WebDirect では使えません。プラグイン関数は FileMaker の計算スレッドで同期実行され、Pro ではこれが実質メイン UI スレッドのためダイアログを出せます（macOS の AppKit は内部でメインスレッド同期）。

## `zoo_DialogColour`

```
zoo_DialogColour ( bFull)        // ← 本家プロトタイプ表記のスペース欠落も再現
```

カラーピッカーを開いて選んだ色を返します。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `bFull` | Boolean | いいえ | False | True でフルピッカー（Windows の `CC_FULLOPEN`） |

**戻り値**: **6 桁大文字 16 進** `"RRGGBB"`、またはキャンセルで `zoo_DialogColour|Err_2`。

```
zoo_DialogColour                  // → "FF8800" 等
zoo_DialogColour ( True )         // フルピッカー
```

> **注意**: 戻り値形式（`"RRGGBB"` か `"R,G,B"` 十進か `#` 付きか）は本家との完全互換確認が未了です。現状は `"RRGGBB"` 6 桁大文字 16 進を返します。
>
> **macOS の制限**: macOS には Windows の `ChooseColor` 相当の **モーダル**カラーピッカーが無いため未対応 = 常に `Err_2`。

---

## `zoo_DialogFile`

```
zoo_DialogFile ( {bOpen ; sTitle ; sDefault } )
```

ファイル選択ダイアログを開きます。全引数オプション。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `bOpen` | Boolean | いいえ | False | True で「開く」、False で「保存」 |
| `sTitle` | String | いいえ | | ダイアログタイトル |
| `sDefault` | String | いいえ | | 既定ファイル名 |

**戻り値**: 選択パス、またはキャンセルで `zoo_DialogFile|Err_2`。

```
zoo_DialogFile ( True ; "ファイルを選択" ; "" )
zoo_DialogFile ( False ; "保存先" ; "output.csv" )
```

---

## `zoo_DialogFolder`

```
zoo_DialogFolder ( { sTitle ; bNewFolder } )
```

フォルダ選択ダイアログ。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sTitle` | String | いいえ | | タイトル |
| `bNewFolder` | Boolean | いいえ | False | True で「新規フォルダ」ボタン表示 |

**戻り値**: 選択パス、またはエラー。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_2` | キャンセル |
| `Err_3` | 選択パスの取得失敗 |

```
zoo_DialogFolder ( "保存先を選択" ; True )
```

---

# Printer 関数（2）

> FileMaker の印刷には影響しません。**システム既定**の取得/設定です。Pro 以外（Server/WebDirect）では実質意味なし。

## `zoo_PrinterDefault`

```
zoo_PrinterDefault ( { sPrinter } )
```

引数なし/空 → 既定プリンタ名を取得。非空 → 既定として設定し `1` を返す。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| `sPrinter` | String | いいえ | 設定する場合のプリンタ名 |

**戻り値**: 取得時はプリンタ名（テキスト）、設定時は `1`、失敗で `zoo_PrinterDefault|Err_N`。

**エラー**: `Err_2`（取得失敗）、`Err_3`（設定失敗 / 指定プリンタなし）。

**実装**:
- Windows: `GetDefaultPrinterW` / `SetDefaultPrinterW`
- POSIX: CUPS の `cupsGetNamedDest` / `cupsSetDests`

```
zoo_PrinterDefault                            // 取得
zoo_PrinterDefault ( "Canon PIXUS" )          // 設定 → 1
```

---

## `zoo_PrinterList`

```
zoo_PrinterList ( { sSeparator } )
```

インストール済みプリンタ名の一覧。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sSeparator` | String | いいえ | `|` | 区切り |

**戻り値**: 区切り連結のテキスト、または `zoo_PrinterList|Err_N`。

**エラー**: `Err_2`（取得失敗）、`Err_3`（1 台も無し）。

```
zoo_PrinterList                       // → "Canon PIXUS|Brother MFC|Microsoft Print to PDF"
zoo_PrinterList ( "¶" )               // 改行区切り
```

---

# Process 関数（4）

## `zoo_ProcessCount`

```
zoo_ProcessCount ( sProcess )
```

実行中プロセス数を数えます。`sProcess` が空なら全体、非空なら一致する数。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| `sProcess` | String | はい | プロセス名（空可） |

**戻り値**: 数値（件数）。

**名前マッチの規則**:
- 大文字小文字を無視。
- Windows の `.exe` 拡張子の有無を吸収（`"notepad"` と `"notepad.exe"` は一致）。

```
zoo_ProcessCount ( "" )                // 全プロセス数
zoo_ProcessCount ( "FileMaker Pro" )   // 一致数
```

---

## `zoo_ProcessRunning`

```
zoo_ProcessRunning ( sProcess )
```

一致するプロセスが実行中かを `1` / `0` で返します。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| `sProcess` | String | はい | プロセス名 |

**戻り値**: 実行中 `1` / なし `0` / エラー `zoo_ProcessRunning|Err_N`。

**エラー**: `Err_2`（空入力 = "Invalid input process."）。

```
zoo_ProcessRunning ( "FileMaker Pro" )
```

---

## `zoo_ProcessList`

```
zoo_ProcessList ( { sSeparator } )
```

実行中プロセス名の一覧。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sSeparator` | String | いいえ | `|` | 区切り |

**戻り値**: 区切り連結のテキスト。

> **注意**: 既定の separator は本家との完全互換確認が未了です。ZooPlug は `|`（パイプ）を採用しています。

```
zoo_ProcessList
zoo_ProcessList ( "¶" )
```

---

## `zoo_ProcessKill`

```
zoo_ProcessKill ( sProcess )
```

一致するプロセスを**すべて**強制終了します。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| `sProcess` | String | はい | プロセス名 |

**戻り値**: 成功 `1` / 失敗 `zoo_ProcessKill|Err_N`。

**エラー**: `Err_2`（空 / 一致なし / 終了失敗 = "Error terminating process."）。

**実装**: Win = `OpenProcess + TerminateProcess` / POSIX = `kill(SIGKILL)`。

> **注意**: 自プロセス（FileMaker Pro 自身）や重要なシステムプロセスを指定しないこと。

```
zoo_ProcessKill ( "notepad" )
```

---

# 進捗ダイアログ（1）

## `zoo_ProgressOptions`

```
zoo_ProgressOptions ( sTitle {; sCaption ; bCancel } )
```

Download / FTP（`bProgress=True`）のときに表示する進捗ダイアログのオプションを設定します。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sTitle` | String | はい | | ダイアログのタイトル |
| `sCaption` | String | いいえ | | 説明文 |
| `bCancel` | Boolean | いいえ | False | キャンセルボタン表示 |

**戻り値**: 成功で `1`、または `zoo_ProgressOptions|Err_1`。

> **現状の制限**: ZooPlug は状態を保存して `1` を返すだけで、**進捗ダイアログ UI そのものはまだ未配線**（Tier C の後続実装予定）。Download/FTP の `bProgress=True` も現状受けるだけで無視されます。

```
zoo_ProgressOptions ( "ダウンロード中" ; "お待ちください" ; True )
```

---

# Hotkey 関数（3）

> **常駐**してグローバルホットキーを受け取り、押下で **FileMaker スクリプトを起動**します。実装は `kFMXT_Idle` でキューを刈り取って `FMX_StartScript` を呼ぶ標準パターンです。
>
> **FM19.2+ の前提**: 検証ファイルのアクセス権セットで **`fmplugin` 拡張アクセス権を有効化**してください。無効だとプラグインからのスクリプト起動が **エラー 825** で拒否されます。

## `zoo_HotkeyAdd`

```
zoo_HotkeyAdd ( sHotkey ; sFile ; sScript {; sParam ; bAlt ; bControl ; bShift ; bGlobal } )
```

ホットキーを登録し、押下で `sFile` の `sScript` を起動します。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sHotkey` | String | はい | | キー名（後述） |
| `sFile` | String | はい | | スクリプトが定義されたファイル名（`Get(FileName)` を渡すのが堅い） |
| `sScript` | String | はい | | 起動するスクリプト名 |
| `sParam` | String | いいえ | | スクリプト引数（`Get(ScriptParameter)` で受け取る） |
| `bAlt` | Boolean | いいえ | False | Alt 修飾 |
| `bControl` | Boolean | いいえ | False | Ctrl 修飾 |
| `bShift` | Boolean | いいえ | False | Shift 修飾 |
| `bGlobal` | Boolean | いいえ | False | True で FileMaker が前面で無くても発火 |

**戻り値**: 成功 `1` / 失敗 `zoo_HotkeyAdd|Err_N`。

**対応キー**: `A`〜`Z`、`0`〜`9`、`F1`〜`F12`、`SPACE`、`ESC`（`ESCAPE` も可）、`END`、`HOME`、`UP`、`DOWN`、`LEFT`、`RIGHT`（大小無視・前後空白可）。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | 不明なキー |
| `Err_3` | ホットキーウィンドウ作成失敗 |
| `Err_4` | 既登録（同名 canonical キー） |
| `Err_5` | 登録失敗（OS 側拒否） |

> **macOS の制限**: `bGlobal` は無視され**常にグローバル**（Carbon `RegisterEventHotKey` はシステム全体で発火）。前面限定は今のところ未対応。

```
zoo_HotkeyAdd ( "F8" ; Get ( FileName ) ; "OnHotkey" ; "param-test" ; False ; True ; False ; True )
                                          // Ctrl+F8（グローバル）で OnHotkey ( "param-test" ) を起動
```

---

## `zoo_HotkeyList`

```
zoo_HotkeyList ( { sSeparator } )
```

登録済みホットキーの一覧（`"CTRL+SHIFT+A"` のような表示形式）。

| 引数 | 型 | 必須 | 既定 | 説明 |
|---|---|---|---|---|
| `sSeparator` | String | いいえ | `|` | 区切り |

**戻り値**: 連結テキスト、または `zoo_HotkeyList|Err_N`。

**エラー**: `Err_2`（未登録 = 0 件）、`Err_3`（取得失敗）。

```
zoo_HotkeyList                       // → "CTRL+F8|ALT+SHIFT+A"
```

---

## `zoo_HotkeyRemove`

```
zoo_HotkeyRemove ( sHotkey )
```

ホットキーを解除します。**キー名のみ**で識別（修飾子は不要 = canonical キー名で管理されているため）。

| 引数 | 型 | 必須 | 説明 |
|---|---|---|---|
| `sHotkey` | String | はい | 解除するキー名 |

**戻り値**: 成功 `1` / 失敗 `zoo_HotkeyRemove|Err_N`。

**エラー**:
| コード | 説明 |
|---|---|
| `Err_1` | 引数の数が不正 |
| `Err_2` | 未登録（0 件） |
| `Err_3` | 不明なキー |
| `Err_4` | 該当ホットキー無し |
| `Err_5` | 解除失敗 |

```
zoo_HotkeyRemove ( "F8" )
```

---

## 関連ドキュメント

- 設計思想 / Tier 分け / 工数 → [`zoo-plug-implementation-spec.ja.md`](zoo-plug-implementation-spec.ja.md)
- `zoo_powershell` の設計詳細（テンポラリ方式・AppLocker / WDAC 実測） → [`zoo-powershell-design.ja.md`](zoo-powershell-design.ja.md)
