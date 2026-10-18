# TP_GUIdocker

AviUtl ExEdit2（AviUtl2）の画面を、After Effects のように自由に分割・タブ化・並べ替えできるようにする汎用プラグイン（`.aux2`）です。

AviUtl2 標準の画面は決まった区画にパネルを置く作りですが、TP_GUIdocker を有効にすると、次のことができるようになります。

- 縦横に何段でも区画を分割し、境目のドラッグで大きさを変える
- パネルのタブを長押ししてドラッグし、好きな区画へ入れる・分割して置く・浮かせる（落とし先を大きく表示）
- 配置を「ワークスペース」として保存し、画面上部の帯やショートカットキーで一瞬で切り替える
- 配置のロック、パネルの最大化
- 閉じたパネルを開き直すと、前にいた区画へ自動で戻る
- 無効化すれば、いつでも普段の AviUtl2 の画面に戻る（プラグインを外しても普段の画面で起動できる状態を保つ）

> テスト版です。AviUtl2 の公式のものではありません。使用は自己責任でお願いします。

## 動作環境
- AviUtl ExEdit2 version 2.1.14 で確認（2.1.x の範囲の更新では確認の画面は出ません）
- Windows 11

## 導入
1. [Releases](../../releases) から `TP_GUIdocker_<版>.au2pkg.zip` をダウンロードする
2. AviUtl2 を終了し、zip を展開する
3. 中の `Plugin\TP_GUIdocker` フォルダを、AviUtl2 のデータフォルダの `Plugin` フォルダへコピーする（通常は `C:\ProgramData\aviutl2\Plugin`）
4. AviUtl2 を起動し、「編集」→「プラグイン」→「TP_GUIdocker」→「TP_GUIdockerを有効化」を選ぶ

zip を AviUtl2 のプレビュー画面へドラッグ＆ドロップして導入することもできますが、入れている他のプラグインによっては、その最中に AviUtl2 が落ちることがあります（導入時に AviUtl2 が全プラグインを読み込み直すため）。その場合は上の方法で導入してください。

## 元の画面に戻すには
- 「編集」→「プラグイン」→「TP_GUIdocker」→「TP_GUIdockerを無効化」
- それでも戻らない場合は、AviUtl2 を終了して `Plugin\TP_GUIdocker` フォルダを削除してください（AviUtl2 本体の設定ファイルは書き換えていません）

## 知られている制限
- メディアエクスプローラーは、有効化中は使えません（AviUtl2 の仕様で本体から切り離せないため）
- 有効化中は「表示」→「レイアウト」でのレイアウトの切り替え・作成はできません
- プレビューへのファイルのドラッグ＆ドロップはできません（レイヤー編集へはできます）

詳しい使い方は、配布物の `README.txt` を参照してください。

## 不具合の報告
[Issues](../../issues) へお願いします。`Plugin\TP_GUIdocker\TP_GUIdocker.log`（動作の記録）を添えていただけると原因を調べやすくなります。記録にはパソコン上のフォルダの場所（ユーザー名を含む場合があります）が書かれているので、気になる場合はその部分を消してください。

## ビルド
Visual Studio 2026 Build Tools（MSVC, x64）が必要です。

```bat
git submodule update --init
src\build.bat               :: 開発版 → src\out\TP_GUIdocker.aux2
src\build.bat release       :: 配布版 → src\out\release\TP_GUIdocker.aux2
tests\build_test.bat        :: 単体テスト → tests\out\test_core.exe
py -3 tools\make_package.py :: 配布用 zip → dist\out\TP_GUIdocker_<版>.au2pkg.zip
```

構成の要点と仕組みは [docs/design.md](docs/design.md) と [CLAUDE.md](CLAUDE.md) にまとめています。

- `src/` — プラグイン本体
- `tests/` — 分割木・レイアウトファイル・メニュー探索の単体テスト
- `tools/` — 画面の文言・見た目の定義から指示書を作る道具、見本・資料の PDF を作る道具、配布用 zip を作る道具
- `docs/` — 設計書、文言・見た目の定義（`ui_spec.json`）、資料
- `dist/` — 配布物に同梱する説明書きと書体
- `third_party/aviutl2_sdk/` — AviUtl2 SDK（サブモジュール）

## ライセンス
[MIT License](LICENSE)

同梱の書体 IBM Plex Sans JP（Copyright © 2017 IBM Corp.）は SIL Open Font License 1.1 です（`dist/TP_GUIdocker_fonts/IBMPlexSansJP_OFL.txt`）。
