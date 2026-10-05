//----------------------------------------------------------------------------------
//	分割木 (画面の分割構造)
//	節 = 横並び (H) または縦並び (V) の2分割と比率、葉 = パネル群 (タブ)
//----------------------------------------------------------------------------------
#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace tpgd {

struct Node {
	bool is_split = false;

	// 分割 (is_split == true)
	bool vertical = false;          // true: 上下に並べる / false: 左右に並べる
	double ratio = 0.5;             // 先頭側 (左または上) の割合
	std::unique_ptr<Node> a, b;     // 先頭側, 後方側

	// 葉 (is_split == false)
	std::vector<std::wstring> panels;  // この葉に属するパネル (レイアウトのセクション名)
	std::wstring active;               // 表示中のタブ
	bool keep_empty = false;           // 空でも自動で閉じない (「この区画を分割」で作った空き区画)

	// 配置計算の結果
	RECT rect = {};      // この節・葉の全体
	RECT splitter = {};  // 分割: 境界線の範囲
	RECT tabs = {};      // 葉: タブ帯
	RECT body = {};      // 葉: パネル本体
};

// 文字列との相互変換 (設定ファイルへの保存用)
//   葉:   L(active;key1|key2|...)   空でも閉じない葉は E(...)
//   分割: H(ratio,先頭,後方) / V(ratio,先頭,後方)
//   セクション名中の記号は %XX で符号化する
std::wstring serialize_tree(const Node* n);
std::unique_ptr<Node> parse_tree(const std::wstring& s);

// After Effects 風の初期配置
std::unique_ptr<Node> default_tree();

// 全ての葉を巡回する
void for_each_leaf(Node* n, const std::function<void(Node*)>& f);

// key を含む葉
Node* find_leaf_with(Node* root, const std::wstring& key);

// 新規パネルの既定の置き場所 (オブジェクト設定の葉、無ければ最大の葉)
Node* default_leaf(Node* root);

// 配置を計算する (gap: 境界線の幅, tab_h: タブ帯の高さ)
void compute_layout(Node* n, const RECT& rc, int gap, int tab_h);

//---------------------------------------------------------------------
//	組み替え (ドッキング)
//	※ 葉を分割に変える・空いた葉を閉じる時は、節を「その場で」書き換える。
//	   そのため、操作対象以外の Node* は操作後も有効なまま残る
//---------------------------------------------------------------------

// 落とし先
enum class DropZone { None, Center, Left, Right, Top, Bottom };

// 座標 pt を含む葉 (配置計算済みであること)
Node* leaf_at(Node* root, POINT pt);

// key を target の葉へ入れる
//   Center: タブとして加える (tab_index の位置。-1 なら末尾) / 上下左右: 葉を分割してその側に置く
void insert_key(Node* target, const std::wstring& key, DropZone zone, int tab_index = -1);

// key を今の葉から target へ移す。空いた葉は閉じる (keep_empty の葉を除く)
// 同じ葉の中央へ移す場合はタブの並べ替えになる
void move_key(Node* root, const std::wstring& key, Node* target, DropZone zone, int tab_index = -1);

// key を分割木から除く。空いた葉は閉じる (keep_empty の葉を除く)。見つかれば true
bool remove_key(Node* root, const std::wstring& key);

// 葉を分割し、side 側に空き区画 (keep_empty) を作る。作った葉を返す
Node* split_leaf(Node* leaf, DropZone side);

// 空き区画を閉じる (keep_empty を外して閉じる)
void close_leaf(Node* root, Node* leaf);

// 空いた葉 (keep_empty でないもの) を閉じ、隣を広げる。根は閉じない
void collapse_empty(Node* root);

// 分割の予告範囲: target の葉を zone で分けた時に、新しいパネルが置かれる範囲
RECT drop_preview_rect(const Node* target, DropZone zone, int gap);

} // namespace tpgd
