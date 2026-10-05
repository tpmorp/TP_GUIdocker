//----------------------------------------------------------------------------------
//	分割木・レイアウトファイル読み書きの単体テスト
//	使い方: tests\build_test.bat 実行後 tests\out\test_core.exe <レイアウトファイル>
//----------------------------------------------------------------------------------
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "../src/tree.h"
#include "../src/layout_file.h"

using namespace tpgd;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("NG: %s (line %d)\n", #cond, __LINE__); failures++; } else { printf("OK: %s\n", #cond); } } while (0)

static std::string read_all(const std::wstring& path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

int wmain(int argc, wchar_t** argv) {
	// 分割木: 初期配置の往復
	auto t = default_tree();
	std::wstring s1 = serialize_tree(t.get());
	auto t2 = parse_tree(s1);
	CHECK(t2 != nullptr);
	CHECK(t2 && serialize_tree(t2.get()) == s1);

	// 記号を含むセクション名の符号化
	auto leaf = std::make_unique<Node>();
	leaf->panels = { L"explorer:1", L"a|b(c);d,e%f", L"日本語パネル" };
	leaf->active = L"a|b(c);d,e%f";
	std::wstring s2 = serialize_tree(leaf.get());
	auto leaf2 = parse_tree(s2);
	CHECK(leaf2 && leaf2->panels.size() == 3);
	CHECK(leaf2 && leaf2->panels[1] == L"a|b(c);d,e%f");
	CHECK(leaf2 && leaf2->panels[2] == L"日本語パネル");
	CHECK(leaf2 && leaf2->active == L"a|b(c);d,e%f");

	// 壊れた文字列は受け付けない
	CHECK(parse_tree(L"") == nullptr);
	CHECK(parse_tree(L"H(0.5,L(;a)") == nullptr);
	CHECK(parse_tree(L"X(1)") == nullptr);

	// 比率の範囲制限
	auto t3 = parse_tree(L"H(1.5000,L(;a),L(;b))");
	CHECK(t3 && t3->ratio <= 0.95);

	// 配置計算: 子の矩形が親に収まり、重ならない
	RECT rc = { 0, 0, 1000, 600 };
	compute_layout(t.get(), rc, 7, 24);
	CHECK(t->a->rect.bottom + 7 == t->b->rect.top);
	CHECK(t->b->rect.bottom == 600);
	Node* found = find_leaf_with(t.get(), L"preview");
	CHECK(found && found->body.top == found->rect.top + 24);
	CHECK(default_leaf(t.get()) == find_leaf_with(t.get(), L"setting"));

	// レイアウトファイル: 読み込み→保存で内容が変わらない
	if (argc >= 2) {
		std::wstring src = argv[1];
		IniDoc doc;
		CHECK(doc.load(src));
		std::wstring tmp = std::wstring(argv[0]) + L".roundtrip.layout";
		CHECK(doc.save(tmp));
		std::string a = read_all(src), b = read_all(tmp);
		// 元ファイル末尾の改行有無の差は許容する
		while (!a.empty() && (a.back() == '\n' || a.back() == '\r')) a.pop_back();
		while (!b.empty() && (b.back() == '\n' || b.back() == '\r')) b.pop_back();
		CHECK(a == b);
		CHECK(doc.find("Window.preview") != nullptr);
		if (auto s = doc.find("Window.preview")) {
			s->set("floating", "1");
			CHECK(s->get("floating") == "1");
			CHECK(s->has("area"));
		}
		DeleteFileW(tmp.c_str());
	}

	// 本体の設定ファイルからの取り出し: レイアウトファイルの項目のみ・同じ順序になる
	if (argc >= 3) {
		IniDoc ini, basic;
		CHECK(ini.load(argv[2]));
		CHECK(basic.load(argv[1]));
		IniDoc ex = extract_layout(ini);
		CHECK(!ex.sections.empty());
		bool only_layout_keys = true, same_order = true;
		for (auto& s : ex.sections) {
			if (s.name.rfind("Window.", 0) != 0) only_layout_keys = false;
			for (auto& l : s.lines) {
				std::string k = l.substr(0, l.find('='));
				static const char* allowed[] = { "left", "group", "area", "top", "hide", "right", "bottom", "floating", "x", "y", "w", "h", "zoom" };
				bool ok = false;
				for (auto a : allowed) if (k == a) ok = true;
				if (!ok) { only_layout_keys = false; printf("  余計な項目: [%s] %s\n", s.name.c_str(), k.c_str()); }
			}
			// basic に同じセクションがあれば、項目の並びが一致すること
			if (auto b = basic.find(s.name)) {
				std::vector<std::string> kb, ke;
				for (auto& l : b->lines) kb.push_back(l.substr(0, l.find('=')));
				for (auto& l : s.lines) ke.push_back(l.substr(0, l.find('=')));
				if (kb != ke) { same_order = false; printf("  並び不一致: [%s]\n", s.name.c_str()); }
			}
		}
		CHECK(only_layout_keys);
		CHECK(same_order);
		printf("  取り出したセクション数=%zu (basic=%zu)\n", ex.sections.size(), basic.sections.size());
	}

	printf(failures ? "\n%d 件失敗\n" : "\nすべて成功\n", failures);
	return failures ? 1 : 0;
}
