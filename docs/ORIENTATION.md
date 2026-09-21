# sp_listen — how this works, written for a fresh AI session

Read this first, then `OPEN-ISSUES.md`, then `FINDING-dormancy-gate.md`.
`HANDOFF-sp_listen.md` has the setup details. `ISSUESsp_listen.md` is the long
historical log — useful, but large and partly superseded; trust this file and
`FINDING-dormancy-gate.md` where they disagree.

---

## 1. What the project is

SUPER PEOPLE (internal name **BravoHotel**) is a 2021 UE4 battle royale whose
backend is dead. the maintainer owns the game and is running private matches by injecting
a DLL into the **shipping client** and making it act as a listen server. There
is no dedicated-server binary — only the client exists, so everything is done by
patching the client at runtime.

- Target: `BravoHotelClient-Win64-Shipping.exe`, build **1.3.0.473797**,
  ImageBase `0x140000000`. **Every address in these documents is an RVA** —
  add the loaded image base.
- The patch is `sp_listen.dll`, a single ~9000-line `sp_listen.cpp`.
- Injection: `dxgi.dll` + `DList.ini` in the game folder.
- **Host** = the maintainer's host machine (runs `host.bat`). **Client** = a second machine (joins).
- Everything lives in
  `<GAME_DIR>`

---

## 2. The working loop

1. You edit `sp_listen.cpp` and write it back into that folder.
2. the maintainer runs `build.bat` in an x64 Native Tools prompt. **The game must be
   closed** — the DLL is locked while it runs.
3. He plays a round.
4. You read `sp_listen.log` from the same folder.

**Check the first line of `sp_listen.log` against the version you just shipped.**
A "still broken" report on an unrebuilt DLL looks exactly like a failed fix.
This has cost real rounds.

The client's own log (`BravoHotelGame.log` on the PC) is the other half of the
picture and has repeatedly held the answer — it showed the corrupt pak, the
missing perk DataTable rows, and the HUD init ordering. Ask for it.

**Conventions**: code comments in **English**; log text in **German, ASCII only**
(ae/oe/ue); replies to the maintainer in **English**.

---

## 3. The one mechanism you must understand: the standalone mask

sp_listen nulls `World->NetDriver` (+0x58) and the LevelCollections during game
logic, so the game believes it is in single player. It unmasks inside its own
windows — `MyTickFlush` calls `MaskOff()`, runs the engine's tick, does its work,
then `MaskOn()`.

This mask is the root of more bugs than anything else, because a great deal of
engine code reads `World->NetDriver` directly and silently takes the
single-player branch when it is null:

- `AActor::GetNetMode` (`0x3F92730`) reads it → every actor thought it was
  standalone → fixed by hooking it (v42).
- `AActor::FlushNetDormancy` (`0x3F891D0`) reads it via `GetNetDriver`
  (`0x3F8E300`) → dormancy wake-ups became silent no-ops → see
  `FINDING-dormancy-gate.md`.
- `ForceNetUpdate` likewise.

**When something "just doesn't happen" and there is no error, ask whether that
code path reads `World->NetDriver`.** That single question has produced most of
the real findings.

---

## 4. The replication picture, as actually measured

The game uses a replication graph (`UBasicReplicationGraph`). Its global nodes
are a `GridSpatialization2D` and an `ActorList` (always-relevant). Per
connection there is a `TearOff_ForConnection` and an
`AlwaysRelevant_ForConnection`.

Verified by disassembly, not assumption:

```
GridSpatialization2D::Gather (0x13B12F0)
    for each viewer:
        if (node[0x64] /*ConnectionMaxZ, float*/ < viewer.Z) skip
        CellX = (X - Bias.X) / CellSize        // node +0x5C, +0x58 (floats)
        CellY = (Y - Bias.Y) / CellSize        // node +0x60
        Cell  = Grid[CellX][CellY]             // Grid at node+0x210, Num +0x218
        if (Cell) Cell->vtable[0x288](Cell, Params)

GridCell::Gather (0x13AF990)
    if (this[0x68] > 0) Out.Add(this[0x58]);            // own list
    StreamingLevelCollection(this+0x70)                  // 0x13AF790
    for (child in this[0x30]) child->Gather(Params)      // AllChildNodes, count +0x38

ActorListFrequencyBuckets::Gather (0x13AFA50)
    idx = FrameNum % this[0xA8];      // NumBuckets
    Out.Add(&this[0xA0][idx * 0x18]); // exactly ONE bucket per frame, by design
```

Gather parameters: viewers `+0xC8` / count `+0xD0` (`FNetViewer` is `0x30`
bytes), **output object `+0xE8` with its list counter at `+0x50`**,
`ClientVisibleLevelNamesRef` at `+0xF0` (**a reference — dereference twice**).

The grid is healthy: ~1200 occupied cells, correct bias and cell size,
`ConnectionMaxZ` fine, the player's own cell present. The graph **does** gather.

---

## 5. Rules learned the hard way

Each of these cost at least one broken round.

1. **Never write the result field of an engine state machine — only the decision
   in front of it.** Broken by raw tile writes (v58h) and the `DORM_Awake` write
   that crashed the host (v59).
2. **Never call an engine function from your own tick if it assumes a context
   only its own pass sets up.** The `0x40` crash is the graph's
   `DORM_DormantAll` branch dereferencing a per-connection pointer that exists
   only inside the per-connection pass.
3. **`IsObjectAlive` is useless in this build** (`FUObjectItem`'s object pointer
   is at +8 and obfuscated) — use `MaybeAlive()`. A liveness check silently
   disabled every instrument for three builds.
4. **Read memory in chunks**, never one big `memcpy` in one `__try`: an object
   ending before a page boundary makes the whole read fail and the watcher
   vanishes silently.
5. **Measure before intervening.** Every theory that died, died to an
   instrument.
6. **Never gate behaviour behind a diagnostic budget.** v86 put the shot counter
   inside the `[pe]` log cap; it stopped counting after the drop and looked like
   a failed fix.
7. **Measure the object the code writes, not the one you were handed.** `[gg]`
   snapshotted the *parameter block* from v77 to v92 while the gather counts
   into `Params[0xE8] -> [0x50]`. About fifteen versions of "the graph gathers
   nothing" measured memory the gather never touches. **The premise was false
   and five experiments were built on it — v67, v69, v71, v73, v78, every one of
   which broke the game.**
8. **A whitelist written for one hunt will silently break the next.** The `[pe]`
   filter was written for ammo words and later dropped every door and window
   RPC. The door class table filled with 16 door *frames* and never admitted a
   window class. Lifecycle events (`ReceiveTick`, `ServerMove`,
   `ReceiveBeginPlay`, join chatter) have flooded a log budget **four separate
   times**. Use two budgets, or a blacklist plus a generous cap, and check what
   actually filled the log before blaming the test.

---

## 6. Working with the maintainer

- He tests on real hardware and each round costs him real time. A build that
  produces an empty log because of your own filter is a wasted round — this has
  happened four times, and he has (rightly) pushed back on it.
- He will tell you plainly when something broke. Believe him and check your own
  instrument before concluding his test was wrong. When he said "I literally did
  what you said", he had — three blind spots in the instrument had made his
  test invisible.
- He prefers being handed a working file over being asked questions. Ship the
  build, name the undo switch, say what to look for.
- **Always give an undo switch** for any behaviour change, defaulted so that a
  bad build costs one flag rather than a reinstall.

---

## 7. Instrument tags in `sp_listen.log`

| Tag | What it shows |
|---|---|
| `[+]` | version banner — **check this first** |
| `[ms]` | shots counted, magazine decrement |
| `[mag]` | magazine/backpack/capacity on change |
| `[mp]` | `ClientSetMagazine` pushes |
| `[pe]` | ProcessEvent on the remote pawn/weapon (whitelisted) |
| `[dw]` | DORM_Initial → DORM_Awake (v96 fix) |
| `[dm]` | FlushNetDormancy calls and v62 refusals |
| `[gx]` `[gt]` `[gc]` `[gn]` `[gg]` | grid, cell census, node internals, gather |
| `[cz]` `[ch]` `[bk]` `[sl]` `[cv]` | cell interior, children, buckets, streaming levels, visible levels |
| `[rs]` | which actors the graph actually replicates |
| `[ps]` | PlayerState census |
| `[pill-data]` `[pill-consume]` `[loot-pool]` `[loot-repair]` | capsules |

The logger has a global cap of 30000 lines **and a per-tag quota of 400**, so no
single chatty instrument can starve the rest. Ammo/diagnostic tags are exempt.
A throttled tag announces itself once. If a tag is missing from a log, check
whether it hit its quota before concluding it never fired.

---

## 8. Where to look things up

- **Addresses and offsets**: the tables at the end of `FINDING-dormancy-gate.md`,
  and `NATIVE-RVAS-from-handoff.md`.
- **Ammo**: `STATIC-FINDINGS-ammo.md` — including the native reflection table in
  `.data` whose names are obfuscated with `byte[i] ^= (0xDD + i)`. That method
  is how `Magazine` was found and is reusable for any game-native property.
  Engine (`AActor`) properties are **not** in that table — they are plain
  UTF-16 strings in `.rdata`.
- **Pills**: `PILLS-WORKING-HANDOFF.md` inside `SP-hand-off-main.zip`, with a
  full working source snapshot under `source/pills-working/`. This is a
  high-quality external document: it named three failures precisely, shipped the
  code, and its RVAs verified exactly against this exe.

**Verify any address before you build on it.** A zipped copy of the exe sits in
the game folder; `pefile` + `capstone` in the sandbox is enough. Checking that an
RVA lands on a function start takes seconds and has caught real errors — and
one wrong flag read (`ClientSetMagazine` "is not an RPC", v62) cost roughly six
versions before the SDK disproved it.
