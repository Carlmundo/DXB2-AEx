# DX-Ball 2 AEx
Anniversary Extras for DX-Ball 2: 20th Anniversary Edition

## Install
1. [Download the latest version](https://github.com/Carlmundo/DXB2-AEx/releases/latest)
2. Extract the .zip and place the files in the game folder.

### Proton / Wine users
Windows users can ignore these steps.
1. In Steam, right click the game and click Properies.
2. Under "Launch Options", in the text box, add: `WINEDLLOVERRIDES="version=n,b" %command%`

## Features
- Play boards from Rival Ball - copy them into the `Boards\Rival Ball` folder
- Skip the Longbow Games splash screen
- Disable the carpet rolling screen transition
- Block dangerous code from community content

You can enable and disable options in `aex.cfg`.

## Build
Prerequisites:
- Visual Studio 2022 with Desktop development with C++, including MSVC x86 tools and a Windows SDK.
- Node.js 20 or newer. No npm install is needed.

Generate the .scratch folder. Change the paths to match your own copies of the repository and game.
```
cd /d "C:\path-to-repo\DX-Ball 2 AEx"
if not exist .scratch\game mkdir .scratch\game
copy /y "C:\SteamLibrary\steamapps\common\DX-Ball 2 - 20th Anniversary Edition\DXBall2.exe" ".scratch\game\DXBall2.exe"
dumpbin /disasm ".scratch\game\DXBall2.exe" > ".scratch\dxball2.asm"
```

Build:

`native\build.cmd`
