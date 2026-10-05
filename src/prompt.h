//----------------------------------------------------------------------------------
//	文字の入力画面 (ワークスペースの名前の入力など)
//----------------------------------------------------------------------------------
#pragma once
#include <windows.h>
#include <string>

namespace tpgd {

// 1行の文字を入力させる。OK なら true を返し、value に入力された文字を入れる
bool prompt_text(HWND owner, const std::wstring& title, const std::wstring& label, std::wstring& value);

} // namespace tpgd
