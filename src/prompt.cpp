//----------------------------------------------------------------------------------
//	文字の入力画面 (ワークスペースの名前の入力など)
//	リソースファイルを使わず、画面の定義をメモリ上で組み立てる
//----------------------------------------------------------------------------------
#include "prompt.h"
#include "ui_spec.h"

#include <vector>

namespace tpgd {

namespace {

enum { ID_EDIT = 100, ID_LABEL = 101 };

struct PromptData {
	std::wstring label;
	std::wstring value;
};

// 画面定義の組み立て (DLGTEMPLATE と DLGITEMTEMPLATE を順に並べる)
class TemplateBuilder {
public:
	void align() { while (buf_.size() % 4) buf_.push_back(0); }
	void word(WORD w) { buf_.push_back((BYTE)(w & 0xFF)); buf_.push_back((BYTE)(w >> 8)); }
	void dword(DWORD d) { word((WORD)(d & 0xFFFF)); word((WORD)(d >> 16)); }
	void str(const std::wstring& s) { for (wchar_t c : s) word((WORD)c); word(0); }

	void dialog(DWORD style, int items, short x, short y, short cx, short cy, const std::wstring& title,
		WORD font_size, const std::wstring& font) {
		dword(style);
		dword(0);            // 拡張スタイル
		word((WORD)items);
		word((WORD)x); word((WORD)y); word((WORD)cx); word((WORD)cy);
		word(0);             // メニュー無し
		word(0);             // 既定のクラス
		str(title);
		word(font_size);
		str(font);
	}
	void item(DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const std::wstring& text) {
		align();
		dword(style);
		dword(0);
		word((WORD)x); word((WORD)y); word((WORD)cx); word((WORD)cy);
		word(id);
		word(0xFFFF); word(cls);   // 定義済みのクラス (0x0080 ボタン, 0x0081 入力欄, 0x0082 文字)
		str(text);
		word(0);                   // 追加データ無し
	}
	DLGTEMPLATE* get() { return (DLGTEMPLATE*)buf_.data(); }

private:
	std::vector<BYTE> buf_;
};

INT_PTR CALLBACK proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_INITDIALOG: {
			auto d = (PromptData*)lp;
			SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)d);
			SetDlgItemTextW(dlg, ID_LABEL, d->label.c_str());
			SetDlgItemTextW(dlg, ID_EDIT, d->value.c_str());
			HWND edit = GetDlgItem(dlg, ID_EDIT);
			SendMessageW(edit, EM_SETSEL, 0, -1);
			SetFocus(edit);
			return FALSE;  // フォーカスを自分で設定した
		}
		case WM_COMMAND:
			if (LOWORD(wp) == IDOK) {
				auto d = (PromptData*)GetWindowLongPtrW(dlg, DWLP_USER);
				wchar_t buf[256] = {};
				GetDlgItemTextW(dlg, ID_EDIT, buf, 256);
				d->value = buf;
				EndDialog(dlg, IDOK);
				return TRUE;
			}
			if (LOWORD(wp) == IDCANCEL) {
				EndDialog(dlg, IDCANCEL);
				return TRUE;
			}
			break;
	}
	return FALSE;
}

} // namespace

bool prompt_text(HWND owner, const std::wstring& title, const std::wstring& label, std::wstring& value) {
	TemplateBuilder t;
	t.dialog(DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU, 4,
		0, 0, 220, 64, title, 9, L"Yu Gothic UI");
	t.item(WS_CHILD | WS_VISIBLE | SS_LEFT, 8, 10, 36, 10, ID_LABEL, 0x0082, label);
	t.item(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 46, 8, 166, 14, ID_EDIT, 0x0081, L"");
	t.item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 104, 40, 52, 16, IDOK, 0x0080, ui::BUTTON_OK);
	t.item(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 160, 40, 52, 16, IDCANCEL, 0x0080, ui::BUTTON_CANCEL);

	PromptData d{ label, value };
	INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), t.get(), owner, proc, (LPARAM)&d);
	if (r != IDOK) return false;
	value = d.value;
	return true;
}

} // namespace tpgd
