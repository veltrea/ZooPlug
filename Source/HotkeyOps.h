// HotkeyOps.h
//
// MooPlug の Hotkey 3 関数の実装。
//   Moo_HotkeyAdd / Moo_HotkeyList / Moo_HotkeyRemove
// グローバルホットキーを登録し、押下で FileMaker スクリプトを起動する。
//
// スレッド規律:
//   - ホットキー押下はホストのイベントループ上のハンドラで捕捉し、FMX API は呼ばず
//     「発火キュー」に plain-data（file/script/param）を積むだけにする。
//   - ZooPlug.cpp の kFMXT_Idle（メインスレッド・kFMXT_Unsafe はスキップ）が
//     HotkeyDrainPending() でキューを刈り取り FMX_StartScript を呼ぶ。
//   - Pro では計算スレッド＝メイン UI スレッドなので、HotkeyAdd 時にメインスレッドで
//     Windows=メッセージ専用ウィンドウ + RegisterHotKey（FileMaker のメッセージポンプが
//     WM_HOTKEY を配送）/ macOS=RegisterEventHotKey + メイン run loop ハンドラを使う。
//     専用スレッドは作らない。
//   - レジストリは canonical なキー名で識別する（Moo_HotkeyRemove( sHotkey ) が
//     モディファイア引数を持たないため。同名キーの二重登録は Err_4）。
//
// FileMaker SDK には依存しない（FMX 呼び出しは ZooPlug.cpp 側）。純粋ロジック
// （ParseHotkeyName / HotkeySignature）は tests/test_hotkeyops.cpp で検証する。
// 各関数は MooPlug のエラー番号（0 = 成功）を返す。引数不足（Err_1）はグルーで弾く。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_HOTKEY_OPS_H
#define ZOO_HOTKEY_OPS_H

#include <string>
#include <vector>

namespace zoo {

// kFMXT_Idle が刈り取る 1 件分の発火（FMX 型を含めない・素のデータのみ）。
struct HotkeyFire {
    std::string file;     // 通知先ファイル名
    std::string script;   // 起動するスクリプト名
    std::string param;    // スクリプト引数（Get(ScriptParameter)）
};

// ---- 純粋ロジック（テスト可能・OS に触れない） ----

// sHotkey 名を正規化（trim + 大文字化 + 別名吸収）し、対応キーか判定する。
// 対応: A-Z / 0-9 / F1-F12 / SPACE / ESC / END / HOME / UP / DOWN / LEFT / RIGHT。
// 既知なら canonical_out に正規形を入れて true、未知なら false。
bool ParseHotkeyName(const std::string& name, std::string& canonical_out);

// 表示・一覧用のシグネチャ文字列（例 "CTRL+SHIFT+A"）。canonical_key は ParseHotkeyName の出力。
std::string HotkeySignature(const std::string& canonical_key, bool alt, bool ctrl, bool shift);

// ---- 登録 API（戻りは Moo エラー番号。0 = 成功） ----

// Moo_HotkeyAdd: 2=不明キー / 3=ホットキーウィンドウ作成失敗 / 4=既登録 / 5=登録失敗
int HotkeyAdd(const std::string& hotkey, const std::string& file, const std::string& script,
              const std::string& param, bool alt, bool ctrl, bool shift, bool global);

// Moo_HotkeyList: 2=未登録（0 件）/ 3=取得失敗。登録済みシグネチャを separator で連結。
int HotkeyList(const std::string& separator, std::string& list_out);

// Moo_HotkeyRemove: 2=未登録（0 件）/ 3=不明キー / 4=該当なし / 5=解除失敗
int HotkeyRemove(const std::string& hotkey);

// ---- Idle / Shutdown（ZooPlug.cpp から呼ぶ） ----

// 溜まった発火を out に移す（FMX は呼ばない。kFMXT_Idle から呼ぶ）。
void HotkeyDrainPending(std::vector<HotkeyFire>& out);

// すべてのホットキーを解除し、ウィンドウ/ハンドラを破棄する（kFMXT_Shutdown から呼ぶ）。
void HotkeyShutdown();

} // namespace zoo

#endif // ZOO_HOTKEY_OPS_H
