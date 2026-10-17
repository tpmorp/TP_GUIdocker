//----------------------------------------------------------------------------------
//	開発版だけの計測・実験 (配布版では何もしない)
//	・起動の所要時間の記録
//	・起動・終了のちらつき対策の実験 (窓を描かせない属性 / 画面外で切り離す)
//	・機能制限の調査用の記録 (有効中に選ばれた本体のメニュー項目・ファイルのドロップ)
//	実験の切り替えは TP_GUIdocker.ini の [Dev] セクションで行う (cloak=1 / offscreen=1)
//----------------------------------------------------------------------------------
#pragma once
#include "common.h"

namespace tpgd {

#ifdef TPGD_DEV

// [Dev] セクションの値 (無ければ 0)
int dev_flag(const wchar_t* key);

// 本体の起動 (プロセスの開始) からの経過時間を記録する
void dev_log_uptime(const wchar_t* what);

// 窓を描かせない実験: main と、これから現れる本体のパネル窓を描かせない
void dev_cloak_begin(HWND main);
// 1枚だけ描かせる状態へ戻す (取り込みの直前に使う。子窓になると属性を外せなくなるため)
void dev_cloak_release(HWND h);
// 描かせない実験の最中なら、今出ている切り離し窓も描かせない (返却で独立窓に戻った直後に使う)
void dev_cloak_refresh();
// すべて描かせる状態へ戻す
void dev_cloak_end();

// 調査用: メニュー全体から項目の文字を探す (無ければ空)
std::wstring dev_menu_text(HMENU root, UINT id);

#else

inline int dev_flag(const wchar_t*) { return 0; }
inline void dev_log_uptime(const wchar_t*) {}
inline void dev_cloak_begin(HWND) {}
inline void dev_cloak_release(HWND) {}
inline void dev_cloak_refresh() {}
inline void dev_cloak_end() {}
inline std::wstring dev_menu_text(HMENU, UINT) { return {}; }

#endif

} // namespace tpgd
