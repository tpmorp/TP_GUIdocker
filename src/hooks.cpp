//----------------------------------------------------------------------------------
//	本体の右クリックメニューの横取り
//	本体 exe の取り込みアドレス表 (IAT) の TrackPopupMenu / TrackPopupMenuEx を差し替える
//
//	取り込み中のパネルでは「ウィンドウ配置」→「ウィンドウを統合」を阻止する
//	(本体がパネルを自身の画面へ戻してしまい、自由分割から外れるため)
//	※ 項目番号は毎回変わるため、項目の文字列で判定する
//----------------------------------------------------------------------------------
#include "hooks.h"
#include "common.h"
#include "dock.h"

namespace tpgd {

using TrackPopupMenuEx_t = BOOL(WINAPI*)(HMENU, UINT, int, int, HWND, LPTPMPARAMS);
using TrackPopupMenu_t = BOOL(WINAPI*)(HMENU, UINT, int, int, int, HWND, const RECT*);
static TrackPopupMenuEx_t orig_TrackPopupMenuEx = nullptr;
static TrackPopupMenu_t orig_TrackPopupMenu = nullptr;

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
	if (!on_panel) return call_orig(flags);

	bool wants_cmd = (flags & TPM_RETURNCMD) != 0;
	UINT cmd = (UINT)call_orig(flags | TPM_RETURNCMD);
	if (cmd == 0) return 0;

	std::wstring text;
	find_menu_text(menu, cmd, text);
	if (text.find(L"ウィンドウを統合") != std::wstring::npos) {
		log(L"取り込み中パネルの「ウィンドウを統合」を阻止");
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

// module の IAT 内で dll!func を指す項目を new_func に差し替え、差し替え前の値を返す
static void* patch_iat(HMODULE module, const char* dll, const char* func, void* new_func, void* match = nullptr) {
	auto base = (BYTE*)module;
	auto dos = (IMAGE_DOS_HEADER*)base;
	auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
	auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress) return nullptr;
	void* target = match ? match : (void*)GetProcAddress(GetModuleHandleA(dll), func);
	if (!target) return nullptr;
	for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
		if (_stricmp((char*)(base + imp->Name), dll) != 0) continue;
		for (auto thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk); thunk->u1.Function; thunk++) {
			if ((void*)thunk->u1.Function != target) continue;
			DWORD old;
			VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
			void* prev = (void*)thunk->u1.Function;
			thunk->u1.Function = (ULONG_PTR)new_func;
			VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
			return prev;
		}
	}
	return nullptr;
}

void install_menu_hooks() {
	HMODULE exe = GetModuleHandleW(nullptr);
	orig_TrackPopupMenuEx = (TrackPopupMenuEx_t)patch_iat(exe, "user32.dll", "TrackPopupMenuEx", (void*)hook_TrackPopupMenuEx);
	orig_TrackPopupMenu = (TrackPopupMenu_t)patch_iat(exe, "user32.dll", "TrackPopupMenu", (void*)hook_TrackPopupMenu);
	log(std::wstring(L"メニュー横取り TrackPopupMenuEx=") + (orig_TrackPopupMenuEx ? L"設置" : L"対象なし") +
		L" TrackPopupMenu=" + (orig_TrackPopupMenu ? L"設置" : L"対象なし"));
}

void uninstall_menu_hooks() {
	HMODULE exe = GetModuleHandleW(nullptr);
	if (orig_TrackPopupMenuEx) patch_iat(exe, "user32.dll", "TrackPopupMenuEx", (void*)orig_TrackPopupMenuEx, (void*)hook_TrackPopupMenuEx);
	if (orig_TrackPopupMenu) patch_iat(exe, "user32.dll", "TrackPopupMenu", (void*)orig_TrackPopupMenu, (void*)hook_TrackPopupMenu);
	orig_TrackPopupMenuEx = nullptr;
	orig_TrackPopupMenu = nullptr;
}

} // namespace tpgd
