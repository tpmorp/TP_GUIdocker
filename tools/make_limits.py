"""
TP_GUIdocker の機能制限と解決案の一覧を、判断用の PDF にする。

  入力: docs/limits.json (調査結果・対応案・判断の記録)
  出力: docs/TP_GUIdocker_機能制限.pdf

頁の作りは tools/make_samples.py と同じ (A4 横)。
使い方: py -3 tools/make_limits.py
"""
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_samples import (  # noqa: E402
    INK, MARGIN, MARK, PAGE_H, PAGE_W, SUB, TABLE_HEAD, TABLE_LINE, font, new_page, wrap,
)

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "docs" / "limits.json"
OUTPUT = ROOT / "docs" / "TP_GUIdocker_機能制限.pdf"

LABEL_W = 210
BODY_W = PAGE_W - 2 * MARGIN - LABEL_W - 24
LINE_H = 27
DECIDED_FILL = (232, 245, 233)
RECOMMEND = (40, 110, 60)


def card_rows(item):
    rows = [
        ("普段の画面では", item["normal"], INK),
        ("有効中の現状", item["now"], INK),
        ("原因", item["cause"], INK),
        ("対応案", "\n".join(item["options"]), INK),
        ("推奨", item["recommend"], RECOMMEND),
    ]
    decided = item.get("decided")
    rows.append(("判断", decided if decided else "（ここを番号で返答してください: 案の記号 / しない / 保留）", INK if decided else MARK))
    return rows


def card_height(d, item):
    h = 50
    for _, text, _ in card_rows(item):
        h += max(1, len(wrap(d, text, font(19), BODY_W))) * LINE_H + 10
    return h + 10


def draw_card(page, d, y, item):
    h = card_height(d, item)
    fill = DECIDED_FILL if item.get("decided") else (250, 250, 250)
    d.rectangle((MARGIN, y, PAGE_W - MARGIN, y + h), fill=fill, outline=TABLE_LINE, width=2)
    d.rectangle((MARGIN, y, PAGE_W - MARGIN, y + 42), fill=TABLE_HEAD)
    d.text((MARGIN + 14, y + 7), f"{item['id']}  {item['title']}", fill="white", font=font(22, bold=True))
    cy = y + 50
    for label, text, color in card_rows(item):
        lines = wrap(d, text, font(19), BODY_W)
        d.text((MARGIN + 14, cy), label, fill=SUB, font=font(18, bold=True))
        for i, line in enumerate(lines):
            d.text((MARGIN + LABEL_W, cy + i * LINE_H), line, fill=color, font=font(19, bold=(label == "判断" and not item.get("decided"))))
        cy += max(1, len(lines)) * LINE_H + 10
    return h


def main():
    data = json.loads(DATA.read_text(encoding="utf-8"))
    pages = []

    # 表紙: 使い方
    page, d = new_page("TP_GUIdocker 機能制限と解決案", f"version {data['version']} での調査結果（{data['date']}）")
    y = 140
    for line in data["intro"]:
        for wl in wrap(d, line, font(22), PAGE_W - 2 * MARGIN):
            d.text((MARGIN, y), wl, fill=INK, font=font(22))
            y += 38
        y += 8
    y += 20
    d.text((MARGIN, y), "項目の一覧", fill=INK, font=font(24, bold=True))
    y += 46
    for sec in data["sections"]:
        d.text((MARGIN, y), sec["title"], fill=INK, font=font(21, bold=True))
        y += 36
        for it in sec["items"]:
            mark = f"［{it['decided']}］" if it.get("decided") else ""
            d.text((MARGIN + 30, y), f"{it['id']}  {it['title']}  {mark}", fill=SUB if it.get("decided") else INK, font=font(19))
            y += 30
        y += 12
    pages.append(page)

    # 各節
    for sec in data["sections"]:
        page, d = new_page(sec["title"])
        y = 124
        for fact in sec.get("facts", []):
            for wl in wrap(d, "・" + fact, font(19), PAGE_W - 2 * MARGIN):
                d.text((MARGIN, y), wl, fill=INK, font=font(19))
                y += 29
        if sec.get("facts"):
            y += 14
        for it in sec["items"]:
            h = card_height(d, it)
            if y + h > PAGE_H - MARGIN:
                pages.append(page)
                page, d = new_page(sec["title"] + " (続き)")
                y = 124
            y += draw_card(page, d, y, it) + 18
        pages.append(page)

    for i, p in enumerate(pages, start=1):
        ImageDraw.Draw(p).text((PAGE_W - MARGIN - 120, PAGE_H - 40), f"{i} / {len(pages)}", fill=SUB, font=font(18))
    opts = dict(save_all=True, append_images=pages[1:], resolution=150, quality=92)
    out = OUTPUT
    try:
        pages[0].save(out, **opts)
    except PermissionError:
        out = OUTPUT.with_name(f"{OUTPUT.stem}_{data['date']}{OUTPUT.suffix}")
        pages[0].save(out, **opts)
        print(f"注意: {OUTPUT.name} が開かれているため、別名で保存した")
    print(f"生成: {out} ({len(pages)} ページ)")


if __name__ == "__main__":
    Image.init()
    main()
