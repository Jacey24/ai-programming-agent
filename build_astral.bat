@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cl /EHsc /std:c++20 /utf-8 /I. source\main.cpp core\agent_policy.cpp core\agent_exec.cpp core\agent_interact.cpp core\cli_builtins.cpp core\shell.cpp core\skill_loader.cpp runtime\api_client.cpp runtime\context_manager.cpp runtime\output_formatter.cpp runtime\xml_protocol.cpp /Fe:Astral.exe
echo Build exit code: %ERRORLEVEL%
