//----------------------------------------------------------------------------------
//	TP_GUIdocker.aux2 — AviUtl2 自由レイアウトプラグイン (段階0 技術検証版)
//	1. 切り離された本体パネル (class 'aviutl2Manager' の独立窓) を
//	   自前のコンテナ窓へ子窓としてはめ込めるかを検証する
//	2. 本体のレイアウト機能を使い、全パネルの切り離し～取り込みを自動化できるかを検証する
//----------------------------------------------------------------------------------
#include <windows.h>
#include <commctrl.h>
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
#define TIMER_WATCH 2
#define SUBCLASS_ID 0x7447

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

// 本プラグイン (DLL) が置かれたフォルダ
static std::wstring module_dir() {
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCWSTR)&module_dir, &self);
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(self, path, MAX_PATH);
	std::wstring p = path;
	p.resize(p.find_last_of(L'\\'));
	return p;
}

// 本体のログ窓に加え、検証用に DLL と同じ場所の TP_GUIdocker.log へも書き出す
static void log_info(const std::wstring& s) {
	if (logger) logger->log(logger, s.c_str());
	static std::wstring path = module_dir() + L"\\TP_GUIdocker.log";
	SYSTEMTIME t;
	GetLocalTime(&t);
	wchar_t stamp[32];
	swprintf_s(stamp, L"%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
	std::wstring line = stamp + s + L"\r\n";
	int len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), nullptr, 0, nullptr, nullptr);
	std::string utf8(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), utf8.data(), len, nullptr, nullptr);
	HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f != INVALID_HANDLE_VALUE) {
		DWORD written = 0;
		WriteFile(f, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
		CloseHandle(f);
	}
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

static bool is_directory(const std::wstring& p) {
	DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// 本体のデータフォルダ (Plugin と Default を持つフォルダ) を探す
// 本プラグインは <データフォルダ>\Plugin 以下の任意の深さに置かれ得るため、上位へ順にたどる
static std::wstring data_dir() {
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCWSTR)&data_dir, &self);
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(self, path, MAX_PATH);
	std::wstring p = path;
	while (true) {
		auto pos = p.find_last_of(L'\\');
		if (pos == std::wstring::npos) break;
		p.resize(pos);
		if (is_directory(p + L"\\Plugin") && is_directory(p + L"\\Default")) return p;
	}
	// 見つからない場合は既定の場所
	wchar_t pd[MAX_PATH] = {};
	GetEnvironmentVariableW(L"ProgramData", pd, MAX_PATH);
	return std::wstring(pd) + L"\\aviutl2";
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
//	取り込み中パネルの判定
//---------------------------------------------------------------------

// h が取り込み中パネル自身、またはその子孫なら、そのパネルを返す
static HWND captured_panel_of(HWND h) {
	for (; h; h = GetParent(h)) {
		for (auto& c : captured) if (c.hwnd == h) return h;
	}
	return nullptr;
}

//---------------------------------------------------------------------
//	右クリックメニューの横取り (検証: 項目の記録と「統合」の阻止)
//	本体 exe の取り込みアドレス表 (IAT) の TrackPopupMenu / TrackPopupMenuEx を差し替える
//---------------------------------------------------------------------

using TrackPopupMenuEx_t = BOOL(WINAPI*)(HMENU, UINT, int, int, HWND, LPTPMPARAMS);
using TrackPopupMenu_t = BOOL(WINAPI*)(HMENU, UINT, int, int, int, HWND, const RECT*);
static TrackPopupMenuEx_t orig_TrackPopupMenuEx = nullptr;
static TrackPopupMenu_t orig_TrackPopupMenu = nullptr;

static void log_menu_items(HMENU menu, int depth) {
	int n = GetMenuItemCount(menu);
	for (int i = 0; i < n; i++) {
		MENUITEMINFOW mii = { sizeof(mii) };
		wchar_t text[256] = {};
		mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_SUBMENU | MIIM_STATE;
		mii.dwTypeData = text;
		mii.cch = 256;
		GetMenuItemInfoW(menu, i, TRUE, &mii);
		wchar_t buf[400];
		swprintf_s(buf, L"[TP_GUIdocker]   %*s- id=%u type=0x%X state=0x%X '%s'%s", depth * 2, L"",
			mii.wID, mii.fType, mii.fState, text, mii.hSubMenu ? L" (サブメニュー)" : L"");
		log_info(buf);
		if (mii.hSubMenu) log_menu_items(mii.hSubMenu, depth + 1);
	}
}

static bool find_menu_text(HMENU menu, UINT id, std::wstring& out) {
	int n = GetMenuItemCount(menu);
	for (int i = 0; i < n; i++) {
		HMENU sub = GetSubMenu(menu, i);
		if (sub) {
			if (find_menu_text(sub, id, out)) return true;
		} else if (GetMenuItemID(menu, i) == id) {
			wchar_t text[256] = {};
			GetMenuStringW(menu, i, text, 256, MF_BYPOSITION);
			out = text;
			return true;
		}
	}
	return false;
}

// メニュー表示の共通処理
// 選択結果を受け取るため TPM_RETURNCMD を付けて表示し、呼び出し元の求める形で返す
template <class F>
static BOOL track_popup_common(HMENU menu, UINT flags, int x, int y, HWND hwnd, F call_orig) {
	POINT pt = { x, y };
	HWND panel = captured_panel_of(hwnd);
	if (!panel) panel = captured_panel_of(WindowFromPoint(pt));
	log_info(L"[TP_GUIdocker] メニュー表示 owner=" + describe(hwnd) + (panel ? L" (取り込み中パネル: " + describe(panel) + L")" : L""));
	log_menu_items(menu, 0);

	bool wants_cmd = (flags & TPM_RETURNCMD) != 0;
	UINT cmd = (UINT)call_orig(flags | TPM_RETURNCMD);
	if (cmd == 0) {
		log_info(L"[TP_GUIdocker] メニュー選択なし");
		return 0;
	}
	std::wstring text;
	find_menu_text(menu, cmd, text);
	log_info(L"[TP_GUIdocker] メニュー選択 id=" + std::to_wstring(cmd) + L" '" + text + L"'");

	if (panel && text.find(L"統合") != std::wstring::npos) {
		log_info(L"[TP_GUIdocker] 取り込み中パネルの「統合」を阻止");
		return wants_cmd ? 0 : TRUE;
	}
	if (wants_cmd) return (BOOL)cmd;
	// 本来の動作 (選択項目を WM_COMMAND で通知) を再現する
	if (!(flags & TPM_NONOTIFY)) PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
	return TRUE;
}

static BOOL WINAPI hook_TrackPopupMenuEx(HMENU menu, UINT flags, int x, int y, HWND hwnd, LPTPMPARAMS params) {
	return track_popup_common(menu, flags, x, y, hwnd,
		[&](UINT f) { return orig_TrackPopupMenuEx(menu, f, x, y, hwnd, params); });
}

static BOOL WINAPI hook_TrackPopupMenu(HMENU menu, UINT flags, int x, int y, int reserved, HWND hwnd, const RECT* rect) {
	return track_popup_common(menu, flags, x, y, hwnd,
		[&](UINT f) { return orig_TrackPopupMenu(menu, f, x, y, reserved, hwnd, rect); });
}

// module の IAT 内で dll!func を指す項目を new_func に差し替え、元の関数を返す
static void* patch_iat(HMODULE module, const char* dll, const char* func, void* new_func) {
	auto base = (BYTE*)module;
	auto dos = (IMAGE_DOS_HEADER*)base;
	auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
	auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress) return nullptr;
	void* target = (void*)GetProcAddress(GetModuleHandleA(dll), func);
	for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
		if (_stricmp((char*)(base + imp->Name), dll) != 0) continue;
		for (auto thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk); thunk->u1.Function; thunk++) {
			if ((void*)thunk->u1.Function != target) continue;
			DWORD old;
			VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
			void* prev = (void*)thunk->u1.Function;
			thunk->u1.Function = (ULONG_PTR)new_func;
			VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
			return prev;
		}
	}
	return nullptr;
}

static void install_menu_hooks() {
	HMODULE exe = GetModuleHandleW(nullptr);
	orig_TrackPopupMenuEx = (TrackPopupMenuEx_t)patch_iat(exe, "user32.dll", "TrackPopupMenuEx", (void*)hook_TrackPopupMenuEx);
	orig_TrackPopupMenu = (TrackPopupMenu_t)patch_iat(exe, "user32.dll", "TrackPopupMenu", (void*)hook_TrackPopupMenu);
	log_info(std::wstring(L"[TP_GUIdocker] メニュー横取り TrackPopupMenuEx=") + (orig_TrackPopupMenuEx ? L"設置" : L"対象なし") +
		L" TrackPopupMenu=" + (orig_TrackPopupMenu ? L"設置" : L"対象なし"));
}

static void uninstall_menu_hooks() {
	HMODULE exe = GetModuleHandleW(nullptr);
	if (orig_TrackPopupMenuEx) patch_iat(exe, "user32.dll", "TrackPopupMenuEx", (void*)orig_TrackPopupMenuEx);
	if (orig_TrackPopupMenu) patch_iat(exe, "user32.dll", "TrackPopupMenu", (void*)orig_TrackPopupMenu);
}

//---------------------------------------------------------------------
//	取り込み中パネルの観察 (検証: 移動操作の記録)
//---------------------------------------------------------------------

static LRESULT CALLBACK panel_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	switch (msg) {
		case WM_NCLBUTTONDOWN:
			log_info(L"[TP_GUIdocker] 非クライアント領域クリック hit=" + std::to_wstring(wp) + L" " + describe(hwnd));
			break;
		case WM_SYSCOMMAND:
			log_info(L"[TP_GUIdocker] システムコマンド 0x" + std::to_wstring(wp & 0xFFF0) + L" " + describe(hwnd));
			break;
		case WM_ENTERSIZEMOVE:
			log_info(L"[TP_GUIdocker] 移動・サイズ変更開始 " + describe(hwnd));
			break;
		case WM_EXITSIZEMOVE:
			log_info(L"[TP_GUIdocker] 移動・サイズ変更終了 " + describe(hwnd));
			break;
		case WM_LBUTTONDOWN:
			log_info(L"[TP_GUIdocker] 左クリック x=" + std::to_wstring((short)LOWORD(lp)) + L" y=" + std::to_wstring((short)HIWORD(lp)) + L" " + describe(hwnd));
			break;
	}
	return DefSubclassProc(hwnd, msg, wp, lp);
}

// 取り込み中パネルが本体側へ持ち去られていないかを監視する (安全装置の原型)
static void layout_children();
static void watch_captured() {
	bool changed = false;
	for (auto it = captured.begin(); it != captured.end();) {
		if (!IsWindow(it->hwnd) || GetParent(it->hwnd) != container) {
			log_info(L"[TP_GUIdocker] 取り込み中パネルが外部で移動された: " + (IsWindow(it->hwnd) ? describe(it->hwnd) + L" 新しい親=" + describe(GetParent(it->hwnd)) : std::wstring(L"(破棄済み)")));
			if (IsWindow(it->hwnd)) RemoveWindowSubclass(it->hwnd, panel_subclass_proc, SUBCLASS_ID);
			it = captured.erase(it);
			changed = true;
		} else {
			++it;
		}
	}
	if (changed) layout_children();
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
		SetWindowSubclass(h, panel_subclass_proc, SUBCLASS_ID, 0);
	}
	if (captured.empty()) return;
	ShowWindow(container, SW_SHOW);
	layout_children();
	SetTimer(container, TIMER_WATCH, 1000, nullptr);
}

static void release_panels() {
	if (container) KillTimer(container, TIMER_WATCH);
	for (auto it = captured.rbegin(); it != captured.rend(); ++it) {
		if (!IsWindow(it->hwnd)) continue;
		RemoveWindowSubclass(it->hwnd, panel_subclass_proc, SUBCLASS_ID);
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
			} else if (wp == TIMER_WATCH) {
				watch_captured();
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
	L"TP_GUIdocker version 0.0.3 (段階0 技術検証版)",
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
	uninstall_menu_hooks();
	release_panels();
	if (container) DestroyWindow(container);
}
EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable(void) { return &common_plugin_table; }

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	log_info(L"[TP_GUIdocker] ===== 起動 version 0.0.3 =====");
	// 本体がレイアウト一覧を読む前に専用レイアウトを用意しておく
	generate_layout_file();
	install_menu_hooks();

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
