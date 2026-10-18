//----------------------------------------------------------------------------------
//	開発版だけの計測・実験 (配布版では何もしない)
//	・起動の所要時間の記録
//	・機能制限の調査用の記録 (有効中に選ばれた本体のメニュー項目・ファイルのドロップ)
//	・実験の切り替え: TP_GUIdocker.ini の [Dev] セクション (exit_noapply=1: 終了時に元の配置の適用を省く)
//----------------------------------------------------------------------------------
#pragma once
#include "common.h"

namespace tpgd {

#ifdef TPGD_DEV

// [Dev] セクションの値 (無ければ 0)
int dev_flag(const wchar_t* key);

// 本体の起動 (プロセスの開始) からの経過時間を記録する
void dev_log_uptime(const wchar_t* what);

// 調査用: メニュー全体から項目の文字を探す (無ければ空)
std::wstring dev_menu_text(HMENU root, UINT id);

#else

inline int dev_flag(const wchar_t*) { return 0; }
inline void dev_log_uptime(const wchar_t*) {}
inline std::wstring dev_menu_text(HMENU, UINT) { return {}; }

#endif

} // namespace tpgd
