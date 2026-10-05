//----------------------------------------------------------------------------------
//	段階0 技術検証プラグイン dock_probe.aux2
//	切り離された本体パネル (class 'aviutl2Manager' の独立窓) を
//	自前のコンテナ窓へ子窓としてはめ込めるかを検証する
//----------------------------------------------------------------------------------
#include <windows.h>
#include <vector>
#include <string>

#include "plugin2.h"
#include "logger2.h"

#define CONTAINER_CLASS L"DockProbeContainer"
#define WM_APP_CAPTURE (WM_APP + 1)
#define WM_APP_RELEASE (WM_APP + 2)

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

// コンテナ内で横に均等割り付け
static void layout_children() {
	if (!container || captured.empty()) return;
	RECT rc;
	GetClientRect(container, &rc);
	int n = (int)captured.size();
	int w = (rc.right - rc.left) / n;
	for (int i = 0; i < n; i++) {
		int x = i * w;
		int cw = (i == n - 1) ? (rc.right - x) : (w - 4); // 4px は境界の目印
		SetWindowPos(captured[i].hwnd, nullptr, x, 0, cw, rc.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

static void capture_panels() {
	if (!captured.empty()) {
		log_info(L"[dock_probe] 既に取り込み済み");
		return;
	}
	HWND main = find_main_window();
	if (!main) {
		log_info(L"[dock_probe] メインウィンドウが見つからない");
		return;
	}
	auto panels = find_floating_panels(main);
	log_info(L"[dock_probe] 切り離しパネル数=" + std::to_wstring(panels.size()));
	if (panels.empty()) return;

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
		log_info(L"[dock_probe] 取り込み " + describe(h) + (GetParent(h) == container ? L" 成功" : L" 失敗 err=" + std::to_wstring(err)));
		SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
	}
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
		log_info(L"[dock_probe] 返却 " + describe(it->hwnd));
	}
	captured.clear();
	if (container) ShowWindow(container, SW_HIDE);
}

static LRESULT CALLBACK container_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_APP_CAPTURE: capture_panels(); return 0;
		case WM_APP_RELEASE: release_panels(); return 0;
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

COMMON_PLUGIN_TABLE common_plugin_table = {
	L"Dock Probe",
	L"Dock Probe (段階0 技術検証) version 0.0.1",
};

EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle) { logger = handle; }
EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
	if (logger) {
		wchar_t buf[64];
		swprintf_s(buf, L"[dock_probe] host version=%lu", version);
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
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpszClassName = CONTAINER_CLASS;
	wc.lpfnWndProc = container_proc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	if (!RegisterClassExW(&wc)) return;
	container = CreateWindowExW(0, CONTAINER_CLASS, L"Dock Probe コンテナ",
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		100, 100, 1400, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!container) return;

	host->register_edit_menu(L"レイアウト検証\\切り離しパネルを取り込む", on_capture);
	host->register_edit_menu(L"レイアウト検証\\本体へ返す", on_release);
}
