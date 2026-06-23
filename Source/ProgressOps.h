// ProgressOps.h
//
// MooPlug の Moo_ProgressOptions の状態保持。
// Download / FTP の bProgress（進捗ダイアログ）で使う表示オプションを保存する。
// FileMaker SDK には依存しない（tests/test_progressops.cpp 参照）。
//
// 注意: 進捗ダイアログ UI そのものはまだ未配線（bProgress は現状フラグを受けるだけで無視）。
// この関数は本家どおり「オプションを設定して 1 を返す」挙動だけを提供し、設定値は
// 将来 Download/FTP の進捗 UI を実装したときに参照される。サーバーの並行呼び出しを
// 想定し、状態は mutex で保護する。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_PROGRESS_OPS_H
#define ZOO_PROGRESS_OPS_H

#include <string>

namespace zoo {

struct ProgressOptions {
    bool set = false;          // 一度でも設定されたか
    std::string title;
    std::string caption;
    bool cancel = false;       // キャンセルボタンを出すか
};

// Moo_ProgressOptions( sTitle {; sCaption ; bCancel } ) のロジック。
// 値を保存して 0（成功）を返す。引数不足（Err_1）はグルーで弾く。
int SetProgressOptions(const std::string& title, const std::string& caption, bool cancel);

// 現在の進捗オプションを返す（Download/FTP の進捗 UI 実装時に使う）。
ProgressOptions GetProgressOptions();

// 保存値をクリアする（テスト・後始末用）。
void ClearProgressOptions();

} // namespace zoo

#endif // ZOO_PROGRESS_OPS_H
