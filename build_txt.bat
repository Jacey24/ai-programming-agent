@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cl /EHsc /std:c++20 /utf-8 skill_src\skill_txt_main.cpp /Fe:skills\skill_txt.exe
echo Build exit code: %ERRORLEVEL%