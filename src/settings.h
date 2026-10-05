//----------------------------------------------------------------------------------
//	プラグイン専用の設定ファイル (TP_GUIdocker.ini) の読み書き
//	本体の aviutl2.ini には一切書き込まない
//----------------------------------------------------------------------------------
#pragma once
#include "common.h"
#include <map>

namespace tpgd {

// 自動保存する「自由分割へ入る直前の本体配置」のレイアウト名
constexpr const wchar_t* AUTO_RETURN_LAYOUT = TPGD_NAME L"_元の配置";
// 自由分割用に生成するレイアウト名
constexpr const wchar_t* DOCK_LAYOUT = TPGD_NAME;

struct Settings {
	// [State]
	bool resume = false;           // 前回、自由分割の状態で終了したか (起動時に引き継ぐ)
	bool session_active = false;   // 自由分割中か (正常に終了できなかった場合に 1 が残る)
	DWORD verified_version = 0;    // 利用者が自由分割の使用を認めた本体バージョン

	// [Settings]
	std::wstring return_layout;    // 戻り先レイアウト (空なら自動保存したもの)

	// [Tree]
	std::wstring tree;             // 分割木 (空なら初期配置)

	// [Panels] 窓の題名 → レイアウトのセクション名 (例: プレビュー編集 → preview)
	std::map<std::wstring, std::wstring> title_to_key;

	void load();
	void save() const;

	// 実際に戻す先のレイアウト名
	std::wstring effective_return_layout() const {
		return return_layout.empty() ? std::wstring(AUTO_RETURN_LAYOUT) : return_layout;
	}
};

extern Settings g_settings;

} // namespace tpgd
