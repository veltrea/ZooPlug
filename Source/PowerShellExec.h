// PowerShellExec.h
//
// zoo_powershell の純粋ロジック。PowerShell スクリプトを「テンポラリファイル方式」
// （docs/zoo-powershell-design.md §18）で実行し、出力を UTF-8 文字列で返す。
// FileMaker SDK (FMWrapper) には一切依存しないので、FileMaker を起動せずに
// 単体でビルド・実行・テストできる（tests/test_powershellexec.cpp 参照）。
//
// 設計の核心（§18・全 6 版 × FullLanguage/CLM で実証済み）:
//   1. ユーザーのスクリプトを temp .ps1（UTF-8 BOM 付き）に書く。
//      - BOM 無しだと Windows PowerShell 5.1 がリテラルを CP932 誤読する。
//   2. スクリプトを `& { <user> } 2>&1 | Out-File -Encoding utf8 <out.txt>` でラップ。
//      - 出力は cmdlet (Out-File) でファイルへ。Constrained Language Mode でも安全
//        （.NET 型アクセスを一切持たない）。stdout は使わない（版依存の CP932 を回避）。
//   3. `<host> -NoProfile -NonInteractive -ExecutionPolicy Bypass -File <temp.ps1>` を起動。
//   4. out.txt を UTF-8 として読む。5.1 が付ける先頭 BOM を除去し、改行を CR 正規化。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_POWERSHELL_EXEC_H
#define ZOO_POWERSHELL_EXEC_H

#include <string>

namespace zoo {

// zoo_powershell のオプション。
struct PowerShellOptions {
    // true  : PowerShell 7+ (pwsh) を使う（bCore=True）
    // false : Windows PowerShell 5.1 (powershell.exe) を使う（既定・Windows 標準同梱）
    // ※ macOS/Linux には 5.1 が無いので、どちらでも pwsh を使う。
    bool use_core = false;
};

// ---- 純粋ロジック（プラットフォーム非依存・単体テスト対象）-----------------

// PowerShell の単一引用符文字列に安全に埋め込めるよう、' を '' に倍化する。
std::string PsSingleQuoteEscape(const std::string& s);

// ユーザーのスクリプトを「結合出力を out_path へ UTF-8 で書く」形にラップした
// .ps1 本文（UTF-8、CRLF 改行）を返す。BOM はまだ付けない（WithUtf8Bom で付与）。
//   $ErrorActionPreference = 'Continue'
//   & {
//   <user_command>
//   } 2>&1 | Out-File -FilePath '<out_path>' -Encoding utf8 -Width 8192
std::string BuildWrappedScript(const std::string& user_command_utf8,
                               const std::string& out_path);

// 先頭に UTF-8 BOM (EF BB BF) を付ける。temp .ps1 を 5.1 が UTF-8 と認識するために必要。
std::string WithUtf8Bom(const std::string& text);

// 先頭の UTF-8 BOM (EF BB BF) を 1 個だけ取り除く（5.1 の Out-File -utf8 が付ける）。
std::string StripUtf8Bom(const std::string& text);

// 一意なファイル名の語幹を作る（temp .ps1 / out.txt の衝突回避）。
// 例: MakeStem(1234, 7) -> "zoo_ps_1234_7"
std::string MakeStem(unsigned long pid, unsigned long long counter);

// ---- 実行（プラットフォーム I/O）-------------------------------------------

// command_utf8 を PowerShell で実行し、標準出力＋標準エラーを結合した UTF-8 を返す。
// 改行は CR(\r) に正規化し、末尾の改行は除去する（moo_shell と同じ）。
// 空コマンド、ホスト起動失敗、ロックダウン環境で弾かれた等の場合は空を返す。
std::string RunPowerShell(const std::string& command_utf8,
                          const PowerShellOptions& opts = PowerShellOptions());

} // namespace zoo

#endif // ZOO_POWERSHELL_EXEC_H
