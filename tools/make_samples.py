"""
TP_GUIdocker の見本 PDF を作る。

  入力: プラグインの「見本を書き出す」で書き出された撮影画像と位置の記録 (samples.json)
        docs/ui_spec.json (文言・見た目・動作の定義)
  出力: docs/TP_GUIdocker_見本.pdf

撮影画像の各部分に枠と番号を書き込み、番号ごとに指示書の ID・項目・現在の値を一覧にする。
メニュー・名前の入力画面・確認の画面は、文言の定義から見本図を描く。

使い方: py -3 tools/make_samples.py [見本の書き出し先フォルダ]
"""
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

Image.init()  # PDF の画像保存に使う形式 (JPEG 等) を確実に読み込む

ROOT = Path(__file__).resolve().parent.parent
SPEC = ROOT / "docs" / "ui_spec.json"
OUTPUT = ROOT / "docs" / "TP_GUIdocker_見本.pdf"
DEFAULT_INPUT = Path(r"C:\ProgramData\aviutl2\Plugin\TPplug\TP_GUIdocker_見本")

# A4 横 (150dpi)
PAGE_W, PAGE_H = 1754, 1240
MARGIN = 48
FONT_PATH = r"C:\Windows\Fonts\meiryo.ttc"
FONT_BOLD_PATH = r"C:\Windows\Fonts\meiryob.ttc"

MARK = (230, 40, 40)          # 枠と番号の色
INK = (30, 30, 30)
SUB = (110, 110, 110)
TABLE_HEAD = (58, 74, 107)
TABLE_LINE = (200, 200, 200)


def font(size, bold=False):
    return ImageFont.truetype(FONT_BOLD_PATH if bold else FONT_PATH, size)


def load_spec():
    d = json.loads(SPEC.read_text(encoding="utf-8"))
    items = {}
    for kind, label_key in (("text", "where"), ("design", "item"), ("behavior", "item")):
        for t in d[kind]:
            items[t["id"]] = {
                "kind": {"text": "文言", "design": "見た目", "behavior": "動作"}[kind],
                "label": t[label_key],
                "value": t["value"],
            }
    return items


def text_of(spec, item_id, **fmt):
    v = spec[item_id]["value"]
    for k, x in fmt.items():
        v = v.replace(k, str(x))
    return v


def wrap(draw, text, fnt, width):
    """幅に収まるよう折り返す (日本語は1文字単位)"""
    lines = []
    for para in str(text).split("\n"):
        line = ""
        for ch in para:
            if draw.textlength(line + ch, font=fnt) > width and line:
                lines.append(line)
                line = ch
            else:
                line += ch
        lines.append(line)
    return lines


def new_page(title, subtitle=""):
    page = Image.new("RGB", (PAGE_W, PAGE_H), "white")
    d = ImageDraw.Draw(page)
    d.text((MARGIN, 28), title, fill=INK, font=font(34, bold=True))
    if subtitle:
        d.text((MARGIN, 76), subtitle, fill=SUB, font=font(20))
    d.line((MARGIN, 108, PAGE_W - MARGIN, 108), fill=TABLE_LINE, width=2)
    return page, d


def draw_badge(d, x, y, n, r=18):
    d.ellipse((x - r, y - r, x + r, y + r), fill=MARK, outline="white", width=3)
    s = str(n)
    f = font(22, bold=True)
    w = d.textlength(s, font=f)
    d.text((x - w / 2, y - 15), s, fill="white", font=f)


def crop_detail(img, rect, box_w, box_h):
    """rect の周りを切り出して拡大し、赤い枠を書き込んだ画像を返す"""
    l, t, r, b = rect
    w, h = max(1, r - l), max(1, b - t)
    # 細長い部分 (帯・境界線) は全体を収めると縮みすぎるため、一部分を拡大して見せる
    if w > h * 6:
        r = l + min(w, max(h * 8, 240))     # 横長: 左端の部分
    elif h > w * 6:
        mid = (t + b) // 2                  # 縦長: 中央の部分
        half = min(h, max(w * 8, 240)) // 2
        t, b = mid - half, mid + half
    w, h = max(1, r - l), max(1, b - t)
    pad_x = max(60, w // 3)
    pad_y = max(40, h // 3)
    cl, ct = max(0, l - pad_x), max(0, t - pad_y)
    cr, cb = min(img.width, r + pad_x), min(img.height, b + pad_y)
    # 枠に合わせて縦横比をそろえる (足りない側を広げる)
    want = box_w / box_h
    cw, ch = cr - cl, cb - ct
    if cw / ch < want:
        extra = int(ch * want) - cw
        cl, cr = max(0, cl - extra // 2), min(img.width, cr + extra - extra // 2)
    else:
        extra = int(cw / want) - ch
        ct, cb = max(0, ct - extra // 2), min(img.height, cb + extra - extra // 2)
    crop = img.crop((cl, ct, cr, cb))
    scale = min(box_w / crop.width, box_h / crop.height)
    crop = crop.resize((max(1, int(crop.width * scale)), max(1, int(crop.height * scale))), Image.LANCZOS)
    d = ImageDraw.Draw(crop)
    d.rectangle(((l - cl) * scale, (t - ct) * scale, (r - cl) * scale, (b - ct) * scale), outline=MARK, width=3)
    return crop


def detail_pages(scene, img, spec):
    """番号ごとに拡大図と ID・項目・現在の値を並べる (収まらなければ次のページへ続ける)"""
    pages = []
    title = scene["title"] + " ― 拡大図と項目"
    page, d = new_page(title, "番号ごとに、その部分の拡大図と、指示書の ID・項目・現在の値を示します。")
    thumb_w, thumb_h = 420, 200
    col_x = MARGIN + thumb_w + 40
    cols = [300, 560, PAGE_W - MARGIN - col_x - 300 - 560]
    f, fb = font(18), font(18, bold=True)
    y = 124

    def header(d, y):
        d.rectangle((MARGIN, y, PAGE_W - MARGIN, y + 34), fill=TABLE_HEAD)
        d.text((MARGIN + 8, y + 6), "拡大図", fill="white", font=fb)
        x = col_x
        for w, h in zip(cols, ["ID (指示書)", "項目・表示される場所", "現在の値"]):
            d.text((x + 8, y + 6), h, fill="white", font=fb)
            x += w
        return y + 40

    y = header(d, y)
    for n, note in enumerate(scene["notes"], start=1):
        # この番号の行の高さを見積もる
        rows = []
        for item_id in note["ids"]:
            info = spec.get(item_id, {"label": "(指示書に無い項目)", "value": ""})
            cells = [item_id, info["label"], info["value"]]
            wrapped = [wrap(d, c, f, w - 16) for c, w in zip(cells, cols)]
            rows.append(wrapped)
        text_h = sum(max(len(l) for l in r) * 24 + 10 for r in rows) + 34
        block_h = max(thumb_h + 40, text_h)
        if y + block_h > PAGE_H - MARGIN:
            pages.append(page)
            page, d = new_page(title + " (続き)")
            y = header(d, 124)
        crop = crop_detail(img, note["rect"], thumb_w, thumb_h)
        page.paste(crop, (MARGIN, y))
        d.rectangle((MARGIN - 1, y - 1, MARGIN + crop.width, y + crop.height), outline=TABLE_LINE)
        draw_badge(d, MARGIN + 20, y + 20, n)
        d.text((MARGIN, y + crop.height + 6), note.get("note", ""), fill=SUB, font=font(16))
        ry = y
        for wrapped in rows:
            h = max(len(l) for l in wrapped) * 24 + 10
            x = col_x
            for lines, w in zip(wrapped, cols):
                for i, line in enumerate(lines):
                    d.text((x + 8, ry + 5 + i * 24), line, fill=INK, font=f)
                x += w
            ry += h
            d.line((col_x, ry, PAGE_W - MARGIN, ry), fill=TABLE_LINE, width=1)
        y += block_h
        d.line((MARGIN, y - 8, PAGE_W - MARGIN, y - 8), fill=(160, 160, 160), width=2)
    pages.append(page)
    return pages


def scene_pages(scene, folder, spec):
    img = Image.open(folder / scene["image"]).convert("RGB")
    page, d = new_page(scene["title"], "撮影した実際の画面です。赤い枠と番号の詳細は、次のページの拡大図と項目を見てください。")
    area_top, area_h = 124, PAGE_H - 124 - MARGIN
    scale = min((PAGE_W - 2 * MARGIN) / img.width, area_h / img.height)
    sw, sh = int(img.width * scale), int(img.height * scale)
    shown = img.resize((sw, sh), Image.LANCZOS)
    ox = (PAGE_W - sw) // 2
    page.paste(shown, (ox, area_top))
    d.rectangle((ox - 1, area_top - 1, ox + sw, area_top + sh), outline=TABLE_LINE, width=1)
    for i, note in enumerate(scene["notes"], start=1):
        l, t, r, b = note["rect"]
        x1, y1 = max(ox, ox + l * scale), max(area_top, area_top + t * scale)
        x2, y2 = min(ox + sw, ox + r * scale), min(area_top + sh, area_top + b * scale)
        x2, y2 = max(x2, x1 + 4), max(y2, y1 + 4)
        d.rectangle((x1, y1, x2, y2), outline=MARK, width=3)
        bx = min(max(x1, ox + 18), ox + sw - 18)
        by = min(max(y1, area_top + 18), area_top + sh - 18)
        draw_badge(d, bx, by, i)
    return [page] + detail_pages(scene, img, spec)


# ---------------------------------------------------------------------
#  メニュー・画面の見本図
# ---------------------------------------------------------------------

MENU_BG = (43, 43, 43)
MENU_TEXT = (240, 240, 240)
MENU_GRAY = (150, 150, 150)
MENU_SEP = (80, 80, 80)
ID_COLOR = (230, 90, 90)


def draw_menu(d, x, y, items, width=520):
    """items: [(ID or None, 表示文字, 子メニュー有り)] / None は区切り線。描いた高さを返す"""
    f = font(22)
    fid = font(15)
    h_item, h_sep = 40, 14
    total = sum(h_sep if it is None else h_item for it in items) + 12
    d.rectangle((x, y, x + width, y + total), fill=MENU_BG, outline=(90, 90, 90), width=1)
    cy = y + 6
    for it in items:
        if it is None:
            d.line((x + 12, cy + h_sep // 2, x + width - 12, cy + h_sep // 2), fill=MENU_SEP, width=1)
            cy += h_sep
            continue
        item_id, label, sub = it
        d.text((x + 36, cy + 7), label, fill=MENU_TEXT, font=f)
        if sub:
            d.text((x + width - 30, cy + 7), "▸", fill=MENU_TEXT, font=f)
        if item_id:
            d.text((x + width + 12, cy + 12), item_id, fill=ID_COLOR, font=fid)
        cy += h_item
    return total


def menus_page(spec):
    page, d = new_page("メニューの見本", "文言は指示書の定義から描いた見本です。赤い文字が指示書の ID です。")
    t = lambda i, **k: text_of(spec, i, **k)
    y = 140
    d.text((MARGIN, y), "「編集」→「プラグイン」→「TP_GUIdocker」", fill=INK, font=font(22, bold=True))
    main_items = [
        ("MENU_ENABLE", t("MENU_ENABLE") + "  (有効中は " + t("MENU_DISABLE") + ")", False),
        ("MENU_WS_GROUP", t("MENU_WS_GROUP"), True),
        ("MENU_MAXIMIZE", t("MENU_MAXIMIZE"), False),
        ("MENU_LOCK", t("MENU_LOCK") + "  (ロック中は " + t("MENU_UNLOCK") + ")", False),
        ("MENU_RESET", t("MENU_RESET"), False),
        ("MENU_CHOOSE_RETURN", t("MENU_CHOOSE_RETURN"), False),
        ("MENU_DROP_STYLE_COMPASS", t("MENU_DROP_STYLE_COMPASS") + "  (または " + t("MENU_DROP_STYLE_REGION") + ")", False),
        ("MENU_EXPORT_SAMPLES", t("MENU_EXPORT_SAMPLES"), False),
    ]
    draw_menu(d, MARGIN, y + 40, main_items, width=880)
    d.text((MARGIN + 1160, y), "「ワークスペース」の中", fill=INK, font=font(22, bold=True))
    ws_items = [("MENU_WS_SWITCH" if i == 1 else None, t("MENU_WS_SWITCH", **{"%d": i}), False) for i in range(1, 4)]
    ws_items.append((None, "…", False))
    ws_items.append((None, t("MENU_WS_SWITCH", **{"%d": 9}), False))
    draw_menu(d, MARGIN + 1160, y + 40, ws_items, width=340)

    y2 = 620
    d.text((MARGIN, y2), "タブの右クリック", fill=INK, font=font(22, bold=True))
    tab_items = [
        ("TAB_MAXIMIZE", t("TAB_MAXIMIZE") + "  (最大化中は " + t("TAB_RESTORE") + ")", False),
        None,
        ("TAB_FLOAT", t("TAB_FLOAT"), False),
        ("TAB_CLOSE", t("TAB_CLOSE"), False),
        ("TAB_MOVE_TO", t("TAB_MOVE_TO"), True),
        ("TAB_SPLIT", t("TAB_SPLIT"), True),
        None,
        ("AREA_CLOSE", t("AREA_CLOSE") + "  (空き区画のみ)", False),
    ]
    draw_menu(d, MARGIN, y2 + 40, tab_items, width=600)
    d.text((MARGIN + 860, y2), "「" + t("TAB_MOVE_TO") + "」の中", fill=INK, font=font(22, bold=True))
    draw_menu(d, MARGIN + 860, y2 + 40, [
        ("AREA_NAME_FORMAT", t("AREA_NAME_FORMAT", **{"%s": "プレビュー編集"}), False),
        ("AREA_NAME_EMPTY", t("AREA_NAME_EMPTY", **{"%d": 1}), False),
    ], width=360)
    d.text((MARGIN + 860, y2 + 170), "「" + t("TAB_SPLIT") + "」の中", fill=INK, font=font(22, bold=True))
    draw_menu(d, MARGIN + 860, y2 + 210, [
        ("SPLIT_LEFT", t("SPLIT_LEFT"), False),
        ("SPLIT_RIGHT", t("SPLIT_RIGHT"), False),
        ("SPLIT_TOP", t("SPLIT_TOP"), False),
        ("SPLIT_BOTTOM", t("SPLIT_BOTTOM"), False),
    ], width=360)
    return page


def workspace_page(spec):
    page, d = new_page("ワークスペースの操作の見本", "文言は指示書の定義から描いた見本です。赤い文字が指示書の ID です。")
    t = lambda i, **k: text_of(spec, i, **k)
    y = 140
    d.text((MARGIN, y), "ワークスペースのボタンの右クリック", fill=INK, font=font(22, bold=True))
    draw_menu(d, MARGIN, y + 40, [
        ("WS_MENU_RENAME", t("WS_MENU_RENAME"), False),
        ("WS_MENU_SAVE", t("WS_MENU_SAVE"), False),
        ("WS_MENU_RESET", t("WS_MENU_RESET"), False),
        None,
        ("WS_MENU_DELETE", t("WS_MENU_DELETE"), False),
    ], width=480)

    # 名前の入力画面
    x, y = MARGIN + 860, 140
    d.text((x, y), "名前の入力画面 (「＋」・名前を変更)", fill=INK, font=font(22, bold=True))
    y += 40
    w, h = 620, 200
    d.rectangle((x, y, x + w, y + h), fill=(240, 240, 240), outline=(120, 120, 120), width=2)
    d.rectangle((x, y, x + w, y + 40), fill=(220, 220, 220))
    d.text((x + 14, y + 7), t("WS_NAME_TITLE"), fill=INK, font=font(20))
    d.text((x + w + 12, y + 12), "WS_NAME_TITLE", fill=ID_COLOR, font=font(15))
    d.text((x + 20, y + 72), t("WS_NAME_LABEL"), fill=INK, font=font(20))
    d.text((x + w + 12, y + 76), "WS_NAME_LABEL", fill=ID_COLOR, font=font(15))
    d.rectangle((x + 110, y + 66, x + w - 20, y + 100), fill="white", outline=(120, 120, 120))
    d.text((x + 118, y + 70), t("WS_NEW_NAME", **{"%d": 2}), fill=INK, font=font(20))
    d.text((x + w + 12, y + 110), "WS_NEW_NAME (初期値)", fill=ID_COLOR, font=font(15))
    for i, (bid, bx) in enumerate((("BUTTON_OK", x + w - 290), ("BUTTON_CANCEL", x + w - 150))):
        d.rectangle((bx, y + 140, bx + 130, y + 180), fill=(225, 225, 225), outline=(120, 120, 120))
        label = t(bid)
        lw = d.textlength(label, font=font(20))
        d.text((bx + 65 - lw / 2, y + 147), label, fill=INK, font=font(20))
        d.text((bx, y + 186), bid, fill=ID_COLOR, font=font(15))

    # 帯の要素
    y = 600
    d.text((MARGIN, y), "ワークスペースの帯 (画面上部)", fill=INK, font=font(22, bold=True))
    y += 44
    bar_items = [
        ("WS_DEFAULT_NAME", t("WS_DEFAULT_NAME"), True),
        ("WS_NEW_NAME", t("WS_NEW_NAME", **{"%d": 2}), False),
        ("WS_ADD", t("WS_ADD"), False),
    ]
    bx = MARGIN
    d.rectangle((MARGIN, y, PAGE_W - MARGIN, y + 44), fill=(56, 56, 56))
    for row, (item_id, label, sel) in enumerate(bar_items):
        lw = d.textlength(label, font=font(20)) + 32
        if sel:
            d.rectangle((bx, y, bx + lw, y + 44), fill=(112, 112, 192))
        d.text((bx + 16, y + 9), label, fill=(255, 255, 255) if sel else (150, 150, 150), font=font(20))
        # ラベルは段をずらし、項目から線を引いて対応を示す
        ly = y + 60 + row * 26
        d.line((bx + lw / 2, y + 44, bx + lw / 2, ly + 8), fill=ID_COLOR, width=1)
        d.text((bx + lw / 2 + 6, ly), item_id, fill=ID_COLOR, font=font(15))
        bx += lw
    lock = "(錠前の絵)"
    lw = d.textlength(lock, font=font(20)) + 32
    lx = PAGE_W - MARGIN - lw
    d.text((lx + 16, y + 9), lock, fill=(150, 150, 150), font=font(20))
    lock_label = "錠前の絵 / 説明: UNLOCK_BADGE (" + t("UNLOCK_BADGE") + ")・LOCK_BADGE (" + t("LOCK_BADGE") + ")"
    tw = d.textlength(lock_label, font=font(15))
    d.line((lx + lw / 2, y + 44, lx + lw / 2, y + 68), fill=ID_COLOR, width=1)
    d.text((PAGE_W - MARGIN - tw, y + 60), lock_label, fill=ID_COLOR, font=font(15))
    return page


def messages_pages(spec):
    """確認・知らせの画面の文言を、画面の形で並べる"""
    ids = ["CONFIRM_FIRST", "CONFIRM_HOST_UPDATED", "EXPLORER_NOTICE", "WS_DELETE_CONFIRM", "WS_DELETE_LAST",
           "DOCK_UNKNOWN_PANEL", "EXPORT_DONE", "EXPORT_NEED_ACTIVE"]
    fail = ["FAIL_TITLE", "REASON_SAVE_CURRENT", "REASON_REGISTER", "REASON_READ_SAVED", "REASON_WRITE",
            "REASON_LOAD_FAILED", "REASON_NOT_IN_MENU", "REASON_MISSING", "REASON_CAPTURE", "FAIL_RESTORED", "FAIL_NOT_RESTORED"]
    examples = {"%lu": "2011200", "%d": "1"}
    s_examples = {"EXPORT_DONE": r"C:\ProgramData\aviutl2\Plugin\TPplug\TP_GUIdocker_見本", "WS_DELETE_CONFIRM": "ワークスペース2"}
    pages = []
    page, d = new_page("確認・知らせの画面の見本", "%s・%d・%lu は実際の名前や数に置き換わる部分です (見本では例を入れています)。")
    x, y, col = MARGIN, 130, 0
    box_w = (PAGE_W - 2 * MARGIN - 40) // 2
    for item_id in ids:
        msg = spec[item_id]["value"].replace("%s", s_examples.get(item_id, "(名前)"))
        if item_id == "CONFIRM_HOST_UPDATED":
            msg = msg.replace("%lu", "2011100", 1)  # 旧バージョンの例
        for k, v in examples.items():
            msg = msg.replace(k, v)
        lines = wrap(d, msg, font(18), box_w - 40)
        h = 50 + len(lines) * 26 + 20
        if y + h > PAGE_H - MARGIN:
            col += 1
            if col > 1:
                pages.append(page)
                page, d = new_page("確認・知らせの画面の見本 (続き)")
                col = 0
            x = MARGIN + col * (box_w + 40)
            y = 130
        d.rectangle((x, y, x + box_w, y + h), fill=(246, 246, 246), outline=(150, 150, 150))
        d.text((x + 14, y + 8), item_id, fill=ID_COLOR, font=font(16, bold=True))
        for i, line in enumerate(lines):
            d.text((x + 20, y + 40 + i * 26), line, fill=INK, font=font(18))
        y += h + 18
    pages.append(page)

    # 有効化に失敗した時は、FAIL_TITLE + 理由 + 末尾 を組み合わせて出す
    page, d = new_page("有効化に失敗した時の知らせの見本", "先頭 (FAIL_TITLE) + 理由 (REASON_…) + 末尾 (FAIL_RESTORED または FAIL_NOT_RESTORED) を組み合わせて表示します。")
    y = 130
    for item_id in fail:
        msg = spec[item_id]["value"].replace("%s", "preview").strip("\n")
        lines = wrap(d, msg, font(18), PAGE_W - 2 * MARGIN - 360)
        d.text((MARGIN, y), item_id, fill=ID_COLOR, font=font(16, bold=True))
        for i, line in enumerate(lines):
            d.text((MARGIN + 320, y + i * 26), line, fill=INK, font=font(18))
        y += max(1, len(lines)) * 26 + 14
        d.line((MARGIN, y - 6, PAGE_W - MARGIN, y - 6), fill=TABLE_LINE)
    pages.append(page)
    return pages


def cover_page(folder, version):
    page, d = new_page("TP_GUIdocker 見本", f"プラグイン version {version} の実際の画面と、文言の見本です。")
    lines = [
        "この見本の使い方",
        "",
        "・各ページの赤い枠と番号は、ページ下の表の「ID」に対応します。",
        "・直したい箇所があれば、指示書 (TP_GUIdocker_指示書.xlsx) の同じ ID の「修正指示」欄に書き込んでください。",
        "・この見本の番号やページで「3ページの2番の色を明るく」のように伝えていただいても構いません。",
        "・見本は、プラグインの「見本を書き出す」と tools/make_samples.py でいつでも作り直せます。",
        "",
        f"撮影画像の書き出し元: {folder}",
    ]
    y = 150
    for i, line in enumerate(lines):
        d.text((MARGIN, y), line, fill=INK, font=font(26 if i == 0 else 22, bold=(i == 0)))
        y += 44
    return page


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_INPUT
    data = json.loads((folder / "samples.json").read_text(encoding="utf-8"))
    spec = load_spec()
    pages = [cover_page(folder, data.get("version", ""))]
    for scene in data["scenes"]:
        pages.extend(scene_pages(scene, folder, spec))
    pages.append(menus_page(spec))
    pages.append(workspace_page(spec))
    pages.extend(messages_pages(spec))
    # ページ番号
    for i, p in enumerate(pages, start=1):
        ImageDraw.Draw(p).text((PAGE_W - MARGIN - 120, PAGE_H - 40), f"{i} / {len(pages)}", fill=SUB, font=font(18))
    out = OUTPUT
    try:
        pages[0].save(out, save_all=True, append_images=pages[1:], resolution=150)
    except PermissionError:
        # PDF がビューアで開かれていて上書きできない場合は、版を付けた別名で保存する
        out = OUTPUT.with_name(f"{OUTPUT.stem}_{data.get('version', 'new')}{OUTPUT.suffix}")
        pages[0].save(out, save_all=True, append_images=pages[1:], resolution=150)
        print(f"注意: {OUTPUT.name} が開かれているため、別名で保存した")
    print(f"生成: {out} ({len(pages)} ページ)")


if __name__ == "__main__":
    main()
