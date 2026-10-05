//----------------------------------------------------------------------------------
//	TP_GUIdocker.aux2 — AviUtl2 向け 自由レイアウトプラグイン
//	プラグインの入口 (本体から呼ばれる関数とメニュー登録)
//----------------------------------------------------------------------------------
#include "common.h"
#include "dock.h"
#include "hooks.h"
#include "settings.h"

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
static void on_enter(EDIT_SECTION*) { dock_request_enter(); }
static void on_exit(EDIT_SECTION*) { dock_request_exit(); }
static void on_reset(EDIT_SECTION*) { dock_request_reset_tree(); }
static void on_choose_return(EDIT_SECTION*) { dock_request_choose_return_layout(); }

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	log(L"===== 起動 version " TPGD_VERSION L" (本体 " + std::to_wstring(g_host_version) + L") =====");
	g_settings.load();

	// 本体がレイアウト一覧を読み込む前に、戻り先と自由分割用のレイアウトを用意する
	dock_prepare_layouts();
	install_menu_hooks();
	if (!dock_init()) return;

	host->register_edit_menu(TPGD_NAME L"\\自由分割を開始", on_enter);
	host->register_edit_menu(TPGD_NAME L"\\自由分割を終了", on_exit);
	host->register_edit_menu(TPGD_NAME L"\\配置を初期状態に戻す", on_reset);
	host->register_edit_menu(TPGD_NAME L"\\戻り先レイアウトを選ぶ", on_choose_return);

	// 本体の起動完了後に、前回の状態を引き継ぐ
	dock_request_startup();
}
