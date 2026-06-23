# ZooPlug

**他の言語で読む:** [English](README.md)

Windows / macOS / Linux 用の **独立した** FileMaker プラグイン。ファイル / フォルダ操作、
シェル実行(cmd / PowerShell)、ダウンロード、FTP、Zip、ハッシュ、ダイアログ、プリンタ、
プロセス、ホットキーなど **39 関数** を `zoo_*` 名前空間で提供します。

```
zoo_FileWrite ( Get(TemporaryPath) & "log.txt" ; "hello" )    // → 1
zoo_Hash ( "sha256" ; "abc" )                                  // → ba7816bf...
zoo_DownloadText ( "https://example.com/" )                    // → HTML 本文
zoo_Shell ( "ipconfig" )                                       // → ネットワーク情報
zoo_powershell ( "Write-Output 'Hello 表予能'" )              // → Hello 表予能（UTF-8 安全）
```

## なぜ作ったか — 本家 MooPlug の状況

ZooPlug は、Adam Dempsey 氏の Windows 用 FileMaker プラグイン **MooPlug**(2007–2023)
を起点として作られたものです。MooPlug は File / Folder / Shell / Dialog / FTP / Hash /
Zip / Hotkey / Process / Printer など **36 関数**(+ 未公開の `Moo_Shell` /
`Moo_FTPDelete` を含めると 38 関数)を FileMaker の計算式から直接呼べる、非常に
実用的なツールで、長年にわたって多くの FileMaker ソリューションに組み込まれて
きました。

しかし現在:

- 配布元の **mooplug.com は閉鎖**されており、公式ドキュメントは
  [Wayback Machine](https://web.archive.org/web/*/mooplug.com) でしか読めません。
- **64 bit 版がリリースされないまま開発が止まり**、FileMaker Pro 19+(64 bit のみ)
  では本家 MooPlug を使えなくなりました。
- バイナリ配布のため利用者側での修正もできず、OS や FileMaker 更新のたびに
  「入手も修正もできないバイナリ」への依存が重くなっています。

ZooPlug は、この状況を埋めるために MooPlug の **挙動と関数群を参考にしつつ、
完全に独立した名前空間 `zoo_*` で C++ から再実装した** プラグインです。
本家のソースコードは持っていません(Adam 氏のソースは公開されたことがありません)。

## 姉妹プロダクト cowPlug — 既存 MooPlug ソリューション向け

「既存の FileMaker ファイル中の `Moo_FileWrite( ... )` 等を **書き換えず**
プラグインの差し替えだけで動かしたい」という用途には、姉妹プロダクトの
**cowPlug** を別途用意しています。

| 製品 | 関数プレフィックス | 含む関数 | 用途 |
|---|---|---|---|
| **ZooPlug** | `zoo_*` | 38 関数 + `zoo_powershell` 拡張 = **39 関数** | 独自の名前空間。新規ソリューション向け。本家と衝突しない |
| **cowPlug** | `Moo_*`(本家厳密準拠) | 38 関数のみ(`zoo_powershell` 等の拡張は **含まない**) | 既存 MooPlug ソリューションをプラグイン差し替えだけで動かしたい場合 |

引数構成・戻り値・エラーコードは ZooPlug と完全に同一で、違いは関数プレフィックスと、
cowPlug に独自拡張(`zoo_powershell` 等)が含まれないことだけです。**cowPlug は
ビルド済みバイナリのみで配布**します(Releases から)。ZooPlug がオープンソース版です。

> ⚠️ **本家 MooPlug の Adam Dempsey 氏が公開を再開された場合、`Moo_*` 名前空間と
> 衝突するため cowPlug は配布を取り下げます。** ZooPlug 側(`zoo_*`)は独立した
> 名前空間なので影響を受けません。**新規プロジェクトで使うなら ZooPlug を推奨します。**
> cowPlug はあくまで本家不在期間の一時的な救済目的です。

## API 設計の起源

> **API 設計(引数構成、エラーコード体系、戻り値の方針、関数機能の区分)は、
> Adam Dempsey 氏の MooPlug の挙動観察に由来します。** ZooPlug はその設計を
> 参考にした **独立実装** であり、設計の独自性を主張するものではありません。
> Adam 氏の長年の開発・公開に深く感謝します。
>
> 本記述に Adam 氏ご本人から訂正・要望があれば、いつでも反映します(GitHub Issue
> または連絡先まで)。**配布の中止が必要であればそれにも従います。**

ZooPlug / cowPlug は本家のソースコードを持っていません(Adam 氏のソースは公開された
ことがありません)。本家の公開マニュアル(Wayback Machine の保存版)と、FileMaker
Pro 11 上で MooPlug 0.4.9 を実際に動作させたときの **挙動観察** — 計算式エディタに
表示される関数一覧・登録プロトタイプ、各関数を呼んだときの戻り値、`Moo_ErrorDetail`
が返すエラー文字列など、いずれも FileMaker のユーザーであれば誰でも確認できる情報
— を一次資料として、**C++ で独自に実装し直した**ものです。実装コードに本家のコード
は含みません。

## 何ができるか (ZooPlug)

| 区分 | 関数数 | 例 |
|---|---|---|
| メタ | 2 | `zoo_Version` / `zoo_ErrorDetail` |
| シェル実行 | 1 | `zoo_Shell`(cmd) |
| **PowerShell 実行(独自拡張)** | **1** | **`zoo_powershell`**(PowerShell 5.1 / 7・UTF-8 安全・CLM/WDAC 配下でも動く) |
| ファイル | 7 | `zoo_FileExists` / `zoo_FileCopy` / `zoo_FileDelete` / `zoo_FileMove` / `zoo_FileRead` / `zoo_FileWrite` / `zoo_FileInfo` |
| フォルダ | 6 | `zoo_FolderExists` / `zoo_FolderCopy` / `zoo_FolderCreate` / `zoo_FolderDelete` / `zoo_FolderMove` / `zoo_FolderList` |
| ハッシュ | 1 | `zoo_Hash`(MD5 / SHA-1 / SHA-256 / SHA-512、ファイル可) |
| Zip | 3 | `zoo_ZipCompress` / `zoo_ZipExtract` / `zoo_ZipList` |
| ネット | 5 | `zoo_DownloadText` / `zoo_DownloadFile` / `zoo_FTPDownload` / `zoo_FTPUpload` / `zoo_FTPDelete` |
| ダイアログ | 3 | `zoo_DialogColour` / `zoo_DialogFile` / `zoo_DialogFolder` |
| プリンタ | 2 | `zoo_PrinterDefault` / `zoo_PrinterList` |
| プロセス | 4 | `zoo_ProcessCount` / `zoo_ProcessKill` / `zoo_ProcessList` / `zoo_ProcessRunning` |
| 進捗 UI | 1 | `zoo_ProgressOptions` |
| ホットキー | 3 | `zoo_HotkeyAdd` / `zoo_HotkeyList` / `zoo_HotkeyRemove`(押下で FileMaker スクリプト起動) |

**全 39 関数の詳細仕様は → [docs/function-reference.ja.md](docs/function-reference.ja.md)**。

cowPlug の関数表は、上記から **`zoo_*` を `Moo_*` に読み替えた上で `zoo_powershell`
を除外**したもの = MooPlug 0.4.9 と同一の 38 関数です。

> **`zoo_powershell` について:** ZooPlug 独自の関数で、cowPlug には含まれません。
> `zoo_Shell` の cmd 経由ではうまく扱えないケース — 日本語の UTF-8 往復、
> AppLocker / WDAC 配下の Constrained Language Mode、複数行スクリプト・引用符・`$`
> を含むスクリプトなど — をカバーするために用意しました。設計の詳細は
> [`docs/zoo-powershell-design.ja.md`](docs/zoo-powershell-design.ja.md)。

## 対応プラットフォーム

本家 MooPlug は **Windows(32 bit のみ)専用**でしたが、ZooPlug / cowPlug は次の
3 環境に対応します。

| プラットフォーム | バイナリ | 動作確認 |
|---|---|---|
| **Windows 64 bit** | `ZooPlug.fmx64` / `cowPlug.fmx64` | FileMaker Pro 19 |
| **macOS**(Intel + Apple Silicon、universal) | `ZooPlug.fmplugin` / `cowPlug.fmplugin` | macOS 15 (Sequoia) |
| **Linux**(FileMaker Server) | `ZooPlug.fmx` / `cowPlug.fmx` | Ubuntu 22.04 / 24.04 |

> iOS は OS の制約(サンドボックス内でプロセス起動不可)で非対応です。
> macOS では `zoo_DialogColour`(モーダルカラーピッカーが OS に無い)と
> `zoo_HotkeyAdd` の `bGlobal` フラグ(Carbon の制限)に固有の制約があります
> (詳細は [function-reference.ja.md](docs/function-reference.ja.md))。

## インストール

1. FileMaker Pro を終了します。
2. プラグインファイルを Extensions フォルダーへコピー:
   - Windows: `ZooPlug.fmx64` を `C:\Program Files\FileMaker\FileMaker Pro 19\Extensions\` へ
   - macOS: `ZooPlug.fmplugin` を `~/Library/Application Support/FileMaker/Extensions/` へ
3. FileMaker Pro を起動し、**環境設定 → プラグイン** で **ZooPlug** を有効化します。
4. 計算式から `zoo_*` 各関数が使えます。

> cowPlug を使う場合は上記の `ZooPlug` を `cowPlug` に読み替えてください。
> **ZooPlug と cowPlug は関数名前空間が分かれているので同時に有効化できます。**
> ただし cowPlug を本家 MooPlug と同時に有効化すると `Moo_*` 名前で衝突します
> (どちらか一方だけ有効にしてください)。

> **macOS Gatekeeper:** 配布バイナリは ad-hoc 署名済みです。最初の起動で
> ブロックされた場合は `xattr -dr com.apple.quarantine ZooPlug.fmplugin` で
> 隔離属性を外してください。
>
> **FileMaker 19.2+ でホットキーを使う場合:** 検証ファイルのアクセス権セットで
> `fmplugin` 拡張アクセス権を有効化してください(無いとエラー 825 でスクリプト
> 起動が拒否されます)。

## ソースからビルドする

テストだけなら FileMaker SDK は不要です。

### 単体テスト(SDK 不要・どこでも)

```sh
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`shellexec` / `powershellexec` / `mooerror` / `fileops` / `hash` / `zipops` /
`netops` / `processops` / `printerops` / `progressops` / `hotkeyops` の 11 本が
走ります。

### Windows → `ZooPlug.fmx64`

```bat
scripts\build-windows-plugin.bat            REM x64 (FileMaker Pro 19)
scripts\build-windows-plugin.bat x86        REM 32 bit (FileMaker Pro 11)
```

Visual Studio 2019 / 2022 / 2026 を自動検出し、同梱の `FMWrapper.lib` をリンク
して `build\ZooPlug.fmx64` を生成します。Windows 標準の `wininet` / `winspool` /
`comdlg32` / `user32` 等を使うため、追加 SDK の取得は不要です(32 bit / FM11 版は
別途 FM11 SDK が必要 — ビルドスクリプト参照)。

### macOS → `ZooPlug.fmplugin`(ad-hoc 署名付き・universal)

```sh
bash scripts/build-and-sign-mac.sh
```

CMake ビルド(arm64 + x86_64) → ad-hoc 署名(`codesign --sign - --deep --force --timestamp=none`)
→ `dist/ZooPlug.fmplugin` を生成します。`INSTALL=1` を付けると
`~/Library/Application Support/FileMaker/Extensions/` に直接配置します。

### Linux → `ZooPlug.fmx`(FileMaker Server)

Linux 用 `libFMWrapper.so` は同梱していません。
[Claris SDK](https://www.claris.com/resources/downloads/) から取得して
`Libraries/linux/<U22|U24>/<x64|arm64>/` に置くか、`-DFMX_LIB=/path/to/libFMWrapper.so`
で渡してください:

```sh
cmake -B build -DBUILD_PLUGIN=ON \
      -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS="-stdlib=libc++"
cmake --build build
```

Ubuntu 22.04 / clang 14 / libc++ で検証済み。

## 検証と互換性

全 39 関数を 1 つずつ Data Viewer で評価する手順、および全関数を一括で走らせる
自己テストスクリプトを用意しています:

- [docs/function-reference.ja.md](docs/function-reference.ja.md) — ZooPlug 全 39 関数(`zoo_*`)の利用者向けリファレンス

cowPlug は上記を `zoo_` → `Moo_` に読み替え、`zoo_powershell` を除いたもの = MooPlug 0.4.9 と同一の 38 関数です。

本家ドキュメントと、FileMaker 上で実際に動作させたときの挙動が食い違う箇所は、
**実機の挙動を採用**しています。代表的なもの:

- エラーコード形式は `Err_N`(アンダースコア付き)が正しい — ドキュメントの `ErrN`
  は誤記(実機のエラー戻り値で確認できる)。
- `zoo_FileWrite`(cowPlug 側では `Moo_FileWrite`)は **3 引数**(`bAppend` のみ)。
  ドキュメントの 4 引数仕様(`bAppend ; bOverwrite`)は実機の計算式エディタに
  表示されません。
- `zoo_FTPDelete`(cowPlug 側では `Moo_FTPDelete`)は本家ドキュメントに記述が
  無いが、0.4.9 では実機の外部関数一覧に登録されています。
- `Moo_PrinterSet` は本家ドキュメントには記述があるが実機の外部関数一覧には出ない
  — ZooPlug / cowPlug いずれでも提供しません。

細かい戻り値仕様 (`zoo_Version` の戻り文字列、`zoo_DialogColour` の戻り値形式、
`zoo_ProcessList` の既定 separator など) は関数リファレンス中で「暫定」と注記
されており、今後の互換調査結果次第で変わる可能性があります。

## 連絡・貢献

「この関数の挙動が本家と違う」「この関数の使い方が分からない」「こういう挙動を
追加してほしい」など、Issue を歓迎します。

- リポジトリ: <https://github.com/veltrea/ZooPlug>

## クレジットとライセンス

- **MooPlug — Adam Dempsey 氏。** ZooPlug / cowPlug の起源となった FileMaker
  プラグイン。**API 設計(引数構成、エラーコード体系、戻り値方針、関数機能の区分)
  は Adam 氏の MooPlug の挙動観察に由来します。** ZooPlug は名前空間を `zoo_*` に
  変えた独立実装、cowPlug は `Moo_*` 名前空間で挙動を再現した互換実装です。
  長年にわたる開発・公開に感謝します。
- **[SimplePlugin](http://banks.id.au/filemaker/plugins/simpleplugin/) — Mark Banks 氏。**
  ZooPlug / cowPlug のプラグイン土台(エントリポイント・関数登録の枠組み)。
  BSD 3-Clause で利用。
- **[miniz](https://github.com/richgel999/miniz) 3.0.2** — Zip 関数の実装に同梱(MIT)。
- **FMWrapper(Claris FileMaker Plug-In API)** — `Headers/` / `Libraries/` に同梱。
  Claris International Inc. のライセンスに従い再配布。
- ZooPlug / cowPlug 本体は **BSD 3-Clause License** で公開しています。

詳細は [License.txt](License.txt) を参照。
