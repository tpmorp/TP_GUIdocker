//----------------------------------------------------------------------------------
//	自由分割画面 (入れ物・パネルの取り込みと返却・境界線・タブ・安全装置)
//
//	仕組み:
//	1. 本体のレイアウト機能で全パネルを「切り離し」状態にする (生成した TP_GUIdocker.layout を適用)
//	2. 切り離された独立窓を子窓化し、本体メイン窓の中に置いた入れ物へはめ込む
//	3. 入れ物が分割木に従って各パネルの位置と大きさを決める
//	4. 終了時は全パネルを返却し、戻り先レイアウトを本体に適用して標準の画面へ戻す
//----------------------------------------------------------------------------------
#include "dock.h"
#include "host.h"
#include "layout_file.h"
#include "settings.h"
#include "tree.h"
#include "diag.h"

#include <algorithm>
#include <memory>

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
	WM_APP_DIAG,
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

//---------------------------------------------------------------------
//	状態
//---------------------------------------------------------------------

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
static bool g_moving = false;          // 自分で位置を変えている最中
static int g_startup_tries = 0;

static std::vector<Panel> g_panels;
static std::unique_ptr<Node> g_root;
static std::vector<TabHit> g_tab_hits;

// 生成レイアウトで各セクションに与えた仮位置 (切り離し窓とセクション名の照合用)
struct LayoutSlot { std::wstring key; POINT pt; };
static std::vector<LayoutSlot> g_slots;

// 境界線のドラッグ
static Node* g_drag = nullptr;

// 描画用
static HFONT g_font = nullptr;
static double g_scale = 1.0;

//---------------------------------------------------------------------
//	寸法・配色 (本体の style.conf に合わせる)
//---------------------------------------------------------------------

static int gap_size() { return style_size("WindowSeparatorSize", 7, g_scale); }
static int tab_height() { return style_size("GroupTabHeight", 24, g_scale); }
static int footer_height() { return style_size("FooterHeight", 24, g_scale); }
static int min_pane() { return (int)(MIN_PANE * g_scale); }

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
//	パネル検索
//---------------------------------------------------------------------

static Panel* find_panel(HWND h) {
	for (auto& p : g_panels) if (p.hwnd == h) return &p;
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
	g_settings.tree = serialize_tree(g_root.get());
	g_settings.save();
}

//---------------------------------------------------------------------
//	レイアウトファイルの準備
//---------------------------------------------------------------------

static bool starts_with(const std::string& s, const char* prefix) {
	return s.rfind(prefix, 0) == 0;
}

void dock_prepare_layouts() {
	std::wstring dir = layout_dir();
	std::wstring return_path = dir + L"\\" + AUTO_RETURN_LAYOUT + L".layout";
	std::wstring dock_path = dir + L"\\" + DOCK_LAYOUT + L".layout";

	// aviutl2.ini には削除済みプラグインの古いセクションも残るため、
	// 本体自身が保存したレイアウトファイル (利用者が作成したもの) に載っているセクションに絞る
	// ※ 本体のレイアウトファイルが1つも無い場合は絞り込まない
	std::vector<std::string> known;
	for (auto& name : list_layouts()) {
		if (name == DOCK_LAYOUT || name == AUTO_RETURN_LAYOUT) continue;
		IniDoc d;
		if (!d.load(dir + L"\\" + name + L".layout")) continue;
		for (auto& s : d.sections) {
			if (std::find(known.begin(), known.end(), s.name) == known.end()) known.push_back(s.name);
		}
	}
	auto drop_unknown = [&](IniDoc& doc) {
		if (known.empty()) return;
		auto& secs = doc.sections;
		for (auto it = secs.begin(); it != secs.end();) {
			if (std::find(known.begin(), known.end(), it->name) == known.end()) {
				log(L"古いセクションを除外: " + utf8_to_wide(it->name));
				it = secs.erase(it);
			} else {
				++it;
			}
		}
	};

	// 本体の現在の配置 (前回終了時に本体が保存したもの) を読む ※読むだけで書き込まない
	IniDoc ini;
	IniDoc snapshot;
	if (ini.load(app_data_dir() + L"\\aviutl2.ini")) snapshot = extract_layout(ini);
	drop_unknown(snapshot);

	// 表示中のパネルが全て切り離されている配置は、自由分割の名残であり通常の配置ではない
	int visible = 0, floating = 0;
	for (auto& s : snapshot.sections) {
		if (s.get("hide") != "0") continue;
		visible++;
		if (s.get("floating") == "1") floating++;
	}
	bool snapshot_is_normal = visible > 0 && floating < visible;
	if (!snapshot_is_normal) {
		log(L"本体の配置が通常の状態ではない (表示 " + std::to_wstring(visible) + L" 枚中 " + std::to_wstring(floating) + L" 枚が切り離し)");
	}

	// 戻り先の自動保存: 前回が正常終了 (自由分割を正しく解除できた) で、通常の配置である場合のみ更新する
	// 異常終了時の本体配置はパネルが切り離されたままの可能性があるため、上書きしない
	IniDoc template_doc;
	if (!g_settings.session_active && snapshot_is_normal) {
		if (snapshot.save(return_path)) log(L"戻り先レイアウトを保存: " + return_path);
		else log(L"戻り先レイアウトを保存できない: " + return_path);
		template_doc = snapshot;
	} else if (template_doc.load(return_path)) {
		template_doc = extract_layout(template_doc);
		drop_unknown(template_doc);
		// 過去の版が書いた不要な項目・セクションを除いて保存し直す
		template_doc.save(return_path);
		log(L"戻り先レイアウトは更新せず、前回保存したものを使う");
	} else {
		template_doc = snapshot;
	}
	if (template_doc.sections.empty()) {
		log(L"本体の配置情報が読めないため、自由分割用レイアウトを生成できない");
		return;
	}

	// 自由分割用: 全パネルを切り離し状態にし、照合用に一意の仮位置を与える
	// 表示・非表示は直前の本体配置を引き継ぐ (非表示のものも切り離し状態にしておくと、
	// 後で「表示」メニューから開いた時に切り離し窓として現れ、自動で取り込める)
	g_slots.clear();
	int i = 0;
	for (auto& s : template_doc.sections) {
		if (!starts_with(s.name, "Window.")) continue;
		int x = 24 + (i % 16) * 36;
		int y = 24 + (i / 16) * 36;
		s.set("floating", "1");
		s.set("x", std::to_string(x));
		s.set("y", std::to_string(y));
		s.set("w", "480");
		s.set("h", "320");
		g_slots.push_back({ utf8_to_wide(s.name.substr(7)), { x, y } });
		i++;
	}
	if (template_doc.save(dock_path)) log(L"自由分割用レイアウトを生成: " + dock_path + L" (" + std::to_wstring(i) + L" 枚)");
	else log(L"自由分割用レイアウトを生成できない: " + dock_path);
}

// 切り離し窓のセクション名を決める
static std::wstring identify_key(HWND h, std::vector<bool>& used) {
	RECT r;
	GetWindowRect(h, &r);
	std::wstring title = window_text(h);

	// 1. 生成レイアウトの仮位置との照合
	int best = -1, best_dist = INT_MAX;
	for (size_t i = 0; i < g_slots.size(); i++) {
		if (used[i]) continue;
		int d = abs(r.left - g_slots[i].pt.x) + abs(r.top - g_slots[i].pt.y);
		if (d < best_dist) { best_dist = d; best = (int)i; }
	}
	int tolerance = (int)(16 * g_scale);
	if (best >= 0 && best_dist <= tolerance) {
		used[best] = true;
		g_settings.title_to_key[title] = g_slots[best].key;
		log(L"照合(位置) '" + title + L"' → " + g_slots[best].key + L" 誤差=" + std::to_wstring(best_dist));
		return g_slots[best].key;
	}

	// 2. 過去に学習した題名との対応
	auto it = g_settings.title_to_key.find(title);
	if (it != g_settings.title_to_key.end()) {
		log(L"照合(題名) '" + title + L"' → " + it->second +
			L" (位置 " + std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L")");
		return it->second;
	}

	// 3. 不明: 題名をそのまま識別名にする
	log(L"照合できない '" + title + L"' 位置=" + std::to_wstring(r.left) + L"," + std::to_wstring(r.top));
	return L"title:" + title;
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
	int pad = (int)(10 * g_scale);
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

// activate: 取り込んだパネルを葉の表示中タブにするか (後から開かれたパネル向け)
static bool capture_panel(HWND h, std::vector<bool>& used, bool activate) {
	Panel p;
	p.hwnd = h;
	p.title = window_text(h);
	p.key = identify_key(h, used);
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
		// 失敗: 元に戻す
		SetWindowLongPtrW(h, GWL_STYLE, p.style);
		SetWindowLongPtrW(h, GWL_EXSTYLE, p.ex_style);
		log(L"取り込み失敗 " + describe(h) + L" err=" + std::to_wstring(GetLastError()));
		return false;
	}
	p.assigned = { OFFSCREEN, OFFSCREEN, OFFSCREEN + 320, OFFSCREEN + 240 };
	p.visible = IsWindowVisible(h) != FALSE;
	g_panels.push_back(p);
	SetWindowSubclass(h, panel_subclass_proc, PANEL_SUBCLASS_ID, 0);
	g_moving = true;
	SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	g_moving = false;

	// 分割木での置き場所: 既知なら元の葉、未知なら既定の葉へ追加する
	// 表示中タブは、保存された選択を優先する (表示できない場合は relayout で補正される)
	Node* leaf = find_leaf_with(g_root.get(), p.key);
	if (!leaf) {
		leaf = default_leaf(g_root.get());
		if (leaf) leaf->panels.push_back(p.key);
	}
	if (leaf && (activate || leaf->active.empty())) leaf->active = p.key;
	log(L"取り込み " + p.key + L" " + describe(h));
	return true;
}

static int capture_floating_panels(bool activate) {
	std::vector<bool> used(g_slots.size(), false);
	// 既に取り込み済みのセクション名の仮位置は照合対象から外す
	for (auto& p : g_panels) {
		for (size_t i = 0; i < g_slots.size(); i++) if (g_slots[i].key == p.key) used[i] = true;
	}
	int n = 0;
	for (HWND h : find_floating_panels(g_main)) {
		if (find_panel(h)) continue;
		if (capture_panel(h, used, activate)) n++;
	}
	return n;
}

static void release_all_panels() {
	g_moving = true;
	for (auto it = g_panels.rbegin(); it != g_panels.rend(); ++it) {
		HWND h = it->hwnd;
		if (!IsWindow(h)) continue;
		RemoveWindowSubclass(h, panel_subclass_proc, PANEL_SUBCLASS_ID);
		bool visible = IsWindowVisible(h) != FALSE;
		SetParent(h, nullptr);
		SetWindowLongPtrW(h, GWL_STYLE, (it->style & ~WS_VISIBLE) | (visible ? WS_VISIBLE : 0));
		SetWindowLongPtrW(h, GWL_EXSTYLE, it->ex_style);
		SetWindowLongPtrW(h, GWLP_HWNDPARENT, (LONG_PTR)it->owner);
		SetWindowPos(h, nullptr, it->float_rect.left, it->float_rect.top,
			it->float_rect.right - it->float_rect.left, it->float_rect.bottom - it->float_rect.top,
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		log(L"返却 " + it->key);
	}
	g_moving = false;
	g_panels.clear();
}

//---------------------------------------------------------------------
//	監視 (安全装置)
//---------------------------------------------------------------------

static void watch() {
	if (!g_active) return;
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

	// 「表示」メニュー等で新たに開かれたパネル (切り離し窓として現れる) を取り込む
	if (capture_floating_panels(true) > 0) changed = true;

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

static bool enter_dock(bool interactive) {
	if (g_active) return true;
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

	// 専用レイアウトが本体のメニューにあるか (初回は本体の再起動が必要)
	bool has_dock = find_menu_command(g_main, { L"表示", L"レイアウト", DOCK_LAYOUT }) != 0;
	bool has_return = find_menu_command(g_main, { L"表示", L"レイアウト", g_settings.effective_return_layout() }) != 0;
	if (!has_dock || !has_return) {
		log(L"専用レイアウトが本体のメニューに無い dock=" + std::to_wstring(has_dock) + L" return=" + std::to_wstring(has_return));
		UINT restart = find_menu_command(g_main, { L"編集", L"アプリケーションを再起動" });
		std::wstring msg = L"TP_GUIdocker の準備ができました。\n\n"
			L"本体がレイアウト一覧を読み込み直す必要があるため、AviUtl2 の再起動が必要です (初回のみ)。";
		if (interactive && restart) {
			msg += L"\n\n今すぐ再起動しますか？ 再起動後、自動で自由分割になります。";
			if (MessageBoxW(g_main, msg.c_str(), TPGD_NAME, MB_YESNO | MB_ICONINFORMATION) == IDYES) {
				g_settings.resume = true;
				g_settings.save();
				PostMessageW(g_main, WM_COMMAND, MAKEWPARAM(restart, 0), 0);
			}
		} else if (interactive) {
			MessageBoxW(g_main, msg.c_str(), TPGD_NAME, MB_OK | MB_ICONINFORMATION);
		}
		return false;
	}

	// 分割木を用意する
	if (!g_root) {
		g_root = parse_tree(g_settings.tree);
		if (!g_root) g_root = default_tree();
	}

	g_settings.session_active = true;
	g_settings.save();
	log(L"===== 自由分割を開始 =====");

	// 1. 全パネルを切り離す
	if (!apply_host_layout(g_main, DOCK_LAYOUT)) {
		g_settings.session_active = false;
		g_settings.save();
		return false;
	}

	// 2. 入れ物をメイン窓の中へ置く
	SetWindowLongPtrW(g_container, GWL_STYLE, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	SetParent(g_container, g_main);
	g_active = true;
	position_container();
	ShowWindow(g_container, SW_SHOWNA);

	// 3. 切り離し窓を取り込む
	int n = capture_floating_panels(false);
	log(L"取り込み数=" + std::to_wstring(n));
	if (n == 0) {
		log(L"パネルを1枚も取り込めないため中止する (安全装置)");
		exit_dock(true);
		if (interactive) MessageBoxW(g_main, L"パネルを取り込めませんでした。標準の画面に戻します。", TPGD_NAME, MB_OK | MB_ICONWARNING);
		return false;
	}
	// 照合できなかったパネルの仮の識別名 (title:...) のうち、今回いないものは分割木から除く
	for_each_leaf(g_root.get(), [](Node* leaf) {
		auto& v = leaf->panels;
		v.erase(std::remove_if(v.begin(), v.end(), [](const std::wstring& k) {
			if (k.rfind(L"title:", 0) != 0) return false;
			for (auto& p : g_panels) if (p.key == k) return false;
			return true;
		}), v.end());
	});
	relayout();
	save_tree();
	SetTimer(g_container, TIMER_WATCH, 300, nullptr);
	return true;
}

static void exit_dock(bool apply_return) {
	if (!g_active) return;
	log(L"===== 自由分割を終了 =====");
	KillTimer(g_container, TIMER_WATCH);
	if (g_root) save_tree();
	release_all_panels();
	g_active = false;
	g_drag = nullptr;

	// 入れ物を外す
	ShowWindow(g_container, SW_HIDE);
	SetParent(g_container, nullptr);
	SetWindowLongPtrW(g_container, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN);

	// 戻り先レイアウトで標準の画面へ戻す
	if (apply_return && g_main && IsWindow(g_main)) {
		if (!apply_host_layout(g_main, g_settings.effective_return_layout()) && !g_settings.return_layout.empty()) {
			apply_host_layout(g_main, AUTO_RETURN_LAYOUT);
		}
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
//	戻り先レイアウトの選択
//---------------------------------------------------------------------

static void choose_return_layout() {
	HWND owner = g_main ? g_main : find_main_window();
	if (!owner) return;
	HMENU menu = CreatePopupMenu();
	auto names = list_layouts();
	names.erase(std::remove_if(names.begin(), names.end(), [](const std::wstring& n) {
		return n == DOCK_LAYOUT || n == AUTO_RETURN_LAYOUT;
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
			RECT line = { t.rect.right - 1, t.rect.top + 4, t.rect.right, t.rect.bottom - 4 };
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
			g_settings.resume = false;
			exit_dock(true);
			g_settings.save();
			return 0;
		case WM_APP_RESET_TREE:
			g_root = default_tree();
			if (g_active) {
				// 取り込み済みパネルを初期配置の葉へ入れ直す
				for (auto& p : g_panels) {
					if (!find_leaf_with(g_root.get(), p.key)) {
						if (Node* l = default_leaf(g_root.get())) l->panels.push_back(p.key);
					}
				}
				relayout();
			}
			save_tree();
			log(L"配置を初期状態に戻した");
			return 0;
		case WM_APP_CHOOSE_RETURN:
			choose_return_layout();
			return 0;
		case WM_APP_RELAYOUT:
			if (g_active) relayout();
			return 0;
		case WM_APP_DIAG:
			if (g_active) {
				MessageBoxW(g_main, L"診断は自由分割を終了してから実行してください。", TPGD_NAME, MB_OK | MB_ICONINFORMATION);
			} else {
				diag_run(hwnd);
			}
			return 0;

		case WM_TIMER:
			if (wp == TIMER_STARTUP) {
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
			} else if (wp == TIMER_DIAG_DIALOG) {
				diag_on_timer(hwnd);
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
				int lo = min_pane(), hi = total - min_pane();
				if (total > 0 && hi > lo) {
					pos = std::clamp(pos, lo, hi);
					g_drag->ratio = (double)pos / total;
					relayout();
					UpdateWindow(hwnd);
				}
			}
			return 0;
		case WM_LBUTTONUP:
			if (g_drag) {
				ReleaseCapture();
			}
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

void dock_request_startup() { if (g_container) PostMessageW(g_container, WM_APP_STARTUP, 0, 0); }
void dock_request_enter() { if (g_container) PostMessageW(g_container, WM_APP_ENTER, 0, 0); }
void dock_request_exit() { if (g_container) PostMessageW(g_container, WM_APP_EXIT, 0, 0); }
void dock_request_reset_tree() { if (g_container) PostMessageW(g_container, WM_APP_RESET_TREE, 0, 0); }
void dock_request_choose_return_layout() { if (g_container) PostMessageW(g_container, WM_APP_CHOOSE_RETURN, 0, 0); }
void dock_request_diag() { if (g_container) PostMessageW(g_container, WM_APP_DIAG, 0, 0); }

void dock_shutdown() {
	if (g_active) exit_dock(g_main && IsWindow(g_main) && !g_main_destroying);
	if (g_main_subclassed && g_main && IsWindow(g_main)) RemoveWindowSubclass(g_main, main_subclass_proc, MAIN_SUBCLASS_ID);
	if (g_container) DestroyWindow(g_container);
	g_container = nullptr;
	if (g_font) DeleteObject(g_font);
	g_font = nullptr;
}

} // namespace tpgd
