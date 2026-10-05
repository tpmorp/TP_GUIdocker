//----------------------------------------------------------------------------------
//	TP_GUIdocker.aux2 — AviUtl2 自由レイアウトプラグイン (段階0 技術検証版)
//	1. 切り離された本体パネル (class 'aviutl2Manager' の独立窓) を
//	   自前のコンテナ窓へ子窓としてはめ込めるかを検証する
//	2. 本体のレイアウト機能を使い、全パネルの切り離し～取り込みを自動化できるかを検証する
//----------------------------------------------------------------------------------
#include <windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>

#include "plugin2.h"
#include "logger2.h"

#define CONTAINER_CLASS L"TP_GUIdockerContainer"
#define LAYOUT_NAME L"TP_GUIdocker"
#define WM_APP_CAPTURE (WM_APP + 1)
#define WM_APP_RELEASE (WM_APP + 2)
#define WM_APP_AUTO (WM_APP + 3)
#define TIMER_CAPTURE_AFTER_LAYOUT 1

static LOG_HANDLE* logger = nullptr;
static HWND container = nullptr;

// はめ込んだ窓の元の状態
struct Captured {
	HWND hwnd;
	LONG_PTR style;
	LONG_PTR ex_style;
	HWND owner;
	RECT rect;
};
static std::vector<Captured> captured;

static void log_info(const std::wstring& s) {
	if (logger) logger->log(logger, s.c_str());
}

static std::wstring describe(HWND h) {
	wchar_t cls[128] = {}, text[128] = {};
	GetClassNameW(h, cls, 128);
	GetWindowTextW(h, text, 128);
	wchar_t buf[400];
	swprintf_s(buf, L"0x%p class='%s' text='%s'", (void*)h, cls, text);
	return buf;
}

//---------------------------------------------------------------------
//	窓の探索
//---------------------------------------------------------------------

// 本体のメインウィンドウ (オーナーを持たない aviutl2Manager) を探す
static HWND find_main_window() {
	struct Ctx { DWORD pid; HWND found; } ctx = { GetCurrentProcessId(), nullptr };
	EnumWindows([](HWND h, LPARAM l) -> BOOL {
		auto c = (Ctx*)l;
		DWORD pid = 0;
		GetWindowThreadProcessId(h, &pid);
		if (pid != c->pid) return TRUE;
		wchar_t cls[64] = {};
		GetClassNameW(h, cls, 64);
		if (wcscmp(cls, L"aviutl2Manager") == 0 && !GetWindow(h, GW_OWNER)) {
			c->found = h;
			return FALSE;
		}
		return TRUE;
	}, (LPARAM)&ctx);
	return ctx.found;
}

// 切り離された本体パネル (メインウィンドウをオーナーに持つ aviutl2Manager の可視窓) を列挙
static std::vector<HWND> find_floating_panels(HWND main) {
	struct Ctx { DWORD pid; HWND main; std::vector<HWND> list; } ctx = { GetCurrentProcessId(), main, {} };
	EnumWindows([](HWND h, LPARAM l) -> BOOL {
		auto c = (Ctx*)l;
		DWORD pid = 0;
		GetWindowThreadProcessId(h, &pid);
		if (pid != c->pid) return TRUE;
		wchar_t cls[64] = {};
		GetClassNameW(h, cls, 64);
		if (wcscmp(cls, L"aviutl2Manager") == 0 && GetWindow(h, GW_OWNER) == c->main && IsWindowVisible(h)) {
			c->list.push_back(h);
		}
		return TRUE;
	}, (LPARAM)&ctx);
	return ctx.list;
}

//---------------------------------------------------------------------
//	メニューの探索 (項目番号はプラグイン数で変動するため名前で探す)
//---------------------------------------------------------------------

static std::wstring menu_text(HMENU menu, int pos) {
	wchar_t buf[256] = {};
	GetMenuStringW(menu, pos, buf, 256, MF_BYPOSITION);
	std::wstring s = buf;
	auto tab = s.find(L'\t');
	if (tab != std::wstring::npos) s.resize(tab);
	return s;
}

static HMENU find_submenu(HMENU menu, const std::wstring& name) {
	int n = GetMenuItemCount(menu);
	for (int i = 0; i < n; i++) {
		if (menu_text(menu, i) == name) return GetSubMenu(menu, i);
	}
	return nullptr;
}

// 「表示」→「レイアウト」→ name の項目番号を返す (見つからなければ 0)
static UINT find_layout_command(HWND main, const std::wstring& name) {
	HMENU bar = GetMenu(main);
	if (!bar) { log_info(L"[TP_GUIdocker] メニューバーが取得できない"); return 0; }
	HMENU view = find_submenu(bar, L"表示");
	if (!view) { log_info(L"[TP_GUIdocker] 「表示」メニューが見つからない"); return 0; }
	HMENU layout = find_submenu(view, L"レイアウト");
	if (!layout) { log_info(L"[TP_GUIdocker] 「レイアウト」メニューが見つからない"); return 0; }
	int n = GetMenuItemCount(layout);
	for (int i = 0; i < n; i++) {
		if (menu_text(layout, i) == name) return GetMenuItemID(layout, i);
	}
	log_info(L"[TP_GUIdocker] レイアウト「" + name + L"」がメニューに無い");
	return 0;
}

//---------------------------------------------------------------------
//	専用レイアウトファイルの生成
//	テンプレート (basic.layout) の表示中パネルをすべて切り離し状態にして書き出す
//---------------------------------------------------------------------

static std::wstring data_dir() {
	// 本プラグインは <データフォルダ>\Plugin に置かれる前提
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCWSTR)&data_dir, &self);
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(self, path, MAX_PATH);
	std::wstring p = path;
	p.resize(p.find_last_of(L'\\'));        // ...\Plugin
	p.resize(p.find_last_of(L'\\'));        // ...\aviutl2
	return p;
}

static bool generate_layout_file() {
	std::wstring dir = data_dir() + L"\\Default";
	std::wstring src = dir + L"\\basic.layout";
	std::wstring dst = dir + L"\\" LAYOUT_NAME L".layout";

	std::ifstream in(src, std::ios::binary);
	if (!in) { log_info(L"[TP_GUIdocker] テンプレートが読めない: " + src); return false; }
	std::stringstream ss;
	ss << in.rdbuf();
	std::string text = ss.str();

	// セクション単位で処理する
	std::vector<std::string> lines;
	{
		std::string line;
		std::istringstream is(text);
		while (std::getline(is, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			lines.push_back(line);
		}
	}
	std::string out;
	int cascade = 0;
	for (size_t i = 0; i < lines.size();) {
		// セクション見出しまで、またはファイル先頭の非セクション行
		size_t end = i + 1;
		while (end < lines.size() && (lines[end].empty() || lines[end][0] != '[')) end++;
		std::vector<std::string> sec(lines.begin() + i, lines.begin() + end);
		bool is_panel = sec[0].rfind("[Window.", 0) == 0;
		bool visible = false;
		for (auto& l : sec) if (l == "hide=0") visible = true;
		if (is_panel && visible) {
			int x = 100 + cascade * 40, y = 100 + cascade * 40;
			cascade++;
			for (auto& l : sec) {
				if (l.rfind("floating=", 0) == 0) l = "floating=1";
				// 浮動時の位置・大きさが未設定 (0) の場合は仮の値を与える
				else if (l == "x=0") l = "x=" + std::to_string(x);
				else if (l == "y=0") l = "y=" + std::to_string(y);
				else if (l == "w=0") l = "w=640";
				else if (l == "h=0") l = "h=480";
			}
		}
		for (auto& l : sec) { out += l; out += "\r\n"; }
		i = end;
	}

	std::ofstream o(dst, std::ios::binary | std::ios::trunc);
	if (!o) { log_info(L"[TP_GUIdocker] レイアウトを書き出せない: " + dst); return false; }
	o << out;
	log_info(L"[TP_GUIdocker] レイアウト生成: " + dst + L" (切り離し対象 " + std::to_wstring(cascade) + L" 枚)");
	return true;
}

//---------------------------------------------------------------------
//	取り込み・返却
//---------------------------------------------------------------------

// コンテナ内で格子状に割り付け
static void layout_children() {
	if (!container || captured.empty()) return;
	RECT rc;
	GetClientRect(container, &rc);
	int n = (int)captured.size();
	int cols = (int)std::ceil(std::sqrt((double)n));
	int rows = (n + cols - 1) / cols;
	int cw = rc.right / cols, ch = rc.bottom / rows;
	for (int i = 0; i < n; i++) {
		int c = i % cols, r = i / cols;
		int x = c * cw, y = r * ch;
		int w = (c == cols - 1) ? (rc.right - x) : (cw - 4); // 4px は境界の目印
		int h = (r == rows - 1) ? (rc.bottom - y) : (ch - 4);
		SetWindowPos(captured[i].hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

static void capture_panels() {
	HWND main = find_main_window();
	if (!main) {
		log_info(L"[TP_GUIdocker] メインウィンドウが見つからない");
		return;
	}
	auto panels = find_floating_panels(main);
	log_info(L"[TP_GUIdocker] 切り離しパネル数=" + std::to_wstring(panels.size()));

	for (auto h : panels) {
		Captured c = {};
		c.hwnd = h;
		c.style = GetWindowLongPtrW(h, GWL_STYLE);
		c.ex_style = GetWindowLongPtrW(h, GWL_EXSTYLE);
		c.owner = GetWindow(h, GW_OWNER);
		GetWindowRect(h, &c.rect);
		captured.push_back(c);

		// 枠・タイトルを外して子窓化
		LONG_PTR style = c.style;
		style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
		style |= WS_CHILD | WS_CLIPSIBLINGS;
		SetWindowLongPtrW(h, GWL_STYLE, style);
		LONG_PTR ex = c.ex_style & ~(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME);
		SetWindowLongPtrW(h, GWL_EXSTYLE, ex);
		SetLastError(0);
		SetParent(h, container);
		DWORD err = GetLastError();
		log_info(L"[TP_GUIdocker] 取り込み " + describe(h) + (GetParent(h) == container ? L" 成功" : L" 失敗 err=" + std::to_wstring(err)));
		SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
	}
	if (captured.empty()) return;
	ShowWindow(container, SW_SHOW);
	layout_children();
}

static void release_panels() {
	for (auto it = captured.rbegin(); it != captured.rend(); ++it) {
		if (!IsWindow(it->hwnd)) continue;
		SetParent(it->hwnd, nullptr);
		SetWindowLongPtrW(it->hwnd, GWL_STYLE, it->style);
		SetWindowLongPtrW(it->hwnd, GWL_EXSTYLE, it->ex_style);
		SetWindowLongPtrW(it->hwnd, GWLP_HWNDPARENT, (LONG_PTR)it->owner);
		SetWindowPos(it->hwnd, nullptr, it->rect.left, it->rect.top,
			it->rect.right - it->rect.left, it->rect.bottom - it->rect.top,
			SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		log_info(L"[TP_GUIdocker] 返却 " + describe(it->hwnd));
	}
	captured.clear();
	if (container) ShowWindow(container, SW_HIDE);
}

// 専用レイアウトを本体に適用させ、少し待ってから取り込む
static void auto_capture() {
	if (!captured.empty()) {
		log_info(L"[TP_GUIdocker] 既に取り込み済み");
		return;
	}
	HWND main = find_main_window();
	if (!main) {
		log_info(L"[TP_GUIdocker] メインウィンドウが見つからない");
		return;
	}
	UINT cmd = find_layout_command(main, LAYOUT_NAME);
	if (!cmd) {
		MessageBoxW(container, L"「表示」→「レイアウト」に TP_GUIdocker がありません。\nAviUtl2 を再起動してから再度お試しください。",
			L"TP_GUIdocker", MB_OK | MB_ICONINFORMATION);
		return;
	}
	log_info(L"[TP_GUIdocker] レイアウト適用 command=" + std::to_wstring(cmd));
	SendMessageW(main, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
	// 本体が切り離し窓を作り終えるのを待つ
	SetTimer(container, TIMER_CAPTURE_AFTER_LAYOUT, 500, nullptr);
}

static LRESULT CALLBACK container_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_APP_CAPTURE:
			if (captured.empty()) capture_panels();
			else log_info(L"[TP_GUIdocker] 既に取り込み済み");
			return 0;
		case WM_APP_RELEASE: release_panels(); return 0;
		case WM_APP_AUTO: auto_capture(); return 0;
		case WM_TIMER:
			if (wp == TIMER_CAPTURE_AFTER_LAYOUT) {
				KillTimer(hwnd, TIMER_CAPTURE_AFTER_LAYOUT);
				capture_panels();
			}
			return 0;
		case WM_SIZE: layout_children(); return 0;
		case WM_CLOSE:
			release_panels(); // 閉じる前に必ず本体へ返す (安全装置の原型)
			return 0;
		case WM_ERASEBKGND: {
			RECT rc; GetClientRect(hwnd, &rc);
			FillRect((HDC)wp, &rc, (HBRUSH)GetStockObject(DKGRAY_BRUSH));
			return 1;
		}
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

// メニューは編集セクション内で呼ばれるため、窓操作は外へ逃がして行う
static void on_capture(EDIT_SECTION*) { PostMessageW(container, WM_APP_CAPTURE, 0, 0); }
static void on_release(EDIT_SECTION*) { PostMessageW(container, WM_APP_RELEASE, 0, 0); }
static void on_auto(EDIT_SECTION*) { PostMessageW(container, WM_APP_AUTO, 0, 0); }

COMMON_PLUGIN_TABLE common_plugin_table = {
	L"TP_GUIdocker",
	L"TP_GUIdocker version 0.0.2 (段階0 技術検証版)",
};

EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle) { logger = handle; }
EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
	if (logger) {
		wchar_t buf[64];
		swprintf_s(buf, L"[TP_GUIdocker] host version=%lu", version);
		logger->log(logger, buf);
	}
	return true;
}
EXTERN_C __declspec(dllexport) void UninitializePlugin() {
	release_panels();
	if (container) DestroyWindow(container);
}
EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable(void) { return &common_plugin_table; }

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	// 本体がレイアウト一覧を読む前に専用レイアウトを用意しておく
	generate_layout_file();

	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpszClassName = CONTAINER_CLASS;
	wc.lpfnWndProc = container_proc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	if (!RegisterClassExW(&wc)) return;
	container = CreateWindowExW(0, CONTAINER_CLASS, L"TP_GUIdocker",
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		100, 100, 1400, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!container) return;

	host->register_edit_menu(L"TP_GUIdocker\\全パネルを自動で取り込む", on_auto);
	host->register_edit_menu(L"TP_GUIdocker\\切り離しパネルを取り込む", on_capture);
	host->register_edit_menu(L"TP_GUIdocker\\本体へ返す", on_release);
}
