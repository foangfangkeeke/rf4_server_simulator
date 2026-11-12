@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo Run this from an x64 Native Tools Command Prompt for Visual Studio.
  exit /b 1
)
cd /d "%~dp0"
python generate_catalog.py --check
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File make-exports.ps1
if errorlevel 1 exit /b 1
lib /nologo /DEF:out\system.def /MACHINE:X64 /OUT:out\system.lib
if errorlevel 1 exit /b 1
cl /nologo /LD /O2 /MT /EHsc /W4 /std:c++17 /utf-8 client.cpp /Foout\client.obj /Feout\winmm.dll /link /DEF:out\winmm.def /IMPLIB:out\client.lib out\system.lib user32.lib
if errorlevel 1 exit /b 1
copy /y "%SystemRoot%\System32\winmm.dll" "out\rf4_remote_winmm_system.dll" >nul
if errorlevel 1 exit /b 1
if "%~1"=="test" (
  cl /nologo /O2 /MT /EHsc /W4 /wd4505 /std:c++17 /utf-8 test_bridge.cpp /Foout\test_bridge.obj /Feout\test_bridge.exe /link user32.lib
  if errorlevel 1 exit /b 1
  out\test_bridge.exe
  if errorlevel 1 exit /b 1
)
exit /b %errorlevel%
