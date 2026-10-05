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
	static const std::wstring reset_name = std::wstring(TPGD_NAME L"\\") + ui::MENU_RESET;
	host->register_edit_menu(reset_name.c_str(), on_reset);
	static const std::wstring return_name = std::wstring(TPGD_NAME L"\\") + ui::MENU_CHOOSE_RETURN;
	host->register_edit_menu(return_name.c_str(), on_choose_return);

	// 本体の起動完了後に、前回の状態を引き継ぐ
	dock_request_startup();
}
