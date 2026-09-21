# sp_listen — a listen server for SUPER PEOPLE (BravoHotel)

SUPER PEOPLE (internal name **BravoHotel**) is a 2021 Unreal Engine 4.25 battle royale whose official servers are gone. No dedicated-server binary was ever shipped — only the client. `sp_listen.dll` is a patch that is injected into the **shipping client** (`BravoHotelClient-Win64-Shipping.exe`, build `1.3.0.473797`) and turns it into a **listen server**: one player hosts, everybody else joins.

This repository contains the patch source, the build and host scripts, and every note we have on how the game's netcode behaves under the patch. It is meant to be picked up by other people — see [CONTRIBUTING.md](CONTRIBUTING.md) and [NOTES.md](NOTES.md).

> Status (September 2026, v174): a full round with several players works end to end — join, aircraft, loot, weapons, reload, attachments, blue zone, results/gold. Known open problems are listed in [NOTES.md § Open issues](NOTES.md#open-issues). Nothing here touches the game files on disk; everything is done in memory at runtime.

## How it works (one paragraph)

The client exe has all server code compiled in (it is a UE4 client target, so `UWorld::GetNetMode` is folded to *client-or-standalone*). The DLL redirects the map load into its own `Listen()` (creates a `UNetDriver`, calls `InitListen`), then hides the driver from the game logic (`World->NetDriver = null`, the "standalone mask") and shows it again only inside the engine paths that need it (connection accept, control messages, `TickFlush`, dormancy). Around that core sit ~40 targeted hooks: net-mode answers for remote players' actors, RPC callspace, dormancy, replication-graph relevance, blue-zone relevance, loot spawning around remote players, weapon state pushes, a gold/result ledger, a loadout injector and a small command-file interface for an admin panel. Every hook is a runtime patch on verified RVAs of build 473797; nothing is written to disk.

## Requirements

* The game files of SUPER PEOPLE build **1.3.0.473797** (`BravoHotelClient-Win64-Shipping.exe`, 126,454,064 bytes). Other builds will not work — every address in the source is specific to this exe.
* Windows x64, Visual Studio 2019/2022 with the C++ workload (for `cl.exe`). A cross-compile with `clang++ --target=x86_64-w64-mingw32 -fms-extensions` also compiles the source; the shipped `build.bat` uses MSVC.
* A DLL injector that loads `sp_listen.dll` into the game process at start. We use the `dxgi.dll` proxy loader with its `DList.ini` (see `tools/DList.ini`); any injector that loads the DLL before the first map load works.
* A backend for login/lobby is **optional**. Players can join a listen server directly by IP (see *Joining*). The backend the maintainer runs (login, lobby queue, match rewards) is a separate project and not part of this repo.

## Building

1. Copy `src/sp_listen.cpp` and `tools/build.bat` next to each other (the game folder is fine).
2. Open the **x64 Native Tools Command Prompt for VS**, `cd` into that folder and run `build.bat`. It compiles with `cl /LD /O2 /EHsc /std:c++17` and links `psapi.lib`.
3. `build.bat` copies `sp_listen.dll` into the game folder given by the `SP_GAMEDIR` environment variable, or leaves it next to the source if that is not set. The game must be closed while building — a running game keeps the DLL locked.
4. After the next start, the first line of `sp_listen.log` (written next to the exe) must show the version you just built: `[+] sp_listen v174 (...)`.

## Hosting

Put `dxgi.dll`, `DList.ini` and `sp_listen.dll` in the game's `Binaries\Win64` folder, together with `tools/host_fpp.bat` (first person) or `tools/host_tpp.bat` (third person). Then:

```
host_fpp.bat            # headless: -nullrhi, no renderer, no sound (dedicated-style host)
host_fpp.bat quiet      # tiny window, no sound
host_fpp.bat window     # normal small window (the host can also play)
```

What the script does: it starts the exe on `LV-OrbIsland?listen?AutoStart=1?StartingPlayerCountRate=0?StartingTimeSecond=30` with `-ApiPhase=dev2s -ServicePlatform=Steam -IgnoreCatalogue -log` and the patch switches `-loadout -solo -fpp -cmdfile`. The match starts automatically 30 s after the first player is in, or when the admin command file says so.

Networking: the listen server listens on UDP **7777** by default. Forward that port (or use a tunnel such as playit.gg) and give players the public address. The host needs upload bandwidth — set `MaxClientRate`/`MaxInternetClientRate` to 150000 in `Engine.ini` under `[/Script/OnlineSubsystemUtils.IpNetDriver]` for more than a handful of players (see `docs/history/HANDOFF-sp_listen.md`).

Patch switches (all parsed from the command line by the DLL; the full table is in `NOTES.md § Switches`):

| switch | effect |
|---|---|
| `-fpp` / `-tpp` | force the round to first / third person (written into the GameState) |
| `-solo` | not read by the DLL — read by the lobby tool (`listen-alive`) together with `-fpp`/`-tpp` to advertise exactly that mode |
| `-loadout` | inject gold/level/outfit from `loadouts.txt` into each join (backend-driven) |
| `-cmdfile[=path]` | read admin commands from `sp_listen_cmd.txt` next to the exe (`cheatable`, `StartGame ...`), reply in the log as `[cmd] <id> ok|fail <text>` |
| `-rpcnames` | diagnostics: log every distinct RPC once (`[rn]`) — use when reporting a bug |
| `-no<feature>` | every fix has an off switch; see NOTES.md |

## Joining

Players need the **unmodified** game (no DLL). Two ways in:

1. **Direct connect.** Start the client normally (`-ApiPhase=dev2s -ServicePlatform=Steam -IgnoreCatalogue`), open the console and type `open <host-ip>:7777`. The patch ignores the encryption token in the `NMT_Hello` handshake, so a plain `open` works. If your build has the console disabled, a shortcut with `<host-ip>:7777/Game/BravoHotel/Maps/OrbIsland/LV-OrbIsland` as the map argument does the same (that is how the lobby launches it).
2. **Through a private backend.** With the maintainer's backend the launcher logs the player in, the lobby queues him and the client is started with the listen server's address, UID and loadout options; `-loadout` on the host then applies gold/level/outfit and the host books the round's rewards (`[gg]` lines). That backend is a separate repository.

Players see the lobby, the aircraft, the map, loot, other players and the blue zone as usual. See *Open issues* for what still differs from an official server.

## Logs — what to send with a bug report

* `sp_listen.log` from the **host** (next to the exe). Its first line is the version, then one `[+]` line per installed hook, then a `[gs]` phase line on every phase change and an `[ls]` status line every 20 s with all counters.
* `BravoHotelGame.log` from the **player** (`BravoHotelGame\Saved\Logs`). It has repeatedly held the other half of the answer (HUD, RepNotify, movement warnings).

Both logs contain player names and UIDs; strip them before posting publicly. The host log never contains IPs.

## Layout

```
src/sp_listen.cpp        the patch (v174, ~4,300 lines, 21 numbered sections)
legacy/sp_listen_v169.cpp the last pre-restructure version, kept for reference
tools/build.bat          MSVC build + install
tools/host_fpp.bat       host scripts (first / third person)
tools/host_tpp.bat
tools/DList.ini          loader list for the dxgi.dll proxy injector
NOTES.md                 findings, per-version changes, open issues, next steps
CONTRIBUTING.md          conventions, the testing loop, security rules
docs/ORIENTATION.md      "read this first" for the mechanisms (mask, replication graph)
docs/history/            the older issue logs and findings (partly superseded, still useful)
```

## Credits

Reverse engineering, testing and the project itself: the maintainer. The class/field offsets used since v173 come from the reflection dump in the *Super People Preservation* project (an SDK generated from this exact build); see `NOTES.md § SDK`.

## Legal

This is a fan preservation effort for a game that can no longer be played on official servers. It patches the client in memory only; no game assets or executables are distributed here. You need your own copy of the game.
