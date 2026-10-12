//----------------------------------------------------------------------------------
//	本体 (AviUtl2) との窓・メニュー・データフォルダのやり取り
//----------------------------------------------------------------------------------
#include "host.h"
#include "menu_find.h"

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

void prime_host_menus(HWND main) {
	HMENU bar = main ? GetMenu(main) : nullptr;
	if (!bar) return;
	int n = GetMenuItemCount(bar);
	for (int i = 0; i < n; i++) {
		HMENU sub = GetSubMenu(bar, i);
		if (!sub) continue;
		SendMessageW(main, WM_INITMENUPOPUP, (WPARAM)sub, MAKELPARAM(i, FALSE));
		// 2段目 (「表示」→「レイアウト」等) も同様に
		int m = GetMenuItemCount(sub);
		for (int j = 0; j < m; j++) {
			if (HMENU sub2 = GetSubMenu(sub, j)) SendMessageW(main, WM_INITMENUPOPUP, (WPARAM)sub2, MAKELPARAM(j, FALSE));
		}
	}
}

bool wait_host_menus_ready(HWND main, int wait_ms) {
	DWORD start = GetTickCount();
	for (int round = 0;; round++) {
		HMENU bar = main ? GetMenu(main) : nullptr;
		if (bar && find_layout_menu(bar)) {
			if (round > 0) log(L"メニューの準備: 中身を作らせて使えるようになった (" + std::to_wstring(round) + L" 回目)");
			return true;
		}
		if (GetTickCount() - start > (DWORD)wait_ms) break;
		if (round == 0) log(L"メニューの準備: 「レイアウト」メニューがまだ無いため、中身を作らせて待つ");
		prime_host_menus(main);
		// 本体が後から作る場合に備え、少し待つ (その間も画面の処理は進める)
		DWORD until = GetTickCount() + 250;
		while (GetTickCount() < until) {
			MSG m;
			while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
				if (m.message == WM_QUIT) {
					// 本体の終了の知らせは本体のメッセージループへ返す
					PostQuitMessage((int)m.wParam);
					return false;
				}
				TranslateMessage(&m);
				DispatchMessageW(&m);
			}
			Sleep(20);
		}
	}
	log(L"メニューの準備: 待っても「レイアウト」メニューが使えるようにならなかった");
	return false;
}

UINT find_menu_command(HWND main, const std::vector<std::wstring>& path) {
	HMENU bar = main ? GetMenu(main) : nullptr;
	if (!bar || path.empty()) {
		if (!bar) log(L"メニューの探索: 本体のメニューバーが無い");
		return 0;
	}
	std::wstring how;
	UINT cmd = find_host_command(bar, path, &how);
	if (!cmd && find_layout_menu(bar) == nullptr) {
		// メニューの中身がまだ作られていない場合がある (開いた時に作る作り)。作らせてから探し直す
		prime_host_menus(main);
		bar = GetMenu(main);
		cmd = bar ? find_host_command(bar, path, &how) : 0;
		if (cmd) {
			log(L"メニューの探索: 中身を作らせてから見つけた");
		}
		if (!bar) return 0;
	}
	std::wstring joined;
	for (auto& p : path) joined += (joined.empty() ? L"" : L" → ") + p;
	if (cmd && how != L"道筋") {
		// 他のプラグインがメニューの文字や構成を変えている環境 (決め打ちの道筋では見つからない)
		log(L"メニューの探索: 「" + joined + L"」を" + how + L"から見つけた");
	}
	if (!cmd && find_layout_menu(bar) == nullptr) {
		// レイアウトのメニュー自体が見つからない: 原因を調べられるよう、メニューの構成を1回だけ記録に残す
		static bool dumped = false;
		if (!dumped) {
			dumped = true;
			log(L"メニューの探索: 「" + joined + L"」が見つからない。メニューの構成:\n" + dump_menu(bar, 3));
		}
	}
	return cmd;
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
