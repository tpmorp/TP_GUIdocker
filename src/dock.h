//----------------------------------------------------------------------------------
//	自由分割画面 (入れ物・パネルの取り込みと返却・境界線・タブ・安全装置)
//----------------------------------------------------------------------------------
#pragma once
#include "common.h"

namespace tpgd {

// 入れ物の窓を作る (RegisterPlugin 内で呼ぶ)
bool dock_init();

// 戻り先レイアウトの自動保存と、自由分割用レイアウトの生成 (RegisterPlugin 内で呼ぶ)
void dock_prepare_layouts();

// 起動完了後の処理 (前回の状態を引き継ぐ)
void dock_request_startup();

// 自由分割を開始・終了する (メニューから。編集セクション外で実行するため入れ物へ依頼する)
void dock_request_enter();
void dock_request_exit();
void dock_request_reset_tree();
void dock_request_choose_return_layout();

// プラグイン解放時
void dock_shutdown();

// h が取り込み中パネル (またはその子孫) か
bool dock_is_captured(HWND h);

} // namespace tpgd
