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
	std::wstring s = L"L(" + encode(n->active) + L";";
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
		if (kind == L'L') {
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
//	┌──────┬────────────┬──────┐
//	│ 素材 │ プレビュー │ 設定 │
//	├──────┴────────────┴──────┤
//	│      タイムライン        │
//	└──────────────────────────┘
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

std::unique_ptr<Node> default_tree() {
	auto left = leaf({ L"scene.list", L"explorer:1", L"explorer:2", L"explorer:3", L"explorer:4", L"object.list", L"mark.list" });
	auto center = leaf({ L"preview", L"scene.preview" });
	auto right = leaf({ L"setting", L"setting.color", L"setting.text", L"setting.time", L"layer.setting" });
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

} // namespace tpgd
