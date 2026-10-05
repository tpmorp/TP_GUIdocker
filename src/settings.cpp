//----------------------------------------------------------------------------------
//	プラグイン専用の設定ファイル (TP_GUIdocker.ini) の読み書き
//	日本語を扱うため、UTF-16LE (BOM 付き) のファイルを Win32 のプロファイル関数で読み書きする
//----------------------------------------------------------------------------------
#include "settings.h"
#include "ui_spec.h"

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
	std::vector<wchar_t> buf(65536);
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

// 浮いた窓の位置の一覧を1行の文字列にする: セクション名<TAB>左,上,右,下<TAB>...
static std::wstring encode_floating(const FloatingMap& m) {
	std::wstring s;
	for (auto& [key, r] : m) {
		wchar_t buf[64];
		swprintf_s(buf, L"%ld,%ld,%ld,%ld", r.left, r.top, r.right, r.bottom);
		if (!s.empty()) s += L'\t';
		s += key + L'\t' + buf;
	}
	return s;
}

static FloatingMap decode_floating(const std::wstring& s) {
	FloatingMap m;
	std::vector<std::wstring> parts;
	size_t start = 0;
	while (start <= s.size()) {
		size_t tab = s.find(L'\t', start);
		if (tab == std::wstring::npos) tab = s.size();
		parts.push_back(s.substr(start, tab - start));
		start = tab + 1;
	}
	for (size_t i = 0; i + 1 < parts.size(); i += 2) {
		RECT r = {};
		if (swscanf_s(parts[i + 1].c_str(), L"%ld,%ld,%ld,%ld", &r.left, &r.top, &r.right, &r.bottom) == 4 &&
			r.right > r.left && r.bottom > r.top && !parts[i].empty()) {
			m[parts[i]] = r;
		}
	}
	return m;
}

void Settings::load() {
	auto path = settings_path();
	resume = GetPrivateProfileIntW(L"State", L"resume", 0, path.c_str()) != 0;
	session_active = GetPrivateProfileIntW(L"State", L"session_active", 0, path.c_str()) != 0;
	verified_version = (DWORD)GetPrivateProfileIntW(L"State", L"verified_version", 0, path.c_str());
	return_layout = read_string(path, L"Settings", L"return_layout");
	drop_style = (int)GetPrivateProfileIntW(L"Settings", L"drop_style", (UINT)-1, path.c_str());
	locked = GetPrivateProfileIntW(L"Settings", L"locked", 0, path.c_str()) != 0;
	tree_version = GetPrivateProfileIntW(L"Tree", L"version", 0, path.c_str());

	workspaces.clear();
	int count = GetPrivateProfileIntW(L"Workspaces", L"count", 0, path.c_str());
	for (int i = 0; i < count; i++) {
		std::wstring sec = L"Workspace." + std::to_wstring(i);
		Workspace w;
		w.name = read_string(path, sec.c_str(), L"name");
		w.saved_tree = read_string(path, sec.c_str(), L"saved_tree");
		w.saved_floating = decode_floating(read_string(path, sec.c_str(), L"saved_floating"));
		w.work_tree = read_string(path, sec.c_str(), L"work_tree");
		w.work_floating = decode_floating(read_string(path, sec.c_str(), L"work_floating"));
		if (w.name.empty()) w.name = ui::WS_DEFAULT_NAME;
		workspaces.push_back(w);
	}
	active_ws = GetPrivateProfileIntW(L"Workspaces", L"active", 0, path.c_str());

	if (workspaces.empty()) {
		// 旧版 (ワークスペース導入前) の配置を、最初のワークスペースとして引き継ぐ
		Workspace w;
		w.name = ui::WS_DEFAULT_NAME;
		w.work_tree = w.saved_tree = read_string(path, L"Tree", L"layout");
		FloatingMap old;
		for (auto& [key, value] : read_section(path, L"Floating")) {
			RECT r = {};
			if (swscanf_s(value.c_str(), L"%ld,%ld,%ld,%ld", &r.left, &r.top, &r.right, &r.bottom) == 4 &&
				r.right > r.left && r.bottom > r.top) {
				old[key] = r;
			}
		}
		w.work_floating = w.saved_floating = old;
		workspaces.push_back(w);
		active_ws = 0;
	}
	if (active_ws < 0 || active_ws >= (int)workspaces.size()) active_ws = 0;
	tree = workspaces[active_ws].work_tree;
	floating = workspaces[active_ws].work_floating;
}

void Settings::save() {
	auto path = settings_path();
	ensure_unicode_file(path);

	// 使用中の配置を、使用中のワークスペースへ書き戻す
	if (active_ws >= 0 && active_ws < (int)workspaces.size()) {
		workspaces[active_ws].work_tree = tree;
		workspaces[active_ws].work_floating = floating;
	}

	WritePrivateProfileStringW(L"State", L"resume", resume ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"State", L"session_active", session_active ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"State", L"verified_version", std::to_wstring(verified_version).c_str(), path.c_str());
	WritePrivateProfileStringW(L"Settings", L"return_layout", return_layout.c_str(), path.c_str());
	WritePrivateProfileStringW(L"Settings", L"drop_style", std::to_wstring(drop_style).c_str(), path.c_str());
	WritePrivateProfileStringW(L"Settings", L"locked", locked ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"Tree", L"version", std::to_wstring(tree_version).c_str(), path.c_str());
	// 旧版の項目は使わないため消す
	WritePrivateProfileStringW(L"Tree", L"layout", nullptr, path.c_str());
	WritePrivateProfileStringW(L"Panels", nullptr, nullptr, path.c_str());
	WritePrivateProfileStringW(L"Floating", nullptr, nullptr, path.c_str());

	// ワークスペース (減った分の古いセクションも消す)
	int old_count = GetPrivateProfileIntW(L"Workspaces", L"count", 0, path.c_str());
	for (int i = (int)workspaces.size(); i < old_count; i++) {
		WritePrivateProfileStringW((L"Workspace." + std::to_wstring(i)).c_str(), nullptr, nullptr, path.c_str());
	}
	WritePrivateProfileStringW(L"Workspaces", L"count", std::to_wstring(workspaces.size()).c_str(), path.c_str());
	WritePrivateProfileStringW(L"Workspaces", L"active", std::to_wstring(active_ws).c_str(), path.c_str());
	for (size_t i = 0; i < workspaces.size(); i++) {
		const Workspace& w = workspaces[i];
		std::wstring sec = L"Workspace." + std::to_wstring(i);
		WritePrivateProfileStringW(sec.c_str(), L"name", w.name.c_str(), path.c_str());
		WritePrivateProfileStringW(sec.c_str(), L"saved_tree", w.saved_tree.c_str(), path.c_str());
		WritePrivateProfileStringW(sec.c_str(), L"saved_floating", encode_floating(w.saved_floating).c_str(), path.c_str());
		WritePrivateProfileStringW(sec.c_str(), L"work_tree", w.work_tree.c_str(), path.c_str());
		WritePrivateProfileStringW(sec.c_str(), L"work_floating", encode_floating(w.work_floating).c_str(), path.c_str());
	}
}

} // namespace tpgd
