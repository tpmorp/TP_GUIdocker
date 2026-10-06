"""
TP_GUIdocker の配布用パッケージ (au2pkg.zip) を作る。

  入力: src/out/TP_GUIdocker.aux2 (src/build.bat でビルドしたもの)
        dist/README.txt (説明書き。{VERSION} は版に置き換える)
        dist/TP_GUIdocker_fonts/ (同梱の書体と許諾文書)
  出力: dist/out/TP_GUIdocker_<版>.au2pkg.zip

au2pkg.zip は AviUtl2 のデータフォルダを起点に展開される。プレビュー画面へドラッグ＆ドロップすると導入できる。
中身は Plugin/TP_GUIdocker/ にまとめる (プラグインは自分の置き場所を基準に書体・設定・記録を扱う)。

使い方: py -3 tools/make_package.py
"""
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AUX2 = ROOT / "src" / "out" / "TP_GUIdocker.aux2"
COMMON_H = ROOT / "src" / "common.h"
README = ROOT / "dist" / "README.txt"
FONTS = ROOT / "dist" / "TP_GUIdocker_fonts"
OUT_DIR = ROOT / "dist" / "out"
BASE = "Plugin/TP_GUIdocker"


def version():
    m = re.search(r'#define TPGD_VERSION L"([^"]+)"', COMMON_H.read_text(encoding="utf-8"))
    if not m:
        sys.exit("src/common.h に TPGD_VERSION が見つからない")
    return m.group(1)


def main():
    ver = version()
    if not AUX2.exists():
        sys.exit(f"{AUX2} が無い。先に src\\build.bat でビルドすること")
    # ビルドが古いまま包むのを防ぐ
    newest_src = max(p.stat().st_mtime for p in (ROOT / "src").glob("*.[ch]*") if p.is_file())
    if AUX2.stat().st_mtime < newest_src:
        sys.exit("TP_GUIdocker.aux2 がソースより古い。src\\build.bat でビルドし直すこと")
    fonts = sorted(p for p in FONTS.iterdir() if p.is_file())
    if not any(p.suffix.lower() in (".ttf", ".otf", ".ttc") for p in fonts):
        sys.exit(f"{FONTS} に書体が無い")
    if not any(p.suffix.lower() == ".txt" for p in fonts):
        sys.exit(f"{FONTS} に書体の許諾文書が無い (同梱が許諾の条件)")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out = OUT_DIR / f"TP_GUIdocker_{ver}.au2pkg.zip"
    readme = README.read_text(encoding="utf-8").replace("{VERSION}", ver)
    # 中のファイル名は英字だけにする (展開する側の文字コードの違いで化けないように)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(AUX2, f"{BASE}/TP_GUIdocker.aux2")
        # メモ帳で確実に読めるよう、UTF-8 (BOM 付き)・改行 CRLF にする
        z.writestr(f"{BASE}/README.txt", ("﻿" + readme.replace("\r\n", "\n").replace("\n", "\r\n")).encode("utf-8"))
        for p in fonts:
            z.write(p, f"{BASE}/TP_GUIdocker_fonts/{p.name}")
    with zipfile.ZipFile(out) as z:
        for i in z.infolist():
            print(f"  {i.filename}  ({i.file_size:,} バイト)")
    print(f"生成: {out}")


if __name__ == "__main__":
    main()
