# DX-Ball 2 AEx
Anniversary Extras for DX-Ball 2: 20th Anniversary Edition

## Install
1. [Download the latest version](https://github.com/Carlmundo/DXB2-AEx/releases/latest)
2. Extract the .zip and place the contents in the game folder.

### Proton / Wine users
Windows users can ignore the next additional steps.
1. In Steam, right click the game and click Properies.
2. Under "Launch Options", in the text box, add: `WINEDLLOVERRIDES="version=n,b" %command%`

## Features
### In-Game
- Play boards from Rival Ball (with leaderboards) - copy them into the `Boards\Rival Ball` folder
- Skip the Longbow Games splash screen
- Disable the carpet rolling screen transition
- Hold left-click to fire the laser paddle
- Restore the original ball launch angle
- Block dangerous code from community content
- Stop a specific "cheat code" from being able to reset Steam achievements
- Use online leaderboards for specific community content
- Fixes:
	- Music becoming silent and/or muted notes at lower Master and Music volume settings
	- Lives counter display cut off when you have 5 or more lives
	- Paddle could appear in the top left when loading a paused saved game
	- Invisible bricks:
		- explosion and Fireball would only reveal, not destroy them
		- could become visible when moved with Falling Bricks and cause other bricks to become invisible in its former place
		- were not revealed by Zap Bricks
		- stayed hidden when changed to an exploding brick by Expand
	- Reflected laser shots permanently using up ammo after hitting permanent bricks with Thru Brick (under Classic Rules)

You can enable and disable options in `aex.cfg`.

### Editor
- Edit boardsets included with the game (they no longer show as locked)
- Use scripts up to 1MB (increased from 16KB)
- Script text editor fixes:
	- broken Undo/Redo
	- `{}\` characters in comments deleting code
	- typing `--` from moving preceding text to the end of the line
	- a space being added after a final newline

## Build
Prerequisites:
- Visual Studio 2022 with Desktop development with C++, including MSVC x86 tools and a Windows SDK.
- Node.js 20 or newer. No npm install is needed.

Generate the .scratch folder. Change the paths to match your own copies of the repository and game.
```
cd /d "C:\path-to-repo\DX-Ball 2 AEx"
if not exist .scratch\game mkdir .scratch\game
copy /y "C:\SteamLibrary\steamapps\common\DX-Ball 2 - 20th Anniversary Edition\DXBall2.exe" ".scratch\game\DXBall2.exe"
copy /y "C:\SteamLibrary\steamapps\common\DX-Ball 2 - 20th Anniversary Edition\steam_api.dll" ".scratch\game\steam_api.dll"
dumpbin /disasm ".scratch\game\DXBall2.exe" > ".scratch\dxball2.asm"
```

Build:
```
native\build.cmd
native\build-editor.cmd
```
