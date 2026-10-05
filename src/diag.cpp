//----------------------------------------------------------------------------------
//	診断: 本体のレイアウト読み込み仕様を実験で確かめる (第3回)
//
//	第1回: 全パネル (非表示を含む) を切り離しにすると読み込み失敗が多発し、本体標準パネルは切り離されない
//	第2回: 表示中パネルだけの切り離し、位置・大きさの指定、非表示パネル1枚の切り離しは、いずれも成功
//	→ 非表示パネルのうち、まとめて切り離すと失敗するものを組分けで特定する
//
//	土台 = basic の表示中パネルだけを切り離し (第2回で成功を確認済み)
//	試験1: 土台 + 非表示の本体標準パネル (エクスプローラー以外)
//	試験2: 土台 + 非表示のエクスプローラー 1〜4
//	試験3: 土台 + 非表示のエクスプローラー 5 以降 (style.conf の既定数は 4)
//	試験4: 土台 + 非表示の他者製パネル
//	試験5: 土台 + 非表示パネル全部
//	試験6: 試験5 と同じ内容をもう一度 (第1回で回数ごとに失敗数が減ったため)
//	各試験の後に basic を適用して状態を揃える
//----------------------------------------------------------------------------------
#include "diag.h"
#include "host.h"
#include "layout_file.h"

#include <algorithm>
#include <map>

namespace tpgd {

namespace {

struct DiagTest {
	std::wstring name;         // レイアウト名 (メニュー表記)
	std::wstring description;
	IniDoc doc;
	std::map<std::wstring, POINT> expected;  // セクション名 → 与えた位置
};

std::vector<DiagTest> g_tests;
int g_dialogs_closed = 0;

std::wstring test_name(int i) {
	return std::wstring(TPGD_NAME) + L"_試験" + std::to_wstring(i);
}

bool load_layout(const std::wstring& name, IniDoc& doc) {
	return doc.load(layout_dir() + L"\\" + name + L".layout") && !doc.sections.empty();
}

void set_floating(DiagTest& t, const char* section, int x, int y, int w, int h) {
	IniSection* s = t.doc.find(section);
	if (!s) {
		t.description += L" ※セクション無し";
		return;
	}
	s->set("floating", "1");
	s->set("x", std::to_string(x));
	s->set("y", std::to_string(y));
	s->set("w", std::to_string(w));
	s->set("h", std::to_string(h));
	t.expected[utf8_to_wide(std::string(section).substr(7))] = { x, y };
}

} // namespace

// 本体標準パネルのセクション名か
static bool is_builtin(const std::string& section) {
	static const char* BUILTIN[] = {
		"Window.preview", "Window.edit", "Window.setting", "Window.setting.color", "Window.setting.text",
		"Window.setting.time", "Window.scene.list", "Window.scene.preview", "Window.object.list",
		"Window.mark.list", "Window.layer.setting", "Window.log", "Window.graph.histogram", "Window.system.info",
	};
	for (auto b : BUILTIN) if (section == b) return true;
	return section.rfind("Window.explorer:", 0) == 0;
}

void diag_prepare() {
	g_tests.clear();
	IniDoc basic;
	if (!load_layout(L"basic", basic)) {
		log(L"[診断] basic レイアウトが無いため診断を用意しない");
		return;
	}

	// 土台: basic の表示中パネルだけを切り離し (第2回で成功を確認済み)
	DiagTest base{ L"", L"", basic, {} };
	int slot = 0;
	auto next_pos = [&](int& x, int& y) {
		x = 40 + (slot % 16) * 40;
		y = 40 + (slot / 16) * 40;
		slot++;
	};
	for (auto& s : base.doc.sections) {
		if (s.name.rfind("Window.", 0) != 0 || s.get("hide") != "0") continue;
		int x, y;
		next_pos(x, y);
		set_floating(base, s.name.c_str(), x, y, 480, 320);
	}

	// 土台に、条件に合う非表示パネルを切り離しとして加える
	auto make = [&](int no, const std::wstring& desc, auto pred) {
		DiagTest t = base;
		t.name = test_name(no);
		t.description = desc;
		int added = 0;
		std::wstring list;
		for (auto& s : t.doc.sections) {
			if (s.name.rfind("Window.", 0) != 0 || s.get("hide") == "0" || !pred(s)) continue;
			int x, y;
			next_pos(x, y);
			set_floating(t, s.name.c_str(), x, y, 480, 320);
			added++;
			list += L" " + utf8_to_wide(s.name.substr(7));
		}
		t.description += L" (" + std::to_wstring(added) + L" 件:" + list + L")";
		return t;
	};

	auto is_explorer = [](const IniSection& s) { return s.name.rfind("Window.explorer:", 0) == 0; };
	auto explorer_no = [](const IniSection& s) { return atoi(s.name.c_str() + 16); };

	DiagTest t1 = make(1, L"土台 + 非表示の本体標準パネル (エクスプローラー以外)",
		[&](const IniSection& s) { return is_builtin(s.name) && !is_explorer(s); });
	DiagTest t2 = make(2, L"土台 + 非表示のエクスプローラー 1〜4",
		[&](const IniSection& s) { return is_explorer(s) && explorer_no(s) <= 4; });
	DiagTest t3 = make(3, L"土台 + 非表示のエクスプローラー 5 以降",
		[&](const IniSection& s) { return is_explorer(s) && explorer_no(s) >= 5; });
	DiagTest t4 = make(4, L"土台 + 非表示の他者製パネル",
		[&](const IniSection& s) { return !is_builtin(s.name); });
	DiagTest t5 = make(5, L"土台 + 非表示パネル全部",
		[&](const IniSection&) { return true; });
	DiagTest t6 = t5;
	t6.name = test_name(6);
	t6.description = L"試験5 と同じ内容をもう一度 (2回目で結果が変わるか)";

	g_tests = { t1, t2, t3, t4, t5, t6 };
	for (auto& t : g_tests) {
		std::wstring path = layout_dir() + L"\\" + t.name + L".layout";
		if (!t.doc.save(path)) log(L"[診断] 書き出し失敗: " + path);
	}
	log(L"[診断] 第3回の試験用レイアウトを " + std::to_wstring(g_tests.size()) + L" 件用意");
}

//---------------------------------------------------------------------
//	本体のエラー表示の監視
//	レイアウト適用は本体内でモーダルなメッセージ表示になるため、その間もタイマーで監視して閉じる
//---------------------------------------------------------------------

static std::wstring dialog_text(HWND dlg) {
	std::wstring all;
	EnumChildWindows(dlg, [](HWND c, LPARAM l) -> BOOL {
		auto s = (std::wstring*)l;
		std::wstring t = window_text(c);
		if (!t.empty()) { *s += t; *s += L" / "; }
		return TRUE;
	}, (LPARAM)&all);
	return all;
}

void diag_on_timer(HWND) {
	struct Ctx { DWORD pid; std::vector<HWND> list; } ctx = { GetCurrentProcessId(), {} };
	EnumWindows([](HWND h, LPARAM l) -> BOOL {
		auto c = (Ctx*)l;
		DWORD pid = 0;
		GetWindowThreadProcessId(h, &pid);
		if (pid != c->pid || !IsWindowVisible(h)) return TRUE;
		wchar_t cls[32] = {};
		GetClassNameW(h, cls, 32);
		if (wcscmp(cls, L"#32770") == 0) c->list.push_back(h);
		return TRUE;
	}, (LPARAM)&ctx);
	for (HWND dlg : ctx.list) {
		std::wstring text = dialog_text(dlg);
		if (text.find(L"レイアウト") == std::wstring::npos) continue;
		g_dialogs_closed++;
		PostMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
	}
}

//---------------------------------------------------------------------
//	実行
//---------------------------------------------------------------------

static std::wstring rect_text(const RECT& r) {
	return std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L" " +
		std::to_wstring(r.right - r.left) + L"x" + std::to_wstring(r.bottom - r.top);
}

// 本体パネル窓 (メイン窓がオーナー) を全て列挙する (非表示を含む)
static std::vector<HWND> owned_panels(HWND main) {
	struct Ctx { DWORD pid; HWND main; std::vector<HWND> list; } ctx = { GetCurrentProcessId(), main, {} };
	EnumWindows([](HWND h, LPARAM l) -> BOOL {
		auto c = (Ctx*)l;
		DWORD pid = 0;
		GetWindowThreadProcessId(h, &pid);
		if (pid != c->pid) return TRUE;
		wchar_t cls[64] = {};
		GetClassNameW(h, cls, 64);
		if (wcscmp(cls, HOST_PANEL_CLASS) == 0 && GetWindow(h, GW_OWNER) == c->main) c->list.push_back(h);
		return TRUE;
	}, (LPARAM)&ctx);
	return ctx.list;
}

// レイアウトを1件適用し、エラー表示の回数を返す (-1: メニューに無い)
static int apply_layout_counting_errors(HWND owner, HWND main, const std::wstring& name, DWORD& elapsed) {
	UINT cmd = find_menu_command(main, { L"表示", L"レイアウト", name });
	if (!cmd) return -1;
	g_dialogs_closed = 0;
	SetTimer(owner, TIMER_DIAG_DIALOG, 100, nullptr);
	DWORD t0 = GetTickCount();
	SendMessageW(main, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
	elapsed = GetTickCount() - t0;
	// 適用後に遅れて出る表示も拾う
	for (int i = 0; i < 20; i++) {
		MSG m;
		while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
		diag_on_timer(owner);
		Sleep(50);
	}
	KillTimer(owner, TIMER_DIAG_DIALOG);
	return g_dialogs_closed;
}

static void run_test(HWND owner, HWND main, const DiagTest& t) {
	log(L"[診断] ---- " + t.name + L": " + t.description);
	RECT before;
	GetWindowRect(main, &before);
	DWORD elapsed = 0;
	int errors = apply_layout_counting_errors(owner, main, t.name, elapsed);
	if (errors < 0) {
		log(L"[診断]   メニューに無い (本体の再起動が必要)");
		return;
	}
	RECT after;
	GetWindowRect(main, &after);
	log(L"[診断]   結果: " + std::wstring(errors ? L"失敗" : L"成功") + L" (エラー表示 " + std::to_wstring(errors) + L" 回, " + std::to_wstring(elapsed) + L"ms)");
	if (!EqualRect(&before, &after)) log(L"[診断]   メイン窓が変化: " + rect_text(before) + L" → " + rect_text(after));

	// 指定したセクションごとに、指定位置に窓ができたか (非表示の窓も含めて) を確かめる
	auto panels = owned_panels(main);
	std::wstring ok_list, ng_list;
	int visible = 0;
	for (auto& [key, pt] : t.expected) {
		HWND found = nullptr;
		for (HWND h : panels) {
			RECT r;
			GetWindowRect(h, &r);
			if (r.left == pt.x && r.top == pt.y) { found = h; break; }
		}
		if (found) {
			ok_list += L" " + key + (IsWindowVisible(found) ? L"(表示)" : L"(非表示)");
		} else {
			ng_list += L" " + key;
		}
	}
	for (HWND h : panels) if (IsWindowVisible(h)) visible++;
	log(L"[診断]   指定位置に窓あり:" + ok_list);
	log(L"[診断]   指定位置に窓なし:" + ng_list);
	log(L"[診断]   切り離し窓の総数: " + std::to_wstring(panels.size()) + L" (表示中 " + std::to_wstring(visible) + L")");

	// basic に戻して状態を揃える
	int restore_errors = apply_layout_counting_errors(owner, main, L"basic", elapsed);
	log(L"[診断]   basic に戻す: エラー表示 " + std::to_wstring(restore_errors) + L" 回");
}

void diag_run(HWND owner) {
	HWND main = find_main_window();
	if (!main || g_tests.empty()) {
		log(L"[診断] 実行できない (メイン窓または試験が無い)");
		return;
	}
	log(L"[診断] ===== 第3回 開始 (表示倍率 " + std::to_wstring(GetDpiForWindow(main)) + L"dpi) =====");
	DWORD elapsed = 0;
	int e = apply_layout_counting_errors(owner, main, L"basic", elapsed);
	log(L"[診断] 開始前に basic を適用: エラー表示 " + std::to_wstring(e) + L" 回");
	for (auto& t : g_tests) run_test(owner, main, t);
	log(L"[診断] ===== 終了 =====");
	MessageBoxW(main, L"診断が完了しました。", TPGD_NAME, MB_OK | MB_ICONINFORMATION);
}

} // namespace tpgd
