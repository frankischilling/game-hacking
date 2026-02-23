# Game Hacking Demos

A collection of small Windows-based memory-editing demos intended for learning how process memory, the Win32 API, and basic read/write techniques work. Each demo is self-contained in its own folder. All target **AssaultCube v1.3.0.2**.

## Repository layout

| Folder | Demo | Description |
|--------|------|-------------|
| `1/cube/assaultcube/` | Hello World | First demo: read/write ammo in the running AC process |
| `2/cube2` | cube2 | Pointer-chain hack that writes ammo via base + offsets |
| `3/cube3` | cube3 | Unlimited ammo patcher — NOPs a 2-byte instruction |
| `4/cube4/` | cube4 | Trainer: read position/health, press F1 to set health |
| `5/cube5/` | cube5 | ESP overlay: DirectX 9 boxes + name labels over players |

## Conventions

- Each project directory should include a short README describing what it does, build instructions, and any configuration notes.
- Projects target Windows and usually require Administrator privileges.
- Memory addresses in examples are illustrative and must be replaced with addresses found on your system.

## Legal & ethical

- These examples are for education and research only. Do not use them to gain unfair advantage in multiplayer games or to violate terms of service.
- Always respect software EULAs, server rules, and applicable law.

## Contributing

- Add a new folder for your demo, include source files and a project README describing build/run steps and any safety notes.
- Do not include game binaries or copyrighted assets.
