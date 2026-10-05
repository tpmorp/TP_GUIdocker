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

	// 配置計算の結果
	RECT rect = {};      // この節・葉の全体
	RECT splitter = {};  // 分割: 境界線の範囲
	RECT tabs = {};      // 葉: タブ帯
	RECT body = {};      // 葉: パネル本体
};

// 文字列との相互変換 (設定ファイルへの保存用)
//   葉:   L(active;key1|key2|...)
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

} // namespace tpgd
