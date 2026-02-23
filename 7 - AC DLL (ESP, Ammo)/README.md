# cube7 - Assault Cube DLL (ESP + Infinite Ammo)

DLL that injects into Assault Cube v1.3.0.2. Renders an ESP overlay with DirectX 9 and provides an infinite ammo toggle.

## Features

- **ESP boxes** - White rectangles around visible players (enemies only in team modes)
- **Health bar** - Color-coded bar (green/yellow/red) beside each box
- **Names** - Player names above boxes
- **Infinite ammo** - F2 toggles. When on, all weapon ammo stays at 999

## Build

1. Install [DirectX SDK (June 2010)](https://www.microsoft.com/en-us/download/details.aspx?id=6812)
2. Open `7/cube7.slnx` in Visual Studio (or open the folder)
3. Build **Release | Win32** (Assault Cube is 32-bit)
4. Output: `cube7.dll` in the build output folder (e.g. `Release\` or `x86\Release\`)

If DXSDK is not in the default path (`C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)`), set the `DXSDK_DIR` environment variable or edit the vcxproj Include/Lib paths.

## Usage

1. Start Assault Cube and join a map
2. Inject `cube7.dll` into `ac_client.exe` using your injector (e.g. cube6)
3. ESP overlay appears over the game
4. Press **F2** to toggle infinite ammo

## Notes

- Assault Cube must be running before injection
- Requires Administrator if injecting into a protected process
- For educational use only
