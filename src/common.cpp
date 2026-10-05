//----------------------------------------------------------------------------------
//	TP_GUIdocker 共通処理 (ログ・文字コード変換等)
//----------------------------------------------------------------------------------
#include "common.h"

namespace tpgd {

LOG_HANDLE* g_logger = nullptr;
CONFIG_HANDLE* g_config = nullptr;
DWORD g_host_version = 0;

std::wstring utf8_to_wide(const std::string& s) {
	if (s.empty()) return {};
	int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring w(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
	return w;
}

std::string wide_to_utf8(const std::wstring& w) {
	if (w.empty()) return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), len, nullptr, nullptr);
	return s;
}

std::wstring module_dir() {
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCWSTR)&module_dir, &self);
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(self, path, MAX_PATH);
	std::wstring p = path;
	p.resize(p.find_last_of(L'\\'));
	return p;
}

std::wstring window_text(HWND h) {
	wchar_t text[256] = {};
	GetWindowTextW(h, text, 256);
	return text;
}

std::wstring format_s(const std::wstring& text, const std::wstring& value) {
	std::wstring out = text;
	auto pos = out.find(L"%s");
	if (pos != std::wstring::npos) out.replace(pos, 2, value);
	return out;
}

std::wstring describe(HWND h) {
	wchar_t cls[128] = {};
	GetClassNameW(h, cls, 128);
	wchar_t buf[400];
	swprintf_s(buf, L"0x%p class='%s' text='%s'", (void*)h, cls, window_text(h).c_str());
	return buf;
}

void log(const std::wstring& s) {
	std::wstring msg = L"[" TPGD_NAME L"] " + s;
	if (g_logger) g_logger->log(g_logger, msg.c_str());

	static std::wstring path = module_dir() + L"\\" TPGD_NAME L".log";
	SYSTEMTIME t;
	GetLocalTime(&t);
	wchar_t stamp[32];
	swprintf_s(stamp, L"%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
	std::string utf8 = wide_to_utf8(stamp + msg + L"\r\n");
	HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f != INVALID_HANDLE_VALUE) {
		DWORD written = 0;
		WriteFile(f, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
		CloseHandle(f);
	}
}

} // namespace tpgd
