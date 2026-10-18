//----------------------------------------------------------------------------------
//	開発版だけの計測・実験
//----------------------------------------------------------------------------------
#include "dev_probe.h"

#ifdef TPGD_DEV

namespace tpgd {

int dev_flag(const wchar_t* key) {
	std::wstring path = module_dir() + L"\\" TPGD_NAME L".ini";
	return (int)GetPrivateProfileIntW(L"Dev", key, 0, path.c_str());
}

void dev_log_uptime(const wchar_t* what) {
	FILETIME created, exited, kernel, user, now;
	if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return;
	GetSystemTimeAsFileTime(&now);
	ULARGE_INTEGER a, b;
	a.LowPart = created.dwLowDateTime; a.HighPart = created.dwHighDateTime;
	b.LowPart = now.dwLowDateTime; b.HighPart = now.dwHighDateTime;
	unsigned long long ms = (b.QuadPart - a.QuadPart) / 10000;
	log(L"[計測] 本体の起動から " + std::to_wstring(ms) + L" ms: " + what);
}

std::wstring dev_menu_text(HMENU root, UINT id) {
	if (!root) return {};
	int n = GetMenuItemCount(root);
	for (int i = 0; i < n; i++) {
		if (HMENU sub = GetSubMenu(root, i)) {
			std::wstring t = dev_menu_text(sub, id);
			if (!t.empty()) return t;
		} else if (GetMenuItemID(root, i) == id) {
			wchar_t buf[256] = {};
			GetMenuStringW(root, i, buf, 256, MF_BYPOSITION);
			return buf;
		}
	}
	return {};
}

} // namespace tpgd

#endif
