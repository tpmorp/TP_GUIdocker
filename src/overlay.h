//----------------------------------------------------------------------------------
//	ドラッグ中の表示 (落とし先の予告・十字ボタン・タブの影)
//	半透明の重ね窓 (レイヤードウィンドウ) で描く。マウス操作は下の窓へ素通りさせる
//----------------------------------------------------------------------------------
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace tpgd {

// ドーナツ型のボタン (中心の円 = タブとして重ねる / 周りの扇形4つ = 上下左右に分割)
struct CompassDonut {
	POINT center = {};   // 画面座標
	int radius = 0;      // 外側の半径
	int hole = 0;        // 中心の円の半径
};

// ドーナツ型のボタンの当たり判定 (DropZone の値を返す。外れなら 0 = None)
int compass_zone_at(const CompassDonut& c, POINT pt);

// 落とし先の表示内容 (座標はすべて画面座標)
struct DropVisual {
	bool has_preview = false;
	RECT preview = {};       // 置かれる範囲 (色付きで塗る)
	bool has_compass = false;
	CompassDonut compass;    // 十字ボタン式の場合
	int hot_zone = 0;        // カーソルが乗っている部分 (DropZone の値)
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
	// 画面上の位置 (見本の書き出し用)
	RECT rect() const {
		RECT r = {};
		if (hwnd_) GetWindowRect(hwnd_, &r);
		return r;
	}

private:
	HWND hwnd_ = nullptr;
	SIZE size_ = {};
};

} // namespace tpgd
