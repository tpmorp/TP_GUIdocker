//----------------------------------------------------------------------------------
//	本体のメニュー項目の探索
//	他のプラグインがメニューの文字 (「表示(&V)」等) や構成 (項目の移動) を変えていても見つけられるよう、
//	文字をそろえて比べ、決め打ちの道筋で見つからない時は目印の項目から探す。
//	窓に依存せず HMENU だけを扱う (単体テストの対象)
//----------------------------------------------------------------------------------
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace tpgd {

// 比較用に文字をそろえる (ショートカット表示・アクセスキー表記「(&V)」「&」・末尾の「...」「…」・前後の空白を除く)
std::wstring normalize_menu_text(const std::wstring& s);

// 項目の文字 (そろえたもの)
std::wstring menu_item_text(HMENU menu, int pos);

// menu の直下で文字が一致する項目の位置 (無ければ -1)
int find_menu_item(HMENU menu, const std::wstring& text);

// 道筋をたどって項目の番号を返す (見つからなければ 0)
UINT find_menu_by_path(HMENU bar, const std::vector<std::wstring>& path);

// text の項目を直下に持つ下位メニューを、全体から探す (無ければ nullptr)
HMENU find_popup_containing(HMENU root, const std::wstring& text);

// popup を直下に持つ親メニュー (無ければ nullptr)
HMENU find_parent_of(HMENU root, HMENU popup);

// 本体の「レイアウト」下位メニュー (「レイアウトを作成」を持つもの)
HMENU find_layout_menu(HMENU bar);

// 本体の「表示」メニュー (名前で見つからなければ、レイアウト下位メニューの親)
HMENU find_view_menu(HMENU bar);

// 本体のメニュー項目の番号 (道筋の先頭が「表示」のものは、道筋で見つからない時に目印から探す)
// how: 見つけ方 ("道筋" / "レイアウトの目印" / "表示の目印")。見つからなければ 0
UINT find_host_command(HMENU bar, const std::vector<std::wstring>& path, std::wstring* how = nullptr);

// 診断用にメニューの構成を文字で書き出す (depth 段まで)
std::wstring dump_menu(HMENU root, int depth);

} // namespace tpgd
