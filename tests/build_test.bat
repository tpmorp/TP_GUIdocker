@echo off
rem build unit tests (x64)
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /O2 /W4 /EHsc /utf-8 /MT /DUNICODE /D_UNICODE /DNOMINMAX ^
   test_core.cpp ..\src\tree.cpp ..\src\layout_file.cpp ^
   /Fo:out\ /Fe:out\test_core.exe
