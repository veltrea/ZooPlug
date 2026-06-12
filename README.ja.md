# ZooPlug

**他の言語で読む:** [English](README.md)

MooPlug の `moo_shell()` 関数を再現する小さな FileMaker プラグインです。FileMaker
の計算式からワンライナーのシェルコマンドを実行し、その出力をテキストとして受け取れます。

```
moo_shell ( "echo %USERNAME%" )      // → 現在の Windows ユーザー名
moo_shell ( "echo %COMPUTERNAME%" )  // → コンピューター名
moo_shell ( "set" )                  // → 環境変数の一覧（1 行 1 件）
moo_shell ( "net config workstation" )
```

プラグインの土台には Mark Banks の
[SimplePlugin](http://banks.id.au/filemaker/plugins/simpleplugin/) テンプレートを使っています。

## `moo_shell` 関数

```
moo_shell ( command )
```

- **command** — シェルで実行するワンライナーのコマンド。
- **戻り値** — コマンドの標準出力と標準エラーを合わせたものをテキストで返す。

動作:

- **Windows:** `cmd.exe /S /C "<command>"` 経由で実行する。コンソール窓は出ない。
- **macOS:** `/bin/sh -c "<command>"` 経由で実行する（同じ計算式と単体テストが Mac
  でも動くように用意したもの。本家 MooPlug は Windows 専用）。
- 改行は FileMaker の内部改行である CR (`\r`) に正規化し、末尾の改行は取り除く。
  そのため `moo_shell ( "echo %USERNAME%" )` は末尾に空行が付かず、名前だけを返す。
- Windows ではコンソールの OEM コードページ（日本語環境なら CP932 / Shift_JIS）として
  出力を解釈し、Unicode にして FileMaker へ返す。

> **セキュリティ:** `moo_shell` は渡された文字列をそのまま実行する。信頼できない入力
> （ユーザーが入力したフィールド値、Web から取得したデータ等）からコマンドを組み立てない
> こと。シェルインジェクションの危険がある。自分で管理しているコマンドだけを渡すこと。

## リポジトリ構成

```
Source/ShellExec.h      シェル実行の純粋ロジック（FileMaker 非依存）
Source/ShellExec.cpp    Windows (CreateProcess) と POSIX (popen) の実装
Source/ZooPlug.cpp      FileMaker グルー: moo_shell を登録し ShellExec を呼ぶ
tests/test_shellexec.cpp 純粋ロジックのスタンドアロン単体テスト
Headers/FMWrapper/      Claris FileMaker プラグイン API ヘッダー v77（License.txt 参照）
Libraries/              同梱の FMWrapper リンクライブラリ: Windows / macOS（Linux はSDKから）
CMakeLists.txt          テストはどこでも。プラグインは Win/Mac/Linux でビルド
Info.plist              macOS バンドルのメタデータ
```

シェルロジックは FileMaker API からあえて切り離してあり、FileMaker や SDK が**無くても**
ビルド・テストできる。

## ロジックのビルドとテスト（SDK 不要）

```sh
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

テストを直接コンパイルしてもよい:

```sh
c++ -std=c++17 -I Source Source/ShellExec.cpp tests/test_shellexec.cpp -o test
./test
```

Windows では CMake を使わずに `scripts\test-windows.bat` でも同じテストを実行できる
（Visual Studio を自動検出して MSVC でビルドする）。

## プラグイン本体のビルド

ヘッダー（v77）と Windows / macOS の FMWrapper リンクライブラリを `Libraries/` に
同梱しているので、この2つのビルドには
[Claris SDK](https://www.claris.com/resources/downloads/) の別途ダウンロードは不要。
Linux 用ライブラリは同梱していない（SDK から取得。Linux の節を参照）。

### Windows → `ZooPlug.fmx64`

```sh
cmake -B build -DBUILD_PLUGIN=ON
cmake --build build --config Release
```

CMake を使わずに `scripts\build-windows-plugin.bat` でもよい（Visual Studio を自動検出し、
同梱の `FMWrapper.lib` をリンクして `build\ZooPlug.fmx64` を生成する）。フォルダーを
Visual Studio 2019/2022/2026 で直接開く（ファイル → 開く → フォルダー）方法でも、
`CMakeLists.txt` が自動的に認識される。

### macOS → `ZooPlug.fmplugin`

```sh
cmake -B build -DBUILD_PLUGIN=ON
cmake --build build
```

同梱の `FMWrapper.framework`（universal: x86_64 + arm64）をリンクした
`ZooPlug.fmplugin` バンドルが生成される。

### Linux → `ZooPlug.fmx`（FileMaker Server）

Linux 用ライブラリは**同梱していない**。Claris SDK の
`Libraries/Linux/<U22|U24>/<x64|arm64>/libFMWrapper.so` を取得し、本リポジトリの
`Libraries/linux/<U22|U24>/<x64|arm64>/` に置く（CMake が自動検出）か、`-DFMX_LIB`
でパスを渡す:

```sh
cmake -B build -DBUILD_PLUGIN=ON -DFMX_LIB=/path/to/libFMWrapper.so
cmake --build build
```

Claris は公式サンプルを **clang + libc++** でビルドしているので、ABI の安全のため次を推奨:

```sh
cmake -B build -DBUILD_PLUGIN=ON -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS="-stdlib=libc++"
cmake --build build
```

`libFMWrapper.so` を配置すれば `scripts/build-linux.sh` で自動化できる
（Linux ビルドは Ubuntu 22.04 / clang 14 で検証済み）。

> iOS は非対応（moo_shell は iOS のサンドボックスでプロセスを起動できないため）。

## FileMaker へのインストール

1. FileMaker Pro を終了する。
2. `ZooPlug.fmx64`（Windows）または `ZooPlug.fmplugin`（macOS）を FileMaker の
   **Extensions** フォルダーにコピーする。あるいはスクリプトの
   **プラグインファイルをインストール** ステップでインストールする。
3. FileMaker Pro を起動し、**編集 → 環境設定 → プラグイン**（Windows）/
   **FileMaker Pro → 設定 → プラグイン**（macOS）で **ZooPlug** を有効にする。
4. 計算式ダイアログで `moo_shell` が使えるようになる。

## クレジット

- [SimplePlugin](http://banks.id.au/filemaker/plugins/simpleplugin/) by Mark Banks — プラグインの土台。
- MooPlug — 再現元の `moo_shell` 関数。
- FMWrapper ヘッダー © Claris International Inc. 同梱ライセンスに基づき再配布。

詳細は [License.txt](License.txt) を参照。
