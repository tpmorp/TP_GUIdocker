//----------------------------------------------------------------------------------
//	診断: 本体のレイアウト読み込み仕様を実験で確かめる
//	条件を1つずつ変えた試験用レイアウトを順に適用し、エラーの有無・パネル位置・メイン窓の変化を記録する
//----------------------------------------------------------------------------------
#pragma once
#include "common.h"

namespace tpgd {

// 試験用レイアウトを書き出す (RegisterPlugin 内で呼ぶ。メニューへの反映は本体の再起動後)
void diag_prepare();

// 診断を実行する (自由分割中でないこと)
void diag_run(HWND timer_owner);

// 本体のエラー表示を監視するタイマーの処理
void diag_on_timer(HWND timer_owner);

constexpr UINT_PTR TIMER_DIAG_DIALOG = 50;

} // namespace tpgd
