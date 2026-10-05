//----------------------------------------------------------------------------------
//	本体 (AviUtl2) との窓・メニュー・データフォルダのやり取り
//----------------------------------------------------------------------------------
#include "host.h"

namespace tpgd {

static bool is_host_panel_class(HWND h) {
	wchar_t cls[64] = {};
	GetClassNameW(h, cls, 64);
	return wcscmp(cls, HOST_PANEL_CLASS) == 0;
}

HWND find_main_window() {
	struct Ctx { DWORD pid; HWND found; } ctx = { GetCurrentProcessId(), nullptr };
	EnumWindows([](HWND h, LPARAM l) -> BOOL {
		auto c = (Ctx*)l;
		DWORD pid = 0;
		GetWindowThreadProcessId(h, &pid);
		if (pid != c->pid) return TRUE;
		if (is_host_panel_class(h) && !GetWindow(h, GW_OWNER)) {
			c->found = h;
			return FALSE;
		}
		return TRUE;
	}, (LPARAM)&ctx);
	return ctx.found;
}

std::vector<HWND> find_floating_panels(HWND main) {
	struct Ctx { DWORD pid; HWND main; std::vector<HWND> list; } ctx = { GetCurrentProcessId(), main, {} };
	EnumWindows([](HWND h, LPARAM l) -> BOOL {
		auto c = (Ctx*)l;
		DWORD pid = 0;
		GetWindowThreadProcessId(h, &pid);
		if (pid != c->pid) return TRUE;
		if (is_host_panel_class(h) && GetWindow(h, GW_OWNER) == c->main && IsWindowVisible(h)) {
			c->list.push_back(h);
		}
		return TRUE;
	}, (LPARAM)&ctx);
	return ctx.list;
}

static std::wstring menu_text(HMENU menu, int pos) {
	wchar_t buf[256] = {};
	GetMenuStringW(menu, pos, buf, 256, MF_BYPOSITION);
	std::wstring s = buf;
	auto tab = s.find(L'\t');
	if (tab != std::wstring::npos) s.resize(tab);
	return s;
}

UINT find_menu_command(HWND main, const std::vector<std::wstring>& path) {
	HMENU menu = GetMenu(main);
	if (!menu || path.empty()) return 0;
	for (size_t depth = 0; depth < path.size(); depth++) {
		int n = GetMenuItemCount(menu);
		int found = -1;
		for (int i = 0; i < n; i++) {
			if (menu_text(menu, i) == path[depth]) { found = i; break; }
		}
		if (found < 0) return 0;
		if (depth == path.size() - 1) {
			UINT id = GetMenuItemID(menu, found);
			return id == (UINT)-1 ? 0 : id;
		}
		menu = GetSubMenu(menu, found);
		if (!menu) return 0;
	}
	return 0;
}

bool apply_host_layout(HWND main, const std::wstring& name) {
	UINT cmd = find_menu_command(main, { L"表示", L"レイアウト", name });
	if (!cmd) {
		log(L"レイアウト「" + name + L"」が本体のメニューに無い");
		return false;
	}
	log(L"レイアウト「" + name + L"」を適用 command=" + std::to_wstring(cmd));
	SendMessageW(main, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
	return true;
}

static bool is_directory(const std::wstring& p) {
	DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring app_data_dir() {
	if (g_config && g_config->app_data_path && *g_config->app_data_path) {
		std::wstring p = g_config->app_data_path;
		while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
		return p;
	}
	// 予備: DLL の場所から Plugin と Default を持つフォルダを上位へたどる
	std::wstring p = module_dir();
	while (true) {
		if (is_directory(p + L"\\Plugin") && is_directory(p + L"\\Default")) return p;
		auto pos = p.find_last_of(L'\\');
		if (pos == std::wstring::npos) break;
		p.resize(pos);
	}
	wchar_t pd[MAX_PATH] = {};
	GetEnvironmentVariableW(L"ProgramData", pd, MAX_PATH);
	return std::wstring(pd) + L"\\aviutl2";
}

std::wstring layout_dir() {
	return app_data_dir() + L"\\Default";
}

std::vector<std::wstring> list_layouts() {
	std::vector<std::wstring> names;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((layout_dir() + L"\\*.layout").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return names;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		std::wstring n = fd.cFileName;
		names.push_back(n.substr(0, n.size() - 7)); // ".layout" を除く
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return names;
}

double dpi_scale(HWND h) {
	UINT dpi = h ? GetDpiForWindow(h) : 0;
	if (!dpi) dpi = GetDpiForSystem();
	return dpi / 96.0;
}

int style_size(const char* key, int def, double scale) {
	int v = g_config ? g_config->get_layout_size(g_config, key) : 0;
	if (v <= 0) v = def;
	return (int)(v * scale + 0.5);
}

COLORREF style_color(const char* key, COLORREF def) {
	if (!g_config) return def;
	// 色コードが定義されていない場合は 0 が返るため、色数で定義の有無を確かめる
	if (g_config->get_color_code_index(g_config, key, -1) <= 0) return def;
	int c = g_config->get_color_code(g_config, key);
	return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

} // namespace tpgd
