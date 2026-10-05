# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 作業規約

- ユーザーへの返答・報告・質問は、例外なく日本語で書く。長い作業の後の最終報告も同様。送信前に日本語であることを確かめる。
- 説明は英字略語を並べず、使い手から見て何が起きるかを日本語の文章で具体的に述べる。
- ユーザーは AviUtl2 の操作を自分で行うため、確認が必要な場面では番号付きの操作手順を示す（ビルドした `src\out\TP_GUIdocker.aux2` を `C:\ProgramData\aviutl2\Plugin\TPplug` へ上書きコピー → 起動 → 確認項目）。
- 推測で修正せず、記録（`C:\ProgramData\aviutl2\Plugin\TPplug\TP_GUIdocker.log`）や診断で事実を確かめてから直す。本体の挙動は実験で確かめた範囲だけを前提にする（`docs/design.md` 第2章）。
- 画面の文言では「自由分割」を使わず「TP_GUIdockerを有効化／無効化」「有効中」と書く（TP_GUIdocker の後に空白を入れない）。ドラッグ中の予告表示は開発上「配置ガイド」と呼ぶが画面には出さない。
- 切り替え類は2つの項目に分けず、1項目で今の状態を示して切り替える形にする。

## 概要

AviUtl ExEdit2（AviUtl2）向けの汎用プラグイン（`.aux2`、C++、Win32）。本体の固定5区画の画面を、After Effects 風の自由な分割・タブ・ドッキング・ワークスペースに置き換える。設計の全体は `docs/design.md`、経緯と決定事項は `docs/plan.md`。

## コマンド

```bat
git submodule update --init          :: AviUtl2 SDK (third_party/aviutl2_sdk)
src\build.bat                        :: プラグイン → src\out\TP_GUIdocker.aux2 (MSVC x64, /W4 で警告ゼロを保つ)
tests\build_test.bat                 :: 単体テスト → tests\out\test_core.exe
tests\out\test_core.exe C:\ProgramData\aviutl2\Default\basic.layout C:\ProgramData\aviutl2\aviutl2.ini
py -3 tools\gen_ui_spec.py           :: docs\ui_spec.json → src\ui_spec.h と docs\TP_GUIdocker_指示書.xlsx
py -3 tools\make_samples.py          :: プラグインの「見本を書き出す」の撮影結果 → docs\TP_GUIdocker_見本.pdf
```

- ビルドには Visual Studio 2026 Build Tools（`vcvars64.bat` のパスは build.bat に直書き）が必要。Git Bash から .bat を呼ぶと失敗するため PowerShell から `cmd /c` で実行する。
- 単体テストは1本の実行ファイル。引数（本番の basic.layout と aviutl2.ini）を省くと、ファイルを使う試験は飛ばされる。
- 実機確認は AviUtl2 上でのみ可能（自動試験は無い）。

## 構成の要点

### 本体パネルの取り込みの仕組み（`src/dock.cpp`）
本体の標準パネルは、結合中は独立した窓を持たず、本体が1枚の画面に描いている。切り離すと独立窓（クラス `aviutl2Manager`、オーナー＝メイン窓）になるため、次の流れで取り込む。
1. 本体の「表示→レイアウト→レイアウトを作成」をプラグインが自動操作し、今の配置を `TP_GUIdocker_元の配置` として本体自身に保存させる（`host_ops.cpp`）。
2. それを土台に `TP_GUIdocker.layout` の中身を書き換え（`dock_layout.cpp`）、本体にそのレイアウトを適用させて全パネルを切り離す。本体はレイアウトファイルを適用の瞬間に読む。
3. 各パネルに一意の仮位置を与えてあるので、切り離された窓の位置からセクション名（`preview` 等）を照合する。
4. 分割木に属するパネルを子窓化して入れ物（メイン窓の子）へはめ込む。それ以外は浮いた窓のまま置く。
5. 無効化・終了時は全パネルを返却し、`TP_GUIdocker_元の配置` を適用して標準画面へ戻す。

ワークスペースの切り替えも同じ経路（`build_and_capture`）で全パネルを切り離し直して行う。

### 守るべき本体の仕様（実験で確定、`docs/design.md` 第2章）
- メディアエクスプローラー（`Window.explorer:*`）は切り離し不可。レイアウトで切り離しにすると本体が読み込みに失敗し、他のパネルの適用まで崩れる。生成レイアウトでは触れず非表示にする。
- 本体の `aviutl2.ini` には書き込まない。プラグインの設定は DLL と同じ場所の `TP_GUIdocker.ini`（UTF-16、`settings.cpp`）。
- メニュー項目の番号はプラグイン数で変わるため、必ず文字列で探す（`host.cpp` の `find_menu_command`）。
- `register_edit_menu` は階層を1段しか作れない。「TP_GUIdocker」内の小分けは本体がメニューを作った後に `organize_menu` が並べ替える。
- 本体の右クリックメニュー（`TrackPopupMenu`）と確認画面（`MessageBoxW`）は exe の取り込みアドレス表を差し替えて横取りしている（`hooks.cpp`）。「ウィンドウを統合」の阻止と、自動操作中の確認・エラー画面への無音の応答に使う。

### 分割木（`src/tree.cpp`）
節＝左右（H）・上下（V）の2分割と比率、葉＝パネル群（タブ）。組み替え（重ねる・分割して置く・空き区画・閉じる）は節をその場で書き換える。純粋な処理なので単体テストの対象。

### 画面の文言・見た目・動作の値
すべて `docs/ui_spec.json` を元データとし、`tools/gen_ui_spec.py` で `src/ui_spec.h`（`tpgd::ui::` の定数）と指示書（Excel）を生成する。`ui_spec.h` は直接編集しない。ユーザーは指示書の「修正指示」欄で指示し、反映したものは `ui_spec.json` の `history` に記録すると指示書の欄から消える（「反映の記録」シートに残る）。指示書が Excel で開かれていると更新は見送られる。

### 帯の書体
帯とタブの文字は指示書の `FONT`（現在 IBM Plex Sans JP、大きさはメニューバーの文字と同じ）。プラグインは起動時に DLL と同じ場所の `TP_GUIdocker_フォント` フォルダの書体をプロセス内だけで読み込む（`AddFontResourceExW` の `FR_PRIVATE`、Windows へはインストールしない）。配布用の書体と許諾文書は `dist/TP_GUIdocker_フォント`（SIL Open Font License 1.1。許諾文書を必ず同梱し、書体を改変・改名しない）。書体が見つからない時はメニューバーと同じ書体で描く。

### 安全装置
- 有効化の各段階で失敗したら `abort_enter` で全返却して標準画面へ戻す。本体のバージョンが変わったら利用者に確認する。
- 自由分割中の印（`session_active`）が残っていれば異常終了とみなし、まず元の配置へ戻してから再開する。
- 起動時は本体の窓が出た直後に「有効化しています…」の覆いを掛け、通常の画面を見せない。
