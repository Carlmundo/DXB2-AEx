@echo off
setlocal
set "VS_TOOLS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VS_TOOLS%" exit /b 2
call "%VS_TOOLS%" >nul
if errorlevel 1 exit /b %errorlevel%
if not exist build mkdir build
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
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\startup-tests.exe native\startup-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I .scratch /Fo:build\ /Fe:build\transition-tests.exe native\transition-tests.cpp /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /LD /I .scratch /Fo:build\ /Fe:build\version.dll native\mod.cpp /link /DEF:native\version.def /DYNAMICBASE /NXCOMPAT
exit /b %errorlevel%
