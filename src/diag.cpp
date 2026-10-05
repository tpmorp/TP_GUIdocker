//----------------------------------------------------------------------------------
//	診断: 本体のレイアウト読み込み仕様を実験で確かめる
//
//	試験 (基準 = 利用者が本体で保存したレイアウト。basic があれば basic):
//	  試験1: 基準そのまま                       … 対照。必ず成功するはず
//	  試験2: 全パネル切り離し + 一意の位置       … 自由分割で使いたい形
//	  試験3: 試験2 から [Window] を除く          … 仮説「メイン窓の節が無いと失敗」
//	  試験4: 試験2 に余計な項目 (DisplayZoom)    … 仮説「未知の項目で失敗」
//	  試験5: 試験2 に古いセクションを追加        … 仮説「存在しない窓の節で失敗」
//	  試験6: 試験2 の非表示パネルも表示 (hide=0) … 非表示パネルの扱いの確認
//	各試験の後に試験1 (基準) を適用して元に戻す
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
std::wstring g_last_dialog_text;

std::wstring test_name(int i) {
	return std::wstring(TPGD_NAME) + L"_試験" + std::to_wstring(i);
}

// 基準にする、利用者が本体で保存したレイアウト
bool load_base(IniDoc& base, std::wstring& base_name) {
	std::vector<std::wstring> candidates = { L"basic" };
	for (auto& n : list_layouts()) {
		if (n.rfind(TPGD_NAME, 0) == 0) continue;
		candidates.push_back(n);
	}
	for (auto& n : candidates) {
		if (base.load(layout_dir() + L"\\" + n + L".layout") && !base.sections.empty()) {
			base_name = n;
			return true;
		}
	}
	return false;
}

// 全パネル切り離し + 一意の位置
void make_floating(DiagTest& t) {
	int i = 0;
	for (auto& s : t.doc.sections) {
		if (s.name.rfind("Window.", 0) != 0) continue;
		int x = 40 + (i % 16) * 40;
		int y = 40 + (i / 16) * 40;
		s.set("floating", "1");
		s.set("x", std::to_string(x));
		s.set("y", std::to_string(y));
		s.set("w", "480");
		s.set("h", "320");
		t.expected[utf8_to_wide(s.name.substr(7))] = { x, y };
		i++;
	}
}

} // namespace

void diag_prepare() {
	g_tests.clear();
	IniDoc base;
	std::wstring base_name;
	if (!load_base(base, base_name)) {
		log(L"[診断] 基準にするレイアウトが無い");
		return;
	}

	DiagTest t1{ test_name(1), L"基準 (" + base_name + L") そのまま", base, {} };

	DiagTest t2{ test_name(2), L"全パネル切り離し + 一意の位置", base, {} };
	make_floating(t2);

	DiagTest t3 = t2;
	t3.name = test_name(3);
	t3.description = L"試験2 から [Window] を除く";
	t3.doc.sections.erase(std::remove_if(t3.doc.sections.begin(), t3.doc.sections.end(),
		[](const IniSection& s) { return s.name == "Window"; }), t3.doc.sections.end());

	DiagTest t4 = t2;
	t4.name = test_name(4);
	t4.description = L"試験2 に余計な項目 (DisplayZoom) を追加";
	if (auto s = t4.doc.find("Window.preview")) s->lines.insert(s->lines.begin(), "DisplayZoom=75");

	DiagTest t5 = t2;
	t5.name = test_name(5);
	t5.description = L"試験2 に本体の設定ファイルにだけある古いセクションを追加";
	{
		IniDoc ini;
		if (ini.load(app_data_dir() + L"\\aviutl2.ini")) {
			IniDoc ex = extract_layout(ini);
			int added = 0;
			for (auto& s : ex.sections) {
				if (base.find(s.name)) continue;
				// 試験2と同じ形式 (切り離し + 位置) にして末尾の [Window] の前へ入れる
				IniSection c = s;
				c.set("floating", "1");
				auto pos = std::find_if(t5.doc.sections.begin(), t5.doc.sections.end(), [](const IniSection& x) { return x.name == "Window"; });
				t5.doc.sections.insert(pos, c);
				added++;
			}
			t5.description += L" (" + std::to_wstring(added) + L" 件)";
		}
	}

	DiagTest t6 = t2;
	t6.name = test_name(6);
	t6.description = L"試験2 の全パネルを表示 (hide=0)";
	for (auto& s : t6.doc.sections) if (s.name.rfind("Window.", 0) == 0) s.set("hide", "0");

	g_tests = { t1, t2, t3, t4, t5, t6 };
	for (auto& t : g_tests) {
		std::wstring path = layout_dir() + L"\\" + t.name + L".layout";
		if (!t.doc.save(path)) log(L"[診断] 書き出し失敗: " + path);
	}
	log(L"[診断] 試験用レイアウトを " + std::to_wstring(g_tests.size()) + L" 件用意 (基準: " + base_name + L")");
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
		g_last_dialog_text = text;
		g_dialogs_closed++;
		log(L"[診断]   本体のエラー表示を検出して閉じる: " + text);
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

// 1件適用し、結果を記録する
static bool apply_and_record(HWND owner, HWND main, const DiagTest& t, bool detail) {
	UINT cmd = find_menu_command(main, { L"表示", L"レイアウト", t.name });
	if (!cmd) {
		log(L"[診断] " + t.name + L" はメニューに無い (本体の再起動が必要)");
		return false;
	}
	RECT before;
	GetWindowRect(main, &before);
	g_dialogs_closed = 0;
	SetTimer(owner, TIMER_DIAG_DIALOG, 100, nullptr);
	DWORD t0 = GetTickCount();
	SendMessageW(main, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
	DWORD elapsed = GetTickCount() - t0;
	// 適用後に遅れて出る表示も拾う
	for (int i = 0; i < 10; i++) {
		MSG m;
		while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
		diag_on_timer(owner);
		Sleep(50);
	}
	KillTimer(owner, TIMER_DIAG_DIALOG);
	RECT after;
	GetWindowRect(main, &after);

	if (!detail) {
		log(L"[診断]   (元に戻す: " + t.name + L") エラー表示=" + std::to_wstring(g_dialogs_closed) + L" 所要=" + std::to_wstring(elapsed) + L"ms");
		return true;
	}

	log(L"[診断] ---- " + t.name + L": " + t.description);
	log(L"[診断]   エラー表示=" + std::to_wstring(g_dialogs_closed) + L" 所要=" + std::to_wstring(elapsed) + L"ms");
	log(L"[診断]   メイン窓 前=" + rect_text(before) + L" 後=" + rect_text(after) + (EqualRect(&before, &after) ? L" (変化なし)" : L" (変化あり)"));

	int visible = 0, hidden = 0, matched = 0;
	for (HWND h : owned_panels(main)) {
		RECT r;
		GetWindowRect(h, &r);
		bool vis = IsWindowVisible(h) != FALSE;
		vis ? visible++ : hidden++;
		std::wstring match = L"";
		for (auto& [key, pt] : t.expected) {
			if (r.left == pt.x && r.top == pt.y) { match = L" ← 位置一致: " + key; matched++; break; }
		}
		log(L"[診断]   " + std::wstring(vis ? L"表示 " : L"非表示 ") + L"'" + window_text(h) + L"' " + rect_text(r) + match);
	}
	log(L"[診断]   切り離し窓: 表示 " + std::to_wstring(visible) + L" / 非表示 " + std::to_wstring(hidden) + L" / 位置一致 " + std::to_wstring(matched));
	return true;
}

void diag_run(HWND owner) {
	HWND main = find_main_window();
	if (!main || g_tests.empty()) {
		log(L"[診断] 実行できない (メイン窓または試験が無い)");
		return;
	}
	log(L"[診断] ===== 開始 =====");
	const DiagTest& control = g_tests[0];
	for (size_t i = 0; i < g_tests.size(); i++) {
		apply_and_record(owner, main, g_tests[i], true);
		if (i > 0) apply_and_record(owner, main, control, false);
	}
	log(L"[診断] ===== 終了 =====");
	MessageBoxW(main, L"診断が完了しました。", TPGD_NAME, MB_OK | MB_ICONINFORMATION);
}

} // namespace tpgd
