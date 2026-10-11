//----------------------------------------------------------------------------------
//	TP_GUIdocker 蜈ｱ騾壼ｮ夂ｾｩ
//----------------------------------------------------------------------------------
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>

#include "plugin2.h"
#include "logger2.h"
#include "config2.h"

#define TPGD_NAME L"TP_GUIdocker"
#define TPGD_VERSION L"0.4.9"

namespace tpgd {

extern LOG_HANDLE* g_logger;
extern CONFIG_HANDLE* g_config;
extern DWORD g_host_version;

// 譛ｬ菴薙・繝ｭ繧ｰ遯薙→縲．LL 縺ｨ蜷後§蝣ｴ謇縺ｮ TP_GUIdocker.log 縺ｸ譖ｸ縺榊・縺・
void log(const std::wstring& s);

std::wstring utf8_to_wide(const std::string& s);
std::string wide_to_utf8(const std::wstring& s);

// 譛ｬ繝励Λ繧ｰ繧､繝ｳ (DLL) 縺檎ｽｮ縺九ｌ縺溘ヵ繧ｩ繝ｫ繝
std::wstring module_dir();

// 遯薙・隴伜挨逕ｨ譁・ｭ怜・ (繝ｭ繧ｰ逕ｨ)
std::wstring describe(HWND h);

// 遯薙・鬘悟錐
std::wstring window_text(HWND h);

// 譁・ｨ荳ｭ縺ｮ譛蛻昴・ %s 繧・value 縺ｫ鄂ｮ縺肴鋤縺医ｋ (譁・ｨ縺ｮ螳夂ｾｩ縺ｯ ui_spec.h)
std::wstring format_s(const std::wstring& text, const std::wstring& value);

} // namespace tpgd
