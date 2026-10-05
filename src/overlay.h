//----------------------------------------------------------------------------------
//	ドラッグ中の表示 (落とし先の予告・十字ボタン・タブの影)
//	半透明の重ね窓 (レイヤードウィンドウ) で描く。マウス操作は下の窓へ素通りさせる
//----------------------------------------------------------------------------------
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace tpgd {

// 十字ボタン1つ
struct CompassButton {
	RECT rect;         // 画面座標
	int zone;          // DropZone の値 (Center/Left/Right/Top/Bottom)
	bool hot;          // カーソルが乗っている
};

// 落とし先の表示内容 (座標はすべて画面座標)
struct DropVisual {
	bool has_preview = false;
	RECT preview = {};                    // 置かれる範囲 (色付きで塗る)
	std::vector<CompassButton> buttons;   // 十字ボタン式の場合のボタン
};

// 落とし先の重ね窓 (入れ物の全面を覆う)
class DropOverlay {
public:
	void show(HWND owner, const RECT& screen_area);
	void update(const DropVisual& v, COLORREF highlight, int alpha_percent, COLORREF button_face, COLORREF button_edge);
	void hide();
	bool visible() const { return hwnd_ != nullptr && IsWindowVisible(hwnd_); }

private:
	HWND hwnd_ = nullptr;
	RECT area_ = {};
};

// ドラッグ中のタブの影 (カーソルに付いて動く)
class DragGhost {
public:
	void show(HWND owner, const std::wstring& title, HFONT font, COLORREF face, COLORREF text, int alpha_percent, int height);
	void move(POINT cursor);
	void hide();

private:
	HWND hwnd_ = nullptr;
	SIZE size_ = {};
};

} // namespace tpgd
