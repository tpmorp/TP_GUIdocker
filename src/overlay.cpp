//----------------------------------------------------------------------------------
//	ドラッグ中の表示 (落とし先の予告・十字ボタン・タブの影)
//----------------------------------------------------------------------------------
#include "overlay.h"
#include "common.h"

#include <algorithm>
#include <cmath>
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

// DropZone の値 (tree.h と同じ並び)
enum { Z_NONE = 0, Z_CENTER = 1, Z_LEFT = 2, Z_RIGHT = 3, Z_TOP = 4, Z_BOTTOM = 5 };

// 中心からの位置 (dx, dy) がどの部分か。gap: 部分の間の隙間の幅
int donut_zone(double dx, double dy, double radius, double hole, double gap) {
	double d = std::sqrt(dx * dx + dy * dy);
	if (d <= hole) return Z_CENTER;
	if (d > radius) return Z_NONE;
	if (gap > 0) {
		if (d < hole + gap) return Z_NONE;                                     // 円と扇の間の隙間
		if (std::abs(std::abs(dx) - std::abs(dy)) / 1.41421356 < gap / 2) return Z_NONE;  // 扇どうしの隙間
	}
	if (std::abs(dx) >= std::abs(dy)) return dx < 0 ? Z_LEFT : Z_RIGHT;
	return dy < 0 ? Z_TOP : Z_BOTTOM;
}

// 点が三角形の中か
bool in_triangle(double px, double py, const double* t) {
	auto side = [](double ax, double ay, double bx, double by, double x, double y) {
		return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
	};
	double s1 = side(t[0], t[1], t[2], t[3], px, py);
	double s2 = side(t[2], t[3], t[4], t[5], px, py);
	double s3 = side(t[4], t[5], t[0], t[1], px, py);
	bool neg = s1 < 0 || s2 < 0 || s3 < 0, pos = s1 > 0 || s2 > 0 || s3 > 0;
	return !(neg && pos);
}

// ドーナツ型のボタンを描く (縁を滑らかにするため、1画素を 4x4 の点で調べて混ぜる)
void draw_donut(Canvas& cv, POINT c, int radius, int hole, int hot, COLORREF highlight, COLORREF face, COLORREF ink) {
	const double R = radius, H = hole, gap = std::max(2.0, radius / 30.0);
	// 扇ごとの矢印 (外向きの三角形)。中心からの距離 mid の位置に置く
	double mid = (H + R) / 2, a = (R - H) * 0.22;
	double arrows[4][6] = {
		{ -mid - a, 0, -mid + a * 0.6, -a, -mid + a * 0.6, a },   // 左
		{ mid + a, 0, mid - a * 0.6, -a, mid - a * 0.6, a },      // 右
		{ 0, -mid - a, -a, -mid + a * 0.6, a, -mid + a * 0.6 },   // 上
		{ 0, mid + a, -a, mid - a * 0.6, a, mid - a * 0.6 },      // 下
	};
	// 中心の円の印 (タブとして重ねる): 小さな四角2つを重ねた形
	double s = H * 0.42;
	RECT icon_back = { (LONG)(-s * 0.7), (LONG)(-s * 0.9), (LONG)(s * 0.9), (LONG)(s * 0.5) };
	RECT icon_front = { (LONG)(-s * 0.9), (LONG)(-s * 0.5), (LONG)(s * 0.7), (LONG)(s * 0.9) };

	const int N = 4;
	for (int py = c.y - radius - 1; py <= c.y + radius + 1; py++) {
		if (py < 0 || py >= cv.h) continue;
		for (int px = c.x - radius - 1; px <= c.x + radius + 1; px++) {
			if (px < 0 || px >= cv.w) continue;
			double sa = 0, sr = 0, sg = 0, sb = 0;
			for (int sy = 0; sy < N; sy++) {
				for (int sx = 0; sx < N; sx++) {
					double dx = px + (sx + 0.5) / N - c.x - 0.5, dy = py + (sy + 0.5) / N - c.y - 0.5;
					int z = donut_zone(dx, dy, R, H, gap);
					if (z == Z_NONE) continue;
					COLORREF col = (z == hot) ? highlight : face;
					double alpha = (z == hot) ? 0.95 : 0.88;
					// 印 (矢印・四角) は文字色で描く
					bool mark = false;
					if (z == Z_CENTER) {
						POINT q = { (LONG)std::floor(dx), (LONG)std::floor(dy) };
						bool in_front = PtInRect(&icon_front, q) != FALSE;
						bool in_back = PtInRect(&icon_back, q) != FALSE;
						RECT inner = icon_front;
						InflateRect(&inner, -std::max<LONG>(1, (LONG)(s / 6)), -std::max<LONG>(1, (LONG)(s / 6)));
						mark = (in_front && !PtInRect(&inner, q)) || (in_back && !in_front && q.y < icon_front.top);
					} else {
						mark = in_triangle(dx, dy, arrows[z - Z_LEFT]);
					}
					if (mark) { col = ink; alpha = 1.0; }
					sa += alpha;
					sr += GetRValue(col) * alpha;
					sg += GetGValue(col) * alpha;
					sb += GetBValue(col) * alpha;
				}
			}
			if (sa <= 0) continue;
			double n = N * N;
			uint32_t A = (uint32_t)std::min(255.0, sa / n * 255), Rr = (uint32_t)std::min(255.0, sr / n), G = (uint32_t)std::min(255.0, sg / n), B = (uint32_t)std::min(255.0, sb / n);
			// 下の予告表示の上に重ねる (事前乗算アルファの合成)
			uint32_t& dst = cv.px[(size_t)py * cv.w + px];
			uint32_t inv = 255 - A;
			uint32_t da = dst >> 24, dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
			dst = ((A + da * inv / 255) << 24) | ((Rr + dr * inv / 255) << 16) | ((G + dg * inv / 255) << 8) | (B + db * inv / 255);
		}
	}
}

} // namespace

int compass_zone_at(const CompassDonut& c, POINT pt) {
	return donut_zone(pt.x - c.center.x, pt.y - c.center.y, c.radius, c.hole, 0);
}

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
	if (v.has_compass) {
		POINT c = { v.compass.center.x - area_.left, v.compass.center.y - area_.top };
		draw_donut(cv, c, v.compass.radius, v.compass.hole, v.hot_zone, highlight, face, edge);
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
