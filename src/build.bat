@echo off
rem build TP_GUIdocker.aux2 (x64 Release)
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /O2 /W4 /EHsc /utf-8 /MT /LD /DUNICODE /D_UNICODE /DNOMINMAX ^
   /I "..\third_party\aviutl2_sdk\include\aviutl2_sdk" ^
   main.cpp common.cpp host.cpp layout_file.cpp settings.cpp tree.cpp dock.cpp dock_layout.cpp host_ops.cpp hooks.cpp overlay.cpp ^
   /Fo:out\ /Fe:out\TP_GUIdocker.aux2 ^
   /link user32.lib gdi32.lib comctl32.lib
