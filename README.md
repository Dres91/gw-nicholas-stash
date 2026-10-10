# Nicholas Stash

A small add-on for Guild Wars that works next to [GWToolbox++](https://github.com/gwdevhub/GWToolboxpp).

GWToolbox++ already shows, on the items **Nicholas the Traveler** collects, how many he wants and when.
Nicholas Stash adds the other half to that same tooltip: **how many you already have, and where they are.**

```
You have 27 of these:
  Backpack, slot 3: 10
  Xunlai storage pane 2, slot 14: 17
```

It covers the weekly Nicholas the Traveler items, Nicholas Sandford's pre-Searing items and the crafting
ingredients, 151 entries in total (the list is in [`src/NicholasStash.cpp`](src/NicholasStash.cpp)).

Xunlai storage can only be read in towns and outposts. Elsewhere the tooltip shows what the add-on last saw in
storage and says so ("as last seen in a town").

## It is a separate DLL, not a Toolbox plugin

You load it from **GW Launcher's mods tab**, the same way you load GWToolbox++. It does not go into Toolbox's
plugins folder. It needs GWToolbox++ to be loaded as well, because it borrows Toolbox's copy of GWCA
(`gwca.dll`) to read your items. Without Toolbox it quietly does nothing.

## Status

Works in game for its author (Windows, GW Launcher, GWToolbox++ 8.34). **Community review is welcome**:
please read the code before using it and open an Issue about anything odd. It is one source file.

## What it does and does not do

- **Reads only.** It looks at your inventory bags, Xunlai storage panes and material storage.
- **Never** moves, sells, salvages, uses or drops items, sends anything to the game server, or automates anything.
- **No network access and no files written.**
- It adds one block of text to the item tooltip by hooking the game's "get item description" function on top
  of the hook GWToolbox++ already has there. It only does so once it has confirmed that hook is Toolbox's.
- If anything it needs is missing or unexpected, it does nothing instead of guessing. After an unexpected error it
  switches itself off for that session.

## Installing

1. Download `NicholasStash.dll` from the latest [release](../../releases). Compare its SHA-256 with the
   `NicholasStash.dll.sha256` file next to it (in PowerShell: `Get-FileHash NicholasStash.dll`).
2. Put it in a folder of your choice (not inside Guild Wars' or Toolbox's own folders).
3. In GW Launcher, open the **mods** tab, add `NicholasStash.dll`, and keep GWToolbox++ selected too.
4. Start the game. A few seconds after login, hover a Nicholas item in a town.

To remove it, untick it in the mods tab or delete the file.

## How to verify the download

Release files are built **by GitHub Actions from this repository's source**, not on anyone's PC.
See [`.github/workflows/build.yml`](.github/workflows/build.yml). The Actions run log shows the build, and the
checksum is computed in that run. You can also build it yourself and compare behaviour (builds are not
byte-for-byte identical across machines).

## Building it yourself

Needs Windows, Visual Studio 2026 Build Tools (C++ workload, x86) and CMake. Guild Wars is a 32-bit program, so
the DLL must be 32-bit.

```
cmake -S . -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

The result is `build\Release\NicholasStash.dll`. It only depends on `KERNEL32.dll`. The only outside code is
[MinHook](https://github.com/TsudaKageyu/minhook) v1.3.4, pinned to an exact commit in
[`CMakeLists.txt`](CMakeLists.txt) and linked in statically.

## Known limits

- It reads Toolbox's `gwca.dll` as shipped with Toolbox 8.34. Other Toolbox versions may work or may not.
  If the needed functions are missing, it does nothing.
- If you fully close and re-open Toolbox in the middle of a session, the add-on stays off until you restart the
  game.
- Toolbox's "hide item descriptions" setting does not hide the extra lines on Nicholas items.
- After switching accounts without visiting a town, the storage lines can still show the previous account's
  storage until you visit a town.

## Disclaimer

Third-party mods are not supported by the GWToolbox++ team and are not endorsed by ArenaNet. Use at your own
risk. Not affiliated with ArenaNet or the GWToolbox++ team.

## License

MIT, see [LICENSE](LICENSE). MinHook is under its own BSD licence.
