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
#include "../src/dock_layout.h"
#include "../src/common.h"
#include <algorithm>

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

	// 組み替え: 初期配置 (左: scene.list / 中央: preview / 右: setting / 下: edit)
	{
		auto r = default_tree();
		compute_layout(r.get(), { 0, 0, 1000, 600 }, 7, 24);
		Node* center = find_leaf_with(r.get(), L"preview");
		Node* right = find_leaf_with(r.get(), L"setting");

		// タブとして重ねる
		move_key(r.get(), L"scene.list", right, DropZone::Center);
		Node* rl = find_leaf_with(r.get(), L"setting");
		CHECK(rl && rl->panels.size() == 2 && rl->active == L"scene.list");
		// 左の葉は空になって閉じ、葉は3つになる
		int leaves = 0;
		for_each_leaf(r.get(), [&](Node*) { leaves++; });
		CHECK(leaves == 3);
		CHECK(serialize_tree(r.get()).find(L"L(;)") == std::wstring::npos);

		// 右へ分割して置く
		compute_layout(r.get(), { 0, 0, 1000, 600 }, 7, 24);
		center = find_leaf_with(r.get(), L"preview");
		move_key(r.get(), L"scene.list", center, DropZone::Right);
		Node* sl = find_leaf_with(r.get(), L"scene.list");
		Node* pv = find_leaf_with(r.get(), L"preview");
		CHECK(sl && pv && sl != pv && sl->panels.size() == 1);
		compute_layout(r.get(), { 0, 0, 1000, 600 }, 7, 24);
		sl = find_leaf_with(r.get(), L"scene.list");
		pv = find_leaf_with(r.get(), L"preview");
		CHECK(sl && pv && sl->rect.left > pv->rect.left);  // 右側に置かれた

		// 同じ葉の中での並べ替え
		Node* st = find_leaf_with(r.get(), L"setting");
		insert_key(st, L"x1", DropZone::Center);
		insert_key(st, L"x2", DropZone::Center);   // [setting, x1, x2]
		move_key(r.get(), L"x2", st, DropZone::Center, 0);
		st = find_leaf_with(r.get(), L"setting");
		CHECK(st && st->panels.size() == 3 && st->panels[0] == L"x2" && st->panels[1] == L"setting");
		move_key(r.get(), L"x2", st, DropZone::Center, 3);  // 末尾へ
		st = find_leaf_with(r.get(), L"setting");
		CHECK(st && st->panels.back() == L"x2");

		// 1枚だけの葉を自分の端へ分割しても変わらない
		std::wstring before = serialize_tree(r.get());
		Node* e = find_leaf_with(r.get(), L"edit");
		move_key(r.get(), L"edit", e, DropZone::Left);
		CHECK(serialize_tree(r.get()) == before);

		// 取り除くと空いた葉は閉じる
		CHECK(remove_key(r.get(), L"scene.list"));
		CHECK(find_leaf_with(r.get(), L"scene.list") == nullptr);
		CHECK(serialize_tree(r.get()).find(L"L(;)") == std::wstring::npos);

		// 空き区画は閉じずに残り、保存・復元できる
		Node* pv2 = find_leaf_with(r.get(), L"preview");
		Node* fresh = split_leaf(pv2, DropZone::Bottom);
		CHECK(fresh && fresh->keep_empty && fresh->panels.empty());
		collapse_empty(r.get());
		std::wstring with_empty = serialize_tree(r.get());
		CHECK(with_empty.find(L"E(;)") != std::wstring::npos);
		auto restored = parse_tree(with_empty);
		CHECK(restored && serialize_tree(restored.get()) == with_empty);
		// 空き区画にパネルを入れると、普通の葉になる
		compute_layout(r.get(), { 0, 0, 1000, 600 }, 7, 24);
		Node* empty_leaf = nullptr;
		for_each_leaf(r.get(), [&](Node* l) { if (l->keep_empty) empty_leaf = l; });
		CHECK(empty_leaf != nullptr);
		move_key(r.get(), L"x1", empty_leaf, DropZone::Center);
		CHECK(serialize_tree(r.get()).find(L"E(") == std::wstring::npos);
		// 空き区画を閉じる
		Node* pv3 = find_leaf_with(r.get(), L"preview");
		Node* fresh2 = split_leaf(pv3, DropZone::Left);
		close_leaf(r.get(), fresh2);
		CHECK(serialize_tree(r.get()).find(L"E(") == std::wstring::npos);

		// 全部取り除いても根は残る
		auto single = parse_tree(L"L(a;a)");
		CHECK(remove_key(single.get(), L"a"));
		CHECK(single && !single->is_split && single->panels.empty());

		// 予告範囲
		compute_layout(r.get(), { 0, 0, 1000, 600 }, 7, 24);
		Node* ed = find_leaf_with(r.get(), L"edit");
		RECT pr = drop_preview_rect(ed, DropZone::Left, 7);
		CHECK(pr.left == ed->rect.left && pr.right < (ed->rect.left + ed->rect.right) / 2 + 1);
		CHECK(leaf_at(r.get(), { ed->rect.left + 1, ed->rect.top + 1 }) == ed);
	}

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

	// 自由分割用レイアウトの生成 (本番の basic を土台にする)
	if (argc >= 2) {
		IniDoc base;
		CHECK(base.load(argv[1]));
		std::vector<std::wstring> want = { L"preview", L"edit", L"setting", L"scene.list" };
		auto is_visible = [&](const std::wstring& k) { return std::find(want.begin(), want.end(), k) != want.end(); };
		DockLayoutPlan plan = build_dock_layout(base, is_visible, { 100, 50 }, 960, 630);

		bool explorer_untouched = true, others_floating = true, hide_ok = true;
		std::vector<std::pair<int, int>> positions;
		for (auto& s : plan.doc.sections) {
			if (s.name.rfind("Window.", 0) != 0) continue;
			const IniSection* orig = base.find(s.name);
			if (is_explorer_section(s.name)) {
				// 切り離しにしない (元の値のまま) ・非表示にする
				if (s.get("floating") != orig->get("floating") || s.get("x") != orig->get("x")) explorer_untouched = false;
				if (s.get("hide") != "1") explorer_untouched = false;
				continue;
			}
			if (s.get("floating") != "1") others_floating = false;
			std::wstring key = utf8_to_wide(s.name.substr(7));
			if (s.get("hide") != (is_visible(key) ? "0" : "1")) hide_ok = false;
			positions.push_back({ atoi(s.get("x").c_str()), atoi(s.get("y").c_str()) });
		}
		CHECK(explorer_untouched);
		CHECK(others_floating);
		CHECK(hide_ok);
		auto sorted = positions;
		std::sort(sorted.begin(), sorted.end());
		CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());   // 位置が一意
		CHECK(plan.slots.size() == positions.size());
		CHECK(plan.visible_keys.size() == want.size());
		// [Window] (メイン窓) は土台のまま
		const IniSection* w0 = base.find("Window");
		const IniSection* w1 = plan.doc.find("Window");
		CHECK((w0 == nullptr) == (w1 == nullptr));
		if (w0 && w1) CHECK(w0->lines == w1->lines);
		// 照合: 各仮位置 (±2px) から正しいセクション名に戻る。離れた位置は照合しない
		bool match_ok = true;
		for (auto& slot : plan.slots) {
			if (match_slot(plan.slots, slot.pt) != slot.key) match_ok = false;
			if (match_slot(plan.slots, { slot.pt.x + 2, slot.pt.y - 2 }) != slot.key) match_ok = false;
		}
		CHECK(match_ok);
		CHECK(match_slot(plan.slots, { 99999, 99999 }).empty());
		CHECK(match_slot(plan.slots, { plan.slots[0].pt.x + 20, plan.slots[0].pt.y + 20 }).empty());
	}

	printf(failures ? "\n%d 件失敗\n" : "\nすべて成功\n", failures);
	return failures ? 1 : 0;
}
