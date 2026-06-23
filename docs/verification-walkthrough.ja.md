# ZooPlug 検証ウォークスルー — FileMaker Pro 19 / MooPlug 互換 38 関数の動作確認

> 出荷される ZooPlug ビルドは **39 関数**（MooPlug 互換 38 + ZooPlug 独自の
> `zoo_PowerShell`）を登録します。本ウォークスルーは MooPlug 互換 38 関数の
> 確認に絞っています。`zoo_PowerShell` は別系統の検証マトリクスがあり、
> [`docs/zoo-powershell-design.ja.md`](zoo-powershell-design.ja.md) の §17 / §18
> を参照してください。

**他の言語で読む:** [English](verification-walkthrough.md)

**実機検証**を、コピペで進められるようにまとめたもの。
プラグインは macOS は `dist/ZooPlug.fmplugin`（ad-hoc 署名済み）、Windows は `build/ZooPlug.fmx64`（`scripts\build-windows-plugin.bat` で生成）。

## 0. 前提

- **macOS**: 配布の `.fmplugin` を `~/Library/Application Support/FileMaker/Extensions/` に置く。Gatekeeper の隔離を外す:
  ```
  xattr -dr com.apple.quarantine ZooPlug.fmplugin
  ```
- **Windows**: `ZooPlug.fmx64` を `C:\Program Files\FileMaker\FileMaker Pro 19\Extensions\` に置く（FileMaker は起動前に止めておく / DLL ロック対策）。
- FileMaker Pro 19 を起動 → 環境設定 > プラグイン で **ZooPlug が有効** になっていることを確認。
- **重要（FM19.2+）**: 検証ファイルの **アクセス権セットで `fmplugin` 拡張アクセス権を有効化**する。無効だと Hotkey からのスクリプト起動が **エラー 825** で拒否される。

## 1. データビューアで関数を 1 つずつ評価する（最短ルート）

「ツール > データビューア（高度なツール有効化が必要）」の「監視」タブに以下を貼って評価する。
全関数は登録順に並ぶ — 1 つでも `zoo_XXX|Err_N` が返ったら [Source/MooError.cpp](../Source/MooError.cpp) と照合する。

### 1.1 メタ・エラー解説

```
zoo_Version
// 期待: "ZooPlug 1.1.1"（ZooPlug は自身のバージョン文字列を返す）

zoo_ErrorDetail("zoo_FileCopy|Err_3")
// 期待: "Source file does not exist."

zoo_ErrorDetail("zoo_DownloadFile|Err_4")
// 期待: "File download cancelled by user."  ※実機の戻り文言に合わせる
```

### 1.2 File（7）

```
zoo_FileExists(Get(TemporaryPath) & "no_such.txt")     // 期待: 0
zoo_FileWrite(Get(TemporaryPath) & "zoo_test.txt"; "こんにちは 表予能")  // 期待: 1
zoo_FileExists(Get(TemporaryPath) & "zoo_test.txt")    // 期待: 1
zoo_FileRead(Get(TemporaryPath) & "zoo_test.txt")      // 期待: "こんにちは 表予能"
zoo_FileInfo(Get(TemporaryPath) & "zoo_test.txt"; "size")   // 期待: human 文字列（例 "21 bytes"）
zoo_FileInfo(Get(TemporaryPath) & "zoo_test.txt"; "size"; "bytes")  // 期待: 21
zoo_FileCopy(Get(TemporaryPath) & "zoo_test.txt"; Get(TemporaryPath) & "zoo_test2.txt"; True)  // 期待: 1
zoo_FileMove(Get(TemporaryPath) & "zoo_test2.txt"; Get(TemporaryPath) & "zoo_moved.txt")  // 期待: 1
zoo_FileDelete(Get(TemporaryPath) & "zoo_moved.txt")   // 期待: 1
```

### 1.3 Folder（6）

```
zoo_FolderCreate(Get(TemporaryPath) & "zoo_dir/sub")   // 期待: 1（中間も作る）
zoo_FolderExists(Get(TemporaryPath) & "zoo_dir/sub")   // 期待: 1
zoo_FileWrite(Get(TemporaryPath) & "zoo_dir/a.txt"; "a")
zoo_FileWrite(Get(TemporaryPath) & "zoo_dir/b.csv"; "b")
zoo_FolderList(Get(TemporaryPath) & "zoo_dir")           // 期待: "a.txt|b.csv"（順不同可）
zoo_FolderList(Get(TemporaryPath) & "zoo_dir"; "*.csv")  // 期待: "b.csv"
zoo_FolderCopy(Get(TemporaryPath) & "zoo_dir"; Get(TemporaryPath) & "zoo_dir2")
zoo_FolderMove(Get(TemporaryPath) & "zoo_dir2"; Get(TemporaryPath) & "zoo_dir3")
zoo_FolderDelete(Get(TemporaryPath) & "zoo_dir")        // 期待: 1
zoo_FolderDelete(Get(TemporaryPath) & "zoo_dir3")       // 期待: 1
```

### 1.4 Hash（1）— NIST/RFC ベクタで一致確認済み

```
zoo_Hash("md5"; "")        // 期待: d41d8cd98f00b204e9800998ecf8427e
zoo_Hash("sha1"; "abc")    // 期待: a9993e364706816aba3e25717850c26c9cd0d89d
zoo_Hash("sha256"; "abc")  // 期待: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
```

### 1.5 Zip（3）

```
zoo_FileWrite(Get(TemporaryPath) & "z.txt"; "zoo zip")
zoo_ZipCompress(Get(TemporaryPath) & "z.txt")          // 期待: パス（例 "...z.zip"）
zoo_ZipList(Get(TemporaryPath) & "z.zip")              // 期待: "z.txt"
zoo_ZipExtract(Get(TemporaryPath) & "z.zip"; True)     // 期待: テンポラリ内の展開先パス
```

### 1.6 Net = Download + FTP（5）

**HTTP**（Mac/Win とも何かの公開 URL でテスト。匿名アクセス可能なものを）:
```
zoo_DownloadText("https://example.com/")           // 期待: HTML テキスト
zoo_DownloadFile("https://example.com/")           // 期待: ローカルパス
```

**FTP**（要 vsftpd 等。詳細は zoo-plug-implementation-spec.ja.md §8。なければスキップ）:
```
zoo_FTPUpload("ftp.example.com"; "user"; "pw"; Get(TemporaryPath) & "z.txt"; "/upload/z.txt")
zoo_FTPDownload("ftp.example.com"; "user"; "pw"; "/upload/z.txt"; Get(TemporaryPath) & "ftp_dl.txt")
zoo_FTPDelete("ftp.example.com"; "user"; "pw"; "/upload/z.txt")
```

### 1.7 Dialog（3）— GUI モーダル

```
zoo_DialogColour                  // 色を選んで OK → 期待: "RRGGBB" hex（例 "FF0000"）。キャンセル → Err_2
zoo_DialogFile(True; "選択"; "")  // 開く / 期待: パス。キャンセル → Err_2
zoo_DialogFolder("選択"; True)    // 新規フォルダボタン付き / 期待: パス
```

### 1.8 Printer（2）

```
zoo_PrinterList                    // 期待: "プリンタA|プリンタB|..."
zoo_PrinterDefault                 // 期待: 既定プリンタ名
zoo_PrinterDefault("プリンタA")    // 期待: 1。Set は破壊的なので戻すまで観察に注意
```

### 1.9 Process（4）

```
zoo_ProcessCount("")                       // 期待: 全プロセス数
zoo_ProcessCount("FileMaker Pro")          // 期待: >= 1（Win は .exe 吸収）
zoo_ProcessRunning("FileMaker Pro")        // 期待: 1
zoo_ProcessList                            // 期待: "..|..|.." 形式
// zoo_ProcessKill は破壊的。試すなら使い捨ての notepad などで
```

### 1.10 ProgressOptions（1）

```
zoo_ProgressOptions("DL 中"; "お待ちください"; True)    // 期待: 1
// （現状は状態保持のみ。Download/FTP の進捗 UI は未配線）
```

### 1.11 Hotkey（3）— Idle/StartScript 一周の核心テスト

**`fmplugin` 拡張アクセス権が有効** であることが必須（無いと Err 825）。

1. 検証ファイルに通知用スクリプト `OnHotkey` を作る:
   - `カスタムダイアログを表示 [ "Hotkey fired: " & Get(ScriptParameter) ]`
2. データビューアで:
   ```
   zoo_HotkeyAdd("F8"; Get(FileName); "OnHotkey"; "param-test"; False; True; False; True)
   // 期待: 1
   zoo_HotkeyList                                  // 期待: "CTRL+F8"
   ```
3. **キーボードで Ctrl+F8 を押す** → 数秒以内に `OnHotkey` が起動し、ダイアログに `param-test` が表示されれば成功。
   - 即時実行ではなくキュー投入（`FMX_StartScript` はスクリプトをその場で実行せずキューに積む）。実行中スクリプトが終わってから走ることがある。
4. 解除:
   ```
   zoo_HotkeyRemove("F8")                          // 期待: 1
   zoo_HotkeyList                                  // 期待: zoo_HotkeyList|Err_2
   ```

## 2. 自己テストスクリプト — 全関数を一気に走らせて結果を 1 ファイルに書く

`scripts\ZooPlug_SelfTest` 相当の FileMaker スクリプトを 1 本作って **データビューア依存ゼロ** で回す方法。

```
# 変数定義
変数を設定 [ $tmp ; Get(TemporaryPath) ]
変数を設定 [ $out ; $tmp & "zoo_selftest.txt" ]

# 各関数を呼び、結果を改行区切りで結合
変数を設定 [ $log ;
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

# ファイルに保存
変数を設定 [ $rc ; zoo_FileWrite($out ; $log) ]
カスタムダイアログを表示 [ "Wrote " & $out & " (rc=" & $rc & ")" ]
```

保存後、`zoo_FileRead($tmp & "zoo_selftest.txt")` で読み戻して目視。
あるいは実機から結果ファイルを取得して確認する。

## 3. 0.4.9 互換照合（要 Windows 実機 + FM Pro 11 + 0.4.9）

実 0.4.9 を Windows 実機の Pro 11 Extensions に配置しておく。
下記の **TODO-compat** 項目を、同じ式を Pro 11（0.4.9）と Pro 19（ZooPlug）で評価して比較する:

- `zoo_Version`（戻り文字列）
- `zoo_FileInfo` size の sOptions 省略時（human / bytes）
- `zoo_FileExists` の戻り型（GetAsText で数値か文字列か確認）
- `zoo_DialogColour` の戻り値形式（RRGGBB hex か "R,G,B" か）
- `zoo_PrinterDefault` 引数に空文字を渡したとき
- `zoo_ProcessList` 既定 separator

食い違いがあれば TODO-compat を潰し、必要なら実装側の挙動を 0.4.9 側に合わせる。

## 4. 既知の制限（macOS）

- **`zoo_DialogColour`**: macOS には Windows の `ChooseColor` 相当の **モーダル**カラーピッカーが無いため未対応 → `Err_2` を返す。Windows が主対象。
- **`zoo_HotkeyAdd` の `bGlobal`**: macOS の `RegisterEventHotKey` は常にシステム全体で発火するため `bGlobal` を無視する（前面限定は `.mm` 化して NSWorkspace 利用が必要）。

## 5. 失敗時の調べ方

1. `zoo_XXX|Err_N` が返ったら `zoo_ErrorDetail` で文言を取る。
2. それでも腑に落ちなければ [Source/MooError.cpp](../Source/MooError.cpp) の該当エントリと [Source/](../Source/) の純粋ロジック（`FileOps.cpp` / `NetOps.cpp` 等）の `return N;` を辿る。
3. プラットフォーム固有の振る舞いは `#ifdef _WIN32` / `__APPLE__` 分岐を確認。
