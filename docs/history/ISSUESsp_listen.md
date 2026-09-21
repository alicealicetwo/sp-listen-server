# sp_listen — Issue list (as reported by Alen in testing)

Build under test: sp_listen **v59d** (relevance is disproved — the graph sees the weapon 0.6 m from the player; now logging who closes the weapon's channel thirteen times a round, plus `-magtest`, a single write that says whether `+0x1B0` is replicated at all). Previous: **v59c** (the magazine is `weapon+0x1B0` / `+0x1B4` — `[mag]` names it per change; `Pawn+0x648` is a `PlayerInventoryComponent`; now measuring whether the weapon is even relevant to the client's connection and zeroing the graph's per-actor cull copy for the remote player's own actors). Previous: **v59b** (the DORM_Awake write crashed the host the first time it ever ran — off by default now; field window widened to 0x2000 and a third watcher `[wh]` on `Character+0x648`). Previous: **v59** (`IsObjectAlive` returns false for every object in this build and silently switched off everything v58w–v58z added — including the DORM_Awake fix, which had therefore never run). Previous: **v58z** (the weapon is DORM_DormantAll — that is #15; sleeping owned actors are set to DORM_Awake, and the field watcher is repaired). Previous: **v58y** (the host crash is named at last: the graph's DORM_DormantAll branch needs a per-connection context we do not have; the poke is off by default). Previous: **v58x** (handoff mined: 1368 native names→RVAs recovered, the weapon now comes from the game itself, DoReload hooked). Previous: **v58w** (host crash on opening a door fixed — stale actor pointers in the v58t poke list; the magazine measurement stays in). Previous: **v58v** (delivery proven fine; now measuring where the magazine count lives and whether it ever leaves the host). Previous: **v58u** (the remote player's weapon, inventory items and PlayerState finally answer NM_ListenServer instead of NM_Standalone — that is why nothing the host does to them ever reached the client). Previous: **v58t** (floor loot confirmed working; now the ammo counter and the perk UI — the remote player's own actors are kept replicating). Previous: **v58s** (the player on the ground is the primary streaming point, not the host in the aircraft — the actual difference between standalone and the listen server). Previous: **v58r** (no gate ever blocked anything — there are no loot sources out on the map; the host's levels are HLOD proxies, so the tiles around the remote player are now pulled in by calling the engine's own commit). Previous: **v58q** (+0x534 was never a "has spawned" flag; the six gates in front of the spawn are measured and bypassed; the tile under the landed player is loaded blocking). Previous: **v58p** (no loot before the round starts — the phase lift from v58l is off, both spawn paths wait for match phase 4). Previous: **v58o** (loot sources found the way the game finds them, and fired directly). Previous: **v58n** (phase theory disproved; logging the code path that registers pickups). Previous: **v58m** (proof logging for loot spawned before match phase 4). Previous: **v58l** — loot confirmed working while the host was still in the air (12.09.), but in a round where the phase had already reached 4 on its own. Previous: sp_listen **v58l** (v58k's wake-up confirmed working; match-phase gate lifted for the remote player's loot check). Previous: sp_listen **v58k** (DORM_Initial identified as the mechanism; dormant actors around the remote player are woken). Previous: sp_listen **v58j** (streaming proven innocent; per-class counting of the three replication gates). Previous: sp_listen **v58i** (v58h's raw tile writes removed — they were thrashing the streaming state machine; commit hook + streaming-state diagnosis + memory log instead). Previous: sp_listen **v58h** (v58g + detail tiles around the remote player forced to full LOD + host/remote census). v58g round: viewpoint proven correct, the host simply has no world around the landed remote player — see the v58h section. Previous: sp_listen **v58g** (v58f + FNetViewer viewpoint log/fix + actor census around the remote player). v58f round: loot spawns on the host but never reaches the client — see the v58g section. Previous: sp_listen **v58f** (v58e + BeginPlay net-mode gate of the spawn timers forced to Standalone + level-walk decode fixed + spawned-list detection). v58e round (12.09.): **zone works again** (self-heal fired once), loot still missing far from the host — cause found (see v58f section). Previous: v58e (v58d + zone self-heal fixed + level-walk loot sweep + graph-remove diag). v58d round (12.09. 08:50): zone regressed (Alen), loot/windows unchanged. Previous: v58d (v58c + sub-point streaming scale + tile diagnostics). v58c round (11.09. 20:02): **blue zone confirmed working on the PC** (#10/#11/#12 FIXED). Previous: v58c (v58b + dormancy wake-ups through the mask + zone self-heal + loot gate). Previous: (v58 + graph cull copy zeroed for the zone actor + loot check via remote PlayerController). Host = laptop, client = PC. First v58 round (11.09. 19:30): no crash; zone still missing on the PC, floor weapons not visible — both explained below.
Status legend: **OPEN** · **FIXED** (confirmed in a test round) · **FIX PENDING** (implemented, not yet confirmed)

| # | Issue (as reported) | Where | Status | Notes / what was done |
|---|---|---|---|---|
| 1 | Joining client has UI but the camera is stuck under the map | PC | FIXED | ClientRestart / ClientRetryClientRestart were never routed to the client; RPC callspace bits were inverted (v37–v44). Fixed in v45. |
| 2 | Host crashes as soon as the airplane is flying | Laptop | FIXED | Crash-dump replay recorder (`cr.UseDumpCrashRecordingReplay`). `StartRecordingReplay` neutered in v43. |
| 25 | **Host crashed when the PC player opened a door** (12.09., v58v and v58x rounds, identical callstack) | Laptop | FIXED (v58y — v58w's attempt was wrong) | Access violation reading 0x40. **Not a stale pointer** — that was v58w's wrong guess. The graph's `DORM_DormantAll` branch dereferences the per-connection context at `+0x1D8`, which only exists inside its own per-connection pass; our tick called into it from outside. v58y refuses dormancy 2 and 3 in both the poke and the v58k wake-up, and turns the poke off by default (`-ownpoke` re-enables). |
| 26 | **Host crashed a third time, during a reload** (12.09., v59 round) | Laptop | FIXED (v59b — my bug, again) | Same access violation as #25, reading address `0x40`, RVAs `0x11E820A, 0x13BADC9, 0x1394877, 0x13AB3F7, 0x13B9734, 0x43D7DEE` (base 0x7FF74FEC0000). Cause: the `DORM_Awake` write from v58z, which v59 made run for the first time. v58z argued `NetDormancy` was a harmless input field; it is the byte the crashing branch selects on, and the branch then dereferences the per-connection context that only exists inside the graph's own pass. The v59 log shows the thrash plainly — `[wp] +0x20D byte 0 -> 1` (us), `byte 1 -> 0` (the engine, same tick). This is the v58h rule for the third time: **never write the result field of an engine state machine, only the decision in front of it.** v59b defaults the write off; `-dormawake` re-enables it for a deliberate experiment. |
| 3 | **Host crashed again after the PC player walked around for a while** (11.09., first run with v56/v57) | Laptop | FIX PENDING (root cause found) | Crash is in the **garbage collector** (RVA 0x2C42147 = `TFastReferenceCollector::ProcessObjectArray`, token `GCRT_ArrayObject`). Cause: **our own v54 write** — 10.0f into `BlueZone+0x2E0`, which in this build is the data pointer of a `TArray<UObject*>` (AActor fields are shuffled; real net fields: NetUpdateFrequency +0x2A4, MinNetUpdateFrequency +0x218, NetPriority +0x298, confirmed 100/2/1 in the 19:00 run). v55 only survived by luck (write condition hit ~25 % of the time). The write is gone in v58; the 19:00 run with v58 had no crash. Not the nested spawn call, not the flush. |
| 4 | Vaulting into a window teleported the player out of the map ("left the playzone") | PC | FIXED | World-Composition origin rebasing on the host; origin pinned to {0,0,0} in v44. |
| 5 | No floor loot at all (host and client) | both | FIXED | Global NM_ListenServer broke the standalone loot path. Narrow per-actor net mode since v46. |
| 6 | Floor loot only appears once the **host** has landed; no loot far away from the host ("Waffen im Match nicht sichtbar") | PC | REOPENED (v58n) — the wake-up half is confirmed working, but the loot itself still only appears map-wide when the host touches down; the match phase was NOT the trigger (it was already 4 while the host was airborne). (1) `BravoHotelPickup` is `DORM_Initial` and never entered `AddNetworkActor`; v58k's wake-up fixed that and gave 375 pickup channels. (2) The loot itself only spawns once `GameState+0x9F2` reaches 4, which in a two-player match is when the host lands — v58l lifts that gate for the remote player's own check | `CheckSpawnByStandalone` takes `GetFirstPlayerController()` (host), its pawn, and gates on `PC+0x1928` = "in-bound levels loaded", set by the client's `ServerInBoundLevelsAreLoaded` RPC — always the HOST's flag, hence "loot only after the host landed". v58's pawn-thunk override kept the host's flag, so the 19:30 round still spawned nothing for the PC while the host sat in the plane (log: 7645 remote checks, all gated). v58b replaces the thunk hook with a `UWorld::GetFirstPlayerController` override during the nested check: pawn, distance and flag are the remote player's. Log: `[ls] Loot-Spawn ... GetFirstPlayerController OK` and `[ls] Spawn-Pruefung an Gebaeude ...`. Switch `-nospawnremote`. |
| 7 | Rubber-banding / "teleported back" on every step | PC | FIXED (mostly) | `IsLocalController` short-circuit under NM_Standalone. Fixed v46. Alen: "sometimes still gets bugged back" → residual, see #8. |
| 8 | Walking is buggy **while waiting for the match to start** (fine after the drop) | PC | OPEN | Pre-match phase only. Not yet investigated separately. Candidates: the waiting-area tile (v55 anchors only kick in once the remote pawn is possessed) or aircraft/boarding state (#16). Re-test with v58. |
| 9 | Walking normally puts the player inside buildings or under the map | PC | FIXED | Two causes: v47 ran engine calls from the watchdog thread (my bug, fixed v48); MovementBase references into streaming sublevels were replaced by None (1292×/round) → 2-byte patch in v48, count went to 0. |
| 10 | No blue zone on the PC (no zone, permanent storm effects) | PC | FIXED (v58e; self-heal re-adds the actor when the graph drops its entry — seen once per round) | Cause: Engine.ini gives the host `UBasicReplicationGraph`; it culls with the CDO's `NetCullDistanceSquared` (150 m for `BP_BlueZone_C`) and keeps a **per-actor copy** of it (`FGlobalActorReplicationInfo+0x94`), checked in the replicate loop (`comiss` at 0x13BECC6/0x13BED8F: cull <= 0 disables the check). v58 routed the actor to the AlwaysRelevant node (flag set in `AddNetworkActor` — confirmed in the 19:30 log) but the per-actor cull copy stayed 150 m, so it was still culled. v58b zeroes that copy right after the add (`[bz] Graph-Info ... CullDistance 15000 / Sq 225000000 -> 0 / 0`) and patches the CDO at the same moment. The GUObjectArray scan is gone (FUObjectItem pointers are obfuscated, it found nothing). Switch `-nozonerelevant`. |
| 11 | Zone / big map only visible shortly before landing; wanted from boarding the plane on | PC | FIXED (v58c) | Same fix as #10. v57's `FlushNetDormancy` is removed (0 flushes, actor was Awake). |
| 12 | Short blue-zone sound effect "as if inside the storm" although outside; also on phase change | PC | FIXED (v58c, per Alen) | Same zone-state problem as #10/#11; re-check after v58. |
| 13 | Some windows are still there on the PC after being broken; no window-break sound; shooting a window does not break it | PC | FIX PENDING (v58k — same mechanism as #6: `BP-BrokenWindowHISMActor_C` is `DORM_Initial`, so a window that breaks on the host is never announced to a client that did not see it break) | `MultiApplyDamageToGlass` is routed. Glass actors are normal grid actors with 150 m class cull, which is fine for nearby windows — so the remaining suspects are the tile not being loaded on the host (#6, v55) or the glass actor being DORM_Initial and never flushed on the host side. Re-test with v58; if still broken, next step is an `-rpclog` look at `MultiApplyDamageToGlass` vs. the window's channel. |
| 14 | Some doors won't open for the PC player | PC | FIX PENDING (v58k — same mechanism as #6; every door-frame Blueprint in the census is `DORM_Initial`) | Expected to improve with v55/v58 (tiles + actors present on the host). Re-test. |
| 15 | Reloading: ammo display stays at 0 until weapon switch; without switching it applies after 5–10 s. Host is fine. | PC | **OPEN. Confirmed (v59d): the magazine is `weapon+0x1B0`, size 24 at `+0x1B4`, and the host's value is live and correct. Ruled out so far: dormancy, missing channel, and now relevance (the graph sees the weapon 0.6 m from the player; zeroing the cull changed nothing). Open: who closes the weapon's channel 13× a round, and whether `+0x1B0` is replicated at all (`-magtest`).** Earlier (v59c): `weapon+0x1B0`, with `+0x1B4` beside it; `Pawn+0x648` is a `PlayerInventoryComponent`. What is left is delivery, and the suspect is relevance: the weapon class is 150 m cull / not always-relevant, and its channel reopened eleven times in one round. v59c measures the graph distance and zeroes the per-actor cull copy (`-noownrelevant`).** Earlier: the v58z cause is withdrawn (v59b). The DORM_DormantAll theory was measured in v59 and does not hold: the weapon's dormancy oscillates between 2 and 0, and **nothing in the weapon's first 0x800 bytes moves while shooting** except our own `+0x20D` write and a flag at `+0x9B`. The magazine is not in the weapon object's head. What does move is `BHCharacterReplication`: `+0xC0` (2→4→6→8→9) and `+0xD4` = `ReplicatedStateID` (1→19→2→3). v59b widens the window to 0x2000 and watches `Character+0x648`, the weapon holder from `GetCurrentWeapon`. The `DORM_Awake` write is off — it crashed the host (see #26). | Not RPC-based (no ammo RPC among 51 routed names). `net.UseAdaptiveNetUpdateFrequency=0` did not help. Needs an `-ammolog` run with a reload timestamp; next: read the ammo/OnRep chain via the symbol table (`OnRep_ReplicatedStateID` etc. from the handoff). |
| 16 | Perk assignment when boarding the plane: PC UI takes a long time to update | PC | FIX PENDING (v58u — same mechanism as #15; the perk data lives on the PlayerState) | 19:30 round: `[ac] DoInAircraft` fired twice as intended. | Handoff finding, confirmed in the binary: `DoInAircraft` (0x1FEC960) sends the `ClientInAircraft` RPC **only** when `GetNetMode()==NM_DedicatedServer`; under NM_ListenServer the remote client never hears about the boarding. v58 answers "dedicated" for the remote pawn at exactly that call site. Log: `[ac] DoInAircraft fuer Remote-Pawn ... -> NM_DedicatedServer`. Switch `-noaircraftfix`. Whether the perk UI hangs on this is a guess — re-test. |
| 17 | PC game "crashed" after some time (earlier run) | PC | OPEN (unclear) | Client log shows a clean `RequestExit`, no access violation — unclear whether the game or the player closed it. |
| 18 | Loot did not spawn far from the host even with the streaming CVars in Engine.ini | Laptop | superseded | Game re-sets those CVars at runtime; replaced by v55/v58. |
| 19 | "Everything is buggy when I'm not near the host" (summary complaint) | PC | FIX PENDING (v58g viewpoint — one cause for the whole family) | Umbrella for #6, #13, #14: host only streamed tiles / spawned loot around itself. v55 + v58. |
| 24 | **The remote player was only a streaming SUB POINT — the host in the aircraft was the primary one** (12.09., v58r round) | Laptop | **FIXED (v58s, confirmed by Alen: "Boden loot funktioniert jetzt")** — tile under the player went from 3 actors to 69, census 2109, `[lq] 36 ausgeloest`. | The root cause of #6/#19/#23, and it explains Alen's own observation that a standalone match runs fine: standalone has exactly one streaming point and it is the player. Sub points get the base-level distance multiplied by a scale clamped to [0.15, 0.45], so the landed player got ≤45 % while the host got 100 % for a patch of sky. Proof: the tile he stands on is `LODIndex=0` with a level of **3 actors** — an HLOD proxy. v58s makes the remote players the primary point and drops the host's point entirely while he is above z=12000. |
| 23 | **The host's world is almost empty: 1098 levels hold 5409 actors between them (5 per level) and 35 building actors exist in the entire world** (12.09., v58q round) | Laptop | **FIXED (v58s)** — root cause was #24. | This is the real #6. The loot spawn was never blocked by a gate — all six gate counters are 0 outside the lobby — there is simply nothing near the remote player that carries an item spawn box. Five actors per level is the HLOD-proxy signature, and every Prio-500 tile in every log ever taken reads `ShouldBeLoaded=0 / NichtGeladen`. v58r calls `CommitTileStreamingState` itself for the tiles around the remote player, and `[td]` now prints the actor count of the level under him so the next log settles it. |
| 22 | Landed with the PC while the host was in the air: **no loot, and bugged through buildings again** (12.09., v58p round) | PC | **FIXED (v58s)** — root cause was #24. Re-check the clipping specifically now that the world is there. | Both from one measurement: at t=142 (landing) the host had **37 actors** within 200 m of him and only **726 at t≈162**, four seconds before the round ended. No walls to collide with and no loot sources to fire for twenty seconds. Tiles and Engine.ini are correct (`LODIndex=-1`, loaded, visible) — it is the time-slice, with the host at 9.7 GB of 15.6 GB and 895 levels. v58q loads the tile under the landed player blocking (max 24 per round, only below z=12000) and fixes the two loot-source bugs in the row below. |
| 21 | Loot already lies on the map **before the round has started** (12.09., v58o) | both | FIXED (v58p, confirmed) — `Phase angehoben 0`, `davon Loot VOR Phase 4 0`, `in der Lobby uebersprungen 80971`. v58q additionally found that `+0x534` was never a "has spawned" flag (it is recomputed per call and always 1 after a mode-2 pass), so v58o had been skipping every source the game had ever touched, and that none of the six gates in front of `0x2142DA0` was being measured — both fixed in v58q. | Our own regression, not the game's: the nested spawn check lifted `GameState+0x9F2` to 4 while the match was still in phase 1 (the waiting lobby) — 320 lifts in 100 s in the v58o log, every `[ph]` line saying "Match-Phase noch 1". The lift was a v58l guess that v58m had already disproved (phase was 4 at t=84 s with the host still at z=74800, i.e. before anyone landed). v58p turns the lift off by default (`-stateforce` re-enables) and makes both spawn paths return early below phase 4, counted as `in der Lobby uebersprungen`. |
| 20 | Pinging something on the PC shows the ping for the HOST instead of the PC | PC | OPEN (noted 12.09., not yet investigated) | `ClientAddSmartPing` and `ClientUpdateFirePing` are both in the routed client-RPC list, so the RPC leaves the host — the question is whose connection it is addressed to. Suspicion: the ping is executed locally on the host (callspace Local instead of Remote) because the pinging pawn's owner resolves to the host's connection. Look at it after #6. |


## v58c — what the 19:47 round (v58b, started via host.bat) showed

- `host.bat` works: `LogNet: Browse: /Game/BravoHotel/Maps/OrbIsland/LV-OrbIsland?…?listen?AutoStart=1…` — no title/lobby detour.
- Zone: CDO patched, instance flagged, graph cull copy zeroed (`15000 -> 0`). But at ZoneTick the graph handed out a **different** info object for the same actor (fresh from class defaults) — the actor had been taken out of the graph again in between. v58c re-adds it whenever its graph entry is not the one we fixed up (`[bz] Graph-Eintrag ... NEU -> erneut hinzugefuegt`), sets the instance to DORM_Awake before the add (the BlueZone constructor at 0x1D1B340 leaves it in DORM_Initial, which the graph turns into "sleep on the connection after one replication"), and logs every `RemoveNetworkActor` of the zone with the caller's return address.
- **Root cause for windows/doors/ammo delay (#13, #14, #15, probably #16): `AActor::FlushNetDormancy` and `AActor::ForceNetUpdate` end in `AActor::GetNetDriver()` = `World->NetDriver`, which the standalone mask keeps at null.** Every dormancy wake-up (window breaks, door opens, dormant weapon changes ammo) died there and never reached the replication graph; placed DORM_Initial actors never even entered the graph. v58c hooks both functions and runs them inside a MaskOff window (like TickFlush). Switch `-nodormfix`. Log: `[dm] Dormancy-Weckrufe ... FlushNetDormancy OK, ForceNetUpdate OK` and per 20 s `[dm] FlushNetDormancy N (im Fenster M)`.
- Loot: 7493 remote checks ran, but the log only showed vehicle spawners and nothing spawned. v58c logs buildings and vehicles separately, detects an actual spawn (guard byte `building+0x46A` flips → `GESPAWNT`), lifts the `PC+0x1928` gate on the remote PC for the duration of the check if it is still 0 (and reports the flag every 20 s: `Remote-PC ...: InBoundLevelsLoaded-Flag = ...`).
- Aircraft perk UI (#16): `[ac]` fired for both remote pawns; the UI still updates only after the jump — most likely the dormancy problem above (the perk data sits on a dormant-wanting actor), re-test with v58c.

## v58d — what the 20:02 round (v58c) showed

- Zone: works on the PC (client log: `OnRepSelectedPlayZoneName ... ALPHA_24_10 / High School` at 18:03:44, i.e. right at join). Done.
- Dormancy hooks installed and busy (`FlushNetDormancy 1115 (im Fenster 1115)`), yet windows still do not break for the PC and loot still waits for the host. Both point at the same thing: **the buildings/windows around the remote player do not exist on the host.** Evidence: after the PC landed (t≈195 s) and stood at {-118846,126216} for 80 s, the count of building spawn checks for the remote pawn rose by exactly 2 (from 4923 to 4925) — buildings near him are not ticking their spawn timer, while ~1300 checks/s keep running around the host. In the pre-match waiting area (host nearby) the same checks ran fine with the PC's `PC+0x1928` flag = 1.
- World Composition detail from the exe (tile lambda 0x4752FC2): every streaming point after the first is a "sub point"; ignored within 60000 of the host, otherwise streamed with **half** the distance (`s.SubPointLevelStreamingDistanceScale` = 0.5). v58d sets that scale to 1.0 (`-nosubscale` restores) and, every 10 s, logs what World Composition thinks about the tile under the host and under the remote anchor (`[td] ...ShouldBeLoaded=... ShouldBeVisible=...`). That log decides the next step: if the remote tile is not requested, the streaming path itself needs a patch (e.g. remote player as primary point while the host is a ghost); if it is loaded but no building ticks, the building Blueprint gates on the local player.
- Perk UI in the aircraft (#16): the perk RPCs for the PC (`ClientAddPerkLevel`, `ClientAddPerkUIEvent`, `ClientPerkSpinEvent`) were sent (the "No owning connection" drops in the host log are for the HOST's own pawn/PC, harmless). Cause still unknown; needs the client side (PC log shows nothing perk-related).

## v58e — what the 08:50 round (v58d) showed

- Zone regressed although the log looks identical to the working 20:02 round: CDO patched, instance flagged, cull copy zeroed. In BOTH rounds the graph handed ZoneTick a **different** info object than the one fixed up at spawn (the zone's graph entry gets dropped by something that does not go through `UNetDriver::RemoveNetworkActor`; whether the always-relevant node still lists the actor is unknown). v58c's self-heal never fired because of an ordering bug (the first ZoneTick created the fresh entry itself and then treated it as "ours"). v58e: every tick compares the entry pointer, re-adds the actor when it changed (max 30, logged as `[bz] Graph-Eintrag ... NEU`), re-zeroes the cull copy whenever it is non-zero, and logs `Graph::RemoveNetworkActor` calls for the zone actor via the graph vtable (+0x2A8) with the caller's return address.
- Streaming: World Composition **does** request the full tile under the remote player (`[td] Remote ... LODIndex=-1 ShouldBeVisible=1 ShouldBeLoaded=1`), scale 1.0 applied. So the tiles load, yet building spawn checks near the landed PC stayed at zero — the building Blueprint's timer only ever fires around the host. v58e stops depending on that timer: every 2 s it walks `UWorld::Levels` → `ULevel::Actors` (decoded with the handoff's pointer transform), picks every `ABravoHotelBuilding` / `ABravoHotelVehicleSpawnActor` within reach of a remote pawn and runs the native spawn check for it with the remote controller (`[sw] Level-Walk ...` lines; `Gebaeude gespawnt` counter). If `gueltig dekodiert` is ~0 the decode is wrong for this build and the log says so.
- Oddity to watch: the engine's own streaming point for the host was `{0,0}` in every `[td]` line; v58e prints it with z. If the host really streams around the origin, the host's own tiles come from somewhere else (or the host pawn location is what we should pass).
- Windows/doors follow the same tiles+actors question; nothing new for the aircraft perk UI.

## v58f — what the 12.09. morning round (v58e) showed

- Zone works again on the PC. The log confirms the mechanism: the graph replaced the zone's entry once (`[bz] Graph-Eintrag ... NEU (... Refs=0) -> Actor wird erneut hinzugefuegt (#1)`), the re-add re-zeroed the cull copy, and no `Graph::RemoveNetworkActor` call was ever seen for the actor. Keep the self-heal.
- Level walk: `0 gueltig dekodiert` of 7675 slots → the v58e decode was wrong. Checked against the game's own code at 0x432B77D..0x432B796: `not ecx` complements the **low dword only**, the high dword is written back first and survives; v58e zeroed it. Fixed in v58f.
- Loot, the actual cause (read out of `ABravoHotelBuilding::BeginPlay` 0x1FA57F0 and the vehicle spawner's 0x22BF900): the looping 0.1 s timer that drives `CheckSpawnByStandalone` is only started when `AActor::GetNetMode() == NM_Standalone` at BeginPlay. In the log the remote pawn stood still on the ground at {-151766,55665} from t≈150 s to the end and the remote building/vehicle check counters did not move by one (964 / 1810, both from the pre-match island), while the game's own checks kept running at ~1400/s elsewhere. So the buildings around the PC never got a timer. v58f answers NM_Standalone at exactly those two call sites (`[bp] BeginPlay ... sah NetMode N -> NM_Standalone`, counted in the watchdog as `BeginPlay-Gate N, erzwungen M`; `-nobeginplayfix` restores). The level walk (now decoding) additionally runs the native check for every registered building near the remote that has no timer (`[sw] ... nahe Remote (mit Timer / ohne Timer / nicht registriert)`).
- "Spawned" detection: `building+0x46A` is a constant flag (0x01010101 from the constructor at +0x468, mode 2 needs +0x46A=1), not a guard. v58f watches the GameState's spawned-building list (`+0xAD8`) instead (`Spawn-Liste a->b, GESPAWNT`) and the timer handle `+0x470` (`Timer geloescht` = the check passed all gates).
- Watchdog now also counts the game's own building checks within 400 m of the **host** (`davon nahe Host N`) so the two sides can be compared directly.
- The 964 pre-match building checks with zero spawns are expected: the waiting island has no loot buildings (mode-2 flag or no spawner components).

## v58g — what the 12.09. v58f round showed: the loot exists, it never arrives

- The level-walk decode works now (21590 of 21607 slots valid) and the forced spawn runs: `Spawn-Liste 44->271`, i.e. **271 buildings completed `CheckSpawnByStandalone` around the remote player**, and `UNetDriver::AddNetworkActor` took ~930 new actors in the same 60 s (3972 -> 4901) — roughly four per building. The items are on the host and registered with the net driver. The player still saw an empty map. **So this was never a spawn problem; it is a replication problem.**
- `[bp] BeginPlay-Gate 263, erzwungen 0`: the net mode at BeginPlay was never wrong, so that was not why the buildings near him had no timer. (Harmless, stays in.) The remaining reason a building has no timer is the other gate: its location is already in the GameState's spawned list.
- What is missing for the client (floor loot, breakable glass, doors, vehicles) are all **grid** actors; what works (blue zone, his own pawn/controller) are always-relevant and owner-relevant actors, which need no position. The grid node gathers cells around exactly one point per connection.
- That point, read out of the binary: `UBasicReplicationGraph::ServerReplicateActors` 0x13C6A50 (vtable +0x2F0) only drains `ActorsWithoutNetConnection` (graph+0x5C0/+0x5C8) and tail-jumps to `UReplicationGraph::ServerReplicateActors` 0x13C6C60. That walks graph+0x40/+0x48 (`UNetReplicationGraphConnection`, its `UNetConnection` at +0x30), sets `conn->ViewTarget(+0x110) = conn->PlayerController(+0x38) ? PC->GetViewTarget() [vt +0x700] : conn->OwningActor(+0x118)`, skips the connection if `conn->State(+0x1BC)==USOCK_Closed(1)` or the view target is null, and builds `FNetViewer(out, conn, 0.0f)` = **0x43DB130** (out: +0x00 Connection, +0x08 InViewer, +0x10 ViewTarget, +0x18 ViewLocation, +0x24 ViewDir, size 0x30). `ViewLocation` = ViewTarget's root-component translation, then overwritten by `PC->GetPlayerViewPoint` [vt +0x760].
- Suspicion the round has to confirm or kill: for the remote controller this point is not where the player is (his controller actor, the origin, or the host). The waiting island sits at the world origin — which is exactly where everything works today.
- v58g therefore hooks `FNetViewer::FNetViewer`, logs the point the graph really uses per connection (`[vw]` lines), and moves it onto the remote player's pawn when it is more than 50 m off (`-noviewfix` = measure only). Second instrument: the level walk tallies the **classes** of every actor within 200 m of the remote player every ~16 s (`[cn]` lines, each with the class's replication settings from its CDO) — that says once and for all whether pickups stand next to him on the host.

## v58h — what the 12.09. v58g round settled

- **The viewpoint theory is dead, and that is a result.** Every `[vw]` line shows the replication graph using the right point: `ViewTarget BP-BattleRoyalePawn_C -> Blickpunkt {44939,-161553,12519}`, `Abstand zum Blickpunkt 0..400` (the camera offset). 1060 viewers built, 0 corrections while he was on the ground. So the grid node gathers around him correctly and nothing is wrong with the connection (`Status 3` = USOCK_Open, PlayerController and OwningActor both set).
- **The census says what is actually wrong.** Thirty seconds after he landed at {197618,-128405}, the host had **sixteen actors within 200 m of him**: 6 PostProcessVolumes, 2 Emitters, 1 CameraActor, 1 Jeep, 1 PrefabToolActor, 1 fog volume, 1 water volume, 1 StaticMeshActor. No buildings, no doors, no glass, no loot spawners. Compare the pre-match island in the same log: **1287 actors, 47 classes** — 562 StaticMeshActors, 32 fences, 11 door frames, 10 `BP-BrokenWindowHISMActor_C`.
- **And the reason is in the tile diagnostic:** his tile `#1334` was at `LODIndex=0` — the host had the baked **HLOD proxy** of that square, not the real level. A proxy is one merged mesh with no gameplay actors at all. Nothing can spawn, break or open in a world that is not there. Twenty seconds later the same tile flipped to `LODIndex=-1`, i.e. the host was only then starting to bring in the real level. The `[sw]` walk agrees: 8656 actor slots in this round against 21607 in the previous one, where he had stood in the same spot for two minutes and the buildings had finally arrived (and the forced spawn then worked, 271 buildings).
- **This explains the oldest symptom exactly**: "loot only appears once the host has landed" — the host's own streaming pressure drops when it stops flying, and the remote player's area finally gets its base levels. Same root cause for #13 (no glass actor to break), #14 (no door actor) and #19.
- v58h therefore stops waiting for the engine's LOD arithmetic: after each `UpdateStreamingState`, every **detail** tile (box < 500 m, priority not 500 — the host does not load those for itself either) within 200 m of a remote player gets `LevelLODIndex = -1` plus `bShouldBeLoaded`/`bShouldBeVisible` (`[ft]` lines, `-noforcetiles`). Second half of the fix, on the host's `Engine.ini`: the time slice for making levels visible (see `Engine.ini.host-streaming.txt` in the game folder) — 5 ms per frame is why this takes minutes.
- The census now also runs **around the host** and always prints pickup/item classes separately, so one log shows both halves of the map side by side.

## v58i — v58h was wrong; what the log proved and what is now measured

- **v58h's tile forcing is removed.** It wrote `LevelLODIndex` and the visibility bits straight into the `ULevelStreaming`. The log shows the cost: **861 such writes in two minutes**. `UWorldComposition::CommitTileStreamingState` (0x4756450) compares its arguments against exactly those fields, found a difference it had not caused, and therefore ran the full set-LOD path — an unload/reload of the package — every frame. That level can never settle. My mistake, and it was making the round worse rather than better.
- What the round did establish, and it is worth keeping: the remote player's own tile `#187` had `LODIndex=-1, ShouldBeVisible=1, ShouldBeLoaded=1` — the full level was requested and flagged visible — and there were still only **14 actors within 200 m of him**, while the host had 183 around itself (86 StaticMeshActors, 37 LODActors, fences, door frames). So "requested" and "actually in the world" are two different things, and nothing so far has told us which step in between fails.
- **The engine answers that itself, and v58i reads it.** Taken out of `SetLevelLODIndex`/`SetShouldBeVisible`: `ULevelStreaming` keeps `LevelLODIndex` at +0xC0, **CurrentState at +0xC8**, TargetState at +0xC9, the visibility/loaded bits at +0xD0 and its `ULevel` at **+0x140** (proven by 0x475A1D0, which tests +0x140/+0x148 for "has a level"). CurrentState is UE4's enum — Unloaded 1, FailedToLoad 2, Loading 3, LoadedNotVisible 4, MakingVisible 5, LoadedVisible 6 — confirmed by `SetLevelLODIndex` turning 2 back into 1, which is exactly what the engine does with FailedToLoad.
- The new `[st]` lines print, for every detail tile around the remote player **and** around the host: LOD index, the two request flags, the state name, the target state, the `LoadedLevel` pointer, **how many actors that level holds**, and whether it is in `UWorld::Levels` — plus a histogram of all ~3600 tiles by state. The four possible causes read completely differently there: never requested (`NichtGeladen`), stuck in I/O (`Laedt`), loaded but never made visible (`GeladenUnsichtbar`/`WirdSichtbar`), or `GeladenSichtbar` with a full actor list — in which case streaming is innocent and the fault is in replication after all.
- `[mem]` adds the host's working set, peak, and free system RAM every 20 s. A machine purging levels as fast as it loads them looks exactly like this, and that has never been checked.
- The one real fix attempt in this build: **the commit is hooked, not the fields**. For a tile within 150 m of a remote player, `CommitTileStreamingState` is called with "loaded, visible, LOD -1" regardless of what the distance arithmetic decided; the engine's own state machine then does every transition untouched, and the commit's "nothing changed" early-out keeps working, so there is no per-frame churn (`[ct]` lines, `-nocommitfix`).
- Engine.ini: the three `ForcedWCStreamingDistanceScale` lines from the early loot attempts make the host ask for world-composition levels from twice the distance — roughly four times the tiles in flight, on a machine that is already not finishing the ones it has. `Engine.ini.host-streaming.txt` now asks for those to be turned off.

## v58j — streaming is CLEARED, the loot is on the host, next to him

The v58i round answers the question that three builds have been circling, and it answers it with numbers rather than a story.

- **Streaming is fine.** Histogram over all 3616 tiles: `NichtGeladen 2371, GeladenSichtbar 1245` and **zero** in Laedt, WirdSichtbar, GeladenUnsichtbar or LadenFehlgeschlagen. Nothing is stuck anywhere. The tiles the remote player stands in (#1188/#1190/#1192) are `LODIndex=-1, soll geladen=1 sichtbar=1, Zustand GeladenSichtbar, in World->Levels: ja`, with 32/43/31 actors in their levels — the same picture as the host's own tiles.
- **The loot is on the host, where he is.** Census within 200 m of the remote player: **64 x BravoHotelPickup**, 1 x BP-Weapon_AKM_LV3_C, 1 x BP-Weapon_M870_LV4_C, plus 393 StaticMeshActors, 67 PrefabToolActors, 56 BP-Fence_C, 5 warehouses and a dozen door frames. Around the host at the same moment: 88 pickups. So spawning works for both, and the world around him is complete.
- Memory is tight but healthy: 10.0 GB of 15.6 GB in use, 95% system load, 1245 levels — nothing is being purged (the level count grows, it does not oscillate).
- **Therefore the fault is in replication, and only there.** Everything the client is missing is a grid actor; everything that works for him is always-relevant or owner-relevant.
- Two candidate mechanisms are already visible in the census and will be confirmed or killed by the new table: the fences and every door frame carry **Dormancy 4 (DORM_Initial)**, and `UNetDriver::AddNetworkActor` skips DORM_Initial startup actors outright (`IsDormInitialStartupActor` 0x3F93040) — that is a complete explanation for #13 and #14 on its own. And inside `UReplicationGraph::ReplicateSingleActor` (0x13BE920) there are exactly two gates that can drop an actor: a **per-class policy lookup** (map at graph+0x240, 0x1395990, the returned byte must be 1) and the cull-distance check against the viewer.
- v58j counts, per class, the three gates an actor passes: **added** (`UNetDriver::AddNetworkActor`) / **considered** (`ReplicateSingleActor` 0x13BE920, rcx graph, rdx actor, ten arguments) / **channel** (`UActorChannel::SetChannelActor` 0x41AC3C0). `[pl]` lines, every 20 s, loot classes first. For the loot classes it additionally logs the class policy byte, bReplicates, NetDormancy, cull distance and the distance to the player the first three times each. `added 64 / considered 0` means the grid never gathers them; `considered 64 / channel 0` means one of the two inner gates drops them, and the extra line says which.

## v58k — found it: everything that is missing is DORM_Initial and nobody ever wakes it

The v58j table is unambiguous, and it names the mechanism rather than suggesting one.

- **`BravoHotelPickup` never passes through `UNetDriver::AddNetworkActor` — not once**, in four minutes, while 48-64 of them stand within 200 m of the player. It does not appear in the per-class table at all, while fences (537), door frames (115), jeeps (64) and RC bombs (64) do.
- And the census says what the entire missing family has in common: `BravoHotelPickup` **Dormancy 4**, `BP-Fence_C` **Dormancy 4**, `BP-Wood_In_Door05_Frame_C` **Dormancy 4**, `BP-BrokenWindowHISMActor_C` **Dormancy 4** — all `repliziert 1`, all cull 100 m. Dormancy 4 is `DORM_Initial`: *the client already has this actor from its own copy of the level, so do not replicate it until something wakes it.* The engine enforces that by keeping such actors out of the network object list entirely (`UNetDriver::AddNetworkActor` → `IsDormInitialStartupActor` 0x3F93040), and the only thing that ever brings one in is `AActor::FlushNetDormancy`.
- That assumption is true for a fence the client loaded itself. It is **false for every pickup the host spawned during the match**, and false for a window this client never saw break. Nothing in this setup calls `FlushNetDormancy` for them, so they sit on the host, fully spawned, and no client is ever told. This is one mechanism for #6, #13 and #14 together.
- v58k wakes them: in the level walk, every replicated actor with `NetDormancy > DORM_Awake` within 300 m of a remote player gets `FlushNetDormancy` + `AddNetworkActor`, once each (an 8192-slot pointer set prevents repeats), inside the MaskOff window where the driver is visible. `[wk]` lines and a `geweckt` column in the `[pl]` table. `-nowakeup` turns it off for an A/B.
- Second thing this round settles: the graph has reported `Summe 0` replicated actors all evening, and `ReplicateSingleActor` was hit 40 times in four minutes while 320 channels were opened — so the channels are not coming from the graph. `[rg]` now prints how many connections the **graph** holds (`+0x40`/`+0x48`, read out of its own `ServerReplicateActors` 0x13C6C60) against how many the **driver** has. If the graph's list is empty, it is replicating nothing at all and the classic path is carrying the session.
- Also fixed: the class table was capped at 128 entries, which could have hidden a class. It is 256 now and prints how many classes it saw and whether it filled up.

## v58l — v58k worked, and the log named the last gate

**v58k's result, from the v58k round's own numbers:**

| class | added | channel | woken |
|---|---|---|---|
| BravoHotelPickup | 1183 | **375** | 205 |
| BP-Fence_C | 1294 | 116 | 230 |
| BP-Wood_In_Door05_Frame_C | 214 | 13 | 47 |
| BP-BrokenWindowHISMActor_C | 45 | 18 | 15 |

`BravoHotelPickup` went from **never appearing in `UNetDriver::AddNetworkActor` at all** to 375 pickups actually sent to the client. Total channels went from 320 to 841. The replication graph has 1 connection and the driver has 1, so that suspicion is dead too. Waking the dormant actors was right and stays in.

**And the same log shows the gate behind "it only works once the host is on the ground":**

- While the host was still in the air the whole map held **20** pickups (`BravoHotelPickup -- 20 / 0 / 3` at t=120, 140 and 160 s).
- In the twenty seconds around the host's touchdown (host z: 42730 → 21272 → 15009 → 8748 → 6643), **1137 more appeared at once**: `1157 / 0 / 355`.
- 231 of those stood within 200 m of the **remote** player — who was at {89868, 23325} while the host was at {129296, 168732}, i.e. **1.5 km away**.

A map-wide wave 1.5 km from the host is not distance and not streaming. It is the **first** gate of `CheckSpawnByStandalone`: `cmp byte [GameState+0x9F2], 4` at 0x1FA5F6B — the match phase. Every building on the map runs its 0.1 s spawn timer the entire time (that is where the ~1400 checks/s come from) and every one of them bails at that line until the phase reaches 4, which in a two-player match happens when the last player — the host — is down. Our nested call for the remote player was bailing at the same line, which is why forcing his `PC+0x1928` flag alone was never enough: that flag is checked *after* it.

- v58l lifts that gate for our nested call the same way the PC flag is lifted: set `GameState+0x9F2` to 4, run the one check, put it back immediately (`-nostateforce`). Counter `Phase angehoben` in the 20 s line.
- `[gs]` now logs the match phase every half second and the exact moment it changes, next to the host's altitude and both players' `+0x1928` flags — so the next log either confirms this in one line or rules it out.

## v58l round — LOOT CONFIRMED WORKING WITHOUT THE HOST ON THE GROUND

Alen: *"I changed the match phase and it spawned without host landing."* The log agrees, line by line:

- `[gs]` shows the match phase stepping **2 → 3 → 4 at t=80 s**, while the host was still on the waiting island (z=3698) and long before anyone dropped. In every earlier round that step came at the host's touchdown.
- The remote player landed at t≈140 s (z=3545). The host was still at **z=69800**, i.e. in the air, and only came down after t=162 s (z=22822).
- At t≈150 s, with the host still airborne, the census around the **remote** player reads `*** 50 x BravoHotelPickup` — and the sweep lines above it show them being created: `Spawn-Liste 42->43, GESPAWNT`, `43->44`, `44->45`.
- Pipeline at t=160 s: `BravoHotelPickup -- 300 / 0 / 100` — a hundred pickups with an open channel to the client — plus doors (13, 6, 5, 4), windows (6) and fences (45). 361 channels total.

So the chain that was broken is now closed end to end: **match phase gate (v58l) → loot spawns → `DORM_Initial` wake-up (v58k) → the actor enters the network object list → channel → client.** Both halves were needed; either one alone leaves the player with an empty town.

The client log confirms the zone side is healthy too (`OnRepSelectedPlayZoneName ... S. Radio Tower`); its only errors are missing `DevelopmentOnly` tables, which are harmless streaming hitches.

**Open, and now testable for the first time on a map that actually has loot on it:** #13 windows, #14 doors (both now get channels — re-test whether they behave), #15 reload display, #16 perk UI in the aircraft, #8 pre-match walking, #20 ping shown to the host.

## v58m — proving the part that matters: loot for whoever lands FIRST

Alen's point is right, and it is not the same thing the v58l round proved. That round happened to have the match phase reach 4 on its own at t=80 s, before anyone dropped — so the loot he got on landing may simply have come through the normal gate. What has to work is the **other** case: he lands while the phase is still 2 or 3 (because the phase waits for the last player, i.e. the host), and his surroundings get loot anyway.

The code already does exactly that: `RunSpawnForRemotePawns` lifts `GameState+0x9F2` to 4 for the duration of its one nested call and puts it straight back, so a building within 300 m of the remote player spawns its loot no matter what the match phase says. What was missing is a way to see it in the log.

- v58m logs `[ph] Loot fuer den Remote-Spieler gespawnt, obwohl die Match-Phase noch N war` every time loot is actually created while the phase is below 4, and counts it in the 20 s line as `davon Loot VOR Phase 4`.
- Nothing else changed.

**The test that settles it:** undo whatever was changed to make the phase advance early, land first, and leave the host in the air. The `[gs]` line should show `Match-Phase = 2` or `3`, `[ph]` lines should appear, and there should be loot on the ground around you.

## v58n — the phase theory is dead; now the log will name the code path

Alen is right and the v58m log proves it cleanly:

- The match phase reached **4 at t=84 s**, while the host was still at **z=74800** in the aircraft (`[gs] ... Match-Phase = 4 *** GEWECHSELT *** | z=74800`).
- The remote player landed at t≈142 s.
- The loot still did not arrive until the host was on the ground at t≈190 s: `BravoHotelPickup` went **1177 → 3297** and the census around the remote player went **105 → 592** in that single window.

So a closed phase gate is not what holds the map's loot back. The phase lift does work — the `[ph]` lines show 40 buildings spawning for him while the phase was still 1 — but it only ever reaches the handful of buildings our own sweep and the hooked timers cover. Something else fires at the host's touchdown and spawns 2120 pickups across the map at once.

Counters cannot name a code path, so v58n reads the one thing that can. The hook on `UNetDriver::AddNetworkActor` is a 14-byte jmp, so `_ReturnAddress()` inside it is the instruction right after the caller's call. For every `BravoHotelPickup` registered, that address is turned into an RVA and the distinct paths are logged:

```
[lr] Pickup 0x... wird von RVA 0x0XXXXXXX aus registriert (neuer Pfad #1)
[lr]   RVA 0x0XXXXXXX -- 2120 Pickups
```

One round, and the function that creates the wave has a name — after which it can simply be called for the remote player, instead of us re-deriving the spawn one building at a time.

The same build also evaluates, for the **game's own** building check, the five gates it is about to evaluate itself, using the **host's** controller: phase / no controller / no pawn / too far / flag. The `[gs]` line now carries those counters plus the host's own `+0x1928` flag, so what the host path does before and after touchdown is readable instead of inferred.

Reload (#15): the client log for this round shows only `GetSocketByName(Magazine_joint): No SkeletalMesh for Component(FPSK_WeaponShadow)` — a cosmetic weapon-mesh warning during reload, not the ammo counter. The counter is still a UI/delegate problem and needs its own pass.

## v58o — the spawn chain read end to end, and the mistake it exposes

Read out of the binary rather than inferred from another round:

```
ABravoHotelBuilding::CheckSpawnByStandalone 0x1FA5E60
   match phase 4 | first PlayerController | its pawn | < 300 m |
   components registered | PC+0x1928
-> 0x1FA7980(building, FName area, 0.0f, mode 2)
     not outside the zone | building+0x46A
   -> comp = building->vtable[+0x660](UBravoHotelDetectItemSpawnBoxComponent)
-> 0x2142DA0(comp, delay, mode)
     comp world | GameState 0x1CB2110 >= 4 | owner Role == Authority
   -> 0x2141FD0(comp, mode)
        comp+0x534 = "this source has fired"   <- the only real guard
     -> per child component: 0x2177320(child, time, mode)  = one pickup
```

**The mistake this exposes is ours.** A loot building is not "an actor whose class descends from `ABravoHotelBuilding`" — it is simply **any actor carrying a `UBravoHotelDetectItemSpawnBoxComponent`**. Our walk has been asking the wrong question the whole time, which is why it found 38–47 buildings in the entire world while the host's own wave produced 2120 pickups from that same map. Every building we never recognised was a building we never spawned loot for.

v58o asks the game's own question instead:

- every actor within 300 m of a remote player is asked for the component (`GetComponentByClass`, vtable +0x660);
- those whose `+0x534` guard is still clear are fired directly through **`0x2142DA0`**, which takes the **component** — so it is class-agnostic and sits *below* the distance gate, the `PC+0x1928` flag and the phase gate (lifted here too);
- `[lq]` lines report how many actors were checked, how many are loot sources, how many were still open and how many fired.

Switches: `-nolootscan` (do not look), `-nodirectspawn` (count but do not fire).

Also nailed down along the way: `ABravoHotelPickup::StaticClass` 0x24520C0 (class cache 0x73654C0, instance size 0xA30), `UBravoHotelDetectItemSpawnBoxComponent::StaticClass` 0x23C5C40 (cache 0x735FF40), `UBravoHotelPickupManager::StaticClass` 0x2453270, and the `PC+0x1928` mechanism: 0x21DFAF0 accumulates world time in `PC+0x1908` while the player's levels stay loaded and, after 3.0 s, sets the flag and sends `ServerInBoundLevelsAreLoaded` — which is why a parachuting host never has it set.

## v58p — no loot before the round starts (regression from v58l, fixed)

Alen, after the v58o round: *"bevor die runde überhaupt gestartet ist, liegt schon loot"*. The v58o log says exactly where it came from — every single `[ph]` line reads the same way:

```
[ph] Loot fuer den Remote-Spieler gespawnt, obwohl die Match-Phase noch 1 war (nicht 4) -- Gebaeude 000002581F923A60, Abstand 6105 (#1)
[gs] t=100s  Match-Phase = 1 | ... | Phase angehoben 320
```

Phase 1 is the waiting lobby. Our own nested `CheckSpawnByStandalone` was lifting the phase gate to 4 there, 320 times in 100 seconds, and spawning the map while the round did not yet exist.

**The lift should never have been in the build.** It was a v58l guess, and v58m had already disproved it: the phase reached 4 at t=84 s with the host still at z=74800, i.e. while the aircraft was up and nobody was on the ground. The game's own gate is therefore already open by the time the first player lands — lifting it bought nothing and produced this.

v58p:

- `g_stateForce` (the phase lift) is **off by default**. `-stateforce` turns it back on for experiments; `-nostateforce` is gone.
- Both spawn paths return early while the phase is below 4: `RunSpawnForRemotePawns` (the nested building check) and `FireSpawnComponent` (the direct component fire). One helper, `MatchStarted()`, decides it for both.
- The watchdog counts the skips: `in der Lobby uebersprungen N` in the 20 s `[ls]` line.

This keeps the promise the other way round too. From the aircraft phase onwards nothing is blocked any more, so whoever lands first still gets loot around him — which is what the `[lq]` lines were supposed to show in the v58o round and could not, because the `[ph]` path had already consumed every source in the lobby (`107 gefunden, 0 noch offen, 0 ausgeloest, 107 schon vorher fertig`).

## v58q — what the v58p round measured, and the two things it changed

The lobby fix worked: `Phase angehoben 0`, `davon Loot VOR Phase 4 0`, `in der Lobby uebersprungen 80971` (frozen the moment the round started). No loot before the round any more. What the same log then handed over was better data than any previous round.

**1. The flag we treated as "this source has already spawned" was never one.** Disassembly of the inner spawn function `0x2141FD0`:

```
mov  byte [comp+0x534], 0        ; cleared on entry, unconditionally
cmp  byte [comp+0x528], 0
jne  skip
  eax = 0 ; ecx = 1
  cmp   byte [comp+0x529], 0
  cmove eax, ecx                 ; 1 when BOTH mode bytes are zero
  mov   byte [comp+0x534], al
skip:
```

`+0x534` means "spawn every category in this pass". It is recomputed on every call, and mode 2 (which zeroes both mode bytes) always leaves it at 1. v58o read it as a done-flag, so every source the game had ever touched was counted `schon vorher fertig` and skipped forever. v58q keeps its own set of sources it has fired.

**2. Six gates sit in front of the spawn, and the log never said which one was shut.** `0x2142DA0`, mode-2 path, in order:

| # | check | field |
|---|---|---|
| 1 | World | `comp+0xB0` != null |
| 2 | AuthGameMode | `World+0x1D0` != null |
| 3 | GameState | `GameMode+0x370` != null |
| 4 | match phase | `GameState+0x9F2` >= 4 |
| 5 | Owner | `comp+0xA8` != null |
| 6 | Role | `Owner+0x20F` == 3 (ROLE_Authority) |

The v58p log: 30 sources handed in, `0 ausgeloest`. So one of the six is shut and none of them was being measured. v58q evaluates all six itself and prints the breakdown (`[lq] Tore: ...`), then calls the **inner** function `0x2141FD0` directly — it has none of the six, it only walks the child components and creates one pickup per spawn point. Our call does exactly what `0x2142DA0` does on its way in: zero `+0x528`/`+0x529`, set `comp+0x51C = World+0x5C8`, call. **Gate 4 is the one gate we do not step over** — no loot before the round starts. `-nogatebypass` restores the old outer-call-only behaviour.

**3. And this is why he falls through buildings.** Same log, the `[cn]` census around the landed remote player:

```
t=140  Actors um den Remote-Spieler 37
t=160  Actors um den Remote-Spieler 38
t≈162  [cn] Um den REMOTE-Spieler: 726 Actors, 31 Klassen (341 StaticMeshActor, 58 Zaeune, ~50 Tuerrahmen)
t=164  Match-Phase 5 -- Runde vorbei
```

He landed at t=142. For the next twenty seconds the host had **37 actors** around him: no walls to collide with, no loot sources to fire. The world arrived at t≈162, four seconds before the round ended. That single measurement explains both complaints at once — the missing loot and the clipping through buildings.

The tiles themselves are fine: the `[td]` lines for the tile he is standing on (#3254, #3272) read `LODIndex=-1 ShouldBeVisible=1 ShouldBeLoaded=1`, and the host's Engine.ini already carries every streaming setting recommended so far. It is not the decision that is wrong, it is the time-slice: the host is at 9.7 GB of 15.6 GB with 895 levels and brings the level in a few components per frame.

v58q therefore sets `bBlockOnLoad` in the commit arguments for **the tile the player is standing on**, and only there: at most 24 blocking commits per round, only while the pawn is below z=12000 (so never for someone still in the aircraft), only while that tile is not already loaded and visible. That is a handful of frames of hitch on the host in exchange for the world being there when the player lands. `-noblockload` turns it off.

Also in v58q: the census runs every ~4 s instead of ~8 s, so the next log shows the 37 → 726 ramp instead of its endpoints, and the `[lq]` line is printed every 5th sweep instead of every 10th.

## v58r — the gate question is answered, and it was the wrong question

v58q was built to name the gate that blocks the loot spawn. It did, and the answer clears every suspect the last four builds chased:

```
[lq] Tore: keine World 0, kein GameMode 0, kein GameState 0,
           Match-Phase < 4 8, kein Owner 0, Owner nicht Authority 0
[lq] 11 Actors im Umkreis 300 m geprueft, 0 davon sind Loot-Quellen
     | insgesamt: 136 gefunden, 8 versucht, 0 ausgeloest
```

Eight attempts in the whole round, all eight in the waiting lobby, none afterwards. **Not one gate ever blocked a source out on the map — because out on the map there are no sources.** For the entire round, the scan found 0 loot sources within 300 m of the player.

The level walk says why:

```
[sw] Level-Walk #31: 1098 Levels, 5409 Actor-Slots, 35 Gebaeude
```

**1098 levels holding 5409 actors between them — five per level.** That is the signature of an HLOD proxy level, not a real one. And 35 building actors in the entire world, which is why our sweep only ever found a handful. The census around the landed player agrees: 4 actors for forty seconds after landing, 488 at the very end of the round, with fences, door frames, static meshes — and not a single `BPB-` building.

And every tile in every log we have ever taken comes in a pair:

```
Tile #2153 Prio=600 LODs=2/2  LODIndex=-1  ShouldBeVisible=1 ShouldBeLoaded=1
Tile #2154 Prio=500 LODs=0/1  LODIndex=-1  ShouldBeVisible=0 ShouldBeLoaded=0   <- never, in any round
```

The commit hook from v58i cannot reach the Prio-500 layer: it only sees tiles the engine decides to commit, and a tile the engine never wants is never committed. That is exactly why `[ct] korrigiert` froze at 317 and why the block-on-load budget (24 of 24, all spent) changed nothing.

**v58r calls `UWorldComposition::CommitTileStreamingState` itself**, from the game thread in the MaskOff window, for every tile within 250 m of the remote player that is not already loaded and visible at LOD −1 — six per sweep so the host keeps up, with the last two arguments captured from the engine's own calls. This is the engine's own entry point with our arguments, not a write into engine state; the v58h lesson (never write the result fields of a state machine) still holds. `[fx]` lines, `-nopulltiles` to turn it off.

Second instrument, and the one that decides whether this build is the fix or the next dead end: **`[td]` now prints the LoadedLevel and its actor count for the tile the player is standing on.** Two or three actors means he is standing on a proxy. A few hundred means the real level is there and the missing buildings have another cause.

Also noted for later: `APlayerController::ServerForceItemSpawn` (exec 0x2469890, implementation at PC vtable +0x1278, validate +0x1270) is a parameterless client→server RPC, answered by `ClientForceItemSpawnResult` (exec 0x2461600), and `ABravoHotelCharacter::ServerRequestBuildingItemSpawnList` (exec 0x2368DF0) is a second one. The game has a designed path for "client asks the server to spawn the items here". If v58r's measurement shows the buildings really are absent, that RPC is the next thing to follow.

## v58s — why standalone works and the listen server does not

Alen: *"when playing a Standalone match without a listen server it's running fine. Just make it run fine but with listen server."* That is the whole answer, and the v58r log shows exactly where the difference lives.

**The remote player was a sub point.** World Composition takes a list of streaming points. Point 0 is the primary one; every point after it is a **sub point**, and the tile lambda (0x4752FC2) treats those as second class — for a level that is not loaded yet it multiplies the distance by the sub-point scale, clamped to `[0.15, 0.45]` for the base level.

In a standalone match there is exactly **one** point and it is the player, so he gets full-distance base levels everywhere he goes. With the listen server the **host** was point 0 — sitting in the aircraft at z=74800 doing nothing at all — and the player actually on the ground was appended behind him as a sub point, at 45 % at best.

The v58r log says it in one line. The tile the player is standing on:

```
Tile #1730 Prio=600 LODs=2/2  LODIndex=0  ShouldBeVisible=1 ShouldBeLoaded=1
           LoadedLevel ... mit 3 Actors
```

Three actors. He is standing on an HLOD proxy — a baked mesh with no gameplay actors. No buildings, therefore no item spawn boxes, therefore no loot, and nothing solid to collide with either, which is the falling-through-buildings. The host, meanwhile, had the real levels around himself in the sky.

v58s puts the remote players in **first**, as the primary point, and appends the host behind them. While the host is above z=12000 — i.e. in the aircraft — he gets no streaming point at all. That is the standalone situation exactly: one point, the player on the ground. It also halves the tile load on a machine that has been sitting at 97 % memory. `-nohostfirst` restores the old order, `-hostpoint` always keeps the host's own point.

**And the second thing: v58r's tile pull never ran.** `CommitTileStreamingState` returns before touching anything while the tile's evaluation byte is 3:

```
ecx = [tile+0xD8]; ecx--; je apply; ecx--; je apply;
cmp ecx,1; je RETURN          <- Eval == 3 does nothing at all
```

Every tile in every log reads `Eval=3`, so all 174 `[fx]` calls were no-ops. v58s calls the three setters the commit body itself calls once past that byte, in its order: `vtable+0x268` SetShouldBeLoaded, `0x433E0F0` SetShouldBeVisible, `0x433DE00` SetLevelLODIndex(−1). Those are engine setters that notify `UWorld::UpdateStreamingLevelShouldBeConsidered`, not raw field writes, so the v58h lesson still holds. It runs every ~0.5 s now instead of every 2 s, and the commit hook uses the same 250 m radius so the engine does not argue back over the same tiles.

## v58t — floor loot confirmed; the ammo counter and the perk UI are a different layer

**v58s worked.** Alen: *"Boden loot funktioniert jetzt."* The log agrees: `[lq] 1707 gefunden, 36 ausgeloest`, `[cn] Actors um den Remote-Spieler 2109`, `[ws] Reihenfolge: Remote-Spieler ist Punkt 0 (Primaerpunkt)`, and the tile under him now carries 69 actors instead of 3. #6, #19, #22, #23 and #24 are done.

**No, that does not fix #15 or #16** — and the same log says why. They are one layer further in, and both have the same shape.

The reload itself is fine:

```
[ammo] t=23005218  MulticastStopSimulatingReload
[ammo] t=23013671  MulticastStopSimulatingReload
```

`ServerReloadWeapon` arrives on the host (exec thunk 0x248BF70, validate at character vtable +0x958, implementation +0x960; `ServerStartReload(float)` at +0x978/+0x980) and the stop-multicast goes back out. What arrives late is the new **magazine count**, and that travels as a replicated property on the weapon / inventory-item actor plus `ClientModifyInventoryItem`:

```
[ammo] t=23000093  ClientModifyInventoryItem
[ammo] t=23009125  ClientModifyInventoryItem
[ammo] t=23015937  ClientModifyInventoryItem
[ammo] t=23023781  ClientModifyInventoryItem
```

Four times in the whole round, **6 to 9 seconds apart**. That is exactly the delay Alen sees, and it is the same for the perk data on his PlayerState — `[ac] DoInAircraft ... #1, #2` confirms `ClientInAircraft` does go out, so the boarding notification is not the problem; the state behind it lags.

**Why nothing we built so far touches these actors:** v58k's wake-up only looks at level-placed actors with `NetDormancy > DORM_Awake`. A weapon is spawned at runtime, an inventory item is not in `ULevel::Actors` at all, and the PlayerState is neither. So they were never woken and never force-updated.

v58t finds them the way the engine itself does — by the **Owner chain** (`AActor::Owner +0x2B0`, the chain `AActor::IsNetRelevantFor` walks) — during the level sweep, and then pushes the pawn, the controller, the PlayerState (`AController::PlayerState +0x390`) and everything they own out **four times a second**: `FlushNetDormancy` when the actor is dormant, then `ForceNetUpdate`, on the game thread inside the MaskOff window where those are not the no-ops the standalone mask otherwise makes them (the v58c finding). `[ow]` lines name every actor found with its class, dormancy and role. `-noownpoke` turns it off.

`-ammolog` now also watches the perk, aircraft, magazine and simulation RPCs, so **one reload and one boarding in the same round** answer both questions with timestamps.

Names nailed down on the way: `ServerReloadWeapon`, `ServerStartReload`, `ServerStopReload`, `ServerUpdateReloadWhileProning`, `ServerPlayNoAmmoSound`, `ServerBackPackInTotalAmmoCount`, `ServerPlaySimulation`; the perk set `ServerAddPerk`, `ServerEquipPerk`, `ServerChoosePerkDeck`, `ServerReselectPerkDeck`, `ServerChangeRandomPerk`, `ServerSpawnPerk`, `ServerResetPerkInfo`; the client side `ClientAddInventoryItem`, `ClientModifyInventoryItem`, `ClientRemoveInventoryItem`, `ClientSyncInventoryItem`, `MulticastStopSimulatingReload`. The widget end is `UBravoHotelAmmoWidget::UpdateAmmo` and `UBravoHotelPlayerInfoWidget::UpdateAmmoCount`, both bound by name through the delegate helper at 0x1D78A20 — i.e. the widget only redraws when a delegate is broadcast, which is driven by the OnRep (`OnRep_Magazine`, `OnRep_Reload`).

## v58u — the reload, described precisely enough to name the cause

Alen: *"Ich hebe eine Waffe auf, lade nach. Danach steht immer noch null. Wenn ich aber die Waffe wechsle und dann wieder zurückwechsle, ist das Magazin voll. Dann wenn ich schieße von z.B. 20 auf 15, dann wieder wechsle und zurückwechsle, hab ich wieder 20 im Magazin ohne nachzuladen."*

That describes two separate stores on the client:

- **A** — the client's own copy of the inventory item
- **B** — what the ammo widget shows

B is refreshed from A on every equip, and updated locally while you shoot. Read the three observations against that:

| what happens | what it proves |
|---|---|
| pick up + reload → display 0, but re-equip shows full | A was already full; B was simply never refreshed |
| shoot 20 → 15, display follows | B is a local prediction |
| re-equip → 20 again, no reload | the shooting never reached A |

**A is full the whole time and never changes.** Nothing the host does to that item ever reaches the client; the only value the client ever sees is the one the item was created with. The host is fine because the host reads its own copy directly.

**Why nothing reaches it.** Our per-actor net-mode hook (v46) answers `NM_ListenServer` only for actors whose `RemoteRole == ROLE_AutonomousProxy` — that is the remote player's PlayerController and Pawn, and nothing else. His **weapon**, his **inventory items** and his **PlayerState** are not AutonomousProxy, so all of them were answered `NM_Standalone`, and every *"we are a server, so tell the owning client"* branch inside those classes quietly did nothing.

That is exactly the shape of the `DoInAircraft` bug, which we have been fixing one call site at a time since v58. This is the whole family at once — and it covers **#16** too, because the perk data lives on the PlayerState, which was in precisely the same position.

v58u answers `NM_ListenServer` for actors that **belong** to a remote player as well, found through the Owner chain built in v58t (`AActor::Owner +0x2B0`). `AActor::GetNetMode` runs roughly 6000 times a second, so a hash set is used as a fast filter — but every hit is verified against the real Owner chain before the answer changes, so a stale pointer can never widen it. `[om]` lines name each class and the calling site's RVA. `-noownmode` restores the old behaviour.

## v58v — what the v58u round settled, and one suspicion withdrawn

**v58u did not fix it**, and the log says why in two parts.

**1. Delivery is not the problem.** Host and client line up to the millisecond:

| host (`sp_listen.log`) | client (`BravoHotelGame.log`) |
|---|---|
| `t=26791375  ClientAddInventoryItem` | `12:50:11.730  [AddReplicationItems] Type: 1 Index: 1 Item: 9mm` |
| `t=26794234  ClientModifyInventoryItem` | `12:50:14.589  [ModifyReplicationItems] Type: 1 Index: 1 Item: 9mm` |

2859 ms apart on both sides. The perk RPCs (`ClientPerkSpinEvent`, `ClientAddPerkUIEvent`, `ClientAddPerkLevel`) and `ClientInAircraft` all went out as well. Nothing is lost or late in transit — **the host simply never sends the magazine count.**

The net-mode change did take effect (`[om]` shows `BP-Weapon_UMP9_LV4_C` and `BravoHotelPlayerState` switching to NM_ListenServer, 218 substitutions), so that gate was real but not the one holding the magazine back.

**2. A suspicion I had carried since v58j was wrong, and I am withdrawing it.** The line `[rg] ServerReplicateActors ... Summe 0 replizierte Actors` is not a symptom of anything. `UReplicationGraph::ServerReplicateActors` (0x13C6C60) has exactly **one** return path, at 0x13C7955, and it is `xor eax, eax` — the function always returns 0 by design. My counter measured nothing. The same goes for "only 14 ReplicateSingleActor calls": 0x13BE920 is not that function (it takes 10 arguments), which v58k already noted and I then kept quoting anyway. Both numbers are meaningless and every conclusion drawn from them is withdrawn.

**What is actually still open is narrow and physical:** where does the magazine count live, and does it ever leave the host? v58v measures exactly that, three ways:

- **`[ow]`** now prints `bReplicates`, `RemoteRole` and `NetUpdateFrequency` for every actor the remote player owns. A weapon with `bReplicates = 0` cannot send a property to anybody, and then the magazine has to travel inside the inventory array instead — a completely different fix from a weapon that replicates but whose channel never opens.
- **`[ch]`** logs every channel opened for one of his own actors, so "does the weapon reach him at all" stops being a guess.
- **`[wp]`** snapshots the weapon object four times a second and prints every 32-bit slot that changes to another small non-negative number. **Firing one magazine empty names the offset of the magazine counter outright** — no reflection table, no guessing. `-noweaponwatch` turns it off.

## v58w — host crash when a door is opened. My bug, from v58t.

Alen opened a door on the PC and the host died:

```
Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x00000040
[Callstack] 0x11E820A  0x13BADC9  0x1394877  0x13AB3F7  0x13B9734  0x43D7DEE ...
```

The crash site at `0x11E820A` is `mov eax,[rcx+8]` with `rcx = 0x38` — one instruction earlier the caller does `lea rcx,[rdi+0x38]`, so **rdi was NULL** and the read landed on 0x40. Every frame above it is replication-graph code (`0x13BABE0` → `0x1394820` → `0x13AB3B7` → `0x13B96A0`) reached through the net driver at `0x43D7960`, and the instruction just before the call tests `cmp byte [r9+0x20D], 2` — `NetDormancy == DORM_DormantAll`. So the graph was asked about an actor it had no entry for.

**What fed it was `PokeOwnedActors()`**, which I added in v58t. It collected actor pointers during the level sweep, kept them **forever** without ever re-checking them, and called `ForceNetUpdate` on each one four times a second. Opening a door destroys and respawns actors; the first pointer that went stale handed the engine a dead object. A `__try` around the call cannot save that — the memory is still mapped, it is simply no longer that actor.

Fixed on four levels:

- nothing is collected unless it **really replicates**, and never the short-lived classes (`Projectile`, `Bullet`, `Camera`, `Emitter`);
- every poke first checks the object is **still alive** in the GUObjectArray (`IsObjectAlive`) and still replicates;
- a dormant actor is registered with `AddNetworkActor` **before** `FlushNetDormancy`, so the graph has the map entry it looks up — that missing entry is literally the null that crashed;
- anything failing a check is **dropped from the list** instead of being retried forever (`[ow] ... verworfen N`).

The weapon watcher checks liveness the same way. **If anything like this happens again, `-noownpoke` switches the whole mechanism off and the host is safe immediately.**

### From the handoff Alen sent

`SP-hand-off-main/superpeople-research-handoff 5.6Sol pass 1/schemas/native-reflection/native-properties.json` contains 32 confirmed native properties with offsets. Two of them matter here:

```
BravoHotelCharacter::CharacterReplication   object_reference  +0x40C8
BHCharacterReplication::ReplicatedStateID   uint8  +0xD4   CPF_Net, OnRep_ReplicatedStateID
```

**The character has a dedicated replication proxy object.** That is exactly where a game puts state it wants pushed to the owning client, and a magazine count is that kind of state. It is a `UObject`, not an `AActor`, so it never has a channel of its own — it rides on the character's through `ReplicateSubobjects`, which is also why none of our channel-level instruments could ever have seen it. `[cr]` now diffs that object four times a second, exactly like `[wp]` does for the weapon.

The same handoff **retires two of our own leads**: in a run where loot worked, `ServerOnOverlapItemSpawnBox` and `ServerRequestBuildingItemSpawnList` were called **zero** times, so neither is the loot route — and loot is not demand-spawned at all, it spawns map-wide at match start. That is what our own v58s fix ended up doing, so the two findings agree. The `ServerForceItemSpawn` lead noted in v58r is downgraded accordingly.

Their loot conclusion ("actors must answer NM_Standalone") is our own v45/v46 finding quoted back — their document cites `sp_listen.cpp` by name. Nothing new for us there, but it is a useful independent confirmation that the narrow per-actor net mode is the right shape.

## v58x — the handoff, mined properly

Two files in `SP-hand-off-main/superpeople-research-handoff 5.6Sol pass 1/schemas/native-reflection/` carry real value. Everything else in the bundle is either about their solo profile or is our own earlier work quoted back.

**`native-functions.json` — 1368 natives with owner, name and exec-thunk RVA.** Every one of those was an unnamed address to us before. The thunks are ProcessEvent wrappers; for a parameterless native the real implementation is the call/jmp target right after the stack bookkeeping. Resolved and **verified by disassembly**, not taken on trust:

| Name | exec thunk | implementation | signature |
|---|---|---|---|
| `GetCurrentWeapon` | 0x235C540 | **0x2017300** | `void* (character)` |
| `GetCurrentRangedWeapon` | 0x235C3D0 | 0x2015BF0 | `void* (character)` |
| `GetPendingWeapon` | 0x235CFA0 | 0x201D5B0 | `void* (character)` |
| `OnChangeCurrentWeapon` | 0x2361E10 | 0x2040560 | `void (character)` |
| `IsInAircraft` | 0x235EC50 | 0x202F560 | `bool (character)` |
| `DoReload` | 0x2358EC0 | **0x1FF12C0** | `void (character)` |
| `DoReloadImmediately` | 0x2358EE0 | 0x1FF13E0 | `void (character)` |

`GetCurrentWeapon` is nineteen bytes and hands over the design:

```
mov rcx,[rcx+0x648]   ; Character+0x648 is the weapon holder
test rcx,rcx
jne 0x1C56650         ; resolver -> the weapon
xor eax,eax
```

Two things follow, both now implemented:

- **The weapon watcher no longer waits for the level sweep.** It asks the game four times a second and follows every weapon switch the instant it happens — which is exactly the moment Alen's display corrects itself, so it was the worst possible thing to be late on.
- **`DoReload` is hooked** (16-byte prologue, position independent, ending on an instruction boundary — the handoff's own crash was a prologue cut mid-instruction, so this was checked). `[rl]` lines now carry the exact millisecond the host executed a reload, and which weapon it was holding.

Together with `[wp]` and `[cr]`, one reload now pins the magazine field.

**`native-properties.json` — 32 confirmed properties with offsets**, already used in v58w for the character's replication proxy. Also newly on file: `BattleRoyaleGameMode::AircraftFlight +0xAC8`, `bIsUseAircraft +0xAD0`; `BravoHotelAircraftFlight::bScrambled +0x5E0` (net, OnRep_Scrambled), `State +0x889`; `BravoHotelPlayerController::SavedCharacterPawn +0x1770` (net, OnRep); `BravoHotelCharacter::STMComponent +0x608`.

The full recovered table is in **`NATIVE-RVAS-from-handoff.md`** next to this file.

**Two of our own leads are retired by the handoff.** In a run where loot worked, `ServerOnOverlapItemSpawnBox` and `ServerRequestBuildingItemSpawnList` were called **zero** times — neither is the loot route, so the `ServerForceItemSpawn` idea from v58r is dropped. And loot is not demand-spawned at all: it spawns map-wide at match start, which is what v58s ended up producing. Their own loot conclusion ("actors must answer NM_Standalone") is our v45/v46 finding; their document cites `sp_listen.cpp` by name.

Also noted, not needed while loot works: the loot arrays hang off `TBL-MapMode` (14 rows, OrbIsland resolves `MapMode = Default`); the tables are `TBL-AreaItemSpawn`, `TBL-AreaSpawnRate`, `TBL-GroupItem`, `TBL-RandomSpawnItem_Tournament`.

## v58y — the same crash again, and this time the branch has a name

v58w's liveness checks did not help, because **the pointer was never stale.** Reading the crashing code properly instead of theorising about it:

```
cmp  byte [r9+0x20D], 2     ; actor->NetDormancy == DORM_DormantAll
jne  skip
mov  rdi,[rbp+0x1D8]        ; the PER-CONNECTION context
lea  rcx,[rdi+0x38]         ; rdi was NULL -> rcx = 0x38
call 0x11E8200              ; reads [rcx+8] = 0x40 -> access violation
```

That branch exists **only** for a dormant-all actor, and it assumes it is running inside the replication graph's per-connection pass, where `+0x1D8` is set. We called into it from our own tick, outside that pass, so the context was null. A live actor, a valid pointer, and a code path that simply must not be entered from where we entered it. Both crashes, identical callstack, same cause.

v58k's wake-up never crashed because the actors it finds are `DORM_Initial` (4); dormancy 2 and 3 were only ever swept up by accident. **Both places now refuse dormancy 2 and 3 outright.**

**And the poke itself is off by default now** (`-ownpoke` re-enables it). It was a v58t hypothesis; the v58v log already disproved the premise behind it — delivery is fine, the host simply never sends the magazine — and it has since cost two host crashes while measurably buying nothing. That is a bad trade and it should not have taken two crashes to make the call.

Everything read-only stays and now runs independently of that switch: `[wp]` and `[cr]` (the field diffs), `[rl]` (the DoReload timestamps), `[ow]` and `[ch]`. The net-mode substitution from v58u also stays on — it is a pure read, it was measurably working, and it feeds off the same actor set, so the collection is no longer tied to the poke.

## v58z — the weapon is DORM_DormantAll. That is the whole of #15.

The v58y round (pick up, reload, switch away and back, empty the magazine) put the answer in two lines:

```
[ow] BP-Weapon_SKS_LV3_C (repliziert 1, Dormancy 2, Role 3, RemoteRole 1, NetUpdateFrequency 100.0)
[ch] Kanal fuer BP-Weapon_SKS_LV3_C ... auf Verbindung ...   -- six times, same connection
```

**Dormancy 2 is `DORM_DormantAll`.** The actor replicates once and then sleeps, and a sleeping actor sends **no property changes at all**. The channel reopening six times for the same weapon on the same connection is the sleep/wake cycle — the client receives the state as it was at each wake and nothing in between.

That is precisely Alen's description, now with a mechanism behind it: his copy of the magazine never moves, and only a fresh equip refreshes the display from it. The weapon replicates (`repliziert 1`), it has a channel, it is relevant — it is simply asleep.

**The fix, and why it is this one.** We cannot call `FlushNetDormancy` for it — that is the branch that crashed the host twice (v58y). But `NetDormancy` is an **input** field: the replication path reads `actor->NetDormancy` on every pass. So v58z writes `DORM_Awake` into it for the remote player's own sleeping actors — the weapon first, then everything else in the owned list (PlayerState, amplifier, vehicles). A one-byte write to a field we already read everywhere, no engine call, and nothing that can reach the crashing branch. `[da]` lines name each actor and what it was. `-nodormawake` turns it off.

### And my own instrument was broken

v58y produced **not one field diff**, and that was not the game. `SafeCopy` is a single `memcpy` inside one `__try`, so reading 0x600 bytes off an object that ends before the next page boundary fails **entirely** — and the old code then dropped the watcher and went quiet without saying so. That is why `[wp]` stopped after the "Beobachte ab jetzt" line and `[cr]` never appeared at all.

v58z reads in 0x40-byte chunks with a per-chunk validity mask, never drops the watcher over a failed read, and diffs **bytes as well as int32s** — a magazine counter is very often a `uint8`, which the old int-only filter could never have seen. The `[cr]` watcher also says out loud when `Pawn+0x40C8` is null, instead of leaving me unable to tell a missing object from a failed read.

## v59 — three builds measured nothing, and it was one missing guard

The v58z round: no `[da]`, no `[wp]` field diffs, no `[cr]`, and `DoReload` never fired — while `[ow]` and `[ch]` from the level sweep worked perfectly. Everything that went quiet had exactly one thing in common: **a liveness check I added in v58w.**

`IsObjectAlive` walks the GUObjectArray and compares the item's first pointer against the object. In this build that pointer sits at **+8 and is obfuscated**, so the comparison never matches and the function returns **false for every object**.

Our own calibration has been saying so since v58g, once per round, in a line I kept reading past:

```
[ws] GUObjectArray-Layout ABWEICHEND -> nur Controller-Abgleich
```

The older code respected that flag — `RememberRemote` checks `g_objArrayState > 0` before using it. The checks I added in v58w did not. So from v58w onward each of them returned false and silently disabled the thing it was guarding:

| guarded by the broken check | consequence |
|---|---|
| `PokeOneActor` | the poke was already fully dead in v58w — so v58w was never the crash fix either; the crash came from `WakeActorForClients`, which only v58y guarded |
| `RefreshWatchedWeapon` | the weapon was never re-derived from the game |
| `WatchWeaponFields` / `WatchCharRepFields` | no field diffs at all, both rounds |
| `KeepAwakeForClient` (v58z) | **the DORM_Awake fix for #15 has never actually run** |

v59 routes every one of them through `MaybeAlive()`, which only filters when the layout is usable and otherwise gets out of the way, and prints a loud one-off warning next to the calibration line so this cannot go quiet again.

**So the v58z conclusion still stands — the weapon is `DORM_DormantAll`, that is the mechanism behind #15 — and its fix runs for the first time in this build.**

Still open from that round and not explained by this: `DoReload` (0x1FF12C0) never fired although Alen reloaded. That address is the Blueprint-callable entry; the real reload path evidently runs elsewhere. Not chased yet — the field diffs come first.

## v59b — the DORM_Awake write crashed the host, and the v58z cause is dead

Two things came out of the v59 round, and both are corrections of mine.

**1. The host crash (#26) was my write.** v59 was the first build in which `KeepAwakeForClient` actually executed, and the host died during a reload with the same access violation as the two crashes before it. v58z's reasoning — "`NetDormancy` is only an input field the replication path reads, so writing it cannot reach the crashing branch" — was wrong. That byte is exactly what the branch selects on (`cmp byte [r9+0x20D], 2`), and once inside it the code dereferences the per-connection context at `[rbp+0x1D8]`, which only exists inside the graph's own per-connection pass. The log shows the fight in one tick:

```
[da] Die Waffe des Remote-Spielers 00000223D0568020 stand auf Dormancy 2 -> DORM_Awake gesetzt -- #1
[wp] 00000223D0568020: +0x20D  byte 0 -> 1      <- us
[wp] 00000223D0568020: +0x20D  byte 1 -> 0      <- the engine, same tick
```

Three of my interventions have now crashed the host for the same reason, and the rule was already written down in v58h: never write the result field of an engine state machine, only the decision in front of it. The write is off by default; `-dormawake` re-enables it.

**2. The v58z cause for #15 does not survive the measurement.** With the watchers finally alive:

- the weapon's dormancy is **not** stably 2 — it reads 2 at some polls and 0 at others, so "it sleeps, end of story" is too simple;
- across the weapon's whole first **0x800 bytes**, while Alen picked the weapon up, reloaded, switched away and back, and emptied the magazine, **nothing moved** except our own `+0x20D` write and a flag at `+0x9B`. The magazine count is not in that window;
- `BHCharacterReplication` does move: `+0xC0` runs 2 → 4 → 6 → 8 → 9, and `+0xD4` — `ReplicatedStateID`, the `OnRep` property from the handoff — runs 1 → 19 → 2 → 3.

So v59b widens the snapshot window from 0x800 to 0x2000 and adds a third watcher, `[wh]`, on **`Character+0x648`** — the weapon holder, which is certain: `GetCurrentWeapon` is nineteen bytes of `mov rcx,[rcx+0x648]; test rcx,rcx; jne resolver; xor eax,eax`. If the magazine of the current weapon lives in the holder rather than in the weapon actor, the next log names the offset.

## v59c — the magazine has an address, and the question is now relevance

The v59b round did what it was built for. `Pawn+0x648` resolves to a **`PlayerInventoryComponent`** — the game's own name for it — and on the weapon actor two adjacent ints move like nothing else in the whole 0x2000-byte window:

```
[wp] +0x1B0  int  1 -> 9 -> 16 -> 20      <- the reload, across four polls
[wp] +0x1B0  int  20 -> 12 -> 5           <- firing
[wp] +0x1B4  int  4 -> 24                 <- once, at the same moment
```

Everything else that moved in that window is float noise (transform/timestamps at `+0x1A48`–`+0x1BC6`). So `[mag]` now prints the pair with a millisecond stamp on every change; one round with Alen saying what the HUD actually showed turns "looks like the magazine" into "is the magazine".

**Delivery is now the whole of #15**, and the census line names a candidate we have beaten once already:

```
[cn] *** 1 x BP-Weapon_VECTOR_LV4_C (repliziert 1, immer-relevant 0, Dormancy 4, Cull 150 m)
```

150 m class cull, not always-relevant — the shape of the blue-zone bug (#10). `UBasicReplicationGraph` keeps a **per-actor copy** of that cull distance in `FGlobalActorReplicationInfo` (`+0x90`/`+0x94`) and checks it in the replicate loop; zeroing that copy is what made the zone work.

Whether it bites here depends on something never measured: the graph's distance is viewer → **actor location**, and an attached weapon can easily keep a stale root location on the server. If it does, the weapon is out of range almost always, its channel closes and reopens — **the v59b log has eleven channel opens for one weapon** — and the client only ever sees the full state of a fresh channel. That is exactly Alen's "after 5–10 s it applies by itself" and "switch away and back and it's right".

`[wl]` prints the weapon's location, the player's location, the distance, the two relevance flags and the graph's cull copy. The repair (zero the cull copy for the weapon, the pawn, the PlayerState and everything else the remote player owns) runs in the same round, because it costs nothing if the distance turns out to be zero. Switch `-noownrelevant`.

Also noted from that round and not yet explained: `DoReload` still never fires on a real reload (the hook installs fine), and the `[wh]` watcher on the inventory component went silent the moment the weapon appeared.

## v59d — relevance is dead, the magazine is certain, and two questions are left

`[wl]` answered its question in one line:

```
[wl] Waffe bei {-38201,-175208,15265}, Spieler bei {-38178,-175249,15307}
     -- Abstand 63 (1 m) | immer-relevant 0, nur-fuer-Besitzer 0, Dormancy 0
     | Graph-Info ... Cull 15000 / Sq 225000000
[wl] Graph-Cull 150 m -> 0
```

The weapon's location follows the player to within **0.6 m**. It was never outside the 150 m cull, so the cull never culled it, and zeroing the cull copy changed nothing. That is a clean disproof, not a failed fix: **relevance is ruled out**, as dormancy and "no channel" were before it. The repair stays in because it costs nothing, but it is not the fix.

`[mag]` confirmed the field beyond doubt, across two independent rounds:

```
1 → 2 → 9 → 17 → 18 → 19      reload
19 → 12 → 4 → 3 → 1           firing
7 → 14 → 20                   reload again
```

with `+0x1B4` sitting at **24** throughout = the magazine size. So `weapon+0x1B0` is the magazine, the host's copy is correct and live, and only the client's copy is stale.

Two things are left, and v59d measures both rather than guessing:

1. **The weapon's channel was opened thirteen times in one round**, always on the same connection — so something closes it just as often, and a channel that keeps closing cannot carry a property delta. The `SetChannelActor` hook now logs the **close** too, with the caller's RVA.
2. **`-magtest`** writes 99 into `weapon+0x1B0` exactly once per round. If the PC's display jumps to 99, the field is replicated and the fault is on the sending side. If it does not move, the client's HUD never reads that field and the inventory item (`ClientModifyInventoryItem`, four times a round) is the only path left. Off by default.

Still unexplained: `DoReload` never fires on a real reload, and `[wh]` on the inventory component goes silent once the weapon exists.

## Headless host

`host.bat` in the game folder starts the host straight into `LV-OrbIsland?listen` (map URL as first command-line token, plus the game's own `AutoStart=1?StartingPlayerCountRate=0?StartingTimeSecond=30` options). `host.bat` = small window; `host.bat nullrhi` = no rendering/sound. Check the host log for `LogNet: Browse: /Game/BravoHotel/Maps/OrbIsland/LV-OrbIsland?listen`. If the title screen appears instead, the game overrides the startup map and the DLL needs an `-autohost` switch (next step).

## Next test run (checklist)

1. Build v58q from the game folder (`build.bat` there), start host, join with PC. The Engine.ini stays as it is — everything recommended so far is already in it.
1a. Lobby check (passed in v58p, keep an eye on it): no loot on the ground, `Phase angehoben` stays **0**, `in der Lobby uebersprungen` counts up.
1b. **v59d: run the host with `-magtest`.** Pick up a weapon, fire slowly, reload, switch away and back. Three things decide the next build: the `[mag] *** TEST` line — **did the PC display jump to 99?** (yes = the field is replicated and the fault is on the sending side; no = the HUD never reads it and the inventory item is the only path left); the `[ch] ... wird GESCHLOSSEN (Aufrufer +0x...)` lines, which name whoever keeps closing the weapon's channel; and `[wl]`, now expected to keep reading ~1 m. Older lines that still matter:
1d. The `[td]` line for the tile you are standing on: `LoadedLevel ... mit N Actors`. In the v58r round that read **3** — an HLOD proxy. It has to read a few hundred now. Alongside it, `[ws] Reihenfolge: Remote-Spieler ist Punkt 0 (Primaerpunkt)` confirms the swap took effect, and `[lq] ... davon sind Loot-Quellen` has to rise above 0 out on the map. If the host stutters badly while you land, re-run with `-nopulltiles`; if anything about the host's own view breaks, `-nohostfirst` puts the old order back.
2. In `sp_listen.log` right after `=== World->Listen`: `[bz] FName BP_BlueZone_C = {...}`, `[bz] Objektsuche: BP_BlueZone_C @...`, `[bz] *** Zonenklasse ist jetzt bAlwaysRelevant ***`. If instead `FName ... noch nicht in der Namenstabelle`, the class loads late → fallback path, tell me.
3. Before match start on the PC: zone preview top right, big map, no storm sound? (`[bz] bAlwaysRelevant=1` in the log when the actor is found.)
4. Land far from the host, in or next to a town, and stay 2 minutes with the host in the air. The decisive lines are `[lq]` — how many actors near you are loot sources, how many were still open, how many fired — and whether there is loot on the ground. Then, if it still needs it, `[lr]` (the RVA the pickups are registered from — compare the paths seen before and after the host's touchdown) and `[gs]` (which gate the game's own check fails, and the host's own flag). Watch the `[gs]` line: `Match-Phase ... = N` before the host is down, and `Phase angehoben` counting up. **Is the loot there now, before the host lands?** The log lines that say what happened are `[wk] ... geweckt` (how many dormant actors were woken, and which classes), the `geweckt` column in the `[pl]` table, and `[rg] ... Verbindungen im Graphen`. Older but still useful, the `[pl]` tables (per class: added / considered / channel) and the `[pl]` detail lines for the pickup classes. Also still useful, the `[cn]` census blocks: `Um den REMOTE-Spieler` should now list buildings, fences, door frames and window actors like the pre-match one does, and the `***` lines say whether pickup/item classes are there. `[ft] Kachel #... LODIndex 0 -> -1` shows the forcing working. Old checks: `[vw] Verbindung ... -> Blickpunkt {x,y,z}` and the `[vw] Remote-Pawn ... Abstand` line right after it: if the distance is large, that was the bug and `[vw] Blickpunkt ... korrigiert` should have fixed it. Then `[cn] Actor-Klassen im Umkreis` — pickups in that list mean the items really are next to him on the host. Then, as before: loot, doors, windows? Watch `[bp] BeginPlay ... erzwungen` (if this stays 0 the timers were never denied by net mode and the sweep line decides), `[sw] Level-Walk ... gueltig dekodiert > 0`, `nahe Remote (... ohne Timer N)`, `[ls] Spawn-Pruefung an Gebaeude ... Spawn-Liste a->b, GESPAWNT`, and in the 20 s line `davon nahe Host N` vs `fuer Remote-Pawns M`.
5. Board the plane on the PC: `[ac] DoInAircraft fuer Remote-Pawn ...` should appear once; does the perk UI update faster?
6. If anything is worse, isolate with `-nozonerelevant`, `-nospawnremote`, `-noaircraftfix` (one at a time).
7. Reload test with `-ammolog`: note the exact time of one reload on the PC.
