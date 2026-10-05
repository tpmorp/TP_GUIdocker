//----------------------------------------------------------------------------------
//	自由分割画面 (入れ物・パネルの取り込みと返却・境界線・タブ・安全装置)
//----------------------------------------------------------------------------------
#pragma once
#include "common.h"

namespace tpgd {

// 入れ物の窓を作る (RegisterPlugin 内で呼ぶ)
bool dock_init();

// 起動時の後片付け (旧版が残したファイルの削除。RegisterPlugin 内で呼ぶ)
void dock_cleanup_legacy();

// 起動完了後の処理 (前回の状態を引き継ぐ)
void dock_request_startup();

// メニューからの依頼 (編集セクション外で実行するため入れ物の窓へ依頼する)
// 有効化・無効化の切り替え (メニュー項目は1つで、状態に応じて文字が変わる)
void dock_request_toggle();

// 切り替え項目の文字 (無効中に表示するもの。有効中は無効化の文字に書き換える)
constexpr const wchar_t* TOGGLE_LABEL_ENABLE = TPGD_NAME L" を有効化";
constexpr const wchar_t* TOGGLE_LABEL_DISABLE = TPGD_NAME L" を無効化";
void dock_request_reset_tree();
void dock_request_choose_return_layout();

// プラグイン解放時
void dock_shutdown();

// h が取り込み中パネル (またはその子孫) か
bool dock_is_captured(HWND h);

} // namespace tpgd
