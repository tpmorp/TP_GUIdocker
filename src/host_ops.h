//----------------------------------------------------------------------------------
//	本体のレイアウト操作 (作成の自動操作・適用と結果の検証)
//
//	本体のレイアウト操作は、本体内でモーダルな画面 (名前入力・エラー表示) を伴う。
//	その間も入れ物の窓のタイマーは動くため、タイマーで画面を見つけて操作・監視する。
//----------------------------------------------------------------------------------
#pragma once
#include "common.h"

namespace tpgd {

constexpr UINT_PTR TIMER_HOST_OPS = 60;

// 本体の「表示」→「レイアウト」→「レイアウトを作成」を自動操作し、現在の配置を name で保存させる
// ※ 本体がメニューへ登録するため、再起動せずに一覧へ出る
// ※ メインウィンドウの位置は保存しない (戻した時にメイン窓が動かないように)
bool host_create_layout(HWND main, HWND timer_owner, const std::wstring& name);

struct ApplyResult {
	bool in_menu = false;   // メニューに見つかったか
	int errors = 0;         // 本体のエラー表示の回数 (自動で閉じた)
	bool ok() const { return in_menu && errors == 0; }
};

// 本体のレイアウトを適用し、エラー表示を監視する
ApplyResult host_apply_layout(HWND main, HWND timer_owner, const std::wstring& name);

// タイマー処理 (入れ物の窓の WM_TIMER から呼ぶ)
void host_ops_on_timer();

} // namespace tpgd
