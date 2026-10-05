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
// 有効化・無効化の切り替え (メニュー項目は1つで、状態に応じて ui::MENU_ENABLE / ui::MENU_DISABLE に書き換える)
void dock_request_toggle();

// ドラッグ時の表示の方式 (十字ボタン式 / 区画の領域式) を切り替える
void dock_request_toggle_drop_style();

// ワークスペースを切り替える (index は 0 から)
void dock_request_switch_workspace(int index);

// カーソルの下のパネルを最大化・元に戻す
void dock_request_maximize();

// レイアウトのロックを切り替える
void dock_request_toggle_lock();

// 見本の画像を書き出す (指示用)
void dock_request_export_samples();

// テーマの色を選ぶ (reset: 本体設定に戻す)
void dock_request_theme_color(bool reset);
void dock_request_reset_tree();
void dock_request_choose_return_layout();

// プラグイン解放時
void dock_shutdown();

// h が取り込み中パネル (またはその子孫) か
bool dock_is_captured(HWND h);

} // namespace tpgd
