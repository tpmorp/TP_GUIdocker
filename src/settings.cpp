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
	return_saved_clean = GetPrivateProfileIntW(L"State", L"return_saved_clean", 0, path.c_str()) != 0;
	titles.clear();
	for (auto& [k, v] : read_section(path, L"Titles")) {
		// キーは番号、値は セクション名<TAB>題名
		auto tab = v.find(L'	');
		if (tab != std::wstring::npos && tab > 0) titles[v.substr(0, tab)] = v.substr(tab + 1);
	}
	return_mates.clear();
	for (auto& [n, value] : read_section(path, L"Return")) {
		// 値: セクション名<TAB>仲間<TAB>仲間...
		std::vector<std::wstring> parts;
		size_t start = 0;
		while (start <= value.size()) {
			size_t tab = value.find(L'\t', start);
			if (tab == std::wstring::npos) tab = value.size();
			parts.push_back(value.substr(start, tab - start));
			start = tab + 1;
		}
		if (parts.size() >= 2 && !parts[0].empty()) {
			return_mates[parts[0]] = std::vector<std::wstring>(parts.begin() + 1, parts.end());
		}
	}
	return_layout = read_string(path, L"Settings", L"return_layout");
	drop_style = (int)GetPrivateProfileIntW(L"Settings", L"drop_style", (UINT)-1, path.c_str());
	locked = GetPrivateProfileIntW(L"Settings", L"locked", 0, path.c_str()) != 0;
	theme_color = (int)GetPrivateProfileIntW(L"Settings", L"theme_color", (UINT)-1, path.c_str());
	// 旧版で選んだワークスペースの色は、テーマの色として引き継ぐ
	if (theme_color < 0) theme_color = (int)GetPrivateProfileIntW(L"Settings", L"ws_active_color", (UINT)-1, path.c_str());
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

// 設定ファイルの内容を組み立てる。プロファイル関数で1項目ずつ書くと、呼び出しのたびにファイル全体を
// 読み直して書き直すため (実測で1回の保存に約0.6秒)、全体を1回で書き出す
namespace {
struct IniText {
	std::wstring text;
	void section(const std::wstring& name) {
		if (!text.empty()) text += L"\r\n";
		text += L"[" + name + L"]\r\n";
	}
	void set(const std::wstring& key, const std::wstring& value) { text += key + L"=" + value + L"\r\n"; }
};

// 既存の設定ファイルから、このプログラムが管理しないセクション ([Dev] など) をそのまま取り出す
std::wstring unmanaged_sections(const std::wstring& path) {
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return {};
	std::vector<char> raw;
	char buf[8192];
	DWORD got = 0;
	while (ReadFile(f, buf, sizeof(buf), &got, nullptr) && got) raw.insert(raw.end(), buf, buf + got);
	CloseHandle(f);
	if (raw.size() < 2 || (unsigned char)raw[0] != 0xFF || (unsigned char)raw[1] != 0xFE) return {};
	std::wstring all((const wchar_t*)(raw.data() + 2), (raw.size() - 2) / 2);

	std::wstring out;
	bool keep = false;
	size_t pos = 0;
	while (pos < all.size()) {
		size_t nl = all.find(L'\n', pos);
		std::wstring line = all.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
		pos = nl == std::wstring::npos ? all.size() : nl + 1;
		while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.pop_back();
		if (!line.empty() && line.front() == L'[') {
			auto close = line.find(L']');
			std::wstring name = close == std::wstring::npos ? line.substr(1) : line.substr(1, close - 1);
			bool managed = name == L"State" || name == L"Titles" || name == L"Return" || name == L"Settings" ||
				name == L"Tree" || name == L"Panels" || name == L"Floating" || name == L"Workspaces" || name.rfind(L"Workspace.", 0) == 0;
			keep = !managed;
			if (keep) out += L"\r\n";
		}
		if (keep && !line.empty()) out += line + L"\r\n";
	}
	return out;
}

bool write_unicode_file(const std::wstring& path, const std::wstring& text) {
	auto write_to = [&](const std::wstring& target) {
		HANDLE f = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (f == INVALID_HANDLE_VALUE) return false;
		const unsigned char bom[] = { 0xFF, 0xFE };
		DWORD written = 0;
		bool ok = WriteFile(f, bom, sizeof(bom), &written, nullptr) &&
			WriteFile(f, text.data(), (DWORD)(text.size() * sizeof(wchar_t)), &written, nullptr);
		CloseHandle(f);
		return ok;
	};
	// 途中で終わっても設定が壊れないよう、いったん別名で書いてから置き換える
	std::wstring tmp = path + L".tmp";
	if (write_to(tmp) && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
	DeleteFileW(tmp.c_str());
	return write_to(path);
}
} // namespace

void Settings::save() {
	auto path = settings_path();

	// 使用中の配置を、使用中のワークスペースへ書き戻す
	if (active_ws >= 0 && active_ws < (int)workspaces.size()) {
		workspaces[active_ws].work_tree = tree;
		workspaces[active_ws].work_floating = floating;
	}

	IniText ini;
	ini.section(L"State");
	ini.set(L"resume", resume ? L"1" : L"0");
	ini.set(L"session_active", session_active ? L"1" : L"0");
	ini.set(L"verified_version", std::to_wstring(verified_version));
	ini.set(L"return_saved_clean", return_saved_clean ? L"1" : L"0");

	ini.section(L"Titles");
	{
		int n = 0;
		for (auto& [key, title] : titles) {
			if (key.empty() || title.empty()) continue;
			ini.set(std::to_wstring(n++), key + L'\t' + title);
		}
	}
	ini.section(L"Return");
	{
		int n = 0;
		for (auto& [key, mates] : return_mates) {
			if (mates.empty()) continue;
			std::wstring v = key;
			for (auto& m : mates) v += L'\t' + m;
			ini.set(std::to_wstring(n++), v);
		}
	}

	ini.section(L"Settings");
	ini.set(L"return_layout", return_layout);
	ini.set(L"drop_style", std::to_wstring(drop_style));
	ini.set(L"locked", locked ? L"1" : L"0");
	ini.set(L"theme_color", std::to_wstring(theme_color));
	// 旧版の項目 (ws_active_color・[Tree] layout・[Panels]・[Floating]) は使わないため書かない

	ini.section(L"Tree");
	ini.set(L"version", std::to_wstring(tree_version));

	ini.section(L"Workspaces");
	ini.set(L"count", std::to_wstring(workspaces.size()));
	ini.set(L"active", std::to_wstring(active_ws));
	for (size_t i = 0; i < workspaces.size(); i++) {
		const Workspace& w = workspaces[i];
		ini.section(L"Workspace." + std::to_wstring(i));
		ini.set(L"name", w.name);
		ini.set(L"saved_tree", w.saved_tree);
		ini.set(L"saved_floating", encode_floating(w.saved_floating));
		ini.set(L"work_tree", w.work_tree);
		ini.set(L"work_floating", encode_floating(w.work_floating));
	}

	std::wstring text = ini.text + unmanaged_sections(path);
	write_unicode_file(path, text);
}
} // namespace tpgd
