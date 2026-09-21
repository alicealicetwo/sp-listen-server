# sp_listen — handoff (13.09.2026, state after v88)

Paste or attach this at the start of the new chat. The long history lives in
`ISSUESsp_listen.md` (issue table + one section per version) and in the source
header of `sp_listen.cpp`, both in the game folder.

## Setup

- **Project**: `sp_listen.dll`, a listen-server patch for the UE4 client
  `BravoHotelClient-Win64-Shipping.exe`, build 1.3.0.473797, ImageBase
  `0x140000000`. Injected through `dxgi.dll` / `DList.ini`.
- **Host** = Alen's laptop, **client** = his PC.
- **Everything lives in**
  `<GAME_DIR>`
  — exe (plus a **zipped** copy for static analysis), `sp_listen.cpp`,
  `build.bat`, `host.bat`, `host_nullrhi.bat`, `sp_listen.log`,
  `ISSUESsp_listen.md`, `NATIVE-RVAS-from-handoff.md`,
  `STATIC-FINDINGS-ammo.md`, and the `SP-hand-off-main` folder.
- **Build**: Alen runs `build.bat` from that folder in an x64 Native Tools
  prompt. The game must be closed — the DLL is locked while it runs.
- **Workflow**: Claude edits `sp_listen.cpp` and writes it back into that folder;
  Alen builds and plays a round; Claude reads `sp_listen.log` from the folder.
- **Conventions**: code comments English, log text German in ASCII (ae/oe/ue),
  replies to Alen in English.
- **Trap that already cost a round**: check the first line of `sp_listen.log`
  against the version just delivered. A "still broken" report on an unrebuilt
  DLL looks exactly like a failed fix.

## Working (confirmed in play)

Floor loot everywhere on the map, blue zone, the world around the remote player,
the client's camera/spawn, rubber-banding, the vault-out-of-map teleport, and
— since v88 — **the magazine / reload display (#15)**.

## #15 — SOLVED in v87/v88

### What the fix is

Three pieces, all in `sp_listen.cpp`:

1. **`Magazine` is `weapon+0xDD0`** (`CPF_Net | CPF_RepNotify`), not `+0x1B0`.
   Also `BackPackInTotalAmmoCount` `+0xE50`, `MagazineCapacity` `+0x1A28`.
   Found via the native reflection table in `.data` (names obfuscated
   `byte[i] ^= (0xDD + i)`); see `STATIC-FINDINGS-ammo.md`.
2. **`ClientSetMagazine` is a real `NetClient` RPC** — `FunctionFlags 0x1020CC0`.
   `FindClientSetMagazine()` walks the weapon's SuperStruct chain, finds the
   UFunction, verifies `FUNC_Net | FUNC_NetClient`, and `PushMagazineToClient()`
   only sends when `GetFunctionCallspace` returns Remote.
3. **The host never decrements the magazine** — that write lives in Blueprint and
   does not run server-side. `SpendOneRound()` does it instead: one round per
   `ServerFireProjectile` on the tracked weapon. The `[mag]` watcher then pushes
   the new value out via `ClientSetMagazine`.

### Proof (v88 log, shotgun, capacity 5)

```
[ms] Schuss-RPC kommt vom Waffen-Objekt
[ms] Schuss gezaehlt: Magazin 5 -> 4 ... 2 -> 1 ... 1 -> 0     15 shots
[mp] ClientSetMagazine(4) ... Callspace 1                      18 pushes
```

15 `ServerFireProjectile`, 15 rounds counted, 1:1. Three weapon switches in that
round (`K2_OnUnEquip` → `K2_OnEquip`) and the magazine held its value across
every one — 3/3, 5/5, 4/4. That was the headline symptom and it is gone.

`weapon+0xE50` reads 0 on the host all round, but Alen confirmed the client's
backpack ammo **does** go down, so the host-side copy is simply never written.
Cosmetic on the host; do not "fix" it.

### Two traps this issue set, worth remembering

- **v86 put `SpendOneRound()` inside the `[pe]` log budget.** `ServerDoSprinting`
  burned 164 of 250 lines during the drop, so counting silently stopped before
  the first shot. Never gate behaviour behind a diagnostic budget.
- **v62 declared `ClientSetMagazine` "not an RPC"** from FunctionFlags read off
  the wrong structure. The SDK disproved it. That cost roughly six versions.
  Verify a flag read against a second source before building on it.

### Corrections to the old handoff

`weapon+0x1B0` is **not** the ammo counter. It has no reflection entry and is not
replicated. The `-magtest` round that "accepted" a 99 written there was reading
noise. Anything in `ISSUESsp_listen.md` resting on `+0x1B0` is void.

## Other open issues

- **#14** some doors won't open for the client.
- **#13** windows don't break for the client, no break sound.
- **#16** perk UI in the aircraft updates late.
- **#8** movement is buggy while waiting for the match to start.
- **#20** a ping made on the PC shows up for the **host** instead.
- **Host crash**, unresolved: `AV reading 0x00000040`, 6+ occurrences, identical
  callstack, entirely inside engine code —
  `UWorld::Tick → TickDispatch → UActorChannel bunch → BP VM → UNetDriver
  0x43D7960 → [driver+0x1448]->vtable[0x2A0] → spatial node → DORM_DormantAll
  branch → null per-connection pointer`. Predates all recent changes; the one
  fix attempt (v69 spatial guard) made it *more* frequent.

#13/#14/#16 share a root cause: the replication graph gathers only non-spatial
actors. The GridNode is registered, in the PrepareForReplication list, asked
every frame, viewpoint correct to 3 m, grid 220→255 columns, pending lists
drain, `ConnectionMaxZ` = 2097152 — and it still returns nothing spatial. That
is the next real target.

## Reverted experiments — do not re-open without new evidence

| Version | What it did | How it died |
|---|---|---|
| v67/v68 | weapon `bAlwaysRelevant` | picked-up weapons vanished, host crashed more |
| v69 | spatial guard | crashes got *more* frequent |
| v71 | `URealReplicationGraph` | class is **abstract** — "Pure virtual not implemented" |
| v73 | `UActorChannel::ReplicateActor` from our tick | needs the graph's per-connection context; returns 0, then throws |
| v78 | held-weapon graph Remove/Add | broke the blue zone and pushed the player through the floor |
| — | commenting out `ReplicationDriverClassName` in Engine.ini | infinite loading screen. **Both lines must stay.** |

## Rules learned the hard way

1. **Never write the result field of an engine state machine — only the decision
   in front of it.**
2. **Never call an engine function from your own tick if it assumes a context
   only its own pass sets up.**
3. **`IsObjectAlive` is useless in this build** — use `MaybeAlive()`.
4. **Read memory in chunks**, never one big `memcpy` in one `__try`.
5. **Measure before intervening.** Every theory that died, died to an
   instrument, and each one cost a round of Alen's time.
6. **Never gate behaviour behind a log budget** (v86).

## Logging

`MAX_LOG_LINES` = 30000, plus a per-tag quota of 400 lines so no single chatty
instrument can starve the rest. Ammo tags are exempt: `[ms] [mag] [mp] [pe]
[am] [ow] [wk] [rs]`. A throttled tag announces itself once with
`[!] Tag [xx] hat sein Kontingent ... erreicht`.

## Useful switches

Ammo: `-nomagtrack` (stop counting shots), `-nomagpush` (stop sending
`ClientSetMagazine`), `-nopelog` (stop the ProcessEvent log).
Disabled experiments, off by default: `-weaponrelevant`, `-spatialguard`,
`-weaponalways`, `-magrep`, `-graphreal`, `-graphbasic`.
Older: `-noownrelevant`, `-noweaponwatch`, `-ownpoke`, `-dormawake`,
`-noownmode`, `-nopulltiles`, `-nohostfirst`, `-hostpoint`, `-ammolog`,
`-rpclog`, `-nozonerelevant`, `-noaircraftfix`, `-nospawnremote`, `-nodormfix`,
`-stateforce`, `-nodirectspawn`.

## Client-side note

`Engine.ini.client-lowspec.txt` in the game folder holds the low-spec settings
for Alen's iGPU laptop when it is the **joining** client (`sg.ViewDistanceQuality=0`
in GameUserSettings.ini plus `r.ViewDistanceScale=0.35`, `r.ScreenPercentage=70`
and shadow/foliage cuts in Engine.ini `[ConsoleVariables]`).
