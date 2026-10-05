@echo off
rem build TP_GUIdocker.aux2 (x64 Release)
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /O2 /EHsc /utf-8 /MT /LD /DUNICODE /D_UNICODE ^
   /I "..\third_party\aviutl2_sdk\include\aviutl2_sdk" ^
   TP_GUIdocker.cpp /Fo:out\ /Fe:out\TP_GUIdocker.aux2 ^
   /link user32.lib gdi32.lib
