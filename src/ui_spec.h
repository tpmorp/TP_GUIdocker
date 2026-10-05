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
constexpr const wchar_t* MENU_ENABLE = L"TP_GUIdockerを有効化";
// 同上 (有効中) [実装済み]
constexpr const wchar_t* MENU_DISABLE = L"TP_GUIdockerを無効化";
// 「TP_GUIdocker」メニューの項目 [実装済み]
constexpr const wchar_t* MENU_RESET = L"配置を初期状態に戻す";
// 「TP_GUIdocker」メニューの項目 [実装済み]
constexpr const wchar_t* MENU_CHOOSE_RETURN = L"無効時のレイアウトを選ぶ";
// 「TP_GUIdocker」メニューの切り替え項目 (十字ボタン式の時。選ぶと区画の領域式に切り替わる) [実装済み]
constexpr const wchar_t* MENU_DROP_STYLE_COMPASS = L"ドラッグ時の表示:十字ボタン式";
// 同上 (区画の領域式の時。選ぶと十字ボタン式に切り替わる) [実装済み]
constexpr const wchar_t* MENU_DROP_STYLE_REGION = L"ドラッグ時の表示:区画の領域式";
// 「戻り先レイアウトを選ぶ」の一覧の先頭 [実装済み]
constexpr const wchar_t* RETURN_AUTO = L"自動 (有効化する直前の配置)";
// 起動時、準備が整うまで本体の画面を覆う表示 [実装済み]
constexpr const wchar_t* COVER = L"TP_GUIdockerを有効化しています…";
// パネルが1枚も無い区画の中央 [実装済み]
constexpr const wchar_t* EMPTY_AREA = L"";
// 初めて有効化する時の確認 (%lu は本体バージョン) [実装済み]
constexpr const wchar_t* CONFIRM_FIRST = L"TP_GUIdockerを有効化します。\n\n本体の画面に手を加える機能のため、不具合が起きた場合は「編集」→「プラグイン」→「TP_GUIdocker」→「TP_GUIdockerを無効化」で元に戻せます。\n\n有効化しますか？ (本体バージョン %lu)";
// 本体の更新を検知した時の確認 (%lu は旧・新バージョン) [実装済み]
constexpr const wchar_t* CONFIRM_HOST_UPDATED = L"AviUtl2本体が更新されています (%lu → %lu)。\n\nTP_GUIdockerはこのバージョンで未確認です。不具合が起きる可能性があります。\n有効化しますか？";
// 有効化に失敗した時の知らせ (先頭) [実装済み]
constexpr const wchar_t* FAIL_TITLE = L"TP_GUIdockerを有効化できませんでした。\n\n理由: ";
// 同上 (標準の画面に戻せた場合の末尾) [実装済み]
constexpr const wchar_t* FAIL_RESTORED = L"\n\n無効化しました。";
// 同上 (戻せなかった場合の末尾) [実装済み]
constexpr const wchar_t* FAIL_NOT_RESTORED = L"\n\n「表示」→「レイアウト」から普段の配置を選んで戻してください。";
// 失敗の理由 [実装済み]
constexpr const wchar_t* REASON_SAVE_CURRENT = L"現在の配置を本体に保存させることができませんでした。";
// 失敗の理由 [実装済み]
constexpr const wchar_t* REASON_REGISTER = L"TP_GUIdocker用のレイアウトを本体に登録できませんでした。";
// 失敗の理由 [実装済み]
constexpr const wchar_t* REASON_READ_SAVED = L"保存した配置を読み込めませんでした。";
// 失敗の理由 [実装済み]
constexpr const wchar_t* REASON_WRITE = L"TP_GUIdocker用のレイアウトを書き出せませんでした。";
// 失敗の理由 [実装済み]
constexpr const wchar_t* REASON_LOAD_FAILED = L"本体がレイアウトの読み込みに失敗しました。";
// 失敗の理由 [実装済み]
constexpr const wchar_t* REASON_NOT_IN_MENU = L"TP_GUIdocker用のレイアウトがメニューに見つかりません。";
// 失敗の理由 (%s は切り離されなかったパネル) [実装済み]
constexpr const wchar_t* REASON_MISSING = L"一部のパネルが本体に切り離されませんでした (%s)。";
// 失敗の理由 (%s はパネル) [実装済み]
constexpr const wchar_t* REASON_CAPTURE = L"パネル(%s)を取り込めませんでした。";
// 有効中にメディアエクスプローラーを開こうとした時の案内 [実装済み]
constexpr const wchar_t* EXPLORER_NOTICE = L"メディアエクスプローラーは、TP_GUIdockerの有効中は使えません。\n「編集」→「プラグイン」→「TP_GUIdocker」→「TP_GUIdockerを無効化」で標準の画面に戻すと使えます。";
// タブの右クリックメニュー [実装済み]
constexpr const wchar_t* TAB_FLOAT = L"分離する";
// タブの右クリックメニュー [実装済み]
constexpr const wchar_t* TAB_CLOSE = L"閉じる";
// タブの右クリックメニュー (下に区画の一覧が出る) [実装済み]
constexpr const wchar_t* TAB_MOVE_TO = L"他の区画へ移動";
// タブの右クリックメニュー (下に分割の向きが出る) [実装済み]
constexpr const wchar_t* TAB_SPLIT = L"この区画を分割";
// 「この区画を分割」の下の項目 [実装済み]
constexpr const wchar_t* SPLIT_LEFT = L"左に空き区画を作る";
// 「この区画を分割」の下の項目 [実装済み]
constexpr const wchar_t* SPLIT_RIGHT = L"右に空き区画を作る";
// 「この区画を分割」の下の項目 [実装済み]
constexpr const wchar_t* SPLIT_TOP = L"上に空き区画を作る";
// 「この区画を分割」の下の項目 [実装済み]
constexpr const wchar_t* SPLIT_BOTTOM = L"下に空き区画を作る";
// 「他の区画へ移動」の一覧での区画の呼び名 (%s は区画の表示中パネル名) [実装済み]
constexpr const wchar_t* AREA_NAME_FORMAT = L"%sの区画";
// 同上 (空き区画の呼び名。%d は番号) [実装済み]
constexpr const wchar_t* AREA_NAME_EMPTY = L"空き区画%d";
// 空き区画の右クリックメニュー [実装済み]
constexpr const wchar_t* AREA_CLOSE = L"この区画を閉じる";
// 照合できなかった浮いた窓を区画へ入れようとした時の知らせ [実装済み]
constexpr const wchar_t* DOCK_UNKNOWN_PANEL = L"このパネルは識別できないため、区画に入れられません。\n一度閉じて「表示」メニューから開き直してください。";
// 最初のワークスペースの名前 [実装済み]
constexpr const wchar_t* WS_DEFAULT_NAME = L"標準";
// 「＋」で作るワークスペースの名前の初期値 (%d は番号) [実装済み]
constexpr const wchar_t* WS_NEW_NAME = L"ワークスペース%d";
// ワークスペースの帯の追加ボタン [実装済み]
constexpr const wchar_t* WS_ADD = L"＋";
// ワークスペースのボタンの右クリックメニュー [実装済み]
constexpr const wchar_t* WS_MENU_RENAME = L"名前を変更";
// 同上 [実装済み]
constexpr const wchar_t* WS_MENU_SAVE = L"今の配置で上書き保存";
// 同上 [実装済み]
constexpr const wchar_t* WS_MENU_RESET = L"保存時の配置に戻す";
// 同上 [実装済み]
constexpr const wchar_t* WS_MENU_DELETE = L"削除";
// ワークスペースを削除する時の確認 (%s は名前) [実装済み]
constexpr const wchar_t* WS_DELETE_CONFIRM = L"ワークスペース「%s」を削除しますか？";
// 最後の1つを削除しようとした時の知らせ [実装済み]
constexpr const wchar_t* WS_DELETE_LAST = L"最後のワークスペースは削除できません。";
// 名前を入力する画面の題名 [実装済み]
constexpr const wchar_t* WS_NAME_TITLE = L"ワークスペースの名前";
// 名前を入力する画面の説明 [実装済み]
constexpr const wchar_t* WS_NAME_LABEL = L"名前:";
// 名前を入力する画面のボタン [実装済み]
constexpr const wchar_t* BUTTON_OK = L"OK";
// 名前を入力する画面のボタン [実装済み]
constexpr const wchar_t* BUTTON_CANCEL = L"キャンセル";
// 「TP_GUIdocker」→「ワークスペース」の項目 (ショートカットキー割り当て用。%d は番号) [実装済み]
constexpr const wchar_t* MENU_WS_SWITCH = L"ワークスペース%dに切り替え";
// 「TP_GUIdocker」メニュー内の小分けの見出し (ワークスペース1〜9に切り替え) [実装済み]
constexpr const wchar_t* MENU_WS_GROUP = L"ワークスペース";
// 「TP_GUIdocker」メニューの項目 (ショートカットキー割り当て用。カーソルの下のパネルを最大化・元に戻す) [実装済み]
constexpr const wchar_t* MENU_MAXIMIZE = L"パネルを最大化・元に戻す";
// タブの右クリックメニュー (通常時) [実装済み]
constexpr const wchar_t* TAB_MAXIMIZE = L"最大化";
// タブの右クリックメニュー (最大化中) [実装済み]
constexpr const wchar_t* TAB_RESTORE = L"元の大きさに戻す";
// 「TP_GUIdocker」メニューの切り替え項目 (ロックしていない時) [実装済み]
constexpr const wchar_t* MENU_LOCK = L"レイアウトをロック";
// 同上 (ロック中) [実装済み]
constexpr const wchar_t* MENU_UNLOCK = L"レイアウトのロックを解除";
// ロック中、帯の右端の閉じた錠前の絵にカーソルを乗せると出る説明 (クリックで解除) [実装済み]
constexpr const wchar_t* LOCK_BADGE = L"ロック中 (クリックで解除)";
// ロックしていない時、帯の右端の開いた錠前の絵にカーソルを乗せると出る説明 (クリックでロック) [実装済み]
constexpr const wchar_t* UNLOCK_BADGE = L"クリックでレイアウトをロック";
// 「TP_GUIdocker」メニューの項目 (見本の画像を書き出す。指示用) [実装済み]
constexpr const wchar_t* MENU_EXPORT_SAMPLES = L"見本を書き出す";
// 見本を書き出した時の知らせ (%s は書き出し先) [実装済み]
constexpr const wchar_t* EXPORT_DONE = L"見本を書き出しました。\n\n書き出し先: %s";
// 無効中に見本を書き出そうとした時の知らせ [実装済み]
constexpr const wchar_t* EXPORT_NEED_ACTIVE = L"見本は TP_GUIdockerの有効中に書き出せます。";
// ワークスペースのボタンの右クリックメニュー・「TP_GUIdocker」メニュー [実装済み]
constexpr const wchar_t* WS_MENU_COLOR = L"テーマの色を変える…";
// 同上 [実装済み]
constexpr const wchar_t* WS_MENU_COLOR_RESET = L"テーマの色を本体設定に戻す";
// 「TP_GUIdocker」メニュー内の小分けの見出し (ドラッグ時の表示・テーマの色) [実装済み]
constexpr const wchar_t* MENU_GROUP_VIEW = L"表示の設定";
// 「TP_GUIdocker」メニュー内の小分けの見出し (配置を初期状態に戻す・無効時のレイアウトを選ぶ) [実装済み]
constexpr const wchar_t* MENU_GROUP_LAYOUT = L"配置の設定";
// 「TP_GUIdocker」メニュー内の小分けの見出し (見本を書き出す) [実装済み]
constexpr const wchar_t* MENU_GROUP_TOOLS = L"指示用";

//---------------------------------------------------------------------
//	見た目 (数値で指定するもの。本体設定に連動する項目は host.cpp で読む)
//---------------------------------------------------------------------

// テーマの色 (プラグイン全体の強調色): 本体設定 (ButtonBodySelect)。メニューで自由に変更可 (本体設定に連動)
// タブ帯の高さ: メニューバーと同じ高さ (-1) [実装済み]
constexpr int TAB_HEIGHT = -1;
// 区画の間 (境界線) の太さ (% (本体設定 WindowSeparatorSize に対する割合)) [実装済み]
constexpr int SEPARATOR = 50;
// タブの文字の左右の余白 (px (96dpi換算)) [実装済み]
constexpr int TAB_PADDING = 10;
// 区画の最小の大きさ (px (96dpi換算)) [実装済み]
constexpr int MIN_PANE = 60;
// 浮いた窓の既定の幅 (px (96dpi換算)) [実装済み]
constexpr int FLOAT_W = 640;
// 浮いた窓の既定の高さ (px (96dpi換算)) [実装済み]
constexpr int FLOAT_H = 420;
// タブ・ワークスペースの帯の文字: メニューバーと同じ高さ (-1) [実装済み]
constexpr int FONT = -1;
// タブ帯の背景色: 本体設定 (Grouping) (本体設定に連動)
// 選択中のタブの背景色: 本体設定 (GroupingSelect) (本体設定に連動)
// 選択中のタブの文字色: 本体設定 (Text) (本体設定に連動)
// 選択されていないタブの文字色: 本体設定 (TextDisable) (本体設定に連動)
// 区画の間の色: 本体設定 (WindowSeparator) (本体設定に連動)
// 配置ガイドの予告表示の色 (#5A8CFF) [実装済み]
constexpr COLORREF DROP_HIGHLIGHT = 0xFF8C5A;
// 配置ガイドの予告表示の濃さ (% (0=透明)) [実装済み]
constexpr int DROP_ALPHA = 35;
// ドーナツ型のボタンの直径 (px (96dpi換算)) [実装済み]
constexpr int COMPASS_BUTTON = 150;
// 区画の領域式で、上下左右と判定する幅 (% (区画の幅・高さに対する割合)) [実装済み]
constexpr int REGION_EDGE = 30;
// ドラッグ中のタブの影の濃さ (% (0=透明)) [実装済み]
constexpr int DRAG_GHOST_ALPHA = 60;
// ワークスペースの帯の高さ: メニューバーと同じ高さ (-1) [実装済み]
constexpr int WS_BAR_HEIGHT = -1;
// ワークスペースのボタンの文字の左右の余白 (px (96dpi換算)) [実装済み]
constexpr int WS_BUTTON_PADDING = 12;
// 選択中のワークスペースのボタンの色: テーマの色 (本体設定に連動)
// ドーナツ型の中心の円の大きさ (% (直径に対する割合)) [実装済み]
constexpr int COMPASS_HOLE = 40;
// 選択中のタブの下線の太さ (色はテーマの色) (px (96dpi換算)) [実装済み]
constexpr int TAB_ACTIVE_LINE = 2;

//---------------------------------------------------------------------
//	動作
//---------------------------------------------------------------------

// タブのドラッグを始める長押しの時間 (ミリ秒) [実装済み]
constexpr int LONG_PRESS_MS = 300;
// 長押しの後、ドラッグとみなす移動量 (px (96dpi換算)) [実装済み]
constexpr int DRAG_START_PX = 4;
// 配置ガイドの方式: 十字ボタン式 [実装済み]
constexpr int DROP_STYLE = 0;
// 入れ物の外でタブを放した時: 浮いた窓にする [実装済み]
constexpr int DROP_OUTSIDE = 1;
// 区画が空になった時: 自動で閉じて隣が広がる [実装済み]
constexpr int EMPTY_AREA_CLOSE = 1;
// ドラッグを取り消す操作: Esc キー [実装済み]
// 有効中に「表示」から開いたパネル: 浮いた窓で画面中央に出す [実装済み]
// ショートカットキーで切り替えられるワークスペースの数 (個) [実装済み]
constexpr int WS_MAX = 9;
// ワークスペースを切り替えた時の配置: 使用中の配置を保つ [実装済み]
// ロック中に禁止する操作: タブのドラッグ・右クリックの組み替え [実装済み]
// ショートカットキーで最大化するパネル: カーソルの下のパネル [実装済み]

} // namespace tpgd::ui
