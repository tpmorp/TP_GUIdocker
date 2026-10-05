//----------------------------------------------------------------------------------
//	プラグイン専用の設定ファイル (TP_GUIdocker.ini) の読み書き
//	本体の aviutl2.ini には一切書き込まない
//----------------------------------------------------------------------------------
#pragma once
#include "common.h"
#include <map>

namespace tpgd {

// 有効化する直前の本体配置 (本体の「レイアウトを作成」で本体自身に保存させる)
constexpr const wchar_t* AUTO_RETURN_LAYOUT = TPGD_NAME L"_元の配置";
// 有効中に使うレイアウト (枠を本体に作らせ、中身をプラグインが書き換える)
constexpr const wchar_t* DOCK_LAYOUT = TPGD_NAME;

// 分割木の保存形式の版 (古い版の分割木は使わず初期配置にする)
constexpr int TREE_VERSION = 2;

// 浮いた窓のまま使っているパネル (セクション名 → 画面上の位置)
using FloatingMap = std::map<std::wstring, RECT>;

// ワークスペース: 「保存した配置」と「使用中の配置」を分けて持つ
// (使用中の配置は操作のたびに更新される。保存した配置は明示的に保存した時だけ変わる)
struct Workspace {
	std::wstring name;
	std::wstring saved_tree;
	FloatingMap saved_floating;
	std::wstring work_tree;
	FloatingMap work_floating;
};

struct Settings {
	// [State]
	bool resume = false;           // 前回、有効のまま終了したか (起動時に引き継ぐ)
	bool session_active = false;   // 有効中か (正常に終了できなかった場合に 1 が残る)
	DWORD verified_version = 0;    // 利用者が有効化を認めた本体バージョン

	// [Settings]
	std::wstring return_layout;    // 無効時のレイアウト (空なら自動保存したもの)
	int drop_style = -1;           // ドラッグ時の表示の方式 (0: 十字ボタン式 / 1: 区画の領域式 / -1: 既定値)
	bool locked = false;           // レイアウトのロック
	int ws_active_color = -1;      // 選択中のワークスペースのボタンの色 (COLORREF。-1 なら本体設定)

	// [Tree] 分割木の形式の版
	int tree_version = 0;

	// ワークスペース ([Workspaces] と [Workspace.N])
	std::vector<Workspace> workspaces;
	int active_ws = 0;

	// 使用中のワークスペースの、使用中の配置 (workspaces[active_ws].work_* と同じもの)
	std::wstring tree;
	FloatingMap floating;

	void load();
	void save();

	// 実際に戻す先のレイアウト名
	std::wstring effective_return_layout() const {
		return return_layout.empty() ? std::wstring(AUTO_RETURN_LAYOUT) : return_layout;
	}
};

extern Settings g_settings;

} // namespace tpgd
