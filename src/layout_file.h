//----------------------------------------------------------------------------------
//	本体のレイアウトファイル (.layout) / 設定ファイル (aviutl2.ini) の読み書き
//	形式: UTF-8 (BOM なし)、改行 CRLF、[セクション] と key=value
//----------------------------------------------------------------------------------
#pragma once
#include <string>
#include <vector>

namespace tpgd {

struct IniSection {
	std::string name;                 // [] の中身
	std::vector<std::string> lines;   // セクション内の行 (key=value やコメント) をそのまま保持

	bool has(const std::string& key) const;
	std::string get(const std::string& key, const std::string& def = "") const;
	void set(const std::string& key, const std::string& value);
};

struct IniDoc {
	std::vector<std::string> preamble;   // 最初のセクションより前の行
	std::vector<IniSection> sections;

	bool load(const std::wstring& path);
	bool save(const std::wstring& path) const;
	IniSection* find(const std::string& name);
	const IniSection* find(const std::string& name) const;
};

// 本体の設定ファイル (aviutl2.ini) からレイアウトファイルの内容を取り出す
// [Window.*] セクションのうち、レイアウトファイルが持つ項目 (位置・大きさ・表示状態) のみを
// 本体のレイアウトファイルと同じ順序で写す ※それ以外の項目が入ると本体が読み込みに失敗する
IniDoc extract_layout(const IniDoc& ini);

} // namespace tpgd
