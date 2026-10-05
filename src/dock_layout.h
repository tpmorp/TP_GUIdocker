//----------------------------------------------------------------------------------
//	自由分割用レイアウトの生成
//
//	土台 (本体が今保存した配置) を元に、次の規則で中身を作る (診断で確定した仕様に基づく)
//	- メディアエクスプローラー: 切り離し不可 (指定すると本体が読み込みに失敗する) のため、
//	  切り離しにせず非表示にする
//	- それ以外の全パネル: 表示・非表示を問わず切り離しにし、照合用の一意の位置を与える
//	  (非表示のものも切り離しにしておくと、後で「表示」メニューから開いた時に浮いた窓で現れる)
//	- 表示するのは is_visible が真のパネルだけ
//----------------------------------------------------------------------------------
#pragma once
#include "layout_file.h"
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace tpgd {

// 照合用の仮位置
struct LayoutSlot {
	std::wstring key;   // セクション名 (Window. を除いたもの。例: preview)
	POINT pt;           // 切り離し窓の左上 (画面座標)
};

struct DockLayoutPlan {
	IniDoc doc;
	std::vector<LayoutSlot> slots;           // 全ての切り離し対象
	std::vector<std::wstring> visible_keys;  // 表示する切り離し対象
};

// メディアエクスプローラーのセクションか (例: Window.explorer:1)
bool is_explorer_section(const std::string& section);

// 自由分割用レイアウトを作る
//   origin: 仮位置を並べ始める画面座標 (メイン窓のある画面の左上付近)
//   panel_w, panel_h: 切り離し窓の大きさ
DockLayoutPlan build_dock_layout(const IniDoc& base, const std::function<bool(const std::wstring&)>& is_visible,
	POINT origin, int panel_w, int panel_h);

// 切り離し窓の位置から、セクション名を求める (見つからなければ空)
std::wstring match_slot(const std::vector<LayoutSlot>& slots, POINT window_pos);

} // namespace tpgd
