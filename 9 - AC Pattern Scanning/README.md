# cube7 - with pattern scanning

Internal cheat DLL for Assault Cube. Hooks `wglSwapBuffers` (MinHook) to render ESP in the game's OpenGL context.

## Features

- ESP boxes, health bars, player names
- Infinite ammo toggle
- Pattern scanning for offsets (fallbacks for v1.3.0.2)
- Debug console with hook/scan status

## Hotkeys

| Key | Action |
|-----|--------|
| F2  | Toggle infinite ammo |
| F3  | Toggle ESP |
| END | Unload DLL |

## Build

1. Open `cube7.sln` in Visual Studio
2. Build **Release | Win32**
3. Output: `Release/cube7.dll`

Requires MinHook (`libMinHook.x86.lib`, `MinHook.h`) in the `MinHook/` folder.

## Usage

1. Start Assault Cube and load into a map (with bots or other players)
2. Inject `cube7.dll` into `ac_client.exe`
3. Console appears showing hook status and scanned offsets

**Note:** ESP only shows when there are other players/bots in the game.
