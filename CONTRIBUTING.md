# Contributing

## Conventions

* **Code comments in English.** Log text is German ASCII (`ae/oe/ue`, no umlauts) for historical reasons — the maintainer reads the logs — except the `[cmd]` replies, which are English because the admin panel shows them. Keep both as they are; a mixed log is harder to grep.
* **Every new behaviour gets a switch.** Proven fixes ship ON with a `-no<feature>` switch; unproven ones ship OFF with a `-<feature>` switch. Add the field to `struct Config`, the parser line in `ParseCommandLine()`, and a row in `NOTES.md § Switches`.
* **Bump the version** in the banner string in `Main()` (`[+] sp_listen vNNN (...)`) for every change that is built and tested. A "still broken" report on an unrebuilt DLL looks exactly like a failed fix; the first log line is how we tell.
* **Addresses:** everything lives in `namespace RVA` / `namespace OFF` (section 2). Take offsets from the Preservation SDK (`metadata.json`) where possible and note the source in the comment. Never guess an offset from another UE 4.25 game — this build has a shuffled property layout.
* **Never write** `Character+0x3A4`, never set `NetDormancy` to `DormantAll`/`DormantPartial` on a dynamic actor, never call `FlushNetDormancy`/`ForceNetUpdate` on a `DORM_DormantAll` actor (host crash, see `docs/history`). The guards in section 19 stay on.
* Keep the panel wire format: `[cmd] <id> ok|fail <text>` and `[gg] #n t=<unix> CommitRequest[CurrencyGain] (...)` are parsed by the lobby tool.
* Structure: one concern per numbered section; hot paths (`GetNetMode` ~6,000 calls/s, `ProcessEvent`, the graph per-actor loop) must not resolve names — use the `FnVerdictLookup` pointer caches.

## The testing loop

1. Edit `src/sp_listen.cpp`, build (game closed), start the host with `host_fpp.bat`/`host_tpp.bat`, check the banner line.
2. Have at least one remote player join and do the thing you are testing on purpose (kick a door, shoot a window, reload, watch the zone from the lobby on).
3. Read `sp_listen.log` on the host and `BravoHotelGame.log` on the player. Add `-rpcnames` to `SPARGS` for a diagnostic round; take it out again afterwards.
4. Every `[ls]` line carries all counters; compare against a known-good round before concluding anything.

Compile check without Windows: `clang++ --target=x86_64-w64-mingw32 -fms-extensions -std=c++17 -O2 -DNDEBUG -Wall -Wextra -c src/sp_listen.cpp` (SEH is supported); link test with `-shared -lpsapi`. The source should compile with zero warnings.

## What not to commit

`.gitignore` covers it, but to be explicit: no `sp_listen.log`/`BravoHotelGame.log` (player names, UIDs), no `loadouts.txt` (per-player data from the backend), no `sp_listen_cmd.txt`, no `host-token.txt` or `config.json` from the backend, no built `.dll`, no game files. If you need to quote a log in an issue, strip names and UIDs.

## Where to start

`docs/ORIENTATION.md` explains the two mechanisms every bug comes back to (the standalone mask and the replication graph). `NOTES.md § Open issues` lists the current problems with what is known and the concrete next step for each. `docs/history/` has the long investigation log (v42–v169) — partly superseded, still the place where most findings were first made.
