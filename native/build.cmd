@echo off
setlocal
set "VS_TOOLS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VS_TOOLS%" exit /b 2
call "%VS_TOOLS%" >nul
if errorlevel 1 exit /b %errorlevel%
if not exist build mkdir build
node tools\project-version.mjs .scratch\aex-version.h
if errorlevel 1 exit /b %errorlevel%
node tools\native-manifest.mjs .scratch\game\DXBall2.exe .scratch\dxball2.asm .scratch\patches.json
if errorlevel 1 exit /b %errorlevel%
node tools\geometry-manifest.mjs .scratch\dxball2.asm .scratch\geometry.h
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\engine-harness.exe native\engine-harness.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /Fo:build\ /Fe:build\format-harness.exe native\format-harness.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /Fo:build\ /Fe:build\rival-mirror-harness.exe native\rival-mirror-harness.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\save-harness.exe native\save-harness.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /Fo:build\ /Fe:build\save-format-tests.exe native\save-format-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /Fo:build\ /Fe:build\config-tests.exe native\config-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\startup-tests.exe native\startup-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\transition-tests.exe native\transition-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\dangerous-lua-tests.exe native\dangerous-lua-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\aex-update-tests.exe native\aex-update-tests.cpp /link /DYNAMICBASE /NXCOMPAT /MANIFEST:EMBED /MANIFESTUAC:"level='asInvoker' uiAccess='false'"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\version-proxy-tests.exe native\version-proxy-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\nostat-tests.exe native\nostat-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\lives-counter-tests.exe native\lives-counter-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\music-volume-tests.exe native\music-volume-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\loaded-paddle-tests.exe native\loaded-paddle-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\brick-behaviour-tests.exe native\brick-behaviour-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\laser-paddle-tests.exe native\laser-paddle-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\launch-angle-tests.exe native\launch-angle-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\board-order-tests.exe native\board-order-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\online-leaderboards-tests.exe native\online-leaderboards-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\rehab-mode-tests.exe native\rehab-mode-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\rehab-startup-tests.exe native\rehab-startup-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /LD /I .scratch /Fo:build\ /Fe:build\version.dll native\mod.cpp /link /DEF:native\version.def /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
call native\build-tools.cmd
exit /b %errorlevel%
