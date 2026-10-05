//----------------------------------------------------------------------------------
//	TP_GUIdocker 画面文言・見た目・動作の設定
//	※ このファイルは tools/gen_ui_spec.py が docs/ui_spec.json から生成する。直接編集しないこと
//----------------------------------------------------------------------------------
#pragma once
#include <windows.h>

namespace tpgd::ui {

//---------------------------------------------------------------------
//	文言
//---------------------------------------------------------------------

// 「編集」→「プラグイン」→「TP_GUIdocker」の切り替え項目 (無効中) [実装済み]
constexpr const wchar_t* MENU_ENABLE = L"TP_GUIdocker を有効化";
// 同上 (有効中) [実装済み]
constexpr const wchar_t* MENU_DISABLE = L"TP_GUIdocker を無効化";
// 「TP_GUIdocker」メニューの項目 [実装済み]
constexpr const wchar_t* MENU_RESET = L"配置を初期状態に戻す";
// 「TP_GUIdocker」メニューの項目 [実装済み]
constexpr const wchar_t* MENU_CHOOSE_RETURN = L"戻り先レイアウトを選ぶ";
// 「TP_GUIdocker」メニューの項目 (落とし先の見せ方を切り替える) [段階2で実装]
constexpr const wchar_t* MENU_DROP_STYLE = L"落とし先の表示を切り替え";
// 「戻り先レイアウトを選ぶ」の一覧の先頭 [実装済み]
constexpr const wchar_t* RETURN_AUTO = L"自動 (有効化する直前の配置)";
// 起動時、準備が整うまで本体の画面を覆う表示 [実装済み]
constexpr const wchar_t* COVER = L"TP_GUIdocker を有効化しています…";
// パネルが1枚も無い区画の中央 [実装済み]
constexpr const wchar_t* EMPTY_AREA = L"(パネルなし)";
// 初めて有効化する時の確認 (%lu は本体バージョン) [実装済み]
constexpr const wchar_t* CONFIRM_FIRST = L"TP_GUIdocker を有効化します。\n\n本体の画面に手を加える機能のため、不具合が起きた場合は「編集」→「プラグイン」→「TP_GUIdocker」→「TP_GUIdocker を無効化」で元に戻せます。\n\n有効化しますか？ (本体バージョン %lu)";
// 本体の更新を検知した時の確認 (%lu は旧・新バージョン) [実装済み]
constexpr const wchar_t* CONFIRM_HOST_UPDATED = L"AviUtl2 本体が更新されています (%lu → %lu)。\n\nTP_GUIdocker はこのバージョンで未確認です。不具合が起きる可能性があります。\n有効化しますか？";
// 有効化に失敗した時の知らせ (先頭) [実装済み]
constexpr const wchar_t* FAIL_TITLE = L"TP_GUIdocker を有効化できませんでした。\n\n理由: ";
// 同上 (標準の画面に戻せた場合の末尾) [実装済み]
constexpr const wchar_t* FAIL_RESTORED = L"\n\n標準の画面に戻しました。";
// 同上 (戻せなかった場合の末尾) [実装済み]
constexpr const wchar_t* FAIL_NOT_RESTORED = L"\n\n「表示」→「レイアウト」から普段の配置を選んで戻してください。";
// 失敗の理由 [実装済み]
constexpr const wchar_t* REASON_SAVE_CURRENT = L"現在の配置を本体に保存させることができませんでした。";
// 失敗の理由 [実装済み]
constexpr const wchar_t* REASON_REGISTER = L"TP_GUIdocker 用のレイアウトを本体に登録できませんでした。";
// 失敗の理由 [実装済み]
constexpr const wchar_t* REASON_READ_SAVED = L"保存した配置を読み込めませんでした。";
// 失敗の理由 [実装済み]
constexpr const wchar_t* REASON_WRITE = L"TP_GUIdocker 用のレイアウトを書き出せませんでした。";
// 失敗の理由 [実装済み]
constexpr const wchar_t* REASON_LOAD_FAILED = L"本体がレイアウトの読み込みに失敗しました。";
// 失敗の理由 [実装済み]
constexpr const wchar_t* REASON_NOT_IN_MENU = L"TP_GUIdocker 用のレイアウトがメニューに見つかりません。";
// 失敗の理由 (%s は切り離されなかったパネル) [実装済み]
constexpr const wchar_t* REASON_MISSING = L"一部のパネルが本体に切り離されませんでした (%s)。";
// 失敗の理由 (%s はパネル) [実装済み]
constexpr const wchar_t* REASON_CAPTURE = L"パネル (%s) を取り込めませんでした。";
// 有効中にメディアエクスプローラーを開こうとした時の案内 [実装済み]
constexpr const wchar_t* EXPLORER_NOTICE = L"メディアエクスプローラーは、TP_GUIdocker の有効中は使えません。\n「編集」→「プラグイン」→「TP_GUIdocker」→「TP_GUIdocker を無効化」で標準の画面に戻すと使えます。";
// タブの右クリックメニュー [段階2で実装]
constexpr const wchar_t* TAB_FLOAT = L"浮かせる";
// タブの右クリックメニュー [段階2で実装]
constexpr const wchar_t* TAB_CLOSE = L"閉じる";
// タブの右クリックメニュー (下に区画の一覧が出る) [段階2で実装]
constexpr const wchar_t* TAB_MOVE_TO = L"他の区画へ移動";
// タブの右クリックメニュー (下に分割の向きが出る) [段階2で実装]
constexpr const wchar_t* TAB_SPLIT = L"この区画を分割";
// 「この区画を分割」の下の項目 [段階2で実装]
constexpr const wchar_t* SPLIT_LEFT = L"左に空き区画を作る";
// 「この区画を分割」の下の項目 [段階2で実装]
constexpr const wchar_t* SPLIT_RIGHT = L"右に空き区画を作る";
// 「この区画を分割」の下の項目 [段階2で実装]
constexpr const wchar_t* SPLIT_TOP = L"上に空き区画を作る";
// 「この区画を分割」の下の項目 [段階2で実装]
constexpr const wchar_t* SPLIT_BOTTOM = L"下に空き区画を作る";
// 「他の区画へ移動」の一覧での区画の呼び名 (%s は区画の表示中パネル名) [段階2で実装]
constexpr const wchar_t* AREA_NAME_FORMAT = L"%s の区画";
// 同上 (空き区画の呼び名。%d は番号) [段階2で実装]
constexpr const wchar_t* AREA_NAME_EMPTY = L"空き区画 %d";
// 落とし先の表示の切り替え時の知らせ [段階2で実装]
constexpr const wchar_t* DROP_STYLE_COMPASS = L"落とし先の表示: 十字ボタン式";
// 同上 [段階2で実装]
constexpr const wchar_t* DROP_STYLE_REGION = L"落とし先の表示: 区画の領域式";

//---------------------------------------------------------------------
//	見た目 (数値で指定するもの。本体設定に連動する項目は host.cpp で読む)
//---------------------------------------------------------------------

// タブ帯の高さ: 本体設定 (GroupTabHeight, 既定24) (本体設定に連動)
// 区画の間 (境界線) の太さ: 本体設定 (WindowSeparatorSize, 既定7) (本体設定に連動)
// タブの文字の左右の余白 (px (96dpi換算)) [実装済み]
constexpr int TAB_PADDING = 10;
// 区画の最小の大きさ (px (96dpi換算)) [実装済み]
constexpr int MIN_PANE = 60;
// 浮いた窓の既定の幅 (px (96dpi換算)) [実装済み]
constexpr int FLOAT_W = 640;
// 浮いた窓の既定の高さ (px (96dpi換算)) [実装済み]
constexpr int FLOAT_H = 420;
// タブの文字: 本体設定 (Control, 既定 Yu Gothic UI 13) (本体設定に連動)
// タブ帯の背景色: 本体設定 (Grouping) (本体設定に連動)
// 選択中のタブの背景色: 本体設定 (GroupingSelect) (本体設定に連動)
// 選択中のタブの文字色: 本体設定 (Text) (本体設定に連動)
// 選択されていないタブの文字色: 本体設定 (TextDisable) (本体設定に連動)
// 区画の間の色: 本体設定 (WindowSeparator) (本体設定に連動)
// 落とし先の予告表示の色 (#5A8CFF) [段階2で実装]
constexpr COLORREF DROP_HIGHLIGHT = 0xFF8C5A;
// 落とし先の予告表示の濃さ (% (0=透明)) [段階2で実装]
constexpr int DROP_ALPHA = 35;
// 十字ボタン式のボタン1つの大きさ (px (96dpi換算)) [段階2で実装]
constexpr int COMPASS_BUTTON = 48;
// 区画の領域式で、上下左右と判定する幅 (% (区画の幅・高さに対する割合)) [段階2で実装]
constexpr int REGION_EDGE = 30;
// ドラッグ中のタブの影の濃さ (% (0=透明)) [段階2で実装]
constexpr int DRAG_GHOST_ALPHA = 60;

//---------------------------------------------------------------------
//	動作
//---------------------------------------------------------------------

// タブのドラッグを始める長押しの時間 (ミリ秒) [段階2で実装]
constexpr int LONG_PRESS_MS = 300;
// 長押しの後、ドラッグとみなす移動量 (px (96dpi換算)) [段階2で実装]
constexpr int DRAG_START_PX = 4;
// 落とし先の見せ方: 十字ボタン式 [段階2で実装]
constexpr int DROP_STYLE = 0;
// 入れ物の外でタブを放した時: 浮いた窓にする [段階2で実装]
constexpr int DROP_OUTSIDE = 1;
// 区画が空になった時: 自動で閉じて隣が広がる [段階2で実装]
constexpr int EMPTY_AREA_CLOSE = 1;
// ドラッグを取り消す操作: Esc キー [段階2で実装]
// 有効中に「表示」から開いたパネル: 浮いた窓で画面中央に出す [実装済み]

} // namespace tpgd::ui
