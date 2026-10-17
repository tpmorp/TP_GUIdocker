//----------------------------------------------------------------------------------
//	開発版だけの計測・実験
//----------------------------------------------------------------------------------
#include "dev_probe.h"

#ifdef TPGD_DEV

#include "host.h"
#include <dwmapi.h>
#include <set>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace tpgd {

int dev_flag(const wchar_t* key) {
	std::wstring path = module_dir() + L"\\" TPGD_NAME L".ini";
	return (int)GetPrivateProfileIntW(L"Dev", key, 0, path.c_str());
}

void dev_log_uptime(const wchar_t* what) {
	FILETIME created, exited, kernel, user, now;
	if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return;
	GetSystemTimeAsFileTime(&now);
	ULARGE_INTEGER a, b;
	a.LowPart = created.dwLowDateTime; a.HighPart = created.dwHighDateTime;
	b.LowPart = now.dwLowDateTime; b.HighPart = now.dwHighDateTime;
	unsigned long long ms = (b.QuadPart - a.QuadPart) / 10000;
	log(L"[計測] 本体の起動から " + std::to_wstring(ms) + L" ms: " + what);
}

//---------------------------------------------------------------------
//	窓を描かせない実験 (DWMWA_CLOAK)
//---------------------------------------------------------------------

static HWINEVENTHOOK g_hook = nullptr;
static HWND g_cloak_main = nullptr;
static std::set<HWND> g_cloaked;

static void set_cloak(HWND h, bool on) {
	BOOL v = on ? TRUE : FALSE;
	HRESULT hr = DwmSetWindowAttribute(h, DWMWA_CLOAK, &v, sizeof(v));
	if (on && SUCCEEDED(hr)) g_cloaked.insert(h);
	if (!on) g_cloaked.erase(h);
}

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

void dev_cloak_begin(HWND main) {
	if (!dev_flag(L"cloak") || g_hook) return;
	g_cloak_main = main;
	set_cloak(main, true);
	// 既に出ている切り離し窓も描かせない
	for (HWND h : find_floating_panels(main)) set_cloak(h, true);
	// これから現れる窓は、作られた・表示された瞬間に描かせない (同じスレッドの中で即座に呼ばれる形)
	g_hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, (HMODULE)&__ImageBase, on_win_event,
		GetCurrentProcessId(), 0, WINEVENT_INCONTEXT);
	log(L"[実験] 窓を描かせない: 開始 (監視=" + std::to_wstring(g_hook != nullptr) + L")");
}

void dev_cloak_refresh() {
	if (!g_hook || !g_cloak_main) return;
	for (HWND h : find_floating_panels(g_cloak_main)) set_cloak(h, true);
}

std::wstring dev_menu_text(HMENU root, UINT id) {
	if (!root) return {};
	int n = GetMenuItemCount(root);
	for (int i = 0; i < n; i++) {
		if (HMENU sub = GetSubMenu(root, i)) {
			std::wstring t = dev_menu_text(sub, id);
			if (!t.empty()) return t;
		} else if (GetMenuItemID(root, i) == id) {
			wchar_t buf[256] = {};
			GetMenuStringW(root, i, buf, 256, MF_BYPOSITION);
			return buf;
		}
	}
	return {};
}

void dev_cloak_release(HWND h) {
	if (g_cloaked.count(h)) set_cloak(h, false);
}

void dev_cloak_end() {
	if (!g_hook && g_cloaked.empty()) return;
	if (g_hook) UnhookWinEvent(g_hook);
	g_hook = nullptr;
	size_t n = g_cloaked.size();
	std::set<HWND> rest = g_cloaked;
	for (HWND h : rest) {
		if (IsWindow(h)) set_cloak(h, false);
	}
	g_cloaked.clear();
	g_cloak_main = nullptr;
	log(L"[実験] 窓を描かせない: 終了 (戻した窓 " + std::to_wstring(n) + L" 枚)");
}

} // namespace tpgd

#endif
