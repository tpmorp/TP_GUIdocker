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
#include "menu_find.h"
#include "settings.h"
#include "overlay.h"
#include "prompt.h"
#include "tree.h"
#include "ui_spec.h"

#include <algorithm>
#include <memory>
#include <set>

#include <commdlg.h>
// GDI+ は min/max を必要とする (NOMINMAX のため std の版を見せる)
namespace Gdiplus {
using std::max;
using std::min;
}
#include <objidl.h>
#pragma warning(push)
#pragma warning(disable : 4458)  // Windows SDK の GDI+ ヘッダー自身が出す警告
#include <gdiplus.h>
#pragma warning(pop)

namespace tpgd {

//---------------------------------------------------------------------
//	定数
//---------------------------------------------------------------------

#define CONTAINER_CLASS TPGD_NAME L"Container"

enum : UINT {
	WM_APP_STARTUP = WM_APP + 1,
	WM_APP_TOGGLE,
	WM_APP_RESET_TREE,
	WM_APP_CHOOSE_RETURN,
	WM_APP_RELAYOUT,
	WM_APP_TOGGLE_DROP_STYLE,
	WM_APP_DOCK_FLOAT,        // 浮いた窓を区画へ入れる (移動の操作が終わってから行う)
	WM_APP_WS_SWITCH,         // ワークスペースの切り替え (wParam: 番号)
	WM_APP_MAXIMIZE,          // カーソルの下のパネルを最大化・元に戻す
	WM_APP_TOGGLE_LOCK,       // レイアウトのロックの切り替え
	WM_APP_EXPORT_SAMPLES,    // 見本の書き出し
	WM_APP_THEME_COLOR,       // テーマの色を選ぶ (wParam: 1 なら本体設定に戻す)
};

enum : UINT_PTR {
	TIMER_STARTUP = 1,
	TIMER_WATCH = 2,
	TIMER_REENTER = 3,
	TIMER_STARTUP_ENTER = 4,
	TIMER_LONG_PRESS = 5,     // タブの長押しの判定
	TIMER_DRAG_POLL = 6,      // ドラッグ中の Esc キーの監視
};

constexpr UINT_PTR PANEL_SUBCLASS_ID = 0x7447;
constexpr UINT_PTR MAIN_SUBCLASS_ID = 0x7448;
constexpr UINT_PTR FLOAT_SUBCLASS_ID = 0x7449;
constexpr int OFFSCREEN = -30000;   // 非表示タブのパネルを置く座標

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
static int g_menu_wait_tries = 0;   // 起動時の有効化で、本体のメニューの中身を待った回数
static bool g_cover = false;           // 起動時の「準備中」の覆いを表示中

static std::vector<Panel> g_panels;
static std::vector<FloatPanel> g_floats;
static std::unique_ptr<Node> g_root;
static std::vector<TabHit> g_tab_hits;
static std::vector<LayoutSlot> g_slots;   // 照合用の仮位置
static std::set<UINT> g_explorer_cmds;    // 「表示」→「メディアエクスプローラー」の項目番号

// 境界線のドラッグ
static Node* g_drag = nullptr;

// タブ・浮いた窓のドラッグ (ドッキング)
enum class DragState { None, Pressed, Armed, Dragging };
struct DragContext {
	DragState state = DragState::None;
	std::wstring key;          // 動かしているパネル
	std::wstring title;
	POINT down = {};           // 押した位置 (画面座標)
	HWND float_hwnd = nullptr; // 浮いた窓を動かしている場合 (タブの場合は nullptr)
};
static DragContext g_dd;

// 落とし先
struct DropTarget {
	bool inside = false;       // 入れ物の中か
	Node* leaf = nullptr;
	DropZone zone = DropZone::None;
	int tab_index = -1;
};
static DropTarget g_target;
static DropTarget g_pending_dock;   // 浮いた窓を入れる落とし先 (移動の操作が終わるまで預かる)
static DropOverlay g_overlay;
static DragGhost g_ghost;

// ワークスペースの帯
struct WsHit {
	int index;
	RECT rect;
};
static RECT g_bar = {};                  // 帯の全体
static std::vector<WsHit> g_ws_hits;     // ワークスペースのボタン
static RECT g_ws_add = {};               // 「＋」
static RECT g_lock_rect = {};            // ロックの表示 (クリックで切り替え)

// 最大化中のパネル (空なら最大化していない)
static std::wstring g_max_key;
// 最後に操作したパネル (ショートカットキーで最大化する対象の予備)
static std::wstring g_last_key;

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

// 境界線の太さ: 本体設定 (WindowSeparatorSize) に、指示書の割合 (SEPARATOR %) を掛ける
static int gap_size() {
	int host = style_size("WindowSeparatorSize", 7, g_scale);
	return std::max(1, (int)(host * ui::SEPARATOR / 100.0 + 0.5));
}

// 本体のメニューバー (「ファイル」「編集」の帯) の実際の高さ
static int menu_bar_height() {
	if (g_main) {
		MENUBARINFO mbi = { sizeof(mbi) };
		if (GetMenuBarInfo(g_main, OBJID_MENU, 0, &mbi)) {
			int h = mbi.rcBar.bottom - mbi.rcBar.top;
			if (h > 0) return h;
		}
	}
	return GetSystemMetricsForDpi(SM_CYMENU, (UINT)(96 * g_scale + 0.5));
}

// 帯の高さ: 指示書の値 (-1: メニューバーと同じ / 0: 本体設定 / 正の数: 96dpi 換算の画素)
static int bar_size(int spec) {
	if (spec < 0) return menu_bar_height();
	if (spec == 0) return style_size("GroupTabHeight", 24, g_scale);
	return scaled(spec);
}
static int tab_height() { return bar_size(ui::TAB_HEIGHT); }
static int ws_bar_height() { return bar_size(ui::WS_BAR_HEIGHT); }
static int footer_height() { return style_size("FooterHeight", 24, g_scale); }

// 帯の文字
// 書体は指示書の FONT (空ならメニューバーと同じ書体)、大きさはメニューバーの文字と同じ (今の表示倍率のもの)。
// パソコンに無い書体は、DLL と同じ場所の「TP_GUIdocker_fonts」フォルダに置けば、プラグインの中だけで読み込んで使う
static std::vector<std::wstring> g_private_fonts;

static int CALLBACK font_found_proc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lp) {
	*(bool*)lp = true;
	return 0;
}

static bool font_exists(const wchar_t* face) {
	LOGFONTW lf = {};
	lf.lfCharSet = DEFAULT_CHARSET;
	wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
	bool found = false;
	HDC dc = GetDC(nullptr);
	EnumFontFamiliesExW(dc, &lf, font_found_proc, (LPARAM)&found, 0);
	ReleaseDC(nullptr, dc);
	return found;
}

// 「TP_GUIdocker_fonts」フォルダの書体を、このプロセスの中だけで使えるようにする
static void load_private_fonts() {
	std::wstring dir = module_dir() + L"\\" TPGD_NAME L"_fonts";
	for (const wchar_t* ext : { L"*.ttf", L"*.otf", L"*.ttc" }) {
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW((dir + L"\\" + ext).c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) continue;
		do {
			std::wstring path = dir + L"\\" + fd.cFileName;
			if (AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr) > 0) {
				g_private_fonts.push_back(path);
				log(L"書体を読み込んだ: " + path);
			} else {
				log(L"書体を読み込めなかった: " + path);
			}
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
}

static void unload_private_fonts() {
	for (auto& p : g_private_fonts) RemoveFontResourceExW(p.c_str(), FR_PRIVATE, nullptr);
	g_private_fonts.clear();
}

static void update_font() {
	if (g_font) DeleteObject(g_font);
	g_font = nullptr;
	NONCLIENTMETRICSW ncm = { sizeof(ncm) };
	UINT dpi = (UINT)(96 * g_scale + 0.5);
	if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi)) {
		ncm.lfMenuFont.lfQuality = CLEARTYPE_QUALITY;
		if (*ui::FONT) {
			if (font_exists(ui::FONT)) {
				wcsncpy_s(ncm.lfMenuFont.lfFaceName, ui::FONT, _TRUNCATE);
				ncm.lfMenuFont.lfWeight = FW_NORMAL;
				ncm.lfMenuFont.lfItalic = FALSE;
				ncm.lfMenuFont.lfCharSet = DEFAULT_CHARSET;
			} else {
				static bool warned = false;
				if (!warned) log(std::wstring(L"書体が見つからないため、メニューバーと同じ書体を使う: ") + ui::FONT);
				warned = true;
			}
		}
		g_font = CreateFontIndirectW(&ncm.lfMenuFont);
	}
	if (!g_font) {
		// 取得できない場合は本体の設定 (style.conf の Control) を使う
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

// 選択中のワークスペースのボタンの色 (利用者が選んだ色、無ければ本体設定)
static COLORREF theme_color() {
	if (g_settings.theme_color >= 0) return (COLORREF)g_settings.theme_color;
	return style_color("ButtonBodySelect", RGB(0x70, 0x70, 0xc0));
}

// ドラッグ中の予告・選ばれている扇の色 (テーマの色を選んでいればそれを使う)
static COLORREF highlight_color() {
	return g_settings.theme_color >= 0 ? (COLORREF)g_settings.theme_color : ui::DROP_HIGHLIGHT;
}

// テーマの色を選ぶ (reset: 本体設定に戻す)
static void choose_theme_color(bool reset) {
	if (reset) {
		g_settings.theme_color = -1;
	} else {
		static COLORREF custom[16] = {};
		CHOOSECOLORW cc = { sizeof(cc) };
		cc.hwndOwner = g_main ? g_main : find_main_window();
		cc.rgbResult = theme_color();
		cc.lpCustColors = custom;
		cc.Flags = CC_FULLOPEN | CC_RGBINIT;
		if (!ChooseColorW(&cc)) return;
		g_settings.theme_color = (int)cc.rgbResult;
	}
	g_settings.save();
	if (g_container) InvalidateRect(g_container, nullptr, TRUE);
}

// 錠前の絵の説明 (カーソルを乗せると出る)
static HWND g_tooltip = nullptr;
static ULONG_PTR g_gdiplus_token = 0;
static void update_lock_tooltip() {
	if (!g_tooltip) return;
	TOOLINFOW ti = { sizeof(ti) };
	ti.hwnd = g_container;
	ti.uId = 1;
	ti.rect = g_lock_rect;
	ti.lpszText = (LPWSTR)(g_settings.locked ? ui::LOCK_BADGE : ui::UNLOCK_BADGE);
	SendMessageW(g_tooltip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
	SendMessageW(g_tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}

static void create_lock_tooltip() {
	g_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, g_container, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!g_tooltip) return;
	TOOLINFOW ti = { sizeof(ti) };
	ti.uFlags = TTF_SUBCLASS;
	ti.hwnd = g_container;
	ti.uId = 1;
	ti.lpszText = (LPWSTR)ui::UNLOCK_BADGE;
	SendMessageW(g_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

// 錠前の絵を描く (locked: 閉じた錠前 / 開いた錠前)
static void draw_lock_icon(HDC dc, const RECT& r, bool locked, COLORREF color) {
	Gdiplus::Graphics g(dc);
	g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	float h = (float)(r.bottom - r.top);
	float s = h * 0.66f;                                   // 絵の大きさ
	float cx = (r.left + r.right) / 2.0f, top = r.top + (h - s) / 2.0f;
	Gdiplus::Color col(255, GetRValue(color), GetGValue(color), GetBValue(color));

	// 本体 (角の丸い四角)
	float bw = s * 0.74f, bh = s * 0.52f, bx = cx - bw / 2, by = top + s - bh;
	float rad = s * 0.1f;
	Gdiplus::GraphicsPath body;
	body.AddArc(bx, by, rad * 2, rad * 2, 180, 90);
	body.AddArc(bx + bw - rad * 2, by, rad * 2, rad * 2, 270, 90);
	body.AddArc(bx + bw - rad * 2, by + bh - rad * 2, rad * 2, rad * 2, 0, 90);
	body.AddArc(bx, by + bh - rad * 2, rad * 2, rad * 2, 90, 90);
	body.CloseFigure();
	Gdiplus::SolidBrush brush(col);
	g.FillPath(&brush, &body);

	// つる (U 字の輪)。開いた錠前は右へずらし、片側の脚を本体から外す
	float pen_w = s * 0.11f;
	Gdiplus::Pen pen(col, pen_w);
	pen.SetStartCap(Gdiplus::LineCapRound);
	pen.SetEndCap(Gdiplus::LineCapRound);
	float sw = bw * 0.62f, sh = s * 0.58f;
	float sx = cx - sw / 2 + (locked ? 0 : bw * 0.32f);
	float sy = top + (locked ? 0 : -s * 0.06f);
	float leg_bottom = by + pen_w * 0.5f;
	Gdiplus::GraphicsPath shackle;
	shackle.AddLine(sx, leg_bottom, sx, sy + sw / 2);
	shackle.AddArc(sx, sy, sw, sw, 180, 180);
	shackle.AddLine(sx + sw, sy + sw / 2, sx + sw, locked ? leg_bottom : sy + sh * 0.62f);
	g.DrawPath(&pen, &shackle);

	// 鍵穴
	Gdiplus::SolidBrush hole(Gdiplus::Color(255, 40, 40, 40));
	float kr = s * 0.07f;
	g.FillEllipse(&hole, cx - kr, by + bh * 0.42f - kr, kr * 2, kr * 2);
}

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

	// 上端にワークスペースの帯、その下に区画
	g_bar = { rc.left, rc.top, rc.right, rc.top + ws_bar_height() };
	RECT area = { rc.left, g_bar.bottom + gap_size(), rc.right, rc.bottom };
	if (area.bottom < area.top) area.bottom = area.top;
	compute_layout(g_root.get(), area, gap_size(), tab_height());

	// 最大化: そのパネルの葉だけを区画の全面に置き、他の葉は隠す
	if (!g_max_key.empty() && !is_present(g_max_key)) g_max_key.clear();
	if (!g_max_key.empty()) {
		Node* max_leaf = find_leaf_with(g_root.get(), g_max_key);
		if (!max_leaf) {
			g_max_key.clear();
		} else {
			max_leaf->active = g_max_key;
			for_each_leaf(g_root.get(), [&](Node* l) {
				if (l != max_leaf) l->rect = l->tabs = l->body = RECT{};
			});
			compute_layout(max_leaf, area, gap_size(), tab_height());
		}
	}

	HDC dc = GetDC(g_container);
	HGDIOBJ old = SelectObject(dc, g_font);

	// ワークスペースの帯のボタン
	g_ws_hits.clear();
	{
		int pad = scaled(ui::WS_BUTTON_PADDING);
		int x = g_bar.left;
		auto measure = [&](const std::wstring& s) {
			SIZE sz = {};
			GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
			return (int)sz.cx + pad * 2;
		};
		for (int i = 0; i < (int)g_settings.workspaces.size(); i++) {
			int w = measure(g_settings.workspaces[i].name);
			g_ws_hits.push_back({ i, { x, g_bar.top, x + w, g_bar.bottom } });
			x += w;
		}
		int add_w = measure(ui::WS_ADD);
		g_ws_add = { x, g_bar.top, x + add_w, g_bar.bottom };
		// 錠前の絵のボタン (帯の高さに合わせた横長の枠)
		int lock_w = (g_bar.bottom - g_bar.top) * 8 / 5;
		g_lock_rect = { g_bar.right - lock_w, g_bar.top, g_bar.right, g_bar.bottom };
		update_lock_tooltip();
	}

	// 各葉の表示中タブを決め、タブの当たり判定を作る
	g_tab_hits.clear();
	int pad = scaled(ui::TAB_PADDING);
	for_each_leaf(g_root.get(), [&](Node* leaf) {
		if (IsRectEmpty(&leaf->rect)) return;  // 最大化中に隠している葉
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
		if (leaf && leaf->active == p.key && !IsRectEmpty(&leaf->body)) {
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

// 起動時、自由分割の準備が整うまで本体の通常の画面を覆い隠す
static void show_cover() {
	if (g_active || g_cover) return;
	g_cover = true;
	attach_container();
	InvalidateRect(g_container, nullptr, TRUE);
	UpdateWindow(g_container);
}

static void hide_cover() {
	if (!g_cover) return;
	g_cover = false;
	// 自由分割に入れていれば入れ物はそのまま使う。入れなかった場合は外す
	if (!g_active) detach_container();
	else InvalidateRect(g_container, nullptr, TRUE);
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
//	浮いた窓の管理
//---------------------------------------------------------------------

static LRESULT CALLBACK float_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR);

static void add_float(HWND h, const std::wstring& key) {
	if (find_float(h)) return;
	g_floats.push_back({ h, key });
	SetWindowSubclass(h, float_subclass_proc, FLOAT_SUBCLASS_ID, 0);
}

static void remove_float(HWND h) {
	if (IsWindow(h)) RemoveWindowSubclass(h, float_subclass_proc, FLOAT_SUBCLASS_ID);
	g_floats.erase(std::remove_if(g_floats.begin(), g_floats.end(), [&](const FloatPanel& f) { return f.hwnd == h; }), g_floats.end());
}

static void clear_floats() {
	for (auto& f : g_floats) {
		if (IsWindow(f.hwnd)) RemoveWindowSubclass(f.hwnd, float_subclass_proc, FLOAT_SUBCLASS_ID);
	}
	g_floats.clear();
}

//---------------------------------------------------------------------
//	ドッキング: 落とし先の判定と表示
//---------------------------------------------------------------------

static int drop_style() {
	return g_settings.drop_style >= 0 ? g_settings.drop_style : ui::DROP_STYLE;
}

// 十字ボタン式: 葉の本体の中央にドーナツ型のボタンを置く (画面座標)
static CompassDonut compass_of(const Node* leaf) {
	RECT b = leaf->body;
	MapWindowPoints(g_container, nullptr, (POINT*)&b, 2);
	CompassDonut c;
	c.center = { (b.left + b.right) / 2, (b.top + b.bottom) / 2 };
	c.radius = scaled(ui::COMPASS_BUTTON) / 2;
	c.hole = c.radius * ui::COMPASS_HOLE / 100;
	return c;
}

static RECT compass_rect(const CompassDonut& c) {
	return { c.center.x - c.radius, c.center.y - c.radius, c.center.x + c.radius, c.center.y + c.radius };
}

// 区画の領域式: 葉の中の位置から上下左右・中央を決める
static DropZone region_zone(const Node* leaf, POINT client) {
	const RECT& r = leaf->rect;
	double w = std::max<LONG>(1, r.right - r.left), h = std::max<LONG>(1, r.bottom - r.top);
	double dx = (client.x - r.left) / w, dy = (client.y - r.top) / h;
	double e = ui::REGION_EDGE / 100.0;
	double dist[4] = { dx, 1 - dx, dy, 1 - dy };
	DropZone zones[4] = { DropZone::Left, DropZone::Right, DropZone::Top, DropZone::Bottom };
	int best = (int)(std::min_element(dist, dist + 4) - dist);
	return dist[best] < e ? zones[best] : DropZone::Center;
}

// タブ帯の上なら、何番目に差し込むか
static int tab_insert_index(const Node* leaf, POINT client) {
	int index = 0;
	for (auto& t : g_tab_hits) {
		if (t.leaf != leaf) continue;
		if (client.x < (t.rect.left + t.rect.right) / 2) return index;
		index++;
	}
	return index;
}

// 画面座標 pt の下の落とし先を求め、表示を更新する
static void update_drop_target(POINT pt) {
	DropTarget t;
	DropVisual v;
	POINT client = pt;
	ScreenToClient(g_container, &client);
	RECT rc;
	GetClientRect(g_container, &rc);
	t.inside = PtInRect(&rc, client) != FALSE;
	if (t.inside && g_root) {
		t.leaf = leaf_at(g_root.get(), client);
	}
	if (t.leaf) {
		if (PtInRect(&t.leaf->tabs, client)) {
			// タブ帯の上はどちらの方式でも「タブとして重ねる」
			t.zone = DropZone::Center;
			t.tab_index = tab_insert_index(t.leaf, client);
		} else if (drop_style() == 0) {
			v.has_compass = true;
			v.compass = compass_of(t.leaf);
			v.hot_zone = compass_zone_at(v.compass, pt);
			t.zone = (DropZone)v.hot_zone;
		} else {
			t.zone = region_zone(t.leaf, client);
		}
	}
	if (t.leaf && t.zone != DropZone::None) {
		v.has_preview = true;
		v.preview = drop_preview_rect(t.leaf, t.zone, gap_size());
		MapWindowPoints(g_container, nullptr, (POINT*)&v.preview, 2);
	}
	g_target = t;
	g_overlay.update(v, highlight_color(), ui::DROP_ALPHA,
		style_color("ButtonBody", RGB(0x60, 0x60, 0x60)), style_color("Border", RGB(0x90, 0x90, 0x90)));
}

static void show_drop_overlay() {
	RECT area;
	GetWindowRect(g_container, &area);
	g_overlay.show(g_main, area);
}

//---------------------------------------------------------------------
//	ドッキング: パネルの出し入れ
//---------------------------------------------------------------------

// 取り込み中のパネルを区画から外し、浮いた窓にする (at が指定されれば、その位置へ)
static void float_panel(const std::wstring& key, const POINT* at) {
	auto it = std::find_if(g_panels.begin(), g_panels.end(), [&](const Panel& p) { return p.key == key; });
	if (it == g_panels.end()) return;
	Panel p = *it;
	g_panels.erase(it);
	remove_key(g_root.get(), key);
	release_panel(p);
	if (!IsWindowVisible(p.hwnd)) ShowWindow(p.hwnd, SW_SHOWNA);
	// 大きさは、入れ物の中で使っていた大きさを引き継ぐ
	int w = std::max<LONG>(p.assigned.right - p.assigned.left, scaled(200));
	int h = std::max<LONG>(p.assigned.bottom - p.assigned.top, scaled(150));
	if (p.assigned.left == OFFSCREEN) { w = scaled(ui::FLOAT_W); h = scaled(ui::FLOAT_H); }
	if (at) {
		SetWindowPos(p.hwnd, HWND_TOP, at->x - w / 2, at->y - scaled(12), w, h, SWP_NOACTIVATE);
	} else {
		SetWindowPos(p.hwnd, HWND_TOP, 0, 0, w, h, SWP_NOMOVE | SWP_NOACTIVATE);
		move_float_to_center(p.hwnd);
	}
	add_float(p.hwnd, key);
	log(L"浮かせた: " + key);
	relayout();
	save_tree();
}

// 浮いた窓を区画へ入れる
static void dock_float(HWND h, Node* leaf, DropZone zone, int tab_index) {
	FloatPanel* f = find_float(h);
	if (!f || !leaf || zone == DropZone::None) return;
	std::wstring key = f->key;
	if (key.empty()) {
		notify(ui::DOCK_UNKNOWN_PANEL, MB_ICONINFORMATION);
		return;
	}
	remove_float(h);
	insert_key(leaf, key, zone, tab_index);
	if (!capture_panel(h, key)) {
		// 取り込めなければ分割木から外し、浮いた窓に戻す
		remove_key(g_root.get(), key);
		add_float(h, key);
	} else {
		log(L"区画へ入れた: " + key);
	}
	relayout();
	save_tree();
}

// パネルを閉じる (本体の「表示」メニューの項目で非表示にする。区画からの除去は監視で行う)
static void close_panel(const std::wstring& key) {
	for (auto& p : g_panels) {
		if (p.key != key) continue;
		HMENU bar = GetMenu(g_main);
		UINT cmd = find_menu_command(g_main, { L"表示", p.title });
		if (cmd && bar && (GetMenuState(bar, cmd, MF_BYCOMMAND) & MF_CHECKED)) {
			SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
			log(L"閉じた: " + key);
		} else {
			log(L"「表示」メニューに項目が無いため閉じられない: " + key);
		}
		return;
	}
}

//---------------------------------------------------------------------
//	ドッキング: ドラッグ
//---------------------------------------------------------------------

static void end_drag(bool commit) {
	if (g_dd.state == DragState::None) return;
	KillTimer(g_container, TIMER_LONG_PRESS);
	KillTimer(g_container, TIMER_DRAG_POLL);
	bool dragging = (g_dd.state == DragState::Dragging);
	DragContext dd = g_dd;
	DropTarget t = g_target;
	g_dd = {};
	g_target = {};
	g_overlay.hide();
	g_ghost.hide();
	if (!dd.float_hwnd && GetCapture() == g_container) ReleaseCapture();
	if (!commit || !dragging) return;

	if (dd.float_hwnd) {
		// 浮いた窓の移動の操作中なので、操作が終わってから入れる
		if (t.leaf && t.zone != DropZone::None) {
			g_pending_dock = t;
			PostMessageW(g_container, WM_APP_DOCK_FLOAT, (WPARAM)dd.float_hwnd, 0);
		}
		return;
	}
	if (t.leaf && t.zone != DropZone::None) {
		move_key(g_root.get(), dd.key, t.leaf, t.zone, t.tab_index);
		log(L"移動: " + dd.key);
		relayout();
		save_tree();
	} else if (!t.inside && ui::DROP_OUTSIDE) {
		POINT pt;
		GetCursorPos(&pt);
		float_panel(dd.key, &pt);
	}
}

static void begin_drag() {
	g_dd.state = DragState::Dragging;
	show_drop_overlay();
	HFONT font = g_font;
	g_ghost.show(g_main, g_dd.title, font, style_color("GroupingSelect", RGB(0x48, 0x48, 0x48)),
		style_color("Text", RGB(0xff, 0xff, 0xff)), ui::DRAG_GHOST_ALPHA, tab_height());
	POINT pt;
	GetCursorPos(&pt);
	update_drop_target(pt);
	SetTimer(g_container, TIMER_DRAG_POLL, 30, nullptr);
}

// 浮いた窓の移動の操作を観察し、区画の上で放したら入れる
static LRESULT CALLBACK float_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	switch (msg) {
		case WM_ENTERSIZEMOVE:
			if (g_active && !g_busy && g_dd.state == DragState::None) {
				FloatPanel* f = find_float(hwnd);
				g_dd = {};
				g_dd.state = DragState::Armed;
				g_dd.float_hwnd = hwnd;
				g_dd.key = f ? f->key : L"";
				g_dd.title = window_text(hwnd);
				GetCursorPos(&g_dd.down);
			}
			break;
		case WM_MOVING:
			if (g_dd.float_hwnd == hwnd) {
				POINT pt;
				GetCursorPos(&pt);
				if (g_dd.state == DragState::Armed) {
					g_dd.state = DragState::Dragging;
					show_drop_overlay();
				}
				update_drop_target(pt);
			}
			break;
		case WM_EXITSIZEMOVE:
			if (g_dd.float_hwnd == hwnd) {
				// Esc で取り消された移動では入れない
				bool canceled = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
				end_drag(!canceled);
			}
			break;
		case WM_NCDESTROY:
			RemoveWindowSubclass(hwnd, float_subclass_proc, FLOAT_SUBCLASS_ID);
			g_floats.erase(std::remove_if(g_floats.begin(), g_floats.end(), [&](const FloatPanel& f) { return f.hwnd == hwnd; }), g_floats.end());
			break;
	}
	return DefSubclassProc(hwnd, msg, wp, lp);
}

//---------------------------------------------------------------------
//	ドッキング: 右クリックメニュー
//---------------------------------------------------------------------

static std::wstring panel_title(const std::wstring& key) {
	for (auto& p : g_panels) if (p.key == key) return p.title;
	return key;
}

// 区画の呼び名 (「他の区画へ移動」の一覧用)
static std::wstring area_name(Node* leaf, int empty_no) {
	if (!leaf->active.empty()) return format_s(ui::AREA_NAME_FORMAT, panel_title(leaf->active));
	wchar_t buf[128];
	swprintf_s(buf, ui::AREA_NAME_EMPTY, empty_no);
	return buf;
}

static void toggle_maximize(const std::wstring& key);

static void show_tab_menu(Node* leaf, const std::wstring& key, POINT screen) {
	enum { ID_FLOAT = 1, ID_CLOSE, ID_SPLIT_L, ID_SPLIT_R, ID_SPLIT_T, ID_SPLIT_B, ID_AREA_CLOSE, ID_MAXIMIZE, ID_MOVE = 100 };
	// ロック中は組み替えの操作を選べないようにする (最大化は組み替えではないので使える)
	UINT lock = g_settings.locked ? MF_GRAYED : 0;
	HMENU menu = CreatePopupMenu();
	std::vector<Node*> others;
	if (!key.empty()) {
		AppendMenuW(menu, MF_STRING, ID_MAXIMIZE, g_max_key == key ? ui::TAB_RESTORE : ui::TAB_MAXIMIZE);
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING | lock, ID_FLOAT, ui::TAB_FLOAT);
		AppendMenuW(menu, MF_STRING | lock, ID_CLOSE, ui::TAB_CLOSE);
		HMENU move = CreatePopupMenu();
		int empty_no = 0;
		for_each_leaf(g_root.get(), [&](Node* l) {
			if (l->active.empty()) empty_no++;
			if (l == leaf) return;
			AppendMenuW(move, MF_STRING, ID_MOVE + others.size(), area_name(l, empty_no).c_str());
			others.push_back(l);
		});
		AppendMenuW(menu, MF_POPUP | ((others.empty() || lock) ? MF_GRAYED : 0), (UINT_PTR)move, ui::TAB_MOVE_TO);
	}
	HMENU split = CreatePopupMenu();
	AppendMenuW(split, MF_STRING, ID_SPLIT_L, ui::SPLIT_LEFT);
	AppendMenuW(split, MF_STRING, ID_SPLIT_R, ui::SPLIT_RIGHT);
	AppendMenuW(split, MF_STRING, ID_SPLIT_T, ui::SPLIT_TOP);
	AppendMenuW(split, MF_STRING, ID_SPLIT_B, ui::SPLIT_BOTTOM);
	AppendMenuW(menu, MF_POPUP | lock, (UINT_PTR)split, ui::TAB_SPLIT);
	if (leaf->panels.empty() && leaf != g_root.get()) {
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING | lock, ID_AREA_CLOSE, ui::AREA_CLOSE);
	}

	UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, screen.x, screen.y, 0, g_container, nullptr);
	DestroyMenu(menu);

	switch (cmd) {
		case 0: return;
		case ID_MAXIMIZE: toggle_maximize(key); return;
		case ID_FLOAT: float_panel(key, nullptr); return;
		case ID_CLOSE: close_panel(key); return;
		case ID_SPLIT_L: split_leaf(leaf, DropZone::Left); break;
		case ID_SPLIT_R: split_leaf(leaf, DropZone::Right); break;
		case ID_SPLIT_T: split_leaf(leaf, DropZone::Top); break;
		case ID_SPLIT_B: split_leaf(leaf, DropZone::Bottom); break;
		case ID_AREA_CLOSE: close_leaf(g_root.get(), leaf); break;
		default:
			if (cmd >= ID_MOVE && cmd < ID_MOVE + others.size()) {
				move_key(g_root.get(), key, others[cmd - ID_MOVE], DropZone::Center);
			}
			break;
	}
	relayout();
	save_tree();
}

//---------------------------------------------------------------------
//	監視 (安全装置)
//---------------------------------------------------------------------

static void watch() {
	if (!g_active || g_busy || g_dd.state != DragState::None) return;
	bool changed = false;

	for (auto it = g_panels.begin(); it != g_panels.end();) {
		// 本体側に持ち去られた・破棄されたパネルを外す
		if (!IsWindow(it->hwnd) || GetParent(it->hwnd) != g_container) {
			log(L"パネルが外部で移動・破棄された: " + it->key);
			if (IsWindow(it->hwnd)) RemoveWindowSubclass(it->hwnd, panel_subclass_proc, PANEL_SUBCLASS_ID);
			remove_key(g_root.get(), it->key);
			it = g_panels.erase(it);
			changed = true;
			continue;
		}
		// 閉じられた (本体の「ウィンドウを閉じる」や「表示」メニュー) パネルは区画から外し、
		// 非表示の浮いた窓として本体へ返す (再び開かれたら、新しいパネルと同じく浮いた窓で出る)
		if (!IsWindowVisible(it->hwnd)) {
			log(L"閉じられたパネルを区画から外す: " + it->key);
			Panel p = *it;
			it = g_panels.erase(it);
			remove_key(g_root.get(), p.key);
			// 再び開かれた時に照合できるよう、照合用の仮位置へ戻しておく
			for (auto& s : g_slots) {
				if (s.key != p.key) continue;
				int w = p.float_rect.right - p.float_rect.left, h = p.float_rect.bottom - p.float_rect.top;
				p.float_rect = { s.pt.x, s.pt.y, s.pt.x + w, s.pt.y + h };
			}
			release_panel(p);
			changed = true;
			continue;
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
		add_float(h, key);
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
static void organize_menu();
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
	// 他のプラグインが「表示」の文字や構成を変えていても見つける (menu_find.cpp)
	HMENU view = find_view_menu(bar);
	int pos = find_menu_item(view, L"メディアエクスプローラー");
	HMENU ex = pos >= 0 ? GetSubMenu(view, pos) : nullptr;
	if (!ex) return;
	int n = GetMenuItemCount(ex);
	for (int i = 0; i < n; i++) {
		UINT id = GetMenuItemID(ex, i);
		if (id != (UINT)-1 && id != 0) g_explorer_cmds.insert(id);
	}
}

// 開始の途中で失敗した場合の後始末
static void abort_enter(const std::wstring& reason, bool interactive) {
	log(L"有効化を中止: " + reason);
	if (g_active) {
		KillTimer(g_container, TIMER_WATCH);
		release_all_panels();
		clear_floats();
		detach_container();
		g_active = false;
	}
	bool restored = restore_host_layout();
	g_settings.session_active = false;
	g_settings.save();
	if (interactive) {
		notify(ui::FAIL_TITLE + reason + (restored ? ui::FAIL_RESTORED : ui::FAIL_NOT_RESTORED));
	}
}

// 浮いた窓のまま使っているパネルの位置 (表示中のもの)
static FloatingMap collect_floating() {
	FloatingMap m;
	for (auto& f : g_floats) {
		bool alive = IsWindow(f.hwnd) != FALSE;
		bool visible = alive && IsWindowVisible(f.hwnd);
		log(L"浮いた窓 " + (f.key.empty() ? L"(照合できず)" : f.key) + L": " +
			(!alive ? L"破棄済み" : visible ? L"表示中 → 位置を記憶" : L"閉じられている → 記憶しない"));
		if (f.key.empty() || !visible) continue;
		RECT r;
		GetWindowRect(f.hwnd, &r);
		m[f.key] = r;
	}
	return m;
}

// TP_GUIdocker 用レイアウトを生成・適用し、パネルを取り込む (有効化とワークスペースの切り替えで共通)
// 分割木 (g_root) と浮いた窓の位置 (g_settings.floating) に従って、表示するパネルを決める
static bool build_and_capture(bool interactive) {
	// 1. 中身を生成する
	IniDoc base;
	if (!base.load(layout_path(AUTO_RETURN_LAYOUT))) {
		abort_enter(ui::REASON_READ_SAVED, interactive);
		return false;
	}
	auto is_visible = [](const std::wstring& key) {
		return find_leaf_with(g_root.get(), key) != nullptr || g_settings.floating.count(key) > 0;
	};
	RECT work = {};
	MONITORINFO mi = { sizeof(mi) };
	if (GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTOPRIMARY), &mi)) work = mi.rcWork;
	POINT origin = { work.left + scaled(40), work.top + scaled(40) };
	DockLayoutPlan plan = build_dock_layout(base, is_visible, origin, scaled(ui::FLOAT_W), scaled(ui::FLOAT_H));
	if (!plan.doc.save(layout_path(DOCK_LAYOUT))) {
		abort_enter(ui::REASON_WRITE, interactive);
		return false;
	}
	g_slots = plan.slots;
	log(L"TP_GUIdocker 用レイアウトを生成 (切り離し " + std::to_wstring(plan.slots.size()) + L" 件, 表示 " +
		std::to_wstring(plan.visible_keys.size()) + L" 件)");

	g_settings.session_active = true;
	g_settings.save();

	// 2. 適用する (本体のエラー表示を監視)
	ApplyResult applied = host_apply_layout(g_main, g_container, DOCK_LAYOUT);
	if (!applied.ok()) {
		abort_enter(applied.in_menu ? ui::REASON_LOAD_FAILED : ui::REASON_NOT_IN_MENU, interactive);
		return false;
	}

	// 3. 照合: 表示すべきパネルが全て指定位置に現れたこと
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
		abort_enter(format_s(ui::REASON_MISSING, missing), interactive);
		return false;
	}

	// 4. 入れ物を置き、分割木のパネルを取り込む。それ以外は浮いた窓として記憶した位置へ
	g_active = true;
	attach_container();
	for (auto& [key, h] : found) {
		if (find_leaf_with(g_root.get(), key)) {
			if (!capture_panel(h, key)) {
				abort_enter(format_s(ui::REASON_CAPTURE, key), interactive);
				return false;
			}
		} else {
			add_float(h, key);
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
	log(L"帯の高さ=" + std::to_wstring(tab_height()) + L" (メニューバー=" + std::to_wstring(menu_bar_height()) + L") 境界線=" + std::to_wstring(gap_size()));
	log(L"有効化した (取り込み " + std::to_wstring(g_panels.size()) + L" 枚, 浮いた窓 " + std::to_wstring(g_floats.size()) + L" 枚)");
	return true;
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
	// 本体の版の数値は 2.1.14 → 2011400 の形。小さな更新 (2.1.13 → 2.1.14 のように末尾の番号だけが変わるもの) は
	// 画面の仕組みへの影響が小さいため確認を出さずに引き継ぎ、2.1 → 2.2 のような大きな更新の時だけ確認する
	auto major_minor = [](DWORD v) { return v / 10000; };
	if (g_settings.verified_version != 0 && g_settings.verified_version != g_host_version &&
		major_minor(g_settings.verified_version) == major_minor(g_host_version)) {
		log(L"本体の小さな更新を検知 (" + std::to_wstring(g_settings.verified_version) + L" → " + std::to_wstring(g_host_version) +
			L")。確認を出さずに引き継ぐ");
		g_settings.verified_version = g_host_version;
		g_settings.save();
	}
	if (g_settings.verified_version != g_host_version) {
		wchar_t msg[1024];
		if (g_settings.verified_version == 0) {
			swprintf_s(msg, ui::CONFIRM_FIRST, g_host_version);
		} else {
			swprintf_s(msg, ui::CONFIRM_HOST_UPDATED, g_settings.verified_version, g_host_version);
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

	log(L"===== 有効化 =====");

	// 前回が異常終了 (自由分割中の印が残っている) なら、まず本体の配置を正常に戻す
	if (g_settings.session_active) {
		log(L"前回の異常終了を検知。先に本体の配置を戻す");
		restore_host_layout();
		g_settings.session_active = false;
		g_settings.save();
	}

	// 1. 現在の配置を本体自身に保存させる (戻り先)
	if (!host_create_layout(g_main, g_container, AUTO_RETURN_LAYOUT)) {
		abort_enter(ui::REASON_SAVE_CURRENT, interactive);
		return false;
	}

	// 2. 自由分割用レイアウトの枠を本体に作らせる (メニューに無い場合のみ)
	if (!find_menu_command(g_main, { L"表示", L"レイアウト", DOCK_LAYOUT })) {
		if (!host_create_layout(g_main, g_container, DOCK_LAYOUT)) {
			abort_enter(ui::REASON_REGISTER, interactive);
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

	if (g_settings.workspaces[g_settings.active_ws].saved_tree.empty()) {
		// 初めて使うワークスペースは、今の配置を「保存した配置」にもしておく
		g_settings.workspaces[g_settings.active_ws].saved_tree = serialize_tree(g_root.get());
	}
	return build_and_capture(interactive);
}

static void exit_dock(bool apply_return) {
	if (!g_active) return;
	BusyGuard busy;
	log(L"===== 無効化 =====");
	end_drag(false);
	KillTimer(g_container, TIMER_WATCH);
	save_tree();

	// 浮いた窓のまま使っていたパネルの位置を記憶する (次回も浮いた窓で復元する)
	g_settings.floating = collect_floating();
	clear_floats();

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
//	ワークスペース
//---------------------------------------------------------------------

static Workspace& active_workspace() {
	return g_settings.workspaces[g_settings.active_ws];
}

// 今の配置 (分割木と浮いた窓) を、使用中の配置として記録する
static void store_current_into_settings() {
	if (g_root) g_settings.tree = serialize_tree(g_root.get());
	g_settings.floating = collect_floating();
	g_settings.save();
}

// 使用中の配置に従って、パネルを並べ直す (本体のレイアウト機能で全パネルを切り離し直す)
static bool rebuild_from_settings() {
	BusyGuard busy;
	end_drag(false);
	KillTimer(g_container, TIMER_WATCH);
	g_max_key.clear();
	release_all_panels();
	clear_floats();
	g_root = parse_tree(g_settings.tree);
	if (!g_root) g_root = default_tree();
	InvalidateRect(g_container, nullptr, TRUE);
	return build_and_capture(true);
}

static void switch_workspace(int index, bool reset_to_saved) {
	if (!g_active || g_busy || index < 0 || index >= (int)g_settings.workspaces.size()) return;
	if (index == g_settings.active_ws && !reset_to_saved) return;
	store_current_into_settings();
	g_settings.active_ws = index;
	Workspace& w = active_workspace();
	if (reset_to_saved) {
		w.work_tree = w.saved_tree;
		w.work_floating = w.saved_floating;
	}
	g_settings.tree = w.work_tree;
	g_settings.floating = w.work_floating;
	g_settings.save();
	log(L"ワークスペースを切り替え: " + w.name + (reset_to_saved ? L" (保存時の配置に戻す)" : L""));
	rebuild_from_settings();
}

static void new_workspace() {
	wchar_t buf[128];
	swprintf_s(buf, ui::WS_NEW_NAME, (int)g_settings.workspaces.size() + 1);
	std::wstring name = buf;
	if (!prompt_text(g_main, ui::WS_NAME_TITLE, ui::WS_NAME_LABEL, name) || name.empty()) return;
	store_current_into_settings();
	Workspace w;
	w.name = name;
	w.saved_tree = w.work_tree = g_settings.tree;
	w.saved_floating = w.work_floating = g_settings.floating;
	g_settings.workspaces.push_back(w);
	g_settings.active_ws = (int)g_settings.workspaces.size() - 1;
	g_settings.save();
	log(L"ワークスペースを作成: " + name);
	relayout();
}

static void rename_workspace(int index) {
	std::wstring name = g_settings.workspaces[index].name;
	if (!prompt_text(g_main, ui::WS_NAME_TITLE, ui::WS_NAME_LABEL, name) || name.empty()) return;
	g_settings.workspaces[index].name = name;
	g_settings.save();
	relayout();
}

static void save_workspace(int index) {
	if (index != g_settings.active_ws) return;
	store_current_into_settings();
	Workspace& w = active_workspace();
	w.saved_tree = g_settings.tree;
	w.saved_floating = g_settings.floating;
	g_settings.save();
	log(L"ワークスペースを上書き保存: " + w.name);
}

static void delete_workspace(int index) {
	if (g_settings.workspaces.size() <= 1) {
		notify(ui::WS_DELETE_LAST, MB_ICONINFORMATION);
		return;
	}
	std::wstring msg = format_s(ui::WS_DELETE_CONFIRM, g_settings.workspaces[index].name);
	if (MessageBoxW(g_main, msg.c_str(), TPGD_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
	if (index == g_settings.active_ws) {
		// 使用中のものを消す場合は、隣のワークスペースへ切り替えてから消す
		switch_workspace(index > 0 ? index - 1 : 1, false);
		if (!g_active) return;
	}
	log(L"ワークスペースを削除: " + g_settings.workspaces[index].name);
	g_settings.workspaces.erase(g_settings.workspaces.begin() + index);
	if (g_settings.active_ws > index) g_settings.active_ws--;
	g_settings.save();
	relayout();
}

static void show_workspace_menu(int index, POINT screen) {
	enum { ID_RENAME = 1, ID_SAVE, ID_RESET, ID_DELETE, ID_COLOR, ID_COLOR_RESET };
	bool active = (index == g_settings.active_ws);
	HMENU menu = CreatePopupMenu();
	AppendMenuW(menu, MF_STRING, ID_RENAME, ui::WS_MENU_RENAME);
	AppendMenuW(menu, MF_STRING | (active ? 0 : MF_GRAYED), ID_SAVE, ui::WS_MENU_SAVE);
	AppendMenuW(menu, MF_STRING, ID_RESET, ui::WS_MENU_RESET);
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, ID_COLOR, ui::WS_MENU_COLOR);
	AppendMenuW(menu, MF_STRING | (g_settings.theme_color >= 0 ? 0 : MF_GRAYED), ID_COLOR_RESET, ui::WS_MENU_COLOR_RESET);
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, ID_DELETE, ui::WS_MENU_DELETE);
	UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, screen.x, screen.y, 0, g_container, nullptr);
	DestroyMenu(menu);
	switch (cmd) {
		case ID_RENAME: rename_workspace(index); break;
		case ID_SAVE: save_workspace(index); break;
		case ID_RESET: switch_workspace(index, true); break;
		case ID_DELETE: delete_workspace(index); break;
		case ID_COLOR: choose_theme_color(false); break;
		case ID_COLOR_RESET: choose_theme_color(true); break;
	}
}

//---------------------------------------------------------------------
//	最大化・ロック
//---------------------------------------------------------------------

static void toggle_maximize(const std::wstring& key) {
	if (key.empty() || !is_present(key)) return;
	g_max_key = (g_max_key == key) ? L"" : key;
	log(g_max_key.empty() ? L"最大化を解除" : L"最大化: " + key);
	relayout();
}

// ショートカットキーでの最大化: カーソルの下のパネル、無ければ最後に操作したパネル
static void maximize_under_cursor() {
	if (!g_active) return;
	if (!g_max_key.empty()) {
		toggle_maximize(g_max_key);
		return;
	}
	POINT pt;
	GetCursorPos(&pt);
	for (HWND h = WindowFromPoint(pt); h; h = GetParent(h)) {
		if (Panel* p = find_panel(h)) {
			toggle_maximize(p->key);
			return;
		}
	}
	POINT client = pt;
	ScreenToClient(g_container, &client);
	if (Node* leaf = g_root ? leaf_at(g_root.get(), client) : nullptr) {
		if (!leaf->active.empty()) {
			toggle_maximize(leaf->active);
			return;
		}
	}
	toggle_maximize(g_last_key);
}

static void update_lock_menu();
static void toggle_lock() {
	g_settings.locked = !g_settings.locked;
	g_settings.save();
	log(g_settings.locked ? L"レイアウトをロック" : L"レイアウトのロックを解除");
	update_lock_menu();
	relayout();
}

//---------------------------------------------------------------------
//	見本の書き出し (指示用)
//	場面を再現して画面を撮影し、各部分の位置と指示書の ID を samples.json に記録する。
//	tools/make_samples.py がこれを読み、番号付きの見本 PDF を作る
//---------------------------------------------------------------------

namespace {

struct SampleNote {
	std::vector<std::wstring> ids;   // 指示書の ID
	RECT rect;                       // 撮影画像の中の位置
	std::wstring note;               // 補足
};

struct SampleScene {
	std::wstring name;     // 場面名 (ファイル名にも使う)
	std::wstring title;    // 見出し
	std::vector<SampleNote> notes;
};

std::wstring json_escape(const std::wstring& s) {
	std::wstring o;
	for (wchar_t c : s) {
		if (c == L'"' || c == L'\\') { o += L'\\'; o += c; }
		else if (c == L'\n') o += L"\\n";
		else o += c;
	}
	return o;
}

// 24 ビットの画像 (下から上の DIB) を BMP で保存する
bool write_bmp(const BITMAPINFO& bi, const void* bits, const std::wstring& path) {
	int w = bi.bmiHeader.biWidth, h = bi.bmiHeader.biHeight;
	DWORD stride = ((w * 3 + 3) & ~3);
	DWORD image_size = stride * h;
	BITMAPFILEHEADER fh = {};
	fh.bfType = 0x4D42;
	fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
	fh.bfSize = fh.bfOffBits + image_size;
	bool ok = false;
	HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f != INVALID_HANDLE_VALUE) {
		DWORD wr = 0;
		ok = WriteFile(f, &fh, sizeof(fh), &wr, nullptr) && WriteFile(f, &bi.bmiHeader, sizeof(bi.bmiHeader), &wr, nullptr) &&
			WriteFile(f, bits, image_size, &wr, nullptr);
		CloseHandle(f);
	}
	return ok;
}

BITMAPINFO dib_info(int w, int h) {
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = h;   // 下から上 (BMP の標準)
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 24;
	bi.bmiHeader.biCompression = BI_RGB;
	return bi;
}

// 画面の矩形を撮影して BMP で保存する (重ね窓も写るよう CAPTUREBLT を使う)
bool capture_to_bmp(const RECT& r, const std::wstring& path) {
	int w = r.right - r.left, h = r.bottom - r.top;
	if (w <= 0 || h <= 0) return false;
	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	BITMAPINFO bi = dib_info(w, h);
	void* bits = nullptr;
	HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	HGDIOBJ old = SelectObject(mem, bmp);
	BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
	GdiFlush();
	SelectObject(mem, old);
	DeleteDC(mem);
	ReleaseDC(nullptr, screen);
	bool ok = write_bmp(bi, bits, path);
	DeleteObject(bmp);
	return ok;
}

//---------------------------------------------------------------------
//	フォントの比較 (指示用)
//	候補の書体ごとに帯とタブを描き直して撮影し、加えて文字の見本を描いて保存する。
//	未導入の書体は、DLL と同じ場所の「TP_GUIdocker_fonts」フォルダに置けば、プラグインの中だけで読み込む
//---------------------------------------------------------------------

struct FontCandidate {
	const wchar_t* label;              // 見本での名前
	std::vector<const wchar_t*> faces; // 書体名の候補 (英語名・日本語名・配布物ごとの名前)
	const wchar_t* note;               // 特徴
};

const std::vector<FontCandidate>& font_candidates() {
	static const std::vector<FontCandidate> list = {
		{ L"游ゴシック UI", { L"Yu Gothic UI", L"游ゴシック UI" },
			L"Windows 10/11 標準の画面用書体。本体の既定 (style.conf の DefaultFamily)" },
		{ L"BIZ UDPゴシック", { L"BIZ UDPGothic", L"BIZ UDPゴシック" },
			L"Windows 標準。モリサワの読み間違えにくさを重視した書体" },
		{ L"Noto Sans JP", { L"Noto Sans JP" },
			L"Google と Adobe の共同開発。太さを連続的に変えられる新形式の場合あり" },
		{ L"源ノ角ゴシック", { L"源ノ角ゴシック", L"Source Han Sans", L"Source Han Sans JP", L"源ノ角ゴシック JP" },
			L"Noto Sans JP と同じ字形。太さごとに別ファイルの形式" },
		{ L"源暎ゴシック", { L"源暎ゴシックP", L"GenEi Gothic P", L"GenEiGothicP", L"源暎ゴシックP Regular", L"GenEiGothicP Regular" },
			L"源ノ角ゴシックの派生。字面を詰めて引き締めた書体" },
		{ L"LINE Seed JP", { L"LINE Seed JP", L"LINE Seed JP_TTF Regular", L"LINE Seed JP_OTF Regular", L"LINE Seed JP_OTF", L"LINE Seed JP_TTF", L"LINE Seed JP App_OTF", L"LINE Seed JP App_TTF" },
			L"LINE の企業書体。角に丸みのある現代的な書体。太さは4段階" },
		{ L"IBM Plex Sans JP", { L"IBM Plex Sans JP" },
			L"IBM の企業書体。理知的で少し柔らかい" },
		{ L"M PLUS 1p", { L"M PLUS 1p" },
			L"素直で軽やかな書体。太さは7段階" },
	};
	return list;
}

// 文字の見本を描いて保存する (大きさ: 帯の文字と、その 1.5 倍)
bool draw_font_specimen(HFONT small_font, HFONT large_font, const std::wstring& path, double scale) {
	static const wchar_t* lines[] = {
		L"プレビュー編集　レイヤー編集　オブジェクト設定　シーンリスト　色設定",
		L"ワークスペース1　＋　あいうえお　アイウエオ　漢字の見え方　読み間違い",
		L"ABCDEFGHIJ abcdefghij 0123456789  1920x1080  29.97fps  00:01:23.45",
	};
	COLORREF c_bg = style_color("Grouping", RGB(0x38, 0x38, 0x38));
	COLORREF c_text = style_color("Text", RGB(0xff, 0xff, 0xff));
	int pad = (int)(12 * scale + 0.5);
	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	ReleaseDC(nullptr, screen);

	// 必要な大きさを測る
	int w = 0, h = pad;
	std::vector<int> heights;
	for (HFONT f : { small_font, large_font }) {
		HGDIOBJ old = SelectObject(mem, f);
		for (auto* s : lines) {
			SIZE sz = {};
			GetTextExtentPoint32W(mem, s, (int)wcslen(s), &sz);
			w = std::max<int>(w, sz.cx);
			heights.push_back(sz.cy + pad / 2);
			h += sz.cy + pad / 2;
		}
		h += pad;
		SelectObject(mem, old);
	}
	w += pad * 2;

	BITMAPINFO bi = dib_info(w, h);
	void* bits = nullptr;
	HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!bmp) {
		DeleteDC(mem);
		return false;
	}
	HGDIOBJ old_bmp = SelectObject(mem, bmp);
	RECT all = { 0, 0, w, h };
	HBRUSH b = CreateSolidBrush(c_bg);
	FillRect(mem, &all, b);
	DeleteObject(b);
	SetBkMode(mem, TRANSPARENT);
	SetTextColor(mem, c_text);
	int y = pad, i = 0;
	for (HFONT f : { small_font, large_font }) {
		HGDIOBJ old = SelectObject(mem, f);
		for (auto* s : lines) {
			TextOutW(mem, pad, y, s, (int)wcslen(s));
			y += heights[i++];
		}
		y += pad;
		SelectObject(mem, old);
	}
	GdiFlush();
	bool ok = write_bmp(bi, bits, path);
	SelectObject(mem, old_bmp);
	DeleteObject(bmp);
	DeleteDC(mem);
	return ok;
}

// 画面の描き替えが終わるのを待つ
void settle() {
	for (int i = 0; i < 6; i++) {
		MSG m;
		while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&m);
			DispatchMessageW(&m);
		}
		Sleep(50);
	}
	GdiFlush();
}

} // namespace

// 入れ物の座標の矩形を、撮影範囲 (origin 基準) の座標へ
static RECT to_capture(RECT r, POINT origin) {
	MapWindowPoints(g_container, nullptr, (POINT*)&r, 2);
	OffsetRect(&r, -origin.x, -origin.y);
	return r;
}
static RECT screen_to_capture(RECT r, POINT origin) {
	OffsetRect(&r, -origin.x, -origin.y);
	return r;
}

// 見本を撮る対象の葉 (プレビューの葉、無ければ最大の葉)
static Node* sample_leaf() {
	if (Node* l = find_leaf_with(g_root.get(), L"preview")) if (!IsRectEmpty(&l->rect)) return l;
	Node* best = nullptr;
	long area = -1;
	for_each_leaf(g_root.get(), [&](Node* l) {
		long a = (l->rect.right - l->rect.left) * (l->rect.bottom - l->rect.top);
		if (a > area) { area = a; best = l; }
	});
	return best;
}

static Node* first_split(Node* n) {
	if (!n || !n->is_split) return nullptr;
	return n;
}

static void export_samples() {
	if (!g_active) {
		notify(ui::EXPORT_NEED_ACTIVE, MB_ICONINFORMATION);
		return;
	}
	BusyGuard busy;
	std::wstring dir = module_dir() + L"\\" TPGD_NAME L"_見本";
	CreateDirectoryW(dir.c_str(), nullptr);
	log(L"見本の書き出し: " + dir);

	SetForegroundWindow(g_main);
	RECT win;
	GetWindowRect(g_main, &win);
	POINT origin = { win.left, win.top };
	std::vector<SampleScene> scenes;

	auto shoot = [&](SampleScene& s) {
		settle();
		capture_to_bmp(win, dir + L"\\" + s.name + L".bmp");
		scenes.push_back(s);
	};

	relayout();
	Node* leaf = sample_leaf();
	if (!leaf) return;

	// 1. 通常の画面
	{
		SampleScene s{ L"01_normal", L"通常の画面", {} };
		s.notes.push_back({ { L"WS_BAR_HEIGHT" }, to_capture(g_bar, origin), L"ワークスペースの帯" });
		for (auto& w : g_ws_hits) {
			if (w.index == g_settings.active_ws) {
				s.notes.push_back({ { L"COLOR_WS_ACTIVE", L"WS_BUTTON_PADDING", L"WS_DEFAULT_NAME" }, to_capture(w.rect, origin), L"選択中のワークスペース" });
			}
		}
		s.notes.push_back({ { L"WS_ADD" }, to_capture(g_ws_add, origin), L"ワークスペースの追加" });
		s.notes.push_back({ { L"UNLOCK_BADGE", L"MENU_LOCK" }, to_capture(g_lock_rect, origin), L"ロック (クリックで切り替え)" });
		s.notes.push_back({ { L"TAB_HEIGHT", L"COLOR_TABBAR" }, to_capture(leaf->tabs, origin), L"タブ帯" });
		bool active_done = false, inactive_done = false;
		for (auto& t : g_tab_hits) {
			bool sel = (t.leaf->active == t.key);
			if (sel && !active_done) {
				s.notes.push_back({ { L"COLOR_TAB_ACTIVE", L"COLOR_TAB_TEXT", L"TAB_PADDING", L"FONT" }, to_capture(t.rect, origin), L"選択中のタブ" });
				active_done = true;
			} else if (!sel && !inactive_done) {
				s.notes.push_back({ { L"COLOR_TAB_TEXT_OFF" }, to_capture(t.rect, origin), L"選択されていないタブ" });
				inactive_done = true;
			}
		}
		if (Node* sp = first_split(g_root.get())) {
			s.notes.push_back({ { L"SEPARATOR", L"COLOR_SEPARATOR", L"MIN_PANE" }, to_capture(sp->splitter, origin), L"境界線 (ドラッグで大きさを変える)" });
		}
		shoot(s);
	}

	// 2. ドラッグ中 (十字ボタン式) と 3. ドラッグ中 (区画の領域式)
	int saved_style = g_settings.drop_style;
	for (int style = 0; style < 2; style++) {
		g_settings.drop_style = style;
		show_drop_overlay();
		// 区画の中の位置を選んで、落とし先の表示を作る
		POINT pt;
		if (style == 0) {
			// 左の扇の中ほど
			CompassDonut c = compass_of(leaf);
			pt = { c.center.x - (c.radius + c.hole) / 2, c.center.y };
		} else {
			RECT r = leaf->rect;
			MapWindowPoints(g_container, nullptr, (POINT*)&r, 2);
			pt = { r.right - (r.right - r.left) / 10, (r.top + r.bottom) / 2 };  // 右端寄り
		}
		update_drop_target(pt);
		SampleScene s{ style == 0 ? L"02_drag_compass" : L"03_drag_region",
			style == 0 ? L"ドラッグ中 (十字ボタン式)" : L"ドラッグ中 (区画の領域式)", {} };
		RECT preview = drop_preview_rect(leaf, style == 0 ? DropZone::Left : DropZone::Right, gap_size());
		s.notes.push_back({ { L"DROP_HIGHLIGHT", L"DROP_ALPHA" }, to_capture(preview, origin), L"置かれる範囲の予告" });
		if (style == 0) {
			s.notes.push_back({ { L"COMPASS_BUTTON", L"COMPASS_HOLE" }, screen_to_capture(compass_rect(compass_of(leaf)), origin),
				L"ドーナツ型のボタン (中心の円: タブとして重ねる / 周りの扇: その側に分割)" });
		} else {
			s.notes.push_back({ { L"REGION_EDGE" }, to_capture(leaf->rect, origin), L"区画の端から REGION_EDGE % 以内なら上下左右、それより内側ならタブとして重ねる" });
		}
		shoot(s);
		g_overlay.hide();
	}
	g_settings.drop_style = saved_style;
	g_target = {};

	// 4. タブの影
	if (!g_tab_hits.empty()) {
		const TabHit& t = g_tab_hits.front();
		RECT tr = t.rect;
		MapWindowPoints(g_container, nullptr, (POINT*)&tr, 2);
		g_ghost.show(g_main, panel_title(t.key), g_font, style_color("GroupingSelect", RGB(0x48, 0x48, 0x48)),
			style_color("Text", RGB(0xff, 0xff, 0xff)), ui::DRAG_GHOST_ALPHA, tab_height());
		g_ghost.move({ tr.right + scaled(40), tr.bottom + scaled(40) });
		SampleScene s{ L"04_drag_ghost", L"長押しでドラッグの準備ができた時のタブの影", {} };
		settle();
		s.notes.push_back({ { L"DRAG_GHOST_ALPHA", L"LONG_PRESS_MS" }, screen_to_capture(g_ghost.rect(), origin), L"タブの影 (長押しが成立すると出る)" });
		shoot(s);
		g_ghost.hide();
	}

	// 5. 最大化とロック中
	{
		bool saved_lock = g_settings.locked;
		std::wstring saved_max = g_max_key;
		g_settings.locked = true;
		g_max_key = leaf->active;
		relayout();
		Node* ml = find_leaf_with(g_root.get(), g_max_key);
		SampleScene s{ L"05_maximize_lock", L"パネルの最大化とロック中", {} };
		if (ml) s.notes.push_back({ { L"TAB_MAXIMIZE", L"TAB_RESTORE", L"MENU_MAXIMIZE" }, to_capture(ml->rect, origin), L"最大化したパネル (他の区画は隠れる)" });
		s.notes.push_back({ { L"LOCK_BADGE", L"MENU_UNLOCK" }, to_capture(g_lock_rect, origin), L"ロック中の表示" });
		shoot(s);
		g_settings.locked = saved_lock;
		g_max_key = saved_max;
		relayout();
	}

	// 6. フォントの比較 (帯とタブの文字だけを候補の書体に替えて撮影する。大きさは今の帯の文字と同じ)
	struct FontShot {
		const FontCandidate* c;
		std::wstring face;      // 使った書体名 (未導入なら空)
		std::wstring actual;    // Windows が実際に選んだ書体名
		std::wstring image, specimen;
		std::vector<RECT> tabs;
		RECT bar;
	};
	std::vector<FontShot> font_shots;
	{
		LOGFONTW base = {};
		GetObjectW(g_font, sizeof(base), &base);
		HFONT saved_font = g_font;
		int no = 0;
		for (auto& c : font_candidates()) {
			no++;
			FontShot fs{ &c, L"", L"", L"", L"", {}, {} };
			for (auto* face : c.faces) {
				if (font_exists(face)) { fs.face = face; break; }
			}
			if (fs.face.empty()) {
				log(std::wstring(L"見本: 未導入の書体 ") + c.label);
				font_shots.push_back(fs);
				continue;
			}
			LOGFONTW lf = base;
			lf.lfWeight = FW_NORMAL;
			lf.lfItalic = FALSE;
			lf.lfCharSet = DEFAULT_CHARSET;
			lf.lfQuality = CLEARTYPE_QUALITY;
			wcsncpy_s(lf.lfFaceName, fs.face.c_str(), _TRUNCATE);
			HFONT f = CreateFontIndirectW(&lf);
			LOGFONTW lf_large = lf;
			lf_large.lfHeight = lf.lfHeight * 3 / 2;
			HFONT f_large = CreateFontIndirectW(&lf_large);
			if (!f || !f_large) {
				if (f) DeleteObject(f);
				if (f_large) DeleteObject(f_large);
				font_shots.push_back(fs);
				continue;
			}
			{
				HDC dc = GetDC(nullptr);
				HGDIOBJ old = SelectObject(dc, f);
				wchar_t actual[LF_FACESIZE] = {};
				GetTextFaceW(dc, LF_FACESIZE, actual);
				SelectObject(dc, old);
				ReleaseDC(nullptr, dc);
				fs.actual = actual;
			}
			wchar_t name[32];
			swprintf_s(name, L"06_font_%02d", no);
			fs.image = std::wstring(name) + L".bmp";
			fs.specimen = std::wstring(name) + L"_specimen.bmp";

			g_font = f;
			relayout();
			UpdateWindow(g_container);
			settle();
			capture_to_bmp(win, dir + L"\\" + fs.image);
			draw_font_specimen(f, f_large, dir + L"\\" + fs.specimen, g_scale);
			fs.bar = to_capture(g_bar, origin);
			for_each_leaf(g_root.get(), [&](Node* l) {
				if (!IsRectEmpty(&l->tabs)) fs.tabs.push_back(to_capture(l->tabs, origin));
			});
			log(L"見本: 書体 " + fs.face + L" (実際: " + fs.actual + L")");

			g_font = saved_font;
			DeleteObject(f);
			DeleteObject(f_large);
			font_shots.push_back(fs);
		}
		g_font = saved_font;
		relayout();
		UpdateWindow(g_container);
	}

	// 位置の記録 (UTF-8)
	std::wstring js = L"{\n  \"version\": \"" TPGD_VERSION L"\",\n  \"scenes\": [\n";
	for (size_t i = 0; i < scenes.size(); i++) {
		auto& s = scenes[i];
		js += L"    {\"name\": \"" + json_escape(s.name) + L"\", \"title\": \"" + json_escape(s.title) + L"\", \"image\": \"" +
			json_escape(s.name) + L".bmp\", \"notes\": [\n";
		for (size_t j = 0; j < s.notes.size(); j++) {
			auto& n = s.notes[j];
			js += L"      {\"ids\": [";
			for (size_t k = 0; k < n.ids.size(); k++) js += (k ? L", \"" : L"\"") + json_escape(n.ids[k]) + L"\"";
			wchar_t r[96];
			swprintf_s(r, L"], \"rect\": [%ld, %ld, %ld, %ld], ", n.rect.left, n.rect.top, n.rect.right, n.rect.bottom);
			js += r;
			js += L"\"note\": \"" + json_escape(n.note) + L"\"}" + (j + 1 < s.notes.size() ? L",\n" : L"\n");
		}
		js += std::wstring(L"    ]}") + (i + 1 < scenes.size() ? L",\n" : L"\n");
	}
	js += L"  ],\n  \"fonts\": [\n";
	auto rect_json = [](const RECT& r) {
		wchar_t s[96];
		swprintf_s(s, L"[%ld, %ld, %ld, %ld]", r.left, r.top, r.right, r.bottom);
		return std::wstring(s);
	};
	for (size_t i = 0; i < font_shots.size(); i++) {
		auto& f = font_shots[i];
		js += L"    {\"label\": \"" + json_escape(f.c->label) + L"\", \"note\": \"" + json_escape(f.c->note) +
			L"\", \"face\": \"" + json_escape(f.face) + L"\", \"actual\": \"" + json_escape(f.actual) +
			L"\", \"image\": \"" + json_escape(f.image) + L"\", \"specimen\": \"" + json_escape(f.specimen) + L"\"";
		if (!f.image.empty()) {
			js += L", \"bar\": " + rect_json(f.bar) + L", \"tabs\": [";
			for (size_t k = 0; k < f.tabs.size(); k++) js += (k ? L", " : L"") + rect_json(f.tabs[k]);
			js += L"]";
		}
		js += std::wstring(L"}") + (i + 1 < font_shots.size() ? L",\n" : L"\n");
	}
	js += L"  ]\n}\n";
	std::string utf8 = wide_to_utf8(js);
	HANDLE f = CreateFileW((dir + L"\\samples.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f != INVALID_HANDLE_VALUE) {
		DWORD wr = 0;
		WriteFile(f, utf8.data(), (DWORD)utf8.size(), &wr, nullptr);
		CloseHandle(f);
	}
	log(L"見本の書き出しを終えた (" + std::to_wstring(scenes.size()) + L" 場面)");
	notify(format_s(ui::EXPORT_DONE, dir), MB_ICONINFORMATION);
}

//---------------------------------------------------------------------
//	メイン窓の監視
//---------------------------------------------------------------------

static LRESULT CALLBACK main_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	switch (msg) {
		case WM_SIZE: {
			LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
			if (g_active || g_cover) position_container();
			return r;
		}
		case WM_DPICHANGED: {
			LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
			g_scale = dpi_scale(hwnd);
			update_font();
			if (g_active) { position_container(); relayout(); }
			return r;
		}
		case WM_INITMENUPOPUP:
			// 本体がメニューを作り直していても、開く前に小分けを整える
			organize_menu();
			break;
		case WM_COMMAND:
			// メディアエクスプローラーは切り離せない (本体の仕様) ため、自由分割中は開かせない
			if (g_active && HIWORD(wp) == 0 && g_explorer_cmds.count(LOWORD(wp))) {
				notify(ui::EXPLORER_NOTICE, MB_ICONINFORMATION);
				return 0;
			}
			break;
		case WM_CLOSE: {
			// 終了前に標準の画面へ戻し、本体がその状態を保存するようにする
			// (プラグインを外しても標準の画面で起動できる状態を保つ)
			bool was_active = g_active;
			g_settings.resume = was_active;
			g_settings.save();
			log(L"本体の終了要求 (有効中=" + std::to_wstring(was_active) + L")");
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
	AppendMenuW(menu, MF_STRING | (g_settings.return_layout.empty() ? MF_CHECKED : 0), 1, ui::RETURN_AUTO);
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

// メニューの中から、文字が一致する項目を探す (サブメニューもたどる)。見つかれば項目のあるメニューと番号を返す
static bool find_menu_item_by_text(HMENU menu, const wchar_t* a, const wchar_t* b, HMENU& out_menu, UINT& out_id) {
	int n = GetMenuItemCount(menu);
	for (int i = 0; i < n; i++) {
		if (HMENU sub = GetSubMenu(menu, i)) {
			if (find_menu_item_by_text(sub, a, b, out_menu, out_id)) return true;
			continue;
		}
		wchar_t buf[256] = {};
		GetMenuStringW(menu, i, buf, 256, MF_BYPOSITION);
		if (wcscmp(buf, a) == 0 || wcscmp(buf, b) == 0) {
			out_menu = menu;
			out_id = GetMenuItemID(menu, i);
			return true;
		}
	}
	return false;
}

// 切り替え項目の文字を、現在の状態に合わせる (無効中:「有効化」 / 有効中:「無効化」)
static void update_toggle_menu() {
	HWND main = g_main ? g_main : find_main_window();
	HMENU bar = main ? GetMenu(main) : nullptr;
	if (!bar) return;
	HMENU menu = nullptr;
	UINT id = 0;
	if (!find_menu_item_by_text(bar, ui::MENU_ENABLE, ui::MENU_DISABLE, menu, id)) {
		log(L"切り替え項目がメニューに見つからない");
		return;
	}
	const wchar_t* label = g_active ? ui::MENU_DISABLE : ui::MENU_ENABLE;
	MENUITEMINFOW mii = { sizeof(mii) };
	mii.fMask = MIIM_STRING;
	mii.dwTypeData = (LPWSTR)label;
	SetMenuItemInfoW(menu, id, FALSE, &mii);
}

// メニューの中から、文字が一致するサブメニューを探す (サブメニューもたどる)
static HMENU find_popup_by_text(HMENU menu, const wchar_t* text) {
	int n = GetMenuItemCount(menu);
	for (int i = 0; i < n; i++) {
		HMENU sub = GetSubMenu(menu, i);
		if (!sub) continue;
		wchar_t buf[256] = {};
		GetMenuStringW(menu, i, buf, 256, MF_BYPOSITION);
		if (wcscmp(buf, text) == 0) return sub;
		if (HMENU found = find_popup_by_text(sub, text)) return found;
	}
	return nullptr;
}

// 「TP_GUIdocker」メニューを用途ごとに小分けにする
// 本体の登録の仕組みは1段の階層までしか作れないため、本体がメニューを作った後に並べ替える。
// 項目の番号 (押した時の動作) はそのまま使うので、働きは変わらない。何度呼んでもよい
static void organize_menu() {
	HWND main = g_main ? g_main : find_main_window();
	HMENU bar = main ? GetMenu(main) : nullptr;
	if (!bar) return;
	HMENU tp = find_popup_by_text(bar, TPGD_NAME);
	if (!tp) return;

	// 平たく並んでいる項目を集める (既に小分け済みなら何もしない)
	struct Item { UINT id; std::wstring text; };
	std::vector<Item> items;
	int n = GetMenuItemCount(tp);
	for (int i = 0; i < n; i++) {
		if (GetSubMenu(tp, i)) return;  // 小分け済み
		UINT id = GetMenuItemID(tp, i);
		if (id == 0 || id == (UINT)-1) continue;  // 区切り線
		wchar_t buf[256] = {};
		GetMenuStringW(tp, i, buf, 256, MF_BYPOSITION);
		items.push_back({ id, buf });
	}
	if (items.empty()) return;

	// 文字で項目を取り出す (取り出したものは一覧から外す)
	auto take = [&](std::initializer_list<std::wstring> texts, Item& out) {
		for (auto it = items.begin(); it != items.end(); ++it) {
			for (auto& t : texts) {
				if (it->text == t) {
					out = *it;
					items.erase(it);
					return true;
				}
			}
		}
		return false;
	};
	auto add = [](HMENU m, const Item& it) { AppendMenuW(m, MF_STRING, it.id, it.text.c_str()); };

	// 元の項目を消してから、決めた順に並べ直す
	while (GetMenuItemCount(tp) > 0) RemoveMenu(tp, 0, MF_BYPOSITION);
	Item it;
	if (take({ ui::MENU_ENABLE, ui::MENU_DISABLE }, it)) add(tp, it);
	AppendMenuW(tp, MF_SEPARATOR, 0, nullptr);

	HMENU ws = CreatePopupMenu();
	for (int i = 1; i <= 9; i++) {
		wchar_t name[128];
		swprintf_s(name, ui::MENU_WS_SWITCH, i);
		if (take({ name }, it)) add(ws, it);
	}
	AppendMenuW(tp, MF_POPUP, (UINT_PTR)ws, ui::MENU_WS_GROUP);
	if (take({ ui::MENU_MAXIMIZE }, it)) add(tp, it);
	if (take({ ui::MENU_LOCK, ui::MENU_UNLOCK }, it)) add(tp, it);
	AppendMenuW(tp, MF_SEPARATOR, 0, nullptr);

	HMENU view = CreatePopupMenu();
	if (take({ ui::MENU_DROP_STYLE_COMPASS, ui::MENU_DROP_STYLE_REGION }, it)) add(view, it);
	if (take({ ui::WS_MENU_COLOR }, it)) add(view, it);
	if (take({ ui::WS_MENU_COLOR_RESET }, it)) add(view, it);
	AppendMenuW(tp, MF_POPUP, (UINT_PTR)view, ui::MENU_GROUP_VIEW);

	HMENU layout = CreatePopupMenu();
	if (take({ ui::MENU_RESET }, it)) add(layout, it);
	if (take({ ui::MENU_CHOOSE_RETURN }, it)) add(layout, it);
	AppendMenuW(tp, MF_POPUP, (UINT_PTR)layout, ui::MENU_GROUP_LAYOUT);
	AppendMenuW(tp, MF_SEPARATOR, 0, nullptr);

#ifdef TPGD_DEV
	// 開発用 (配布版には項目自体が無い)
	HMENU tools = CreatePopupMenu();
	if (take({ ui::MENU_EXPORT_SAMPLES }, it)) add(tools, it);
	AppendMenuW(tp, MF_POPUP, (UINT_PTR)tools, ui::MENU_GROUP_TOOLS);
#endif

	// 想定外の項目 (文言の変更直後など) は末尾に残す
	if (!items.empty()) {
		AppendMenuW(tp, MF_SEPARATOR, 0, nullptr);
		for (auto& rest : items) add(tp, rest);
		log(L"メニューの小分けで、置き場所の決まっていない項目が " + std::to_wstring(items.size()) + L" 件あった");
	}
	log(L"「TP_GUIdocker」メニューを小分けにした");
}

// 「レイアウトをロック」の項目の文字を、今の状態に合わせる
static void update_lock_menu() {
	HWND main = g_main ? g_main : find_main_window();
	HMENU bar = main ? GetMenu(main) : nullptr;
	if (!bar) return;
	HMENU menu = nullptr;
	UINT id = 0;
	if (!find_menu_item_by_text(bar, ui::MENU_LOCK, ui::MENU_UNLOCK, menu, id)) return;
	MENUITEMINFOW mii = { sizeof(mii) };
	mii.fMask = MIIM_STRING;
	mii.dwTypeData = (LPWSTR)(g_settings.locked ? ui::MENU_UNLOCK : ui::MENU_LOCK);
	SetMenuItemInfoW(menu, id, FALSE, &mii);
}

// 「ドラッグ時の表示」の項目の文字を、今の方式に合わせる
static void update_drop_style_menu() {
	HWND main = g_main ? g_main : find_main_window();
	HMENU bar = main ? GetMenu(main) : nullptr;
	if (!bar) return;
	HMENU menu = nullptr;
	UINT id = 0;
	if (!find_menu_item_by_text(bar, ui::MENU_DROP_STYLE_COMPASS, ui::MENU_DROP_STYLE_REGION, menu, id)) return;
	MENUITEMINFOW mii = { sizeof(mii) };
	mii.fMask = MIIM_STRING;
	mii.dwTypeData = (LPWSTR)(drop_style() == 0 ? ui::MENU_DROP_STYLE_COMPASS : ui::MENU_DROP_STYLE_REGION);
	SetMenuItemInfoW(menu, id, FALSE, &mii);
}

static void reset_tree() {
	g_root = default_tree();
	if (g_active) {
		// 初期配置に含まれないパネルは、浮いた窓へ戻す
		for (auto it = g_panels.begin(); it != g_panels.end();) {
			if (find_leaf_with(g_root.get(), it->key)) { ++it; continue; }
			release_panel(*it);
			add_float(it->hwnd, it->key);
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
	if (!n || !n->is_split || !g_max_key.empty()) return nullptr;  // 最大化中は境界線を動かさない
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

	if (!g_active) {
		// 起動時の「準備中」の覆い
		fill(rc, c_bg);
		SetTextColor(dc, c_text_dis);
		RECT r = rc;
		DrawTextW(dc, ui::COVER, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	} else if (g_root) {
		// ワークスペースの帯
		fill(g_bar, c_tabbar);
		COLORREF c_ws_sel = theme_color();
		for (auto& w : g_ws_hits) {
			bool sel = (w.index == g_settings.active_ws);
			if (sel) fill(w.rect, c_ws_sel);
			SetTextColor(dc, sel ? c_text : c_text_dis);
			RECT r = w.rect;
			DrawTextW(dc, g_settings.workspaces[w.index].name.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		{
			SetTextColor(dc, c_text_dis);
			RECT r = g_ws_add;
			DrawTextW(dc, ui::WS_ADD, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			if (g_settings.locked) fill(g_lock_rect, c_ws_sel);
			draw_lock_icon(dc, g_lock_rect, g_settings.locked, g_settings.locked ? c_text : c_text_dis);
		}

		for_each_leaf(g_root.get(), [&](Node* leaf) {
			if (IsRectEmpty(&leaf->rect)) return;  // 最大化中に隠している葉
			fill(leaf->tabs, c_tabbar);
			if (leaf->active.empty()) {
				fill(leaf->body, c_bg);
				SetTextColor(dc, c_text_dis);
				RECT r = leaf->body;
				DrawTextW(dc, ui::EMPTY_AREA, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			}
		});
		for (auto& t : g_tab_hits) {
			bool sel = (t.leaf->active == t.key);
			if (sel) {
				fill(t.rect, c_tab_sel);
				// 選択中のタブの下線 (テーマの色)
				int line = scaled(ui::TAB_ACTIVE_LINE);
				if (line > 0) fill({ t.rect.left, t.rect.bottom - line, t.rect.right, t.rect.bottom }, c_ws_sel);
			}
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
			SetTimer(hwnd, TIMER_STARTUP, 100, nullptr);
			return 0;
		case WM_APP_TOGGLE:
			if (g_busy) return 0;
			if (g_active) {
				g_settings.resume = false;
				g_settings.save();
				exit_dock(true);
			} else {
				enter_dock(true);
			}
			update_toggle_menu();
			update_lock_menu();
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
				// 本体のメイン窓が表示されるまで待つ (最大 約20秒)
				HWND main = find_main_window();
				if ((!main || !IsWindowVisible(main)) && ++g_startup_tries < 200) return 0;
				KillTimer(hwnd, TIMER_STARTUP);
				if (!main) {
					log(L"本体のメイン窓が見つからないまま起動処理を終える");
					return 0;
				}
				g_main = main;
				subclass_main();
				organize_menu();
				update_toggle_menu();
				update_lock_menu();
				// 前回が有効のまま終了、または異常終了した場合は有効化して始める
				if (g_settings.resume || g_settings.session_active) {
					// 本体の通常の画面が見えないよう、直ちに「準備中」の覆いを掛ける
					g_scale = dpi_scale(g_main);
					update_font();
					show_cover();
					// 本体の初期化が落ち着くのを少し待ってから開始する
					SetTimer(hwnd, TIMER_STARTUP_ENTER, 300, nullptr);
				}
			} else if (wp == TIMER_STARTUP_ENTER) {
				// 本体のメニューの中身ができるのを待つ (環境によっては、利用者がメニューを開くまで空のまま)
				if (!host_menus_ready(g_main)) {
					if (++g_menu_wait_tries == 1) log(L"起動時の有効化: 本体のメニューの中身がまだ無いため待つ");
					if (g_menu_wait_tries < 10) return 0;   // 300ms ごと、約3秒まで
					KillTimer(hwnd, TIMER_STARTUP_ENTER);
					g_menu_wait_tries = 0;
					// 失敗の知らせは出さず、通常の画面のまま使えるようにする。
					// 次回の起動でも引き継ぐよう resume はそのまま残す。メニューを開けば中身ができるので、
					// 利用者は「TP_GUIdockerを有効化」で有効化できる
					log(L"起動時の有効化を見送る: 本体のメニューの中身が作られていない (メニューを開いた後の「有効化」で有効化できる)");
					hide_cover();
					update_toggle_menu();
					return 0;
				}
				KillTimer(hwnd, TIMER_STARTUP_ENTER);
				if (g_menu_wait_tries > 0) log(L"起動時の有効化: 本体のメニューの中身ができた");
				g_menu_wait_tries = 0;
				log(L"前回の状態を引き継いで有効化 (resume=" + std::to_wstring(g_settings.resume) +
					L" session_active=" + std::to_wstring(g_settings.session_active) + L")");
				enter_dock(true);
				hide_cover();
				update_toggle_menu();
				update_lock_menu();
			} else if (wp == TIMER_WATCH) {
				watch();
			} else if (wp == TIMER_LONG_PRESS) {
				KillTimer(hwnd, TIMER_LONG_PRESS);
				if (g_dd.state == DragState::Pressed) {
					// 長押しが成立: ドラッグの準備ができたことを、カーソルとタブの影で知らせる
					g_dd.state = DragState::Armed;
					SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
					g_ghost.show(g_main, g_dd.title, g_font, style_color("GroupingSelect", RGB(0x48, 0x48, 0x48)),
						style_color("Text", RGB(0xff, 0xff, 0xff)), ui::DRAG_GHOST_ALPHA, tab_height());
					SetTimer(hwnd, TIMER_DRAG_POLL, 30, nullptr);
				}
			} else if (wp == TIMER_DRAG_POLL) {
				// Esc キーで取り消す
				if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) end_drag(false);
			} else if (wp == TIMER_REENTER) {
				KillTimer(hwnd, TIMER_REENTER);
				if (!g_main_destroying && g_main && IsWindow(g_main)) {
					log(L"本体の終了が取り消されたため、再び有効化する");
					enter_dock(false);
					update_toggle_menu();
				}
			}
			return 0;

		case WM_SIZE:
			relayout();
			return 0;

		case WM_APP_TOGGLE_DROP_STYLE:
			g_settings.drop_style = (drop_style() == 0) ? 1 : 0;
			g_settings.save();
			update_drop_style_menu();
			log(std::wstring(L"ドラッグ時の表示を切り替え: ") + (g_settings.drop_style == 0 ? L"十字ボタン式" : L"区画の領域式"));
			return 0;
		case WM_APP_WS_SWITCH:
			switch_workspace((int)wp, false);
			return 0;
		case WM_APP_MAXIMIZE:
			if (!g_busy) maximize_under_cursor();
			return 0;
		case WM_APP_THEME_COLOR:
			choose_theme_color(wp != 0);
			return 0;
		case WM_APP_EXPORT_SAMPLES:
			if (!g_busy) export_samples();
			return 0;
		case WM_APP_TOGGLE_LOCK:
			toggle_lock();
			return 0;
		case WM_APP_DOCK_FLOAT: {
			DropTarget t = g_pending_dock;
			g_pending_dock = {};
			if (g_active && !g_busy) dock_float((HWND)wp, t.leaf, t.zone, t.tab_index);
			return 0;
		}

		case WM_SETCURSOR:
			if (LOWORD(lp) == HTCLIENT && g_root) {
				if (g_dd.state == DragState::Armed || g_dd.state == DragState::Dragging) {
					SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
					return TRUE;
				}
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
			if (!g_root || !g_active || g_busy) return 0;
			// ワークスペースの帯
			if (PtInRect(&g_bar, pt)) {
				if (PtInRect(&g_lock_rect, pt)) {
					toggle_lock();
				} else if (PtInRect(&g_ws_add, pt)) {
					new_workspace();
				} else {
					for (auto& w : g_ws_hits) {
						if (PtInRect(&w.rect, pt)) {
							PostMessageW(hwnd, WM_APP_WS_SWITCH, (WPARAM)w.index, 0);
							break;
						}
					}
				}
				return 0;
			}
			if (Node* s = hit_splitter(g_root.get(), pt)) {
				g_drag = s;
				SetCapture(hwnd);
				return 0;
			}
			for (auto& t : g_tab_hits) {
				if (!PtInRect(&t.rect, pt)) continue;
				std::wstring key = t.key;
				g_last_key = key;
				if (t.leaf->active != key) {
					t.leaf->active = key;
					relayout();
					save_tree();
				}
				// ロック中はタブを動かさない
				if (g_settings.locked) return 0;
				// 長押しでドラッグの準備に入る
				g_dd = {};
				g_dd.state = DragState::Pressed;
				g_dd.key = key;
				g_dd.title = panel_title(key);
				GetCursorPos(&g_dd.down);
				SetCapture(hwnd);
				SetTimer(hwnd, TIMER_LONG_PRESS, ui::LONG_PRESS_MS, nullptr);
				return 0;
			}
			return 0;
		}
		case WM_MOUSEMOVE: {
			if (g_drag) {
				POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
				const RECT& r = g_drag->rect;
				int total = (g_drag->vertical ? (r.bottom - r.top) : (r.right - r.left)) - gap_size();
				int pos = g_drag->vertical ? (pt.y - r.top) : (pt.x - r.left);
				int lo = scaled(ui::MIN_PANE), hi = total - scaled(ui::MIN_PANE);
				if (total > 0 && hi > lo) {
					pos = std::clamp(pos, lo, hi);
					g_drag->ratio = (double)pos / total;
					relayout();
					UpdateWindow(hwnd);
				}
				return 0;
			}
			if (g_dd.state == DragState::None || g_dd.float_hwnd) return 0;
			POINT cur;
			GetCursorPos(&cur);
			int moved = std::max(std::abs(cur.x - g_dd.down.x), std::abs(cur.y - g_dd.down.y));
			int threshold = scaled(ui::DRAG_START_PX);
			if (g_dd.state == DragState::Pressed) {
				// 長押しの前に動かした場合はドラッグにしない (誤ってパネルが外れるのを防ぐ)
				if (moved > threshold) end_drag(false);
			} else if (g_dd.state == DragState::Armed) {
				if (moved > threshold) begin_drag();
			} else if (g_dd.state == DragState::Dragging) {
				g_ghost.move(cur);
				update_drop_target(cur);
			}
			return 0;
		}
		case WM_LBUTTONUP:
			if (g_drag) {
				ReleaseCapture();
			} else if (g_dd.state != DragState::None && !g_dd.float_hwnd) {
				end_drag(true);
			}
			return 0;
		case WM_CAPTURECHANGED:
			if (g_drag) {
				g_drag = nullptr;
				save_tree();
			}
			if (g_dd.state != DragState::None && !g_dd.float_hwnd) end_drag(false);
			return 0;
		case WM_RBUTTONUP: {
			if (!g_root || !g_active || g_busy || g_dd.state != DragState::None) return 0;
			POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
			POINT screen = pt;
			ClientToScreen(hwnd, &screen);
			if (PtInRect(&g_bar, pt)) {
				for (auto& w : g_ws_hits) {
					if (PtInRect(&w.rect, pt)) {
						show_workspace_menu(w.index, screen);
						break;
					}
				}
				return 0;
			}
			for (auto& t : g_tab_hits) {
				if (PtInRect(&t.rect, pt)) {
					show_tab_menu(t.leaf, t.key, screen);
					return 0;
				}
			}
			// タブ以外 (タブ帯の空き・空き区画) では、分割と空き区画を閉じる操作のみ
			if (Node* leaf = leaf_at(g_root.get(), pt)) show_tab_menu(leaf, L"", screen);
			return 0;
		}

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

// このプラグイン (DLL) 自身
extern "C" IMAGE_DOS_HEADER __ImageBase;
static HINSTANCE dll_instance() { return (HINSTANCE)&__ImageBase; }

bool dock_init() {
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpszClassName = CONTAINER_CLASS;
	wc.lpfnWndProc = container_proc;
	wc.hInstance = dll_instance();
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	if (!RegisterClassExW(&wc)) {
		bool registered = false;
		if (GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
			// 本体の起動中にプラグインが読み込み直された場合 (パッケージの導入時など)、前の読み込みの登録が残っていることがある。
			// 残った登録は取り外し済みの処理を指していて危険なため、消してから登録し直す
			// (0.4.7 までは本体 (exe) の名義で登録していたため、その名義の分も消す)
			UnregisterClassW(CONTAINER_CLASS, dll_instance());
			UnregisterClassW(CONTAINER_CLASS, GetModuleHandleW(nullptr));
			log(L"前の読み込みで残った入れ物の窓クラスを登録し直す");
			registered = RegisterClassExW(&wc) != 0;
		}
		if (!registered) {
			log(L"入れ物の窓クラスを登録できない");
			return false;
		}
	}
	// メイン窓がまだ無いため、いったん非表示の独立窓として作る (開始時にメイン窓の子にする)
	g_container = CreateWindowExW(WS_EX_TOOLWINDOW, CONTAINER_CLASS, TPGD_NAME, WS_POPUP | WS_CLIPCHILDREN,
		0, 0, 100, 100, nullptr, nullptr, dll_instance(), nullptr);
	if (!g_container) {
		log(L"入れ物の窓を作れない");
		return false;
	}
	g_scale = dpi_scale(nullptr);
	load_private_fonts();
	update_font();

	// 錠前の絵 (GDI+) と、その説明 (ツールチップ) の準備
	Gdiplus::GdiplusStartupInput gsi;
	Gdiplus::GdiplusStartup(&g_gdiplus_token, &gsi, nullptr);
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
	InitCommonControlsEx(&icc);
	create_lock_tooltip();
	return true;
}

void dock_cleanup_legacy() {
#ifdef TPGD_DEV
	// 開発中の診断で作った試験用レイアウトを削除する (配布版の利用者の環境には無いため、開発版だけで行う)
	WIN32_FIND_DATAW fd;
	std::wstring dir = layout_dir();
	HANDLE h = FindFirstFileW((dir + L"\\" TPGD_NAME L"_試験*.layout").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		std::wstring path = dir + L"\\" + fd.cFileName;
		if (DeleteFileW(path.c_str())) log(L"不要になった試験用レイアウトを削除: " + std::wstring(fd.cFileName));
	} while (FindNextFileW(h, &fd));
	FindClose(h);
#endif
}

void dock_request_startup() { if (g_container) PostMessageW(g_container, WM_APP_STARTUP, 0, 0); }
void dock_request_toggle() { if (g_container) PostMessageW(g_container, WM_APP_TOGGLE, 0, 0); }
void dock_request_toggle_drop_style() { if (g_container) PostMessageW(g_container, WM_APP_TOGGLE_DROP_STYLE, 0, 0); }
void dock_request_switch_workspace(int index) { if (g_container) PostMessageW(g_container, WM_APP_WS_SWITCH, (WPARAM)index, 0); }
void dock_request_maximize() { if (g_container) PostMessageW(g_container, WM_APP_MAXIMIZE, 0, 0); }
void dock_request_toggle_lock() { if (g_container) PostMessageW(g_container, WM_APP_TOGGLE_LOCK, 0, 0); }
void dock_request_export_samples() { if (g_container) PostMessageW(g_container, WM_APP_EXPORT_SAMPLES, 0, 0); }
void dock_request_theme_color(bool reset) { if (g_container) PostMessageW(g_container, WM_APP_THEME_COLOR, reset ? 1 : 0, 0); }
void dock_request_reset_tree() { if (g_container) PostMessageW(g_container, WM_APP_RESET_TREE, 0, 0); }
void dock_request_choose_return_layout() { if (g_container) PostMessageW(g_container, WM_APP_CHOOSE_RETURN, 0, 0); }

// プラグインの解放時 (本体の終了時のほか、本体の起動中にプラグインが読み込み直される時にも呼ばれる)。
// 解放後に本体がこの DLL の処理を呼ばないよう、差し込んだもの (窓の横取り・窓クラス・重ね窓) をすべて外す
void dock_shutdown() {
	log(L"プラグインの解放 (有効中=" + std::to_wstring(g_active) + L")");
	if (GetCapture() == g_container) ReleaseCapture();
	g_overlay.hide();
	g_ghost.hide();
	if (g_active) exit_dock(g_main && IsWindow(g_main) && !g_main_destroying);
	if (g_main_subclassed && g_main && IsWindow(g_main)) RemoveWindowSubclass(g_main, main_subclass_proc, MAIN_SUBCLASS_ID);
	g_main_subclassed = false;
	if (g_container) DestroyWindow(g_container);
	g_container = nullptr;
	UnregisterClassW(CONTAINER_CLASS, dll_instance());
	if (g_font) DeleteObject(g_font);
	g_font = nullptr;
	unload_private_fonts();
	if (g_tooltip) DestroyWindow(g_tooltip);
	g_tooltip = nullptr;
	if (g_gdiplus_token) Gdiplus::GdiplusShutdown(g_gdiplus_token);
	g_gdiplus_token = 0;
}

} // namespace tpgd
