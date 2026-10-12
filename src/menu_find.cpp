//----------------------------------------------------------------------------------
//	本体のメニュー項目の探索
//----------------------------------------------------------------------------------
#include "menu_find.h"

namespace tpgd {

static const wchar_t* VIEW_MENU = L"表示";
static const wchar_t* LAYOUT_MENU = L"レイアウト";
static const wchar_t* LAYOUT_CREATE = L"レイアウトを作成";

static bool is_space(wchar_t c) { return c == L' ' || c == L'　' || c == L'\t'; }

std::wstring normalize_menu_text(const std::wstring& src) {
	std::wstring s = src;
	// ショートカット表示 (タブ以降)
	auto tab = s.find(L'\t');
	if (tab != std::wstring::npos) s.resize(tab);
	auto trim_end = [&] { while (!s.empty() && is_space(s.back())) s.pop_back(); };
	trim_end();
	// 末尾のアクセスキー表記「(&V)」「（&V）」
	if (s.size() >= 4) {
		size_t n = s.size();
		wchar_t open = s[n - 4], amp = s[n - 3], close = s[n - 1];
		if ((open == L'(' || open == L'（') && amp == L'&' && (close == L')' || close == L'）')) {
			s.resize(n - 4);
			trim_end();
		}
	}
	// 末尾の「...」「…」
	while (true) {
		if (s.size() >= 3 && s.compare(s.size() - 3, 3, L"...") == 0) s.resize(s.size() - 3);
		else if (!s.empty() && s.back() == L'…') s.pop_back();
		else break;
		trim_end();
	}
	// アクセスキーの「&」(「&&」は文字の「&」)
	std::wstring o;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == L'&') {
			if (i + 1 < s.size() && s[i + 1] == L'&') { o += L'&'; i++; }
			continue;
		}
		o += s[i];
	}
	// 前の空白
	size_t b = 0;
	while (b < o.size() && is_space(o[b])) b++;
	return o.substr(b);
}

std::wstring menu_item_text(HMENU menu, int pos) {
	wchar_t buf[256] = {};
	GetMenuStringW(menu, pos, buf, 256, MF_BYPOSITION);
	return normalize_menu_text(buf);
}

int find_menu_item(HMENU menu, const std::wstring& text) {
	if (!menu) return -1;
	std::wstring want = normalize_menu_text(text);
	int n = GetMenuItemCount(menu);
	for (int i = 0; i < n; i++) {
		if (menu_item_text(menu, i) == want) return i;
	}
	return -1;
}

static UINT command_at(HMENU menu, int pos) {
	if (!menu || pos < 0) return 0;
	UINT id = GetMenuItemID(menu, pos);
	return id == (UINT)-1 ? 0 : id;
}

UINT find_menu_by_path(HMENU bar, const std::vector<std::wstring>& path) {
	HMENU menu = bar;
	for (size_t depth = 0; menu && depth < path.size(); depth++) {
		int pos = find_menu_item(menu, path[depth]);
		if (pos < 0) return 0;
		if (depth == path.size() - 1) return command_at(menu, pos);
		menu = GetSubMenu(menu, pos);
	}
	return 0;
}

HMENU find_popup_containing(HMENU root, const std::wstring& text) {
	if (!root) return nullptr;
	if (find_menu_item(root, text) >= 0) return root;
	int n = GetMenuItemCount(root);
	for (int i = 0; i < n; i++) {
		if (HMENU found = find_popup_containing(GetSubMenu(root, i), text)) return found;
	}
	return nullptr;
}

HMENU find_parent_of(HMENU root, HMENU popup) {
	if (!root || !popup) return nullptr;
	int n = GetMenuItemCount(root);
	for (int i = 0; i < n; i++) {
		HMENU sub = GetSubMenu(root, i);
		if (!sub) continue;
		if (sub == popup) return root;
		if (HMENU found = find_parent_of(sub, popup)) return found;
	}
	return nullptr;
}

HMENU find_layout_menu(HMENU bar) {
	// 決め打ちの道筋 → 目印 (「レイアウトを作成」を持つ下位メニュー)
	int v = find_menu_item(bar, VIEW_MENU);
	HMENU view = v >= 0 ? GetSubMenu(bar, v) : nullptr;
	int l = view ? find_menu_item(view, LAYOUT_MENU) : -1;
	HMENU layout = l >= 0 ? GetSubMenu(view, l) : nullptr;
	if (layout && find_menu_item(layout, LAYOUT_CREATE) >= 0) return layout;
	return find_popup_containing(bar, LAYOUT_CREATE);
}

HMENU find_view_menu(HMENU bar) {
	int v = find_menu_item(bar, VIEW_MENU);
	if (v >= 0) {
		if (HMENU view = GetSubMenu(bar, v)) return view;
	}
	return find_parent_of(bar, find_layout_menu(bar));
}

UINT find_host_command(HMENU bar, const std::vector<std::wstring>& path, std::wstring* how) {
	if (!bar || path.empty()) return 0;
	if (UINT cmd = find_menu_by_path(bar, path)) {
		if (how) *how = L"道筋";
		return cmd;
	}
	if (normalize_menu_text(path[0]) != VIEW_MENU) return 0;
	// 「表示」→「レイアウト」→ 名前: レイアウト下位メニューの中を探す
	if (path.size() == 3 && normalize_menu_text(path[1]) == LAYOUT_MENU) {
		HMENU layout = find_layout_menu(bar);
		if (UINT cmd = command_at(layout, find_menu_item(layout, path[2]))) {
			if (how) *how = L"レイアウトの目印";
			return cmd;
		}
		return 0;
	}
	// 「表示」→ 名前 (パネル等): 表示メニューの中を探す
	if (path.size() == 2) {
		HMENU view = find_view_menu(bar);
		if (UINT cmd = command_at(view, find_menu_item(view, path[1]))) {
			if (how) *how = L"表示の目印";
			return cmd;
		}
	}
	return 0;
}

static void dump_level(HMENU menu, int level, int depth, std::wstring& out) {
	int n = GetMenuItemCount(menu);
	for (int i = 0; i < n; i++) {
		MENUITEMINFOW mi = { sizeof(mi) };
		mi.fMask = MIIM_FTYPE | MIIM_SUBMENU | MIIM_ID;
		GetMenuItemInfoW(menu, i, TRUE, &mi);
		wchar_t raw[256] = {};
		GetMenuStringW(menu, i, raw, 256, MF_BYPOSITION);
		out += std::wstring(level * 2, L' ');
		if (mi.fType & MFT_SEPARATOR) {
			out += L"----\n";
			continue;
		}
		out += L"[" + std::wstring(raw) + L"]";
		if (mi.fType & MFT_OWNERDRAW) out += L" (自作描画)";
		if (mi.hSubMenu) out += GetMenuItemCount(mi.hSubMenu) > 0 ? L" ▸" : L" ▸ (空)";
		else out += L" #" + std::to_wstring(mi.wID);
		out += L"\n";
		if (mi.hSubMenu && level + 1 < depth) dump_level(mi.hSubMenu, level + 1, depth, out);
	}
}

std::wstring dump_menu(HMENU root, int depth) {
	std::wstring out;
	if (root) dump_level(root, 0, depth, out);
	return out;
}

} // namespace tpgd
