//----------------------------------------------------------------------------------
//	本体の関数呼び出しの横取り
//	本体 exe の取り込みアドレス表 (IAT) の項目を差し替える
//
//	1. TrackPopupMenu / TrackPopupMenuEx (右クリックメニュー)
//	   取り込み中のパネルでは「ウィンドウ配置」→「ウィンドウを統合」を阻止する
//	   (本体がパネルを自身の画面へ戻してしまい、自由分割から外れるため)
//	   ※ 項目番号は毎回変わるため、項目の文字列で判定する
//	2. MessageBoxW (確認・エラーの画面)
//	   プラグインが本体のレイアウト操作を自動で行っている間だけ、
//	   「同名のレイアウト」の上書き確認とレイアウト読み込みエラーを、画面を出さずに答える
//	   (画面を出すと警告音が鳴るため)。それ以外の画面は一切変えない
//----------------------------------------------------------------------------------
#include "hooks.h"
#include "common.h"
#include "dock.h"
#include "host_ops.h"

namespace tpgd {

using TrackPopupMenuEx_t = BOOL(WINAPI*)(HMENU, UINT, int, int, HWND, LPTPMPARAMS);
using TrackPopupMenu_t = BOOL(WINAPI*)(HMENU, UINT, int, int, int, HWND, const RECT*);
using MessageBoxW_t = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
static TrackPopupMenuEx_t orig_TrackPopupMenuEx = nullptr;
static TrackPopupMenu_t orig_TrackPopupMenu = nullptr;
static MessageBoxW_t orig_MessageBoxW = nullptr;

//---------------------------------------------------------------------
//	右クリックメニュー
//---------------------------------------------------------------------

static bool find_menu_text(HMENU menu, UINT id, std::wstring& out) {
	int n = GetMenuItemCount(menu);
	for (int i = 0; i < n; i++) {
		HMENU sub = GetSubMenu(menu, i);
		if (sub) {
			if (find_menu_text(sub, id, out)) return true;
		} else if (GetMenuItemID(menu, i) == id) {
			wchar_t text[256] = {};
			GetMenuStringW(menu, i, text, 256, MF_BYPOSITION);
			out = text;
			return true;
		}
	}
	return false;
}

// メニュー表示の共通処理
// 選択結果を知るため TPM_RETURNCMD を付けて表示し、呼び出し元が求める形で結果を返す
template <class F>
static BOOL track_popup_common(HMENU menu, UINT flags, int x, int y, HWND hwnd, F call_orig) {
	POINT pt = { x, y };
	bool on_panel = dock_is_captured(hwnd) || dock_is_captured(WindowFromPoint(pt));
	HWND on_float = on_panel ? nullptr : dock_floating_of(WindowFromPoint(pt));
	if (!on_float) on_float = on_panel ? nullptr : dock_floating_of(hwnd);
	if (!on_panel && !on_float) return call_orig(flags);

	bool wants_cmd = (flags & TPM_RETURNCMD) != 0;
	UINT cmd = (UINT)call_orig(flags | TPM_RETURNCMD);
	if (cmd == 0) return 0;

	std::wstring text;
	find_menu_text(menu, cmd, text);
#ifdef TPGD_DEV
	log(L"[調査] 取り込み中のパネルの右クリックで選択: " + text);
#endif
	if (text.find(L"ウィンドウを統合") != std::wstring::npos) {
		if (on_float) {
			// 浮いた窓: 本体へ統合させると TP_GUIdocker の画面の裏に隠れるため、区画へ入れる操作に置き換える
			dock_request_dock_return(on_float);
		} else {
			log(L"取り込み中パネルの「ウィンドウを統合」を阻止");
		}
		return wants_cmd ? 0 : TRUE;
	}
	if (wants_cmd) return (BOOL)cmd;
	// 本来の動作 (選択項目を WM_COMMAND で通知) を再現する
	if (!(flags & TPM_NONOTIFY)) PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
	return TRUE;
}

static BOOL WINAPI hook_TrackPopupMenuEx(HMENU menu, UINT flags, int x, int y, HWND hwnd, LPTPMPARAMS params) {
	return track_popup_common(menu, flags, x, y, hwnd,
		[&](UINT f) { return orig_TrackPopupMenuEx(menu, f, x, y, hwnd, params); });
}

static BOOL WINAPI hook_TrackPopupMenu(HMENU menu, UINT flags, int x, int y, int reserved, HWND hwnd, const RECT* rect) {
	return track_popup_common(menu, flags, x, y, hwnd,
		[&](UINT f) { return orig_TrackPopupMenu(menu, f, x, y, reserved, hwnd, rect); });
}

//---------------------------------------------------------------------
//	確認・エラーの画面
//---------------------------------------------------------------------

static int WINAPI hook_MessageBoxW(HWND hwnd, LPCWSTR text, LPCWSTR caption, UINT type) {
	if (int answer = host_ops_answer_message_box(text ? text : L"")) return answer;
	return orig_MessageBoxW(hwnd, text, caption, type);
}

//---------------------------------------------------------------------
//	IAT の差し替え
//	取り込み名で項目を探す (他のプラグインが先に差し替えていても、その上に重ねられる)
//---------------------------------------------------------------------

static void* patch_iat(HMODULE module, const char* dll, const char* func, void* new_func) {
	auto base = (BYTE*)module;
	auto dos = (IMAGE_DOS_HEADER*)base;
	auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
	auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress) return nullptr;
	for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
		if (_stricmp((char*)(base + imp->Name), dll) != 0) continue;
		if (!imp->OriginalFirstThunk) continue;
		auto names = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
		auto slots = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
		for (; names->u1.AddressOfData; names++, slots++) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
			auto by_name = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
			if (strcmp((char*)by_name->Name, func) != 0) continue;
			DWORD old;
			VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
			void* prev = (void*)slots->u1.Function;
			slots->u1.Function = (ULONG_PTR)new_func;
			VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
			return prev;
		}
	}
	return nullptr;
}

// 差し替えた項目を元に戻す (自分の差し替えがそのまま残っている場合のみ)
static void unpatch_iat(HMODULE module, const char* dll, const char* func, void* ours, void* original) {
	if (!original) return;
	void* current = patch_iat(module, dll, func, original);
	if (current && current != ours) {
		// 他のプラグインが後から重ねている。外すと連鎖が切れるため元に戻す
		patch_iat(module, dll, func, current);
		log(std::wstring(L"他のプラグインが後から差し替えているため、横取りを残す: ") + utf8_to_wide(func));
		// 横取りが残る以上、この DLL が取り外されると取り外し済みの処理が呼ばれて本体が落ちる。
		// 本体の起動中に読み込み直された場合に備え、この DLL を本体の終了までメモリに残す
		HMODULE self = nullptr;
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)ours, &self)) {
			log(L"横取りが残るため、プラグインを本体の終了までメモリに残す");
		}
	}
}

void install_menu_hooks() {
	HMODULE exe = GetModuleHandleW(nullptr);
	orig_TrackPopupMenuEx = (TrackPopupMenuEx_t)patch_iat(exe, "user32.dll", "TrackPopupMenuEx", (void*)hook_TrackPopupMenuEx);
	orig_TrackPopupMenu = (TrackPopupMenu_t)patch_iat(exe, "user32.dll", "TrackPopupMenu", (void*)hook_TrackPopupMenu);
	orig_MessageBoxW = (MessageBoxW_t)patch_iat(exe, "user32.dll", "MessageBoxW", (void*)hook_MessageBoxW);
	log(std::wstring(L"横取り TrackPopupMenuEx=") + (orig_TrackPopupMenuEx ? L"設置" : L"対象なし") +
		L" TrackPopupMenu=" + (orig_TrackPopupMenu ? L"設置" : L"対象なし") +
		L" MessageBoxW=" + (orig_MessageBoxW ? L"設置" : L"対象なし"));
}

void uninstall_menu_hooks() {
	HMODULE exe = GetModuleHandleW(nullptr);
	unpatch_iat(exe, "user32.dll", "TrackPopupMenuEx", (void*)hook_TrackPopupMenuEx, (void*)orig_TrackPopupMenuEx);
	unpatch_iat(exe, "user32.dll", "TrackPopupMenu", (void*)hook_TrackPopupMenu, (void*)orig_TrackPopupMenu);
	unpatch_iat(exe, "user32.dll", "MessageBoxW", (void*)hook_MessageBoxW, (void*)orig_MessageBoxW);
}

} // namespace tpgd
