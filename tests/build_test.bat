@echo off
rem build unit tests (x64)
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /O2 /W4 /EHsc /utf-8 /MT /DUNICODE /D_UNICODE /DNOMINMAX ^
   /I "..\third_party\aviutl2_sdk\include\aviutl2_sdk" ^
   test_core.cpp ..\src\tree.cpp ..\src\layout_file.cpp ..\src\dock_layout.cpp ..\src\common.cpp ^
   /Fo:out\ /Fe:out\test_core.exe ^
   /link user32.lib
