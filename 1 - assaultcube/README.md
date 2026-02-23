# AssaultCube - Hello World of Game Hacking

Small demonstration that shows how to find a running game window, open its process, and read/write a single memory address (ammo) on Windows.

What this demo does
- Locates the game window by title (`"AssaultCube"`)
- Gets the process ID and opens the process with appropriate access
- Reads the current ammo value and repeatedly overwrites it with `newValue`
- Press F1 to exit the program

Build & run
- Open the `assaultcube` project in Visual Studio and build (Windows only).
- Run the compiled binary as Administrator.
- Start the game first, then run the program.

Configuration
- Edit `main.cpp` and update the `ammoAddress` value to match the address you find with your memory tool (addresses change per version/session).

Notes & warnings
- Educational/demo code only. Do not use to cheat in online games.
- Requires Administrator privileges to open other processes.
- Memory addresses are specific to a game build and may be unsafe to use without understanding.

Files
- `main.cpp` - the demo program.

Tools used
- `AssaultCube` - the game used for testing and demonstration.
- `Cheat Engine` - memory scanner/inspector used to locate addresses to read/write.
- `Visual Studio Code` - recommended editor for editing the source; you can also use Visual Studio to build the project.
