# cube6 - DLL Injector

Console application that injects DLLs into running processes. Supports process selection by PID, process name, or window title, with architecture validation and multiple injection methods.

## Features

- **Process selection** - Inject by process ID, by executable name (e.g. `ac_client.exe`), or by window title (e.g. `AssaultCube`)
- **Injection methods** - CreateRemoteThread (default) or NtCreateThreadEx
- **Architecture validation** - Prevents 32/64-bit mismatch; must build injector for same arch as target
- **Interactive mode** - Run with no args for a menu-driven interface
- **CLI mode** - Full command-line support for scripting

## Build

1. Open `6/cube6.slnx` in Visual Studio (or open the folder)
2. Build **Release | Win32** to inject into 32-bit processes (e.g. Assault Cube)
3. Build **Release | x64** to inject into 64-bit processes
4. Output: `cube6.exe` in the build output folder

## Usage

### Interactive mode (no arguments)

```
cube6.exe
```

Presents a menu:
- `[1]` List running processes
- `[2]` Inject - prompts for process (name or PID) and DLL path
- `[3]` Exit

### CLI mode

```
cube6.exe -p <PID> -d <dll_path> [-m crt|nt]   Inject by process ID
cube6.exe -n <process.exe> -d <dll_path>      Inject by process name
cube6.exe -w <window_title> -d <dll_path>     Inject by window title
cube6.exe -l                                   List running processes
```

| Option | Description |
|--------|-------------|
| `-p` | Process ID |
| `-n` | Process name (e.g. `ac_client.exe`) |
| `-w` | Window title (partial match, e.g. `AssaultCube`) |
| `-d` | Full path to the DLL to inject |
| `-m crt` | CreateRemoteThread (default) |
| `-m nt` | NtCreateThreadEx |
| `-l` | List processes only |

### Example: Inject cube7 ESP into Assault Cube

```
cube6.exe -w AssaultCube -d "C:\path\to\cube7.dll"
```

Or by process name:

```
cube6.exe -n ac_client.exe -d "C:\path\to\cube7.dll"
```

## Notes

- Requires Administrator privileges for most target processes
- Target and injector must match architecture (32-bit injector → 32-bit target)
- For Assault Cube and cube7, build cube6 as **Win32/x86**
- Educational use only
