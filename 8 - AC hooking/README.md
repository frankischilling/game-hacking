# cube8

Internal cheat DLL for Assault Cube v1.3.0.2. Hooks `wglSwapBuffers` via MinHook to render ESP directly through the game's OpenGL context.

## Features

- ESP boxes, health bars, and player names drawn in-game
- Infinite ammo toggle (all weapons set to 999 each frame)
- Debug console opens on inject with hook status logging

## Hotkeys

| Key | Action |
|-----|--------|
| F2  | Toggle infinite ammo |
| F3  | Toggle ESP |
| END | Unload DLL |

## Build

1. Open `cube7.sln` in Visual Studio
2. Build **Release | Win32** (AC is 32-bit)
3. Output: `Release/cube7.dll`

Requires `MinHook/libMinHook.x86.lib` and `MinHook/MinHook.h` — both included in the repo.

## Usage

1. Start Assault Cube and load into a map
2. Inject `cube7.dll` into `ac_client.exe`
3. A console window will appear confirming the hook is active
