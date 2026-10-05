//----------------------------------------------------------------------------------
//	TP_GUIdocker.aux2 — AviUtl2 向け 自由レイアウトプラグイン
//	プラグインの入口 (本体から呼ばれる関数とメニュー登録)
//----------------------------------------------------------------------------------
#include "common.h"
#include "dock.h"
#include "hooks.h"
#include "settings.h"
#include "ui_spec.h"

using namespace tpgd;

COMMON_PLUGIN_TABLE common_plugin_table = {
	TPGD_NAME,
	TPGD_NAME L" version " TPGD_VERSION,
};

EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle) {
	g_logger = handle;
}

EXTERN_C __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* handle) {
	g_config = handle;
}

EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
	g_host_version = version;
	return true;
}

EXTERN_C __declspec(dllexport) void UninitializePlugin() {
	uninstall_menu_hooks();
	dock_shutdown();
	log(L"終了");
}

EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable(void) {
	return &common_plugin_table;
}

// メニューは編集セクション内で呼ばれるため、実際の処理は入れ物の窓へ依頼して後で行う
static void on_toggle(EDIT_SECTION*) { dock_request_toggle(); }
static void on_reset(EDIT_SECTION*) { dock_request_reset_tree(); }
static void on_choose_return(EDIT_SECTION*) { dock_request_choose_return_layout(); }
static void on_drop_style(EDIT_SECTION*) { dock_request_toggle_drop_style(); }
static void on_maximize(EDIT_SECTION*) { dock_request_maximize(); }
static void on_lock(EDIT_SECTION*) { dock_request_toggle_lock(); }
static void on_export_samples(EDIT_SECTION*) { dock_request_export_samples(); }
template <int N> static void on_workspace(EDIT_SECTION*) { dock_request_switch_workspace(N - 1); }

// ワークスペース1〜9の切り替え項目 (本体のショートカットキー設定で割り当てられるよう、メニュー項目として登録する)
static void register_workspace_menus(HOST_APP_TABLE* host) {
	static void (*const handlers[])(EDIT_SECTION*) = {
		on_workspace<1>, on_workspace<2>, on_workspace<3>, on_workspace<4>, on_workspace<5>,
		on_workspace<6>, on_workspace<7>, on_workspace<8>, on_workspace<9>,
	};
	static std::vector<std::wstring> names;
	int count = std::min<int>(ui::WS_MAX, (int)(sizeof(handlers) / sizeof(handlers[0])));
	names.reserve(count);
	for (int i = 0; i < count; i++) {
		wchar_t item[128];
		swprintf_s(item, ui::MENU_WS_SWITCH, i + 1);
		names.push_back(std::wstring(TPGD_NAME L"\\") + ui::MENU_WS_GROUP + L"\\" + item);
		host->register_edit_menu(names.back().c_str(), handlers[i]);
	}
}

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	log(L"===== 起動 version " TPGD_VERSION L" (本体 " + std::to_wstring(g_host_version) + L") =====");
	g_settings.load();
	dock_cleanup_legacy();
	install_menu_hooks();
	if (!dock_init()) return;

	// 有効化・無効化は1つの項目で切り替える (文字は状態に応じて書き換える)
	// ※ 本体が名前の文字列を保持する場合に備え、プログラム終了まで残る文字列を渡す
	static const std::wstring toggle_name = std::wstring(TPGD_NAME L"\\") + ui::MENU_ENABLE;
	host->register_edit_menu(toggle_name.c_str(), on_toggle);
	register_workspace_menus(host);
	static const std::wstring maximize_name = std::wstring(TPGD_NAME L"\\") + ui::MENU_MAXIMIZE;
	host->register_edit_menu(maximize_name.c_str(), on_maximize);
	// ロックも1つの項目で切り替える (文字は今の状態を示す)
	static const std::wstring lock_name = std::wstring(TPGD_NAME L"\\") + (g_settings.locked ? ui::MENU_UNLOCK : ui::MENU_LOCK);
	host->register_edit_menu(lock_name.c_str(), on_lock);
	static const std::wstring reset_name = std::wstring(TPGD_NAME L"\\") + ui::MENU_RESET;
	host->register_edit_menu(reset_name.c_str(), on_reset);
	static const std::wstring return_name = std::wstring(TPGD_NAME L"\\") + ui::MENU_CHOOSE_RETURN;
	host->register_edit_menu(return_name.c_str(), on_choose_return);
	// ドラッグ時の表示の方式も1つの項目で切り替える (文字は今の方式を示す)
	int style = g_settings.drop_style >= 0 ? g_settings.drop_style : ui::DROP_STYLE;
	static const std::wstring drop_name = std::wstring(TPGD_NAME L"\\") +
		(style == 0 ? ui::MENU_DROP_STYLE_COMPASS : ui::MENU_DROP_STYLE_REGION);
	host->register_edit_menu(drop_name.c_str(), on_drop_style);
	static const std::wstring samples_name = std::wstring(TPGD_NAME L"\\") + ui::MENU_EXPORT_SAMPLES;
	host->register_edit_menu(samples_name.c_str(), on_export_samples);

	// 本体の起動完了後に、前回の状態を引き継ぐ
	dock_request_startup();
}
