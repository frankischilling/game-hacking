# cube5 - AssaultCube ESP Overlay

External DirectX 9 ESP overlay for **AssaultCube v1.3.0.2**.  
Draws player boxes and name labels through a transparent layered window.

---

## Features

- 2D bounding box around every visible enemy / teammate
- Player name label above each box
- Team-colour coding: **red** = enemy, **green** = teammate
- Console logging for every rendered entity and all startup steps

---

## Requirements

| Item | Notes |
|------|-------|
| Windows 10/11 | Tested on Win10 x64 |
| AssaultCube v1.3.0.2 | Must be running before the overlay |
| DirectX 9 SDK | Link `d3d9.lib`, `dwmapi.lib` |
| Visual Studio | Project is configured as a Win32 app |

---

## Build

1. Open the `.sln` in Visual Studio.
2. Set configuration to **Release / x86**.
3. Build → Run.

> The overlay **must** be built as **32-bit (x86)** to match the 32-bit AC process.

---

## Offsets (v1.3.0.2)

| Symbol | Address / Offset | Notes |
|--------|-----------------|-------|
| `EntityList` | `0x18AC04` | relative to module base |
| `EntityCount` | `0x18AC0C` | relative to module base |
| `LocalPlayer` | `0x0017E0A8` | relative to module base |
| `ViewMatrix` | `0x57DFD0` | **absolute** static address (no ASLR) |
| `PosX` | `+0x28` | entity member offset |
| `PosY` | `+0x2C` | entity member offset |
| `PosZ` | `+0x30` | entity member offset (vertical / height) |
| `Health` | `+0xEC` | entity member offset |
| `Name` | `+0x205` | entity member offset, `char[16]` |
| `Team` | `+0x31C` | 0 = FFA, 1 = CLA, 2 = RVSF |

---

## Tuning

**Box too tall / too short** - adjust `PLAYER_HEIGHT` near the top of `main.cpp`:

```cpp
constexpr float PLAYER_HEIGHT = 5.0f;
```

**Box too wide / narrow** - adjust the width ratio in `RenderESP()`:

```cpp
float boxW = boxH * 0.4f;  // 0.4 = 40% of box height
```

---

## Console Log

A console window opens automatically on launch. Output looks like:

```
[ESP] ESP starting...
[ESP] game window found: 0x00ABCDEF
[ESP] PID=1234  moduleBase=0x00400000
[ESP] window size: 1280x720
[ESP] overlay window created
[ESP] D3D device ready - entering render loop
[ESP] entity[1] PlayerName   hp= 85  feet=(120.3,44.1,2.0)  screen=(640,360)
```

---

## How It Works

1. Finds the AC window and opens the process with `PROCESS_ALL_ACCESS`.
2. Creates a fullscreen `WS_EX_LAYERED | WS_EX_TRANSPARENT` popup window on top of the game.
3. Each frame: reads the view matrix and entity list from AC memory, projects world positions to screen coordinates using the OpenGL column-major MVP matrix, then draws boxes via D3D9 line primitives and names via GDI.
