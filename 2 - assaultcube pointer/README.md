AssaultCube Pointer Hack (cube2)

Description
- Simple Windows C++ pointer-hacking demonstration that locates the `AssaultCube` process and forces an in-game integer value (ammo) to a fixed amount.

Files
- `main.cpp` - program source. Reads the module base, resolves a pointer and writes a new integer value to the target address.
- `cube2.vcxproj.filters` - Visual Studio project filters (if present in the workspace).

Requirements
- Windows (Win32 API usage)
- Microsoft Visual Studio (recommended) or any toolchain capable of building Win32 C++ apps
- Administrator privileges to open another process with full access

Build
1. Open the Visual Studio project/solution in this folder and build normally.
   - If no solution file is provided, create a new Visual C++ Console project and add `main.cpp`.
2. Ensure the build target is a 32-bit or 64-bit binary matching the target game process architecture.

Usage
1. Start `AssaultCube` and make sure the game window title is `AssaultCube`.
2. Run the compiled executable as Administrator.
3. The program will print the module base and attempt to apply the pointer hack.
4. Press `F1` to quit.

Configuration
- Pointer/base values are defined in `main.cpp`:
  - `staticOffset` -base pointer offset from `ac_client.exe`.
  - `offsets` -additional offsets used to resolve the final dynamic address.
- Modify these values if the target game version changes.

Notes
- Use this code only on systems and software you own or have permission to modify.
- Modifying other processes may be detected by anti-cheat or violate terms of service.

License
- GPLv3. See LICENSE file for details.
