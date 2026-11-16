@echo off
setlocal
set "VS_TOOLS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VS_TOOLS%" exit /b 2
call "%VS_TOOLS%" >nul
if errorlevel 1 exit /b %errorlevel%
if not exist build\Tools mkdir build\Tools
set "STEAM_DLL=%~1"
if not defined STEAM_DLL set "STEAM_DLL=.scratch\game\steam_api.dll"
node tools\editor-exports.mjs "%STEAM_DLL%"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /LD /I .scratch /Fo:build\ /Fe:build\Tools\steam_api.dll native\editor-mod.cpp user32.lib /link /DEF:.scratch\editor-steam.def /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /Fo:build\ /Fe:build\editor-proxy-tests.exe native\editor-proxy-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /Fo:build\ /Fe:build\editor-script-tests.exe native\editor-script-tests.cpp user32.lib /link /DYNAMICBASE /NXCOMPAT
exit /b %errorlevel%
