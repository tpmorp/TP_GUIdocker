//----------------------------------------------------------------------------------
//	自由分割画面 (入れ物・パネルの取り込みと返却・境界線・タブ・安全装置)
//
//	仕組み (docs/design.md 第4章):
//	1. 本体の「レイアウトを作成」を自動操作し、現在の配置を本体自身に保存させる (戻り先)
//	2. それを土台に自由分割用レイアウトを生成し、本体に適用させて全パネルを切り離す
//	   (適用中は本体のエラー表示を監視し、異常があれば直ちに元へ戻す)
//	3. 切り離された窓を、与えた一意の位置で照合してセクション名を特定する
//	4. 分割木に属するパネルを子窓化して入れ物へはめ込む。それ以外は浮いた窓のまま置く
//	5. 終了時は全パネルを返却し、戻り先レイアウトを適用して標準の画面へ戻す
//----------------------------------------------------------------------------------
#include "dock.h"
#include "dock_layout.h"
#include "host.h"
#include "host_ops.h"
#include "layout_file.h"
#include "settings.h"
#include "tree.h"

#include <algorithm>
#include <memory>
#include <set>

namespace tpgd {

//---------------------------------------------------------------------
//	定数
//---------------------------------------------------------------------

#define CONTAINER_CLASS TPGD_NAME L"Container"

enum : UINT {
	WM_APP_STARTUP = WM_APP + 1,
	WM_APP_ENTER,
	WM_APP_EXIT,
	WM_APP_RESET_TREE,
	WM_APP_CHOOSE_RETURN,
	WM_APP_RELAYOUT,
};

enum : UINT_PTR {
	TIMER_STARTUP = 1,
	TIMER_WATCH = 2,
	TIMER_REENTER = 3,
};

constexpr UINT_PTR PANEL_SUBCLASS_ID = 0x7447;
constexpr UINT_PTR MAIN_SUBCLASS_ID = 0x7448;
constexpr int OFFSCREEN = -30000;   // 非表示タブのパネルを置く座標
constexpr int MIN_PANE = 60;        // 分割の最小サイズ (96dpi 換算)
constexpr int FLOAT_W = 640;        // 切り離し窓の既定の大きさ (96dpi 換算)
constexpr int FLOAT_H = 420;

//---------------------------------------------------------------------
//	状態
//---------------------------------------------------------------------

// 入れ物に取り込んだパネル
struct Panel {
	HWND hwnd = nullptr;
	std::wstring key;         // レイアウトのセクション名 (例: preview)
	std::wstring title;       // 窓の題名 (例: プレビュー編集)
	LONG_PTR style = 0;       // 取り込み前のスタイル
	LONG_PTR ex_style = 0;
	HWND owner = nullptr;     // 取り込み前のオーナー
	RECT float_rect = {};     // 取り込み前の位置
	RECT assigned = {};       // 入れ物内の割り当て位置
	bool visible = false;     // 最後に確認した表示状態
};

// 浮いた窓のまま置いているパネル
struct FloatPanel {
	HWND hwnd = nullptr;
	std::wstring key;         // 照合できなかった場合は空
};

struct TabHit {
	Node* leaf;
	std::wstring key;
	RECT rect;
};

static HWND g_container = nullptr;
static HWND g_main = nullptr;
static bool g_main_subclassed = false;
static bool g_main_destroying = false;
static bool g_active = false;
static bool g_busy = false;            // 開始・終了の処理中 (重複実行を防ぐ)
static bool g_moving = false;          // 自分で位置を変えている最中
static int g_startup_tries = 0;

static std::vector<Panel> g_panels;
static std::vector<FloatPanel> g_floats;
static std::unique_ptr<Node> g_root;
static std::vector<TabHit> g_tab_hits;
static std::vector<LayoutSlot> g_slots;   // 照合用の仮位置
static std::set<UINT> g_explorer_cmds;    // 「表示」→「メディアエクスプローラー」の項目番号

// 境界線のドラッグ
static Node* g_drag = nullptr;

// 描画用
static HFONT g_font = nullptr;
static double g_scale = 1.0;

// 処理中の印 (入れ子で使っても、抜けるときに元の値へ戻す)
struct BusyGuard {
	bool prev;
	BusyGuard() : prev(g_busy) { g_busy = true; }
	~BusyGuard() { g_busy = prev; }
};

//---------------------------------------------------------------------
//	寸法・配色 (本体の style.conf に合わせる)
//---------------------------------------------------------------------

static int scaled(int v) { return (int)(v * g_scale + 0.5); }
static int gap_size() { return style_size("WindowSeparatorSize", 7, g_scale); }
static int tab_height() { return style_size("GroupTabHeight", 24, g_scale); }
static int footer_height() { return style_size("FooterHeight", 24, g_scale); }

static void update_font() {
	if (g_font) DeleteObject(g_font);
	std::wstring face = L"Yu Gothic UI";
	float size = 13.0f;
	if (g_config) {
		if (FONT_INFO* fi = g_config->get_font_info(g_config, "Control")) {
			if (fi->name && *fi->name) face = fi->name;
			if (fi->size > 0) size = fi->size;
		}
	}
	g_font = CreateFontW(-(int)(size * g_scale + 0.5), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face.c_str());
}

//---------------------------------------------------------------------
//	検索・保存
//---------------------------------------------------------------------

static Panel* find_panel(HWND h) {
	for (auto& p : g_panels) if (p.hwnd == h) return &p;
	return nullptr;
}

static FloatPanel* find_float(HWND h) {
	for (auto& f : g_floats) if (f.hwnd == h) return &f;
	return nullptr;
}

bool dock_is_captured(HWND h) {
	for (; h; h = GetParent(h)) {
		if (find_panel(h)) return true;
	}
	return false;
}

static bool is_present(const std::wstring& key) {
	for (auto& p : g_panels) if (p.key == key && IsWindowVisible(p.hwnd)) return true;
	return false;
}

static void save_tree() {
	if (!g_root) return;
	g_settings.tree = serialize_tree(g_root.get());
	g_settings.tree_version = TREE_VERSION;
	g_settings.save();
}

static std::wstring layout_path(const std::wstring& name) {
	return layout_dir() + L"\\" + name + L".layout";
}

static void notify(const std::wstring& msg, UINT icon = MB_ICONWARNING) {
	log(L"利用者へ通知: " + msg);
	MessageBoxW(g_main, msg.c_str(), TPGD_NAME, MB_OK | icon);
}

//---------------------------------------------------------------------
//	配置
//---------------------------------------------------------------------

static void position_container() {
	if (!g_container || !g_main) return;
	RECT rc;
	GetClientRect(g_main, &rc);
	rc.bottom = std::max(rc.top, rc.bottom - footer_height());
	SetWindowPos(g_container, HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE);
}

static void relayout() {
	if (!g_active || !g_root) return;
	RECT rc;
	GetClientRect(g_container, &rc);
	compute_layout(g_root.get(), rc, gap_size(), tab_height());

	// 各葉の表示中タブを決め、タブの当たり判定を作る
	g_tab_hits.clear();
	HDC dc = GetDC(g_container);
	HGDIOBJ old = SelectObject(dc, g_font);
	int pad = scaled(10);
	for_each_leaf(g_root.get(), [&](Node* leaf) {
		if (!is_present(leaf->active)) {
			leaf->active.clear();
			for (auto& k : leaf->panels) if (is_present(k)) { leaf->active = k; break; }
		}
		int x = leaf->tabs.left;
		for (auto& k : leaf->panels) {
			if (!is_present(k)) continue;
			std::wstring title;
			for (auto& p : g_panels) if (p.key == k) { title = p.title; break; }
			SIZE sz = {};
			GetTextExtentPoint32W(dc, title.c_str(), (int)title.size(), &sz);
			RECT r = { x, leaf->tabs.top, std::min<LONG>(leaf->tabs.right, x + sz.cx + pad * 2), leaf->tabs.bottom };
			g_tab_hits.push_back({ leaf, k, r });
			x = r.right;
		}
	});
	SelectObject(dc, old);
	ReleaseDC(g_container, dc);

	// パネルを配置する (表示中タブ以外は画面外へ)
	HDWP dwp = BeginDeferWindowPos((int)g_panels.size());
	g_moving = true;
	for (auto& p : g_panels) {
		Node* leaf = find_leaf_with(g_root.get(), p.key);
		RECT r;
		if (leaf && leaf->active == p.key) {
			r = leaf->body;
		} else {
			r = { OFFSCREEN, OFFSCREEN, OFFSCREEN + 320, OFFSCREEN + 240 };
		}
		p.assigned = r;
		if (dwp) dwp = DeferWindowPos(dwp, p.hwnd, nullptr, r.left, r.top,
			std::max<LONG>(0, r.right - r.left), std::max<LONG>(0, r.bottom - r.top), SWP_NOZORDER | SWP_NOACTIVATE);
	}
	if (dwp) EndDeferWindowPos(dwp);
	g_moving = false;
	InvalidateRect(g_container, nullptr, TRUE);
}

// 浮いた窓を置く既定の位置 (メイン窓の中央)
static void move_float_to_center(HWND h) {
	RECT m, r;
	GetWindowRect(g_main, &m);
	GetWindowRect(h, &r);
	int w = r.right - r.left, hgt = r.bottom - r.top;
	int x = (m.left + m.right - w) / 2, y = (m.top + m.bottom - hgt) / 2;
	SetWindowPos(h, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

//---------------------------------------------------------------------
//	パネルの観察 (勝手な移動の阻止)
//---------------------------------------------------------------------

static LRESULT CALLBACK panel_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	switch (msg) {
		case WM_SYSCOMMAND: {
			UINT cmd = (UINT)(wp & 0xFFF0);
			// 本体の見出し帯ドラッグ等による移動・サイズ変更は、入れ物の中では行わせない
			if (cmd == SC_MOVE || cmd == SC_SIZE || cmd == SC_MAXIMIZE || cmd == SC_MINIMIZE || cmd == SC_RESTORE) return 0;
			break;
		}
		case WM_WINDOWPOSCHANGING:
			if (!g_moving) {
				if (Panel* p = find_panel(hwnd)) {
					// 本体側からの位置・大きさの変更は、割り当て位置へ矯正する
					auto pos = (WINDOWPOS*)lp;
					pos->x = p->assigned.left;
					pos->y = p->assigned.top;
					pos->cx = p->assigned.right - p->assigned.left;
					pos->cy = p->assigned.bottom - p->assigned.top;
				}
			}
			break;
		case WM_SHOWWINDOW:
			// 本体による表示・非表示 (「ウィンドウを閉じる」や「表示」メニュー) を反映する
			if (g_container) PostMessageW(g_container, WM_APP_RELAYOUT, 0, 0);
			break;
		case WM_NCDESTROY:
			RemoveWindowSubclass(hwnd, panel_subclass_proc, PANEL_SUBCLASS_ID);
			g_panels.erase(std::remove_if(g_panels.begin(), g_panels.end(), [&](const Panel& p) { return p.hwnd == hwnd; }), g_panels.end());
			if (g_container) PostMessageW(g_container, WM_APP_RELAYOUT, 0, 0);
			break;
	}
	return DefSubclassProc(hwnd, msg, wp, lp);
}

//---------------------------------------------------------------------
//	取り込み・返却
//---------------------------------------------------------------------

static bool capture_panel(HWND h, const std::wstring& key) {
	Panel p;
	p.hwnd = h;
	p.key = key;
	p.title = window_text(h);
	p.style = GetWindowLongPtrW(h, GWL_STYLE);
	p.ex_style = GetWindowLongPtrW(h, GWL_EXSTYLE);
	p.owner = GetWindow(h, GW_OWNER);
	GetWindowRect(h, &p.float_rect);

	// 枠・タイトルを外して子窓化する
	LONG_PTR style = p.style;
	style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
	style |= WS_CHILD | WS_CLIPSIBLINGS;
	SetWindowLongPtrW(h, GWL_STYLE, style);
	SetWindowLongPtrW(h, GWL_EXSTYLE, p.ex_style & ~(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME));
	g_moving = true;
	SetParent(h, g_container);
	g_moving = false;
	if (GetParent(h) != g_container) {
		SetWindowLongPtrW(h, GWL_STYLE, p.style);
		SetWindowLongPtrW(h, GWL_EXSTYLE, p.ex_style);
		log(L"取り込み失敗 " + key + L" " + describe(h) + L" err=" + std::to_wstring(GetLastError()));
		return false;
	}
	p.assigned = { OFFSCREEN, OFFSCREEN, OFFSCREEN + 320, OFFSCREEN + 240 };
	p.visible = IsWindowVisible(h) != FALSE;
	g_panels.push_back(p);
	SetWindowSubclass(h, panel_subclass_proc, PANEL_SUBCLASS_ID, 0);
	g_moving = true;
	SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	g_moving = false;

	if (Node* leaf = find_leaf_with(g_root.get(), key)) {
		if (leaf->active.empty()) leaf->active = key;
	}
	log(L"取り込み " + key + L" '" + p.title + L"'");
	return true;
}

// 1枚を浮いた窓へ戻す
static void release_panel(const Panel& p) {
	HWND h = p.hwnd;
	if (!IsWindow(h)) return;
	RemoveWindowSubclass(h, panel_subclass_proc, PANEL_SUBCLASS_ID);
	bool visible = IsWindowVisible(h) != FALSE;
	g_moving = true;
	SetParent(h, nullptr);
	SetWindowLongPtrW(h, GWL_STYLE, (p.style & ~WS_VISIBLE) | (visible ? WS_VISIBLE : 0));
	SetWindowLongPtrW(h, GWL_EXSTYLE, p.ex_style);
	SetWindowLongPtrW(h, GWLP_HWNDPARENT, (LONG_PTR)p.owner);
	SetWindowPos(h, nullptr, p.float_rect.left, p.float_rect.top,
		p.float_rect.right - p.float_rect.left, p.float_rect.bottom - p.float_rect.top,
		SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	g_moving = false;
	log(L"返却 " + p.key);
}

static void release_all_panels() {
	for (auto it = g_panels.rbegin(); it != g_panels.rend(); ++it) release_panel(*it);
	g_panels.clear();
}

// 入れ物をメイン窓の中へ置く / 外す
static void attach_container() {
	SetWindowLongPtrW(g_container, GWL_STYLE, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	SetParent(g_container, g_main);
	position_container();
	ShowWindow(g_container, SW_SHOWNA);
}

static void detach_container() {
	ShowWindow(g_container, SW_HIDE);
	SetParent(g_container, nullptr);
	SetWindowLongPtrW(g_container, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN);
}

//---------------------------------------------------------------------
//	戻り先レイアウトの適用
//---------------------------------------------------------------------

// 標準の画面へ戻す。成功すれば true
static bool restore_host_layout() {
	if (!g_main || !IsWindow(g_main)) return false;
	ApplyResult r = host_apply_layout(g_main, g_container, g_settings.effective_return_layout());
	if (!r.ok() && !g_settings.return_layout.empty()) {
		log(L"指定の戻り先で戻せないため、自動保存した配置で戻す");
		r = host_apply_layout(g_main, g_container, AUTO_RETURN_LAYOUT);
	}
	return r.ok();
}

//---------------------------------------------------------------------
//	監視 (安全装置)
//---------------------------------------------------------------------

static void watch() {
	if (!g_active || g_busy) return;
	bool changed = false;

	// 本体側に持ち去られた・破棄されたパネルを外す
	for (auto it = g_panels.begin(); it != g_panels.end();) {
		if (!IsWindow(it->hwnd) || GetParent(it->hwnd) != g_container) {
			log(L"パネルが外部で移動・破棄された: " + it->key);
			if (IsWindow(it->hwnd)) RemoveWindowSubclass(it->hwnd, panel_subclass_proc, PANEL_SUBCLASS_ID);
			it = g_panels.erase(it);
			changed = true;
			continue;
		}
		bool v = IsWindowVisible(it->hwnd) != FALSE;
		if (v != it->visible) {
			it->visible = v;
			changed = true;
		}
		++it;
	}
	g_floats.erase(std::remove_if(g_floats.begin(), g_floats.end(), [](const FloatPanel& f) { return !IsWindow(f.hwnd); }), g_floats.end());

	// 「表示」メニュー等で新たに開かれたパネルは、浮いた窓のまま (取り込まない) メイン窓の中央へ出す
	for (HWND h : find_floating_panels(g_main)) {
		if (find_panel(h) || find_float(h)) continue;
		RECT r;
		GetWindowRect(h, &r);
		std::wstring key = match_slot(g_slots, { r.left, r.top });
		g_floats.push_back({ h, key });
		log(L"新しく開かれたパネルを浮いた窓で出す: " + (key.empty() ? L"(照合できず)" : key) + L" '" + window_text(h) + L"'");
		if (!key.empty()) move_float_to_center(h);
	}

	if (changed) {
		relayout();
		save_tree();
	}
}

//---------------------------------------------------------------------
//	開始・終了
//---------------------------------------------------------------------

static LRESULT CALLBACK main_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR);
static void exit_dock(bool apply_return);

static void subclass_main() {
	if (g_main_subclassed || !g_main) return;
	g_main_subclassed = SetWindowSubclass(g_main, main_subclass_proc, MAIN_SUBCLASS_ID, 0) != FALSE;
}

// 「表示」→「メディアエクスプローラー」の項目番号を集める (自由分割中は開かせないため)
static void collect_explorer_commands() {
	g_explorer_cmds.clear();
	HMENU bar = GetMenu(g_main);
	if (!bar) return;
	auto find_sub = [](HMENU m, const wchar_t* name) -> HMENU {
		int n = GetMenuItemCount(m);
		for (int i = 0; i < n; i++) {
			wchar_t buf[256] = {};
			GetMenuStringW(m, i, buf, 256, MF_BYPOSITION);
			if (wcscmp(buf, name) == 0) return GetSubMenu(m, i);
		}
		return nullptr;
	};
	HMENU view = find_sub(bar, L"表示");
	HMENU ex = view ? find_sub(view, L"メディアエクスプローラー") : nullptr;
	if (!ex) return;
	int n = GetMenuItemCount(ex);
	for (int i = 0; i < n; i++) {
		UINT id = GetMenuItemID(ex, i);
		if (id != (UINT)-1 && id != 0) g_explorer_cmds.insert(id);
	}
}

// 開始の途中で失敗した場合の後始末
static void abort_enter(const std::wstring& reason, bool interactive) {
	log(L"自由分割の開始を中止: " + reason);
	if (g_active) {
		KillTimer(g_container, TIMER_WATCH);
		release_all_panels();
		g_floats.clear();
		detach_container();
		g_active = false;
	}
	bool restored = restore_host_layout();
	g_settings.session_active = false;
	g_settings.save();
	if (interactive) {
		notify(L"自由分割を開始できませんでした。\n\n理由: " + reason +
			(restored ? L"\n\n標準の画面に戻しました。" : L"\n\n「表示」→「レイアウト」から普段の配置を選んで戻してください。"));
	}
}

static bool enter_dock(bool interactive) {
	if (g_active) return true;
	if (g_busy) {
		log(L"処理中のため開始要求を見送る");
		return false;
	}
	BusyGuard busy;

	g_main = find_main_window();
	if (!g_main) {
		log(L"メインウィンドウが見つからないため開始できない");
		return false;
	}
	subclass_main();
	g_scale = dpi_scale(g_main);
	update_font();

	// 本体の更新を検知したら、利用者に確認する (安全装置)
	if (g_settings.verified_version != g_host_version) {
		wchar_t msg[512];
		if (g_settings.verified_version == 0) {
			swprintf_s(msg, L"TP_GUIdocker の自由分割を開始します。\n\n"
				L"本体の画面に手を加える機能のため、不具合が起きた場合は「編集」→「プラグイン」→「TP_GUIdocker」→「自由分割を終了」で元に戻せます。\n\n"
				L"開始しますか？ (本体バージョン %lu)", g_host_version);
		} else {
			swprintf_s(msg, L"AviUtl2 本体が更新されています (%lu → %lu)。\n\n"
				L"TP_GUIdocker はこのバージョンで未確認です。不具合が起きる可能性があります。\n"
				L"自由分割を開始しますか？", g_settings.verified_version, g_host_version);
		}
		if (MessageBoxW(g_main, msg, TPGD_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES) {
			g_settings.resume = false;
			g_settings.save();
			log(L"利用者が開始を取り止めた");
			return false;
		}
		g_settings.verified_version = g_host_version;
		g_settings.save();
	}

	log(L"===== 自由分割を開始 =====");

	// 前回が異常終了 (自由分割中の印が残っている) なら、まず本体の配置を正常に戻す
	if (g_settings.session_active) {
		log(L"前回の異常終了を検知。先に本体の配置を戻す");
		restore_host_layout();
		g_settings.session_active = false;
		g_settings.save();
	}

	// 1. 現在の配置を本体自身に保存させる (戻り先)
	if (!host_create_layout(g_main, g_container, AUTO_RETURN_LAYOUT)) {
		abort_enter(L"現在の配置を本体に保存させることができませんでした。", interactive);
		return false;
	}

	// 2. 自由分割用レイアウトの枠を本体に作らせる (メニューに無い場合のみ)
	if (!find_menu_command(g_main, { L"表示", L"レイアウト", DOCK_LAYOUT })) {
		if (!host_create_layout(g_main, g_container, DOCK_LAYOUT)) {
			abort_enter(L"自由分割用のレイアウトを本体に登録できませんでした。", interactive);
			return false;
		}
	}

	// 3. 分割木を用意する (古い形式の分割木は使わない)
	if (!g_root) {
		if (g_settings.tree_version == TREE_VERSION) g_root = parse_tree(g_settings.tree);
		if (!g_root) {
			g_root = default_tree();
			log(L"分割木を初期配置で作成");
		}
	}

	// 4. 自由分割用レイアウトの中身を生成する
	IniDoc base;
	if (!base.load(layout_path(AUTO_RETURN_LAYOUT))) {
		abort_enter(L"保存した配置を読み込めませんでした。", interactive);
		return false;
	}
	auto is_visible = [](const std::wstring& key) {
		return find_leaf_with(g_root.get(), key) != nullptr || g_settings.floating.count(key) > 0;
	};
	RECT work = {};
	MONITORINFO mi = { sizeof(mi) };
	if (GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTOPRIMARY), &mi)) work = mi.rcWork;
	POINT origin = { work.left + scaled(40), work.top + scaled(40) };
	DockLayoutPlan plan = build_dock_layout(base, is_visible, origin, scaled(FLOAT_W), scaled(FLOAT_H));
	if (!plan.doc.save(layout_path(DOCK_LAYOUT))) {
		abort_enter(L"自由分割用のレイアウトを書き出せませんでした。", interactive);
		return false;
	}
	g_slots = plan.slots;
	log(L"自由分割用レイアウトを生成 (切り離し " + std::to_wstring(plan.slots.size()) + L" 件, 表示 " +
		std::to_wstring(plan.visible_keys.size()) + L" 件)");

	g_settings.session_active = true;
	g_settings.save();

	// 5. 適用する (本体のエラー表示を監視)
	ApplyResult applied = host_apply_layout(g_main, g_container, DOCK_LAYOUT);
	if (!applied.ok()) {
		abort_enter(applied.in_menu ? L"本体がレイアウトの読み込みに失敗しました。" : L"自由分割用のレイアウトがメニューに見つかりません。", interactive);
		return false;
	}

	// 6. 照合: 表示すべきパネルが全て指定位置に現れたこと
	std::vector<std::pair<std::wstring, HWND>> found;
	for (HWND h : find_floating_panels(g_main)) {
		RECT r;
		GetWindowRect(h, &r);
		std::wstring key = match_slot(g_slots, { r.left, r.top });
		if (key.empty()) {
			log(L"照合できない切り離し窓: '" + window_text(h) + L"'");
			continue;
		}
		found.push_back({ key, h });
	}
	std::wstring missing;
	for (auto& k : plan.visible_keys) {
		bool ok = std::any_of(found.begin(), found.end(), [&](auto& f) { return f.first == k; });
		if (!ok) missing += L" " + k;
	}
	if (!missing.empty()) {
		abort_enter(L"一部のパネルが本体に切り離されませんでした (" + missing + L" )。", interactive);
		return false;
	}

	// 7. 入れ物を置き、分割木のパネルを取り込む。それ以外は浮いた窓として記憶した位置へ
	g_active = true;
	attach_container();
	for (auto& [key, h] : found) {
		if (find_leaf_with(g_root.get(), key)) {
			if (!capture_panel(h, key)) {
				abort_enter(L"パネル (" + key + L") を取り込めませんでした。", interactive);
				return false;
			}
		} else {
			g_floats.push_back({ h, key });
			auto it = g_settings.floating.find(key);
			if (it != g_settings.floating.end()) {
				const RECT& fr = it->second;
				SetWindowPos(h, nullptr, fr.left, fr.top, fr.right - fr.left, fr.bottom - fr.top, SWP_NOZORDER | SWP_NOACTIVATE);
			} else {
				move_float_to_center(h);
			}
		}
	}
	collect_explorer_commands();
	relayout();
	save_tree();
	SetTimer(g_container, TIMER_WATCH, 300, nullptr);
	log(L"自由分割を開始した (取り込み " + std::to_wstring(g_panels.size()) + L" 枚, 浮いた窓 " + std::to_wstring(g_floats.size()) + L" 枚)");
	return true;
}

static void exit_dock(bool apply_return) {
	if (!g_active) return;
	BusyGuard busy;
	log(L"===== 自由分割を終了 =====");
	KillTimer(g_container, TIMER_WATCH);
	save_tree();

	// 浮いた窓のまま使っていたパネルの位置を記憶する (次回も浮いた窓で復元する)
	g_settings.floating.clear();
	for (auto& f : g_floats) {
		if (f.key.empty() || !IsWindow(f.hwnd) || !IsWindowVisible(f.hwnd)) continue;
		RECT r;
		GetWindowRect(f.hwnd, &r);
		g_settings.floating[f.key] = r;
	}
	g_floats.clear();

	release_all_panels();
	detach_container();
	g_active = false;
	g_drag = nullptr;

	if (apply_return && !restore_host_layout()) {
		log(L"標準の画面へ戻す際に本体がエラーを出した");
	}
	g_settings.session_active = false;
	g_settings.save();
}

//---------------------------------------------------------------------
//	メイン窓の監視
//---------------------------------------------------------------------

static LRESULT CALLBACK main_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	switch (msg) {
		case WM_SIZE: {
			LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
			if (g_active) position_container();
			return r;
		}
		case WM_DPICHANGED: {
			LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
			g_scale = dpi_scale(hwnd);
			update_font();
			if (g_active) { position_container(); relayout(); }
			return r;
		}
		case WM_COMMAND:
			// メディアエクスプローラーは切り離せない (本体の仕様) ため、自由分割中は開かせない
			if (g_active && HIWORD(wp) == 0 && g_explorer_cmds.count(LOWORD(wp))) {
				notify(L"メディアエクスプローラーは、自由分割中は使えません。\n"
					L"「編集」→「プラグイン」→「TP_GUIdocker」→「自由分割を終了」で標準の画面に戻すと使えます。", MB_ICONINFORMATION);
				return 0;
			}
			break;
		case WM_CLOSE: {
			// 終了前に標準の画面へ戻し、本体がその状態を保存するようにする
			// (プラグインを外しても標準の画面で起動できる状態を保つ)
			bool was_active = g_active;
			g_settings.resume = was_active;
			g_settings.save();
			log(L"本体の終了要求 (自由分割中=" + std::to_wstring(was_active) + L")");
			if (was_active) exit_dock(true);
			LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
			// 保存確認などで終了が取り消された場合は、少し待って自由分割へ戻る
			if (was_active && IsWindow(hwnd) && !g_main_destroying && g_container) {
				SetTimer(g_container, TIMER_REENTER, 1500, nullptr);
			}
			return r;
		}
		case WM_DESTROY:
			g_main_destroying = true;
			log(L"本体のメイン窓が破棄される");
			if (g_active) exit_dock(false);
			break;
		case WM_NCDESTROY:
			RemoveWindowSubclass(hwnd, main_subclass_proc, MAIN_SUBCLASS_ID);
			g_main_subclassed = false;
			break;
	}
	return DefSubclassProc(hwnd, msg, wp, lp);
}

//---------------------------------------------------------------------
//	メニュー項目の処理
//---------------------------------------------------------------------

static void choose_return_layout() {
	HWND owner = g_main ? g_main : find_main_window();
	if (!owner) return;
	HMENU menu = CreatePopupMenu();
	auto names = list_layouts();
	names.erase(std::remove_if(names.begin(), names.end(), [](const std::wstring& n) {
		return n.rfind(TPGD_NAME, 0) == 0;
	}), names.end());
	AppendMenuW(menu, MF_STRING | (g_settings.return_layout.empty() ? MF_CHECKED : 0), 1, L"自動 (自由分割に入る直前の配置)");
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	for (size_t i = 0; i < names.size(); i++) {
		AppendMenuW(menu, MF_STRING | (g_settings.return_layout == names[i] ? MF_CHECKED : 0), 100 + i, names[i].c_str());
	}
	POINT pt;
	GetCursorPos(&pt);
	SetForegroundWindow(owner);
	UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, owner, nullptr);
	DestroyMenu(menu);
	if (cmd == 1) {
		g_settings.return_layout.clear();
	} else if (cmd >= 100 && cmd < 100 + names.size()) {
		g_settings.return_layout = names[cmd - 100];
	} else {
		return;
	}
	g_settings.save();
	log(L"戻り先レイアウト: " + g_settings.effective_return_layout());
}

static void reset_tree() {
	g_root = default_tree();
	if (g_active) {
		// 初期配置に含まれないパネルは、浮いた窓へ戻す
		for (auto it = g_panels.begin(); it != g_panels.end();) {
			if (find_leaf_with(g_root.get(), it->key)) { ++it; continue; }
			release_panel(*it);
			g_floats.push_back({ it->hwnd, it->key });
			move_float_to_center(it->hwnd);
			it = g_panels.erase(it);
		}
		for (auto& p : g_panels) {
			if (Node* l = find_leaf_with(g_root.get(), p.key)) if (l->active.empty()) l->active = p.key;
		}
		relayout();
	}
	save_tree();
	log(L"配置を初期状態に戻した");
}

//---------------------------------------------------------------------
//	入れ物の窓
//---------------------------------------------------------------------

static Node* hit_splitter(Node* n, POINT pt) {
	if (!n || !n->is_split) return nullptr;
	if (PtInRect(&n->splitter, pt)) return n;
	if (auto h = hit_splitter(n->a.get(), pt)) return h;
	return hit_splitter(n->b.get(), pt);
}

static void paint(HWND hwnd) {
	PAINTSTRUCT ps;
	HDC dc = BeginPaint(hwnd, &ps);
	RECT rc;
	GetClientRect(hwnd, &rc);

	COLORREF c_sep = style_color("WindowSeparator", RGB(0, 0, 0));
	COLORREF c_bg = style_color("Background", RGB(0x20, 0x20, 0x20));
	COLORREF c_tabbar = style_color("Grouping", RGB(0x38, 0x38, 0x38));
	COLORREF c_tab_sel = style_color("GroupingSelect", RGB(0x48, 0x48, 0x48));
	COLORREF c_tab_line = style_color("GroupingSeparator", RGB(0x4c, 0x4c, 0x4c));
	COLORREF c_text = style_color("Text", RGB(0xff, 0xff, 0xff));
	COLORREF c_text_dis = style_color("TextDisable", RGB(0x90, 0x90, 0x90));

	auto fill = [&](const RECT& r, COLORREF c) {
		HBRUSH b = CreateSolidBrush(c);
		FillRect(dc, &r, b);
		DeleteObject(b);
	};

	fill(rc, c_sep);
	HGDIOBJ old = SelectObject(dc, g_font);
	SetBkMode(dc, TRANSPARENT);

	if (g_root) {
		for_each_leaf(g_root.get(), [&](Node* leaf) {
			fill(leaf->tabs, c_tabbar);
			if (leaf->active.empty()) {
				fill(leaf->body, c_bg);
				SetTextColor(dc, c_text_dis);
				RECT r = leaf->body;
				DrawTextW(dc, L"(パネルなし)", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			}
		});
		for (auto& t : g_tab_hits) {
			bool sel = (t.leaf->active == t.key);
			if (sel) fill(t.rect, c_tab_sel);
			RECT line = { t.rect.right - 1, t.rect.top + scaled(4), t.rect.right, t.rect.bottom - scaled(4) };
			fill(line, c_tab_line);
			std::wstring title;
			for (auto& p : g_panels) if (p.key == t.key) { title = p.title; break; }
			SetTextColor(dc, sel ? c_text : c_text_dis);
			RECT r = t.rect;
			DrawTextW(dc, title.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
	SelectObject(dc, old);
	EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK container_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_APP_STARTUP:
			SetTimer(hwnd, TIMER_STARTUP, 800, nullptr);
			return 0;
		case WM_APP_ENTER:
			enter_dock(true);
			return 0;
		case WM_APP_EXIT:
			if (g_busy) return 0;
			g_settings.resume = false;
			g_settings.save();
			exit_dock(true);
			return 0;
		case WM_APP_RESET_TREE:
			if (!g_busy) reset_tree();
			return 0;
		case WM_APP_CHOOSE_RETURN:
			choose_return_layout();
			return 0;
		case WM_APP_RELAYOUT:
			if (g_active && !g_busy) relayout();
			return 0;

		case WM_TIMER:
			if (wp == TIMER_HOST_OPS) {
				host_ops_on_timer();
			} else if (wp == TIMER_STARTUP) {
				HWND main = find_main_window();
				if (!main && ++g_startup_tries < 25) return 0; // 本体の準備待ち
				KillTimer(hwnd, TIMER_STARTUP);
				g_main = main;
				subclass_main();
				// 前回が自由分割で終了、または異常終了した場合は自由分割で始める
				if (g_settings.resume || g_settings.session_active) {
					log(L"前回の状態を引き継いで自由分割を開始 (resume=" + std::to_wstring(g_settings.resume) +
						L" session_active=" + std::to_wstring(g_settings.session_active) + L")");
					enter_dock(true);
				}
			} else if (wp == TIMER_WATCH) {
				watch();
			} else if (wp == TIMER_REENTER) {
				KillTimer(hwnd, TIMER_REENTER);
				if (!g_main_destroying && g_main && IsWindow(g_main)) {
					log(L"本体の終了が取り消されたため、自由分割へ戻る");
					enter_dock(false);
				}
			}
			return 0;

		case WM_SIZE:
			relayout();
			return 0;

		case WM_SETCURSOR:
			if (LOWORD(lp) == HTCLIENT && g_root) {
				POINT pt;
				GetCursorPos(&pt);
				ScreenToClient(hwnd, &pt);
				Node* s = g_drag ? g_drag : hit_splitter(g_root.get(), pt);
				if (s) {
					SetCursor(LoadCursorW(nullptr, s->vertical ? IDC_SIZENS : IDC_SIZEWE));
					return TRUE;
				}
			}
			break;

		case WM_LBUTTONDOWN: {
			POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
			if (!g_root) return 0;
			if (Node* s = hit_splitter(g_root.get(), pt)) {
				g_drag = s;
				SetCapture(hwnd);
				return 0;
			}
			for (auto& t : g_tab_hits) {
				if (PtInRect(&t.rect, pt)) {
					if (t.leaf->active != t.key) {
						t.leaf->active = t.key;
						relayout();
						save_tree();
					}
					return 0;
				}
			}
			return 0;
		}
		case WM_MOUSEMOVE:
			if (g_drag) {
				POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
				const RECT& r = g_drag->rect;
				int total = (g_drag->vertical ? (r.bottom - r.top) : (r.right - r.left)) - gap_size();
				int pos = g_drag->vertical ? (pt.y - r.top) : (pt.x - r.left);
				int lo = scaled(MIN_PANE), hi = total - scaled(MIN_PANE);
				if (total > 0 && hi > lo) {
					pos = std::clamp(pos, lo, hi);
					g_drag->ratio = (double)pos / total;
					relayout();
					UpdateWindow(hwnd);
				}
			}
			return 0;
		case WM_LBUTTONUP:
			if (g_drag) ReleaseCapture();
			return 0;
		case WM_CAPTURECHANGED:
			if (g_drag) {
				g_drag = nullptr;
				save_tree();
			}
			return 0;

		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
			paint(hwnd);
			return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

//---------------------------------------------------------------------
//	公開関数
//---------------------------------------------------------------------

bool dock_init() {
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpszClassName = CONTAINER_CLASS;
	wc.lpfnWndProc = container_proc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	if (!RegisterClassExW(&wc)) {
		log(L"入れ物の窓クラスを登録できない");
		return false;
	}
	// メイン窓がまだ無いため、いったん非表示の独立窓として作る (開始時にメイン窓の子にする)
	g_container = CreateWindowExW(WS_EX_TOOLWINDOW, CONTAINER_CLASS, TPGD_NAME, WS_POPUP | WS_CLIPCHILDREN,
		0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!g_container) {
		log(L"入れ物の窓を作れない");
		return false;
	}
	g_scale = dpi_scale(nullptr);
	update_font();
	return true;
}

void dock_cleanup_legacy() {
	// 開発中の診断で作った試験用レイアウトを削除する
	WIN32_FIND_DATAW fd;
	std::wstring dir = layout_dir();
	HANDLE h = FindFirstFileW((dir + L"\\" TPGD_NAME L"_試験*.layout").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		std::wstring path = dir + L"\\" + fd.cFileName;
		if (DeleteFileW(path.c_str())) log(L"不要になった試験用レイアウトを削除: " + std::wstring(fd.cFileName));
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

void dock_request_startup() { if (g_container) PostMessageW(g_container, WM_APP_STARTUP, 0, 0); }
void dock_request_enter() { if (g_container) PostMessageW(g_container, WM_APP_ENTER, 0, 0); }
void dock_request_exit() { if (g_container) PostMessageW(g_container, WM_APP_EXIT, 0, 0); }
void dock_request_reset_tree() { if (g_container) PostMessageW(g_container, WM_APP_RESET_TREE, 0, 0); }
void dock_request_choose_return_layout() { if (g_container) PostMessageW(g_container, WM_APP_CHOOSE_RETURN, 0, 0); }

void dock_shutdown() {
	if (g_active) exit_dock(g_main && IsWindow(g_main) && !g_main_destroying);
	if (g_main_subclassed && g_main && IsWindow(g_main)) RemoveWindowSubclass(g_main, main_subclass_proc, MAIN_SUBCLASS_ID);
	if (g_container) DestroyWindow(g_container);
	g_container = nullptr;
	if (g_font) DeleteObject(g_font);
	g_font = nullptr;
}

} // namespace tpgd
