//----------------------------------------------------------------------------------
//	TP_GUIdocker 共通定義
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
#define TPGD_VERSION L"0.2.4"

namespace tpgd {

extern LOG_HANDLE* g_logger;
extern CONFIG_HANDLE* g_config;
extern DWORD g_host_version;

// 本体のログ窓と、DLL と同じ場所の TP_GUIdocker.log へ書き出す
void log(const std::wstring& s);

std::wstring utf8_to_wide(const std::string& s);
std::string wide_to_utf8(const std::wstring& s);

// 本プラグイン (DLL) が置かれたフォルダ
std::wstring module_dir();

// 窓の識別用文字列 (ログ用)
std::wstring describe(HWND h);

// 窓の題名
std::wstring window_text(HWND h);

// 文言中の最初の %s を value に置き換える (文言の定義は ui_spec.h)
std::wstring format_s(const std::wstring& text, const std::wstring& value);

} // namespace tpgd
