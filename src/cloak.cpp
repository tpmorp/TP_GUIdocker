//----------------------------------------------------------------------------------
//	切り替え中の窓を描かせない
//----------------------------------------------------------------------------------
#include "cloak.h"
#include "host.h"

#include <dwmapi.h>
#include <set>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace tpgd {

static HWINEVENTHOOK g_hook = nullptr;
static HWND g_cloak_main = nullptr;
static std::set<HWND> g_cloaked;

static void set_cloak(HWND h, bool on) {
	BOOL v = on ? TRUE : FALSE;
	HRESULT hr = DwmSetWindowAttribute(h, DWMWA_CLOAK, &v, sizeof(v));
	if (on && SUCCEEDED(hr)) g_cloaked.insert(h);
	if (!on) g_cloaked.erase(h);
}

// 本体が切り離した独立窓か (メイン窓をオーナーに持つ aviutl2Manager で、子窓でないもの)
static bool is_host_floating(HWND h) {
	wchar_t cls[64] = {};
	GetClassNameW(h, cls, 64);
	return wcscmp(cls, HOST_PANEL_CLASS) == 0 && GetWindow(h, GW_OWNER) == g_cloak_main &&
		!(GetWindowLongPtrW(h, GWL_STYLE) & WS_CHILD);
}

static void CALLBACK on_win_event(HWINEVENTHOOK, DWORD, HWND h, LONG id_object, LONG, DWORD, DWORD) {
	if (id_object != OBJID_WINDOW || !h || g_cloaked.count(h)) return;
	if (is_host_floating(h)) set_cloak(h, true);
}

void cloak_begin(HWND main) {
	if (g_hook || !main) return;
	g_cloak_main = main;
	for (HWND h : find_floating_panels(main)) set_cloak(h, true);
	// これから現れる窓は、作られた・表示された瞬間に描かせない (同じスレッドの中で即座に呼ばれる形)
	g_hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, (HMODULE)&__ImageBase, on_win_event,
		GetCurrentProcessId(), 0, WINEVENT_INCONTEXT);
	if (!g_hook) log(L"窓を描かせない処理: 監視を設置できなかった (窓が一瞬見えることがある)");
}

void cloak_release(HWND h) {
	if (g_cloaked.count(h)) set_cloak(h, false);
}

void cloak_refresh() {
	if (!g_hook || !g_cloak_main) return;
	for (HWND h : find_floating_panels(g_cloak_main)) set_cloak(h, true);
}

void cloak_end() {
	if (g_hook) UnhookWinEvent(g_hook);
	g_hook = nullptr;
	std::set<HWND> rest = g_cloaked;
	for (HWND h : rest) {
		if (IsWindow(h)) set_cloak(h, false);
	}
	g_cloaked.clear();
	g_cloak_main = nullptr;
}

} // namespace tpgd
