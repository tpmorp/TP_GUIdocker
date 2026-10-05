//----------------------------------------------------------------------------------
//	分割木 (画面の分割構造)
//----------------------------------------------------------------------------------
#include "tree.h"
#include <algorithm>

namespace tpgd {

//---------------------------------------------------------------------
//	文字列化
//---------------------------------------------------------------------

static const wchar_t* RESERVED = L"%()|;,";

static std::wstring encode(const std::wstring& s) {
	std::wstring out;
	for (wchar_t c : s) {
		if (wcschr(RESERVED, c)) {
			wchar_t buf[8];
			swprintf_s(buf, L"%%%02X", (unsigned)c);
			out += buf;
		} else {
			out.push_back(c);
		}
	}
	return out;
}

static std::wstring decode(const std::wstring& s) {
	std::wstring out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == L'%' && i + 2 < s.size()) {
			out.push_back((wchar_t)wcstol(s.substr(i + 1, 2).c_str(), nullptr, 16));
			i += 2;
		} else {
			out.push_back(s[i]);
		}
	}
	return out;
}

std::wstring serialize_tree(const Node* n) {
	if (!n) return L"";
	if (n->is_split) {
		wchar_t r[32];
		swprintf_s(r, L"%.4f", n->ratio);
		return std::wstring(n->vertical ? L"V(" : L"H(") + r + L"," + serialize_tree(n->a.get()) + L"," + serialize_tree(n->b.get()) + L")";
	}
	std::wstring s = std::wstring(n->keep_empty ? L"E(" : L"L(") + encode(n->active) + L";";
	for (size_t i = 0; i < n->panels.size(); i++) {
		if (i) s += L"|";
		s += encode(n->panels[i]);
	}
	return s + L")";
}

namespace {
struct Parser {
	const std::wstring& s;
	size_t pos = 0;

	bool eat(wchar_t c) {
		if (pos < s.size() && s[pos] == c) { pos++; return true; }
		return false;
	}
	std::wstring until(const wchar_t* stops) {
		size_t start = pos;
		while (pos < s.size() && !wcschr(stops, s[pos])) pos++;
		return s.substr(start, pos - start);
	}
	std::unique_ptr<Node> node() {
		if (pos >= s.size()) return nullptr;
		wchar_t kind = s[pos++];
		if (!eat(L'(')) return nullptr;
		auto n = std::make_unique<Node>();
		if (kind == L'L' || kind == L'E') {
			n->keep_empty = (kind == L'E');
			n->active = decode(until(L";)"));
			if (!eat(L';')) return nullptr;
			while (pos < s.size() && s[pos] != L')') {
				auto key = decode(until(L"|)"));
				if (!key.empty()) n->panels.push_back(key);
				eat(L'|');
			}
			if (!eat(L')')) return nullptr;
			return n;
		}
		if (kind == L'H' || kind == L'V') {
			n->is_split = true;
			n->vertical = (kind == L'V');
			n->ratio = _wtof(until(L",").c_str());
			if (!eat(L',')) return nullptr;
			n->a = node();
			if (!n->a || !eat(L',')) return nullptr;
			n->b = node();
			if (!n->b || !eat(L')')) return nullptr;
			n->ratio = std::clamp(n->ratio, 0.05, 0.95);
			return n;
		}
		return nullptr;
	}
};
}

std::unique_ptr<Node> parse_tree(const std::wstring& s) {
	Parser p{ s };
	auto n = p.node();
	if (!n || p.pos != s.size()) return nullptr;
	return n;
}

//---------------------------------------------------------------------
//	初期配置 (After Effects 風)
//	┌──────────┬────────────┬──────────────┐
//	│シーン    │ プレビュー │ オブジェクト │
//	│リスト    │ 編集       │ 設定         │
//	├──────────┴────────────┴──────────────┤
//	│ レイヤー編集                         │
//	└──────────────────────────────────────┘
//---------------------------------------------------------------------

static std::unique_ptr<Node> leaf(std::vector<std::wstring> keys) {
	auto n = std::make_unique<Node>();
	n->panels = std::move(keys);
	if (!n->panels.empty()) n->active = n->panels.front();
	return n;
}

static std::unique_ptr<Node> split(bool vertical, double ratio, std::unique_ptr<Node> a, std::unique_ptr<Node> b) {
	auto n = std::make_unique<Node>();
	n->is_split = true;
	n->vertical = vertical;
	n->ratio = ratio;
	n->a = std::move(a);
	n->b = std::move(b);
	return n;
}

// 本体標準パネルだけの最小構成 (他者製パネルは利用者が後から加える)
std::unique_ptr<Node> default_tree() {
	auto left = leaf({ L"scene.list" });
	auto center = leaf({ L"preview" });
	auto right = leaf({ L"setting" });
	auto bottom = leaf({ L"edit" });
	return split(true, 0.60,
		split(false, 0.17, std::move(left), split(false, 0.76, std::move(center), std::move(right))),
		std::move(bottom));
}

//---------------------------------------------------------------------
//	探索
//---------------------------------------------------------------------

void for_each_leaf(Node* n, const std::function<void(Node*)>& f) {
	if (!n) return;
	if (n->is_split) {
		for_each_leaf(n->a.get(), f);
		for_each_leaf(n->b.get(), f);
	} else {
		f(n);
	}
}

Node* find_leaf_with(Node* root, const std::wstring& key) {
	Node* found = nullptr;
	for_each_leaf(root, [&](Node* l) {
		if (!found && std::find(l->panels.begin(), l->panels.end(), key) != l->panels.end()) found = l;
	});
	return found;
}

Node* default_leaf(Node* root) {
	if (auto l = find_leaf_with(root, L"setting")) return l;
	Node* best = nullptr;
	long best_area = -1;
	for_each_leaf(root, [&](Node* l) {
		long area = (l->rect.right - l->rect.left) * (l->rect.bottom - l->rect.top);
		if (area > best_area) { best_area = area; best = l; }
	});
	return best;
}

//---------------------------------------------------------------------
//	配置計算
//---------------------------------------------------------------------

void compute_layout(Node* n, const RECT& rc, int gap, int tab_h) {
	if (!n) return;
	n->rect = rc;
	if (n->is_split) {
		RECT ra = rc, rb = rc;
		if (n->vertical) {
			int h = rc.bottom - rc.top - gap;
			int ha = (int)(h * n->ratio + 0.5);
			ra.bottom = rc.top + ha;
			rb.top = ra.bottom + gap;
			n->splitter = { rc.left, ra.bottom, rc.right, rb.top };
		} else {
			int w = rc.right - rc.left - gap;
			int wa = (int)(w * n->ratio + 0.5);
			ra.right = rc.left + wa;
			rb.left = ra.right + gap;
			n->splitter = { ra.right, rc.top, rb.left, rc.bottom };
		}
		compute_layout(n->a.get(), ra, gap, tab_h);
		compute_layout(n->b.get(), rb, gap, tab_h);
	} else {
		n->tabs = { rc.left, rc.top, rc.right, std::min(rc.bottom, rc.top + tab_h) };
		n->body = { rc.left, n->tabs.bottom, rc.right, rc.bottom };
	}
}

//---------------------------------------------------------------------
//	組み替え (ドッキング)
//---------------------------------------------------------------------

Node* leaf_at(Node* root, POINT pt) {
	Node* found = nullptr;
	for_each_leaf(root, [&](Node* l) {
		if (!found && PtInRect(&l->rect, pt)) found = l;
	});
	return found;
}

// 葉の表示中タブを、葉に属するパネルへ補正する
static void fix_active(Node* leaf) {
	if (leaf->panels.empty()) {
		leaf->active.clear();
	} else if (std::find(leaf->panels.begin(), leaf->panels.end(), leaf->active) == leaf->panels.end()) {
		leaf->active = leaf->panels.front();
	}
}

void insert_key(Node* target, const std::wstring& key, DropZone zone, int tab_index) {
	if (!target || target->is_split || zone == DropZone::None) return;
	if (zone == DropZone::Center) {
		auto& v = target->panels;
		v.erase(std::remove(v.begin(), v.end(), key), v.end());
		if (tab_index < 0 || tab_index > (int)v.size()) tab_index = (int)v.size();
		v.insert(v.begin() + tab_index, key);
		target->active = key;
		target->keep_empty = false;
		return;
	}
	// 葉をその場で分割に変える: 元の中身は一方の子へ、新しいパネルはもう一方の子へ
	auto old_leaf = std::make_unique<Node>();
	old_leaf->panels = std::move(target->panels);
	old_leaf->active = std::move(target->active);
	old_leaf->keep_empty = target->keep_empty;
	auto new_leaf = std::make_unique<Node>();
	new_leaf->panels = { key };
	new_leaf->active = key;

	target->panels.clear();
	target->active.clear();
	target->keep_empty = false;
	target->is_split = true;
	target->ratio = 0.5;
	target->vertical = (zone == DropZone::Top || zone == DropZone::Bottom);
	bool new_first = (zone == DropZone::Left || zone == DropZone::Top);
	target->a = new_first ? std::move(new_leaf) : std::move(old_leaf);
	target->b = new_first ? std::move(old_leaf) : std::move(new_leaf);
}

// 空いた葉を閉じる (根から再帰)。node 自身は閉じない (親が閉じる)
static void collapse_rec(Node* node) {
	if (!node || !node->is_split) return;
	collapse_rec(node->a.get());
	collapse_rec(node->b.get());
	auto closable = [](const Node* n) { return !n->is_split && n->panels.empty() && !n->keep_empty; };
	if (closable(node->a.get()) || closable(node->b.get())) {
		// 残す側の中身で、この節をその場で置き換える
		std::unique_ptr<Node> keep = closable(node->a.get()) ? std::move(node->b) : std::move(node->a);
		*node = std::move(*keep);
	}
}

void collapse_empty(Node* root) {
	collapse_rec(root);
}

void move_key(Node* root, const std::wstring& key, Node* target, DropZone zone, int tab_index) {
	Node* src = find_leaf_with(root, key);
	if (!src || !target || target->is_split || zone == DropZone::None) return;
	if (src == target) {
		if (zone == DropZone::Center) {
			// 同じ葉の中での並べ替え
			auto& v = src->panels;
			int from = (int)(std::find(v.begin(), v.end(), key) - v.begin());
			v.erase(v.begin() + from);
			if (tab_index > from) tab_index--;  // 自分を抜いた分だけ位置がずれる
			if (tab_index < 0 || tab_index > (int)v.size()) tab_index = (int)v.size();
			v.insert(v.begin() + tab_index, key);
			src->active = key;
			return;
		}
		// 自分1枚だけの葉を自分の端へ分割しても意味が無い
		if (src->panels.size() == 1) return;
	}
	// 先に元の葉から外し (閉じるのは後)、それから入れる
	auto& v = src->panels;
	v.erase(std::remove(v.begin(), v.end(), key), v.end());
	fix_active(src);
	insert_key(target, key, zone, tab_index);
	collapse_empty(root);
}

bool remove_key(Node* root, const std::wstring& key) {
	Node* leaf = find_leaf_with(root, key);
	if (!leaf) return false;
	auto& v = leaf->panels;
	v.erase(std::remove(v.begin(), v.end(), key), v.end());
	fix_active(leaf);
	collapse_empty(root);
	return true;
}

Node* split_leaf(Node* leaf, DropZone side) {
	if (!leaf || leaf->is_split || side == DropZone::None || side == DropZone::Center) return nullptr;
	insert_key(leaf, L"", side);
	// insert_key は新しい葉にキーを1つ入れるため、空き区画に作り直す
	Node* fresh = (side == DropZone::Left || side == DropZone::Top) ? leaf->a.get() : leaf->b.get();
	fresh->panels.clear();
	fresh->active.clear();
	fresh->keep_empty = true;
	return fresh;
}

void close_leaf(Node* root, Node* leaf) {
	if (!leaf || leaf->is_split || !leaf->panels.empty()) return;
	leaf->keep_empty = false;
	collapse_empty(root);
}

RECT drop_preview_rect(const Node* t, DropZone zone, int gap) {
	RECT r = t->rect;
	int w = r.right - r.left, h = r.bottom - r.top;
	switch (zone) {
		case DropZone::Left:   r.right = r.left + (w - gap) / 2; break;
		case DropZone::Right:  r.left = r.right - (w - gap) / 2; break;
		case DropZone::Top:    r.bottom = r.top + (h - gap) / 2; break;
		case DropZone::Bottom: r.top = r.bottom - (h - gap) / 2; break;
		default: break;
	}
	return r;
}

} // namespace tpgd
