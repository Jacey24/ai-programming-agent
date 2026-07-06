@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

echo === Building Astral.exe ===
cl /EHsc /std:c++20 /utf-8 /I. source\main.cpp core\*.cpp runtime\*.cpp /Fe:Astral.exe

echo.
echo === Building skill_mem.exe ===
cl /EHsc /std:c++20 /utf-8 skill_src\skill_mem_main.cpp /Fe:skills\skill_mem.exe

echo.
echo === Building skill_calc.exe ===
cl /EHsc /std:c++20 /utf-8 skill_src\skill_calc_main.cpp /Fe:skills\skill_calc.exe

echo.
echo === Building skill_mask.exe ===
cl /EHsc /std:c++20 /utf-8 skill_src\skill_mask_main.cpp /Fe:skills\skill_mask.exe

echo.
echo === All builds completed ===
echo Astral exit code: %ERRORLEVEL%
