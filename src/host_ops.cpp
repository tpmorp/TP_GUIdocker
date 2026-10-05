//----------------------------------------------------------------------------------
//	本体のレイアウト操作 (作成の自動操作・適用と結果の検証)
//----------------------------------------------------------------------------------
#include "host_ops.h"
#include "host.h"

#include <algorithm>

namespace tpgd {

namespace {

enum class Mode { None, Create, Apply };

Mode g_mode = Mode::None;
std::wstring g_name;                 // 作成するレイアウト名
bool g_create_submitted = false;     // 名前入力画面で OK を押したか
DWORD g_create_seen_tick = 0;        // 名前入力画面を最初に見つけた時刻
int g_errors = 0;                    // 本体のエラー表示の回数
std::vector<HWND> g_handled;         // 処理済みの画面

constexpr const wchar_t* CREATE_TITLE = L"レイアウトを作成";
constexpr DWORD CREATE_TIMEOUT_MS = 5000;

bool handled(HWND h) {
	return std::find(g_handled.begin(), g_handled.end(), h) != g_handled.end();
}

// 画面内の文字 (静的テキスト・ボタン等) をまとめて取得する (記録用)
std::wstring dialog_text(HWND dlg) {
	std::wstring all;
	EnumChildWindows(dlg, [](HWND c, LPARAM l) -> BOOL {
		auto s = (std::wstring*)l;
		std::wstring t = window_text(c);
		if (!t.empty()) { *s += t; *s += L" / "; }
		return TRUE;
	}, (LPARAM)&all);
	return all;
}

// 子孫から、クラス名・文字・ID の条件に合うものを探す
struct FindCtx {
	const wchar_t* cls;
	const wchar_t* text_contains;
	int id;
	HWND found;
};
HWND find_descendant(HWND parent, const wchar_t* cls, const wchar_t* text_contains, int id) {
	FindCtx ctx = { cls, text_contains, id, nullptr };
	EnumChildWindows(parent, [](HWND c, LPARAM l) -> BOOL {
		auto x = (FindCtx*)l;
		wchar_t cls[64] = {};
		GetClassNameW(c, cls, 64);
		if (x->cls && _wcsicmp(cls, x->cls) != 0) return TRUE;
		if (x->id >= 0 && GetDlgCtrlID(c) != x->id) return TRUE;
		if (x->text_contains && window_text(c).find(x->text_contains) == std::wstring::npos) return TRUE;
		x->found = c;
		return FALSE;
	}, (LPARAM)&ctx);
	return ctx.found;
}

// ボタンを押す (画面へ WM_COMMAND を送る)
void press(HWND dlg, HWND button) {
	PostMessageW(dlg, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(button), BN_CLICKED), (LPARAM)button);
}

// 本プロセスの表示中の画面 (#32770)
std::vector<HWND> visible_dialogs() {
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
	return ctx.list;
}

// 名前入力画面を操作する
void operate_create_dialog(HWND dlg) {
	if (!g_create_seen_tick) g_create_seen_tick = GetTickCount();
	if (g_create_submitted) {
		// OK を押した後も残っている場合は、受け付けられなかったとみなして時間切れで閉じる
		if (GetTickCount() - g_create_seen_tick > CREATE_TIMEOUT_MS) {
			log(L"「レイアウトを作成」が閉じないため取り消す");
			PostMessageW(dlg, WM_CLOSE, 0, 0);
			g_handled.push_back(dlg);
		}
		return;
	}
	HWND edit = find_descendant(dlg, L"Edit", nullptr, -1);
	HWND combo = find_descendant(dlg, L"ComboBox", nullptr, -1);
	HWND check = find_descendant(dlg, L"Button", L"メインウィンドウ", -1);
	HWND ok = find_descendant(dlg, L"Button", L"OK", -1);
	if (!edit || !ok) {
		log(L"「レイアウトを作成」の部品が見つからない: " + dialog_text(dlg));
		PostMessageW(dlg, WM_CLOSE, 0, 0);
		g_handled.push_back(dlg);
		return;
	}
	if (combo) SendMessageW(combo, WM_SETTEXT, 0, (LPARAM)g_name.c_str());
	SendMessageW(edit, WM_SETTEXT, 0, (LPARAM)g_name.c_str());
	if (check) SendMessageW(check, BM_SETCHECK, BST_UNCHECKED, 0);
	log(L"「レイアウトを作成」に名前「" + g_name + L"」を入力して OK");
	press(dlg, ok);
	g_create_submitted = true;
}

// 作成・適用中に現れたその他の画面を処理する
void operate_other_dialog(HWND dlg) {
	std::wstring text = dialog_text(dlg);
	// 本体のレイアウト読み込みエラー
	if (text.find(L"レイアウト") != std::wstring::npos && text.find(L"失敗") != std::wstring::npos) {
		g_errors++;
		log(L"本体のエラー表示を検出して閉じる: " + text);
		if (HWND ok = find_descendant(dlg, L"Button", L"OK", -1)) press(dlg, ok);
		else PostMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
		g_handled.push_back(dlg);
		return;
	}
	// 作成時の上書き確認 (同名のレイアウトがある場合)
	if (g_mode == Mode::Create && g_create_submitted) {
		HWND yes = find_descendant(dlg, L"Button", L"はい", -1);
		if (!yes) yes = find_descendant(dlg, L"Button", L"Yes", -1);
		if (yes) {
			log(L"上書きの確認に「はい」で答える: " + text);
			press(dlg, yes);
			g_handled.push_back(dlg);
			return;
		}
	}
	// それ以外は利用者に任せる (触らない)
	log(L"想定外の画面 (操作しない): " + text);
	g_handled.push_back(dlg);
}

// 本体へメニューの命令を送り、終わるまで (モーダル画面の間も) 監視する
void run_command(HWND main, HWND timer_owner, UINT cmd) {
	g_handled.clear();
	g_errors = 0;
	SetTimer(timer_owner, TIMER_HOST_OPS, 50, nullptr);
	SendMessageW(main, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
	// 命令の完了後に遅れて出る表示も拾う
	for (int i = 0; i < 10; i++) {
		MSG m;
		while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&m);
			DispatchMessageW(&m);
		}
		host_ops_on_timer();
		Sleep(30);
	}
	KillTimer(timer_owner, TIMER_HOST_OPS);
}

FILETIME file_time(const std::wstring& path) {
	WIN32_FILE_ATTRIBUTE_DATA a = {};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return {};
	return a.ftLastWriteTime;
}

} // namespace

void host_ops_on_timer() {
	if (g_mode == Mode::None) return;
	for (HWND dlg : visible_dialogs()) {
		if (handled(dlg)) continue;
		if (g_mode == Mode::Create && window_text(dlg) == CREATE_TITLE) {
			operate_create_dialog(dlg);
		} else {
			operate_other_dialog(dlg);
		}
	}
}

bool host_create_layout(HWND main, HWND timer_owner, const std::wstring& name) {
	UINT cmd = find_menu_command(main, { L"表示", L"レイアウト", L"レイアウトを作成" });
	if (!cmd) {
		log(L"「レイアウトを作成」がメニューに無い");
		return false;
	}
	std::wstring path = layout_dir() + L"\\" + name + L".layout";
	FILETIME before = file_time(path);

	g_mode = Mode::Create;
	g_name = name;
	g_create_submitted = false;
	g_create_seen_tick = 0;
	run_command(main, timer_owner, cmd);
	g_mode = Mode::None;

	// 検証: ファイルが新しく書かれ、メニューに登録されたこと
	FILETIME after = file_time(path);
	bool written = CompareFileTime(&after, &before) > 0;
	bool in_menu = find_menu_command(main, { L"表示", L"レイアウト", name }) != 0;
	log(L"レイアウト作成「" + name + L"」: 書き込み=" + std::to_wstring(written) + L" メニュー=" + std::to_wstring(in_menu));
	return written && in_menu;
}

ApplyResult host_apply_layout(HWND main, HWND timer_owner, const std::wstring& name) {
	ApplyResult r;
	UINT cmd = find_menu_command(main, { L"表示", L"レイアウト", name });
	if (!cmd) {
		log(L"レイアウト「" + name + L"」がメニューに無い");
		return r;
	}
	r.in_menu = true;
	g_mode = Mode::Apply;
	run_command(main, timer_owner, cmd);
	g_mode = Mode::None;
	r.errors = g_errors;
	log(L"レイアウト適用「" + name + L"」: エラー表示 " + std::to_wstring(r.errors) + L" 回");
	return r;
}

} // namespace tpgd
