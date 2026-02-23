AssaultCube Trainer

Small Windows console trainer that demonstrates reading and writing memory of the `ac_client.exe` process.

Important
- This code is provided for educational purposes only. Do not use it to break game rules, cheat in multiplayer, or violate any EULA/ToS.
- Offsets are specific to AssaultCube v1.3.0.2 and will change between versions.

Build
- Open the `cube4` project in Visual Studio (Windows).
- Build as a Win32 or x64 console application depending on your target.

Usage
- Start `ac_client.exe` (AssaultCube) first.
- Run the built trainer executable as administrator if necessary.
- Trainer output shows player position and health in the console.
- Press `F1` to set health to 9999.
- Press `END` to exit the trainer.

Notes
- The trainer locates the game module base and uses a hardcoded base-pointer + offsets to read/write the local player's data.
- If the game is updated, offsets must be rescanned and updated in `main.cpp`.

Files
- `cube4/main.cpp` - main trainer implementation.

License
- GPLv3 or later. See LICENSE file for details.
