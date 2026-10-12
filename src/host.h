//----------------------------------------------------------------------------------
//	本体 (AviUtl2) との窓・メニュー・データフォルダのやり取り
//----------------------------------------------------------------------------------
#pragma once
#include "common.h"

namespace tpgd {

// 本体のパネル窓のクラス名
constexpr const wchar_t* HOST_PANEL_CLASS = L"aviutl2Manager";

// 本体のメインウィンドウ (オーナーを持たない aviutl2Manager)
HWND find_main_window();

// 切り離された本体パネル (メインウィンドウをオーナーに持つ aviutl2Manager の可視窓)
std::vector<HWND> find_floating_panels(HWND main);

// メニューバーを名前でたどり、項目の番号を返す (見つからなければ 0)
// 例: { L"表示", L"レイアウト", L"basic" }
// ※ 項目番号は導入プラグイン数で変わるため、必ず名前で探す
UINT find_menu_command(HWND main, const std::vector<std::wstring>& path);

// 本体のメニューの中身を作らせる (メニューを開いた時と同じ知らせを送る)。
// 本体や他のプラグインがメニューの中身を開く時まで作らない場合に、探す前に使う
void prime_host_menus(HWND main);

// 本体の「レイアウト」メニュー (「レイアウトを作成」を含むもの) が使える状態か。
// 無ければ中身を作らせて、wait_ms まで待つ
bool wait_host_menus_ready(HWND main, int wait_ms);

// 本体のレイアウトを名前で適用する (成功時 true)
bool apply_host_layout(HWND main, const std::wstring& name);

// 本体のデータフォルダ (例: C:\ProgramData\aviutl2)
std::wstring app_data_dir();

// 本体のレイアウトファイルの置き場所 (<データフォルダ>\Default)
std::wstring layout_dir();

// 保存済みのレイアウト名の一覧 (拡張子なし)
std::vector<std::wstring> list_layouts();

// 表示倍率 (96dpi を 1.0 とする)
double dpi_scale(HWND h);

// style.conf の値 (表示倍率込み)
int style_size(const char* key, int def, double scale);
COLORREF style_color(const char* key, COLORREF def);

} // namespace tpgd
