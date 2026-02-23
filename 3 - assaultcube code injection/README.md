# cube3 (AssaultCube Unlimited Ammo Patcher)

This small Windows utility patches the running `AssaultCube` process to temporarily replace a 2-byte instruction with NOPs (unlimited ammo). It locates the game's main module, reads the original bytes at a fixed offset, and allows toggling the patch at runtime.

Important: modifying another process's memory can be detected by anti-cheat software and may violate the game's terms of service. Use this code only for educational purposes on copies of the game you own and in offline environments.

## Features
- Find the `AssaultCube` window and open its process.
- Compute a code address using the module base + offset.
- Replace 2 bytes with NOPs to enable unlimited ammo.
- Restore original bytes when disabled or on exit.

## Build
- Open the included Visual Studio project or create a new Win32 console project.
- Add `main.cpp` to the project.
- Build as a 32-bit application (the target game is typically 32-bit).

## Usage
1. Start `AssaultCube`.
2. Run the built executable as an administrator (required to open another process with write permissions).
3. Press `F1` to enable unlimited ammo (writes NOPs).
4. Press `F2` to restore the original instruction.
5. Press `END` to exit the program. The original bytes are restored on exit if the patch is active.

## Notes
- Make sure the process and module names match the running game binary.
- This repository is for educational purposes only. Respect software licenses and game rules.
