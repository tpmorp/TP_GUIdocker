//----------------------------------------------------------------------------------
//	プラグイン専用の設定ファイル (TP_GUIdocker.ini) の読み書き
//	日本語を扱うため、UTF-16LE (BOM 付き) のファイルを Win32 のプロファイル関数で読み書きする
//----------------------------------------------------------------------------------
#include "settings.h"

namespace tpgd {

Settings g_settings;

static std::wstring settings_path() {
	return module_dir() + L"\\" TPGD_NAME L".ini";
}

// プロファイル関数が UTF-16 で書き込むよう、BOM 付きの空ファイルを用意する
static void ensure_unicode_file(const std::wstring& path) {
	if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
	HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return;
	const unsigned char bom[] = { 0xFF, 0xFE };
	DWORD written = 0;
	WriteFile(f, bom, sizeof(bom), &written, nullptr);
	CloseHandle(f);
}

static std::wstring read_string(const std::wstring& path, LPCWSTR section, LPCWSTR key) {
	std::vector<wchar_t> buf(32768);
	GetPrivateProfileStringW(section, key, L"", buf.data(), (DWORD)buf.size(), path.c_str());
	return buf.data();
}

// セクション内の key=value を全て読む (key に '=' は含まれない前提で、最初の '=' で区切る)
static std::map<std::wstring, std::wstring> read_section(const std::wstring& path, LPCWSTR section) {
	std::map<std::wstring, std::wstring> out;
	std::vector<wchar_t> buf(65536);
	DWORD len = GetPrivateProfileSectionW(section, buf.data(), (DWORD)buf.size(), path.c_str());
	for (const wchar_t* p = buf.data(); p < buf.data() + len && *p; p += wcslen(p) + 1) {
		std::wstring line = p;
		auto eq = line.find(L'=');
		if (eq == std::wstring::npos) continue;
		out[line.substr(0, eq)] = line.substr(eq + 1);
	}
	return out;
}

void Settings::load() {
	auto path = settings_path();
	resume = GetPrivateProfileIntW(L"State", L"resume", 0, path.c_str()) != 0;
	session_active = GetPrivateProfileIntW(L"State", L"session_active", 0, path.c_str()) != 0;
	verified_version = (DWORD)GetPrivateProfileIntW(L"State", L"verified_version", 0, path.c_str());
	return_layout = read_string(path, L"Settings", L"return_layout");
	drop_style = GetPrivateProfileIntW(L"Settings", L"drop_style", -1, path.c_str());
	tree = read_string(path, L"Tree", L"layout");
	tree_version = GetPrivateProfileIntW(L"Tree", L"version", 0, path.c_str());

	floating.clear();
	for (auto& [key, value] : read_section(path, L"Floating")) {
		RECT r = {};
		if (swscanf_s(value.c_str(), L"%ld,%ld,%ld,%ld", &r.left, &r.top, &r.right, &r.bottom) == 4 &&
			r.right > r.left && r.bottom > r.top) {
			floating[key] = r;
		}
	}
}

void Settings::save() const {
	auto path = settings_path();
	ensure_unicode_file(path);
	WritePrivateProfileStringW(L"State", L"resume", resume ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"State", L"session_active", session_active ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"State", L"verified_version", std::to_wstring(verified_version).c_str(), path.c_str());
	WritePrivateProfileStringW(L"Settings", L"return_layout", return_layout.c_str(), path.c_str());
	WritePrivateProfileStringW(L"Settings", L"drop_style", std::to_wstring(drop_style).c_str(), path.c_str());
	WritePrivateProfileStringW(L"Tree", L"layout", tree.c_str(), path.c_str());
	WritePrivateProfileStringW(L"Tree", L"version", std::to_wstring(tree_version).c_str(), path.c_str());

	// 旧版の [Panels] は使わないため消す
	WritePrivateProfileStringW(L"Panels", nullptr, nullptr, path.c_str());

	// [Floating] は丸ごと書き直す
	std::wstring block;
	for (auto& [key, r] : floating) {
		wchar_t buf[64];
		swprintf_s(buf, L"%ld,%ld,%ld,%ld", r.left, r.top, r.right, r.bottom);
		block += key + L"=" + buf;
		block.push_back(L'\0');
	}
	block.push_back(L'\0');
	WritePrivateProfileSectionW(L"Floating", block.c_str(), path.c_str());
}

} // namespace tpgd
