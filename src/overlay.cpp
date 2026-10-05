//----------------------------------------------------------------------------------
//	ドラッグ中の表示 (落とし先の予告・十字ボタン・タブの影)
//----------------------------------------------------------------------------------
#include "overlay.h"
#include "common.h"

#include <algorithm>
#include <cstdint>

namespace tpgd {

namespace {

#define OVERLAY_CLASS TPGD_NAME L"Overlay"

bool register_class() {
	static bool done = false;
	if (done) return true;
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpszClassName = OVERLAY_CLASS;
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = GetModuleHandleW(nullptr);
	done = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	return done;
}

// マウス操作を素通りさせる半透明の窓
HWND create_layered(HWND owner) {
	if (!register_class()) return nullptr;
	return CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		OVERLAY_CLASS, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
}

// 32bit (事前乗算アルファ) の描画面
struct Canvas {
	int w = 0, h = 0;
	uint32_t* px = nullptr;
	HDC dc = nullptr;
	HBITMAP bmp = nullptr;
	HGDIOBJ old = nullptr;

	Canvas(int width, int height) : w(std::max(1, width)), h(std::max(1, height)) {
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = w;
		bi.bmiHeader.biHeight = -h;   // 上から下
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		HDC screen = GetDC(nullptr);
		dc = CreateCompatibleDC(screen);
		ReleaseDC(nullptr, screen);
		void* bits = nullptr;
		bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		px = (uint32_t*)bits;
		old = SelectObject(dc, bmp);
		if (px) std::fill(px, px + (size_t)w * h, 0u);
	}
	~Canvas() {
		SelectObject(dc, old);
		DeleteObject(bmp);
		DeleteDC(dc);
	}

	// 色 c を不透明度 a (0〜255) で重ねる
	void fill(RECT r, COLORREF c, int a) {
		if (!px) return;
		r.left = std::max<LONG>(r.left, 0);
		r.top = std::max<LONG>(r.top, 0);
		r.right = std::min<LONG>(r.right, w);
		r.bottom = std::min<LONG>(r.bottom, h);
		uint32_t sr = GetRValue(c) * a / 255, sg = GetGValue(c) * a / 255, sb = GetBValue(c) * a / 255;
		for (int y = r.top; y < r.bottom; y++) {
			uint32_t* row = px + (size_t)y * w;
			for (int x = r.left; x < r.right; x++) {
				uint32_t d = row[x];
				uint32_t da = d >> 24, dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
				uint32_t inv = 255 - a;
				uint32_t na = a + da * inv / 255;
				uint32_t nr = sr + dr * inv / 255, ng = sg + dg * inv / 255, nb = sb + db * inv / 255;
				row[x] = (na << 24) | (nr << 16) | (ng << 8) | nb;
			}
		}
	}

	void frame(RECT r, COLORREF c, int a, int t) {
		fill({ r.left, r.top, r.right, r.top + t }, c, a);
		fill({ r.left, r.bottom - t, r.right, r.bottom }, c, a);
		fill({ r.left, r.top + t, r.left + t, r.bottom - t }, c, a);
		fill({ r.right - t, r.top + t, r.right, r.bottom - t }, c, a);
	}

	void present(HWND hwnd, POINT pos) {
		SIZE size = { w, h };
		POINT src = { 0, 0 };
		BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		UpdateLayeredWindow(hwnd, nullptr, &pos, &size, dc, &src, 0, &bf, ULW_ALPHA);
	}
};

RECT offset(RECT r, int dx, int dy) {
	OffsetRect(&r, dx, dy);
	return r;
}

// 十字ボタンの絵: 枠の中に小さな区画を描き、置かれる側を塗る
void draw_button(Canvas& cv, RECT r, int zone, bool hot, COLORREF highlight, COLORREF face, COLORREF edge) {
	int w = r.right - r.left, h = r.bottom - r.top;
	cv.fill(r, face, hot ? 245 : 215);
	cv.frame(r, hot ? highlight : edge, 255, std::max(1, w / 24));
	int inset = std::max(3, w / 5);
	RECT box = { r.left + inset, r.top + inset, r.right - inset, r.bottom - inset };
	cv.frame(box, edge, 255, std::max(1, w / 32));
	RECT part = box;
	int bw = box.right - box.left, bh = box.bottom - box.top;
	switch (zone) {
		case 2: part.right = box.left + bw / 2; break;   // Left
		case 3: part.left = box.right - bw / 2; break;   // Right
		case 4: part.bottom = box.top + bh / 2; break;   // Top
		case 5: part.top = box.bottom - bh / 2; break;   // Bottom
		default: InflateRect(&part, -std::max(1, bw / 8), -std::max(1, bh / 8)); break;  // Center
	}
	cv.fill(part, highlight, 255);
	(void)h;
}

} // namespace

//---------------------------------------------------------------------
//	落とし先の重ね窓
//---------------------------------------------------------------------

void DropOverlay::show(HWND owner, const RECT& area) {
	if (!hwnd_) hwnd_ = create_layered(owner);
	if (!hwnd_) return;
	area_ = area;
	SetWindowPos(hwnd_, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
		SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void DropOverlay::update(const DropVisual& v, COLORREF highlight, int alpha_percent, COLORREF face, COLORREF edge) {
	if (!hwnd_) return;
	int w = area_.right - area_.left, h = area_.bottom - area_.top;
	Canvas cv(w, h);
	int a = std::clamp(alpha_percent, 0, 100) * 255 / 100;
	if (v.has_preview) {
		RECT p = offset(v.preview, -area_.left, -area_.top);
		cv.fill(p, highlight, a);
		cv.frame(p, highlight, std::min(255, a + 120), 2);
	}
	for (auto& b : v.buttons) {
		draw_button(cv, offset(b.rect, -area_.left, -area_.top), b.zone, b.hot, highlight, face, edge);
	}
	cv.present(hwnd_, { area_.left, area_.top });
}

void DropOverlay::hide() {
	if (hwnd_) {
		DestroyWindow(hwnd_);
		hwnd_ = nullptr;
	}
}

//---------------------------------------------------------------------
//	ドラッグ中のタブの影
//---------------------------------------------------------------------

void DragGhost::show(HWND owner, const std::wstring& title, HFONT font, COLORREF face, COLORREF text, int alpha_percent, int height) {
	if (!hwnd_) hwnd_ = create_layered(owner);
	if (!hwnd_) return;

	// 文字の幅を測る
	HDC screen = GetDC(nullptr);
	HGDIOBJ of = SelectObject(screen, font);
	SIZE ts = {};
	GetTextExtentPoint32W(screen, title.c_str(), (int)title.size(), &ts);
	SelectObject(screen, of);
	ReleaseDC(nullptr, screen);
	size_ = { ts.cx + height, height };

	Canvas cv(size_.cx, size_.cy);
	cv.fill({ 0, 0, size_.cx, size_.cy }, face, 255);
	HGDIOBJ old = SelectObject(cv.dc, font);
	SetBkMode(cv.dc, TRANSPARENT);
	SetTextColor(cv.dc, text);
	RECT r = { 0, 0, size_.cx, size_.cy };
	DrawTextW(cv.dc, title.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	SelectObject(cv.dc, old);
	GdiFlush();
	// GDI の文字描画はアルファを壊すため、全体を一様な不透明度にそろえる
	uint32_t a = (uint32_t)(std::clamp(alpha_percent, 0, 100) * 255 / 100);
	for (int i = 0; i < size_.cx * size_.cy; i++) {
		uint32_t p = cv.px[i];
		uint32_t r8 = ((p >> 16) & 0xFF) * a / 255, g8 = ((p >> 8) & 0xFF) * a / 255, b8 = (p & 0xFF) * a / 255;
		cv.px[i] = (a << 24) | (r8 << 16) | (g8 << 8) | b8;
	}
	POINT cur;
	GetCursorPos(&cur);
	cv.present(hwnd_, { cur.x + 12, cur.y + 12 });
	SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void DragGhost::move(POINT cursor) {
	if (hwnd_) SetWindowPos(hwnd_, HWND_TOP, cursor.x + 12, cursor.y + 12, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void DragGhost::hide() {
	if (hwnd_) {
		DestroyWindow(hwnd_);
		hwnd_ = nullptr;
	}
}

} // namespace tpgd
