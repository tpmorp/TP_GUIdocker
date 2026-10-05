//----------------------------------------------------------------------------------
//	本体のレイアウトファイル / 設定ファイルの読み書き
//----------------------------------------------------------------------------------
#include "layout_file.h"
#include <windows.h>

namespace tpgd {

static bool split_key(const std::string& line, std::string& key, std::string& value) {
	if (line.empty() || line[0] == ';') return false;
	auto eq = line.find('=');
	if (eq == std::string::npos) return false;
	key = line.substr(0, eq);
	value = line.substr(eq + 1);
	return true;
}

bool IniSection::has(const std::string& key) const {
	std::string k, v;
	for (auto& l : lines) if (split_key(l, k, v) && k == key) return true;
	return false;
}

std::string IniSection::get(const std::string& key, const std::string& def) const {
	std::string k, v;
	for (auto& l : lines) if (split_key(l, k, v) && k == key) return v;
	return def;
}

void IniSection::set(const std::string& key, const std::string& value) {
	std::string k, v;
	for (auto& l : lines) {
		if (split_key(l, k, v) && k == key) {
			l = key + "=" + value;
			return;
		}
	}
	lines.push_back(key + "=" + value);
}

bool IniDoc::load(const std::wstring& path) {
	preamble.clear();
	sections.clear();
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER size = {};
	GetFileSizeEx(f, &size);
	std::string text((size_t)size.QuadPart, '\0');
	DWORD read = 0;
	BOOL ok = ReadFile(f, text.data(), (DWORD)text.size(), &read, nullptr);
	CloseHandle(f);
	if (!ok) return false;
	text.resize(read);
	// UTF-8 BOM があれば除く
	if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
		text.erase(0, 3);
	}

	// 行に分割する
	std::vector<std::string> lines;
	size_t pos = 0;
	while (pos < text.size()) {
		size_t end = text.find('\n', pos);
		if (end == std::string::npos) end = text.size();
		std::string line = text.substr(pos, end - pos);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		lines.push_back(line);
		pos = end + 1;
	}

	for (auto& line : lines) {
		if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
			sections.push_back({ line.substr(1, line.size() - 2), {} });
		} else if (sections.empty()) {
			preamble.push_back(line);
		} else {
			sections.back().lines.push_back(line);
		}
	}
	// 末尾の空行は保持しない
	while (!preamble.empty() && preamble.back().empty()) preamble.pop_back();
	for (auto& s : sections) {
		while (!s.lines.empty() && s.lines.back().empty()) s.lines.pop_back();
	}
	return true;
}

bool IniDoc::save(const std::wstring& path) const {
	std::string out;
	for (auto& l : preamble) { out += l; out += "\r\n"; }
	for (auto& s : sections) {
		out += "[" + s.name + "]\r\n";
		for (auto& l : s.lines) { out += l; out += "\r\n"; }
	}
	// 書き込み途中の破損を避けるため、一時ファイルへ書いてから置き換える
	std::wstring tmp = path + L".tmp";
	HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	BOOL ok = WriteFile(f, out.data(), (DWORD)out.size(), &written, nullptr);
	CloseHandle(f);
	if (!ok || written != out.size()) {
		DeleteFileW(tmp.c_str());
		return false;
	}
	return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

IniSection* IniDoc::find(const std::string& name) {
	for (auto& s : sections) if (s.name == name) return &s;
	return nullptr;
}

const IniSection* IniDoc::find(const std::string& name) const {
	for (auto& s : sections) if (s.name == name) return &s;
	return nullptr;
}

} // namespace tpgd
