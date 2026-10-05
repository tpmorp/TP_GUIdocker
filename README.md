# TP_GUIdocker

AviUtl ExEdit2 (AviUtl2) 向けの、After Effects 風の自由レイアウトプラグイン（開発中）。

## 目的
AviUtl2 標準の画面は固定5区画で構成され、区画の数・分け方を変えられない。本プラグインは次を実現する。

- 縦横に何段でも分割できる画面
- タブをつかんだドラッグによるドッキング（落とし先を大きく明示）
- ワークスペースの保存・切替（ショートカットキー／画面上部のボタン）
- レイアウトロック、パネル最大化、表示の細部調整
- 異常時に本体標準の画面へ自動で戻る安全装置

詳細は [docs/plan.md](docs/plan.md) を参照。

## 現状
段階0（技術検証）。本体パネルを「切り離し」た独立窓を、自前のコンテナ窓へはめ込めるかを検証中。

## 構成
- `experiments/dock_probe/` — 段階0の検証用プラグイン
- `tools/probe_windows.ps1` — AviUtl2 の窓構造を読み取り専用で一覧化する調査スクリプト
- `third_party/aviutl2_sdk/` — AviUtl2 SDK（非公式ミラー、サブモジュール）

## ビルド
Visual Studio 2026 Build Tools（MSVC, x64）が必要。

```bat
git submodule update --init
experiments\dock_probe\build.bat
```

生成された `experiments/dock_probe/out/dock_probe.aux2` を `C:\ProgramData\aviutl2\Plugin` に置く。
