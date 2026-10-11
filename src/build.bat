@echo off
rem build TP_GUIdocker.aux2 (x64 Release)
rem   build.bat          dev build     -> out\TP_GUIdocker.aux2 (with dev-only items such as the samples menu)
rem   build.bat release  release build -> out\release\TP_GUIdocker.aux2 (without dev-only items)
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
set OUTDIR=out
set DEVFLAG=/DTPGD_DEV
if /i "%~1"=="release" (
  set OUTDIR=out\release
  set DEVFLAG=
)
if not exist %OUTDIR% mkdir %OUTDIR%
cl /nologo /std:c++20 /O2 /W4 /EHsc /utf-8 /MT /LD /DUNICODE /D_UNICODE /DNOMINMAX %DEVFLAG% ^
   /I "..\third_party\aviutl2_sdk\include\aviutl2_sdk" ^
   main.cpp common.cpp host.cpp menu_find.cpp layout_file.cpp settings.cpp tree.cpp dock.cpp dock_layout.cpp host_ops.cpp hooks.cpp overlay.cpp prompt.cpp ^
   /Fo:%OUTDIR%\ /Fe:%OUTDIR%\TP_GUIdocker.aux2 ^
   /link user32.lib gdi32.lib comctl32.lib gdiplus.lib comdlg32.lib || exit /b 1
