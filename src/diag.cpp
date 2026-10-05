//----------------------------------------------------------------------------------
//	診断: 本体のレイアウト読み込み仕様を実験で確かめる (第2回)
//
//	第1回の結果: 全パネル (非表示を含む) を切り離しにすると、読み込み失敗が複数回出て
//	本体標準パネルは切り離されなかった。本体が保存した切り離しレイアウト (prove) は成功する。
//	→ 条件を1つずつ変えて、失敗の原因を特定する。
//
//	試験1: basic の表示中パネルだけを切り離し (検証版0.0.2と同じ作り方)   … 成功するはず
//	試験2: prove のプレビューの位置・大きさだけを変える (40,40 480x320)      … 値の妥当性
//	試験3: prove の色設定 (結合中) を切り離しに。位置・大きさは保存値 (0) のまま
//	試験4: prove の色設定を切り離しに。位置・大きさを指定
//	試験5: prove の非表示の本体標準パネル (ログ表示) を切り離しに
//	試験6: prove の非表示の他者製パネル (電卓) を切り離しに
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

void diag_prepare() {
	g_tests.clear();
	IniDoc basic, prove;
	if (!load_layout(L"basic", basic)) {
		log(L"[診断] basic レイアウトが無いため診断を用意しない");
		return;
	}
	if (!load_layout(L"prove", prove)) {
		log(L"[診断] prove レイアウトが無いため basic で代用する");
		prove = basic;
	}

	// 試験1: basic の表示中パネルだけを切り離し
	DiagTest t1{ test_name(1), L"basic の表示中パネルだけを切り離し (非表示は触らない)", basic, {} };
	{
		int i = 0;
		for (auto& s : t1.doc.sections) {
			if (s.name.rfind("Window.", 0) != 0 || s.get("hide") != "0") continue;
			set_floating(t1, s.name.c_str(), 40 + i * 40, 40, 480, 320);
			i++;
		}
	}

	DiagTest t2{ test_name(2), L"prove のプレビューの位置・大きさを 40,40 480x320 に", prove, {} };
	set_floating(t2, "Window.preview", 40, 40, 480, 320);

	DiagTest t3{ test_name(3), L"prove の色設定 (結合中) を切り離し。位置・大きさは保存値のまま", prove, {} };
	if (auto s = t3.doc.find("Window.setting.color")) {
		s->set("floating", "1");
		t3.description += L" (x=" + utf8_to_wide(s->get("x")) + L" w=" + utf8_to_wide(s->get("w")) + L")";
	}

	DiagTest t4{ test_name(4), L"prove の色設定を切り離し。位置・大きさを 400,40 480x320 に", prove, {} };
	set_floating(t4, "Window.setting.color", 400, 40, 480, 320);

	DiagTest t5{ test_name(5), L"prove の非表示の本体標準パネル (ログ表示) を切り離し", prove, {} };
	set_floating(t5, "Window.log", 440, 40, 480, 320);

	DiagTest t6{ test_name(6), L"prove の非表示の他者製パネル (電卓) を切り離し", prove, {} };
	set_floating(t6, "Window.電卓", 480, 40, 480, 320);

	g_tests = { t1, t2, t3, t4, t5, t6 };
	for (auto& t : g_tests) {
		std::wstring path = layout_dir() + L"\\" + t.name + L".layout";
		if (!t.doc.save(path)) log(L"[診断] 書き出し失敗: " + path);
	}
	log(L"[診断] 第2回の試験用レイアウトを " + std::to_wstring(g_tests.size()) + L" 件用意");
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

	for (HWND h : owned_panels(main)) {
		if (!IsWindowVisible(h)) continue;
		RECT r;
		GetWindowRect(h, &r);
		std::wstring match;
		for (auto& [key, pt] : t.expected) {
			if (r.left == pt.x && r.top == pt.y) { match = L" ← 指定位置どおり (" + key + L")"; break; }
		}
		log(L"[診断]   切り離し表示中 '" + window_text(h) + L"' " + rect_text(r) + match);
	}

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
	log(L"[診断] ===== 第2回 開始 (表示倍率 " + std::to_wstring(GetDpiForWindow(main)) + L"dpi) =====");
	DWORD elapsed = 0;
	int e = apply_layout_counting_errors(owner, main, L"basic", elapsed);
	log(L"[診断] 開始前に basic を適用: エラー表示 " + std::to_wstring(e) + L" 回");
	for (auto& t : g_tests) run_test(owner, main, t);
	log(L"[診断] ===== 終了 =====");
	MessageBoxW(main, L"診断が完了しました。", TPGD_NAME, MB_OK | MB_ICONINFORMATION);
}

} // namespace tpgd
