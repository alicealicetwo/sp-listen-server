# NOTES — what we know, what is fixed, what is open

Everything here refers to `BravoHotelClient-Win64-Shipping.exe` build **1.3.0.473797**. All addresses are RVAs (add the image base). Read `docs/ORIENTATION.md` first if the "standalone mask" and the replication graph are new to you.

## Current state (v174)

Works in a real round with remote players: join (direct or via lobby), aircraft, landing, world streaming around remote players, loot spawning, weapons (magazine, reload, bolt, attachments, shots heard), doors opening for the player who opens them, blue zone (visible from the lobby on, in world and on the map), match end with results and gold booking, admin command file. Host CPU is fine at 25–30 net ticks/s with logging on.

## Open issues

| # | issue | what we know | next step |
|---|---|---|---|
| 1 | **Door state does not replicate.** A door opened or kicked by one player stays closed for others; a kick on a door the host already considers open is refused (`CanKickDoor`). | `ABravoHotelDoor` (SDK): `DoorState` +0x490 (rep, `OnRep_DoorState`), `CurrentHP` +0x398 (rep), `DestructedPoint` (rep). The actors are level-placed, `bReplicates=1`, woken from `DORM_Initial`, cull set to 0 and `ForceNetUpdate`d (v172 `-doorpush`) — the client still never receives the change. The graph's per-actor loop considers doors ~100k times per round. | v174 counts opened **channels** per class (`Kanaele:` in the `[ls]` line) and prints `DoorState`/HP of pushed doors. If doors get 0 channels, the graph considers them but never replicates them: hook `UReplicationGraph::ReplicateSingleActor` (`0x13BE920`) for door actors near a remote player and look at which gate drops them, or open the channel yourself as the zone fix does (`MarkAlwaysRelevant` + re-add). |
| 2 | **Window break has no sound for other players** (the break itself is visible). | Windows are `BravoHotelHIDestructibleComponent` (a HISM component, SDK): visual = replicated `CurrentHpList`/`DisableInstanceArray`; sound/effect = **`MulticastOnDestructComponent(RepDestructCompInfo)`**. Component RPCs resolve their callspace through the owner's `AActor::GetFunctionCallspace` (hooked, ORs `CS_Remote` for multicasts) and are sent by the graph's `ProcessRemoteFunction`, which skips connections without a channel for the actor. Only ~13 multicasts leave the host per round. | v174 logs every net function once with the callspace answer (`[cs]` lines). If `MulticastOnDestructComponent` appears with "wir 3" and there is still no sound → the graph drops it: send it yourself per connection (or open the actor channel first). If it never appears → the component path bypasses the hook: hook `UActorComponent::GetFunctionCallspace` / `CallRemoteFunction`. |
| 3 | **Perk/level HUD shows Lv.0 in the aircraft**, correct after the jump. | Perk delivery is the client RPCs `ClientAddPerkLevel`, `ClientPerkSpinEvent`, `ClientPhasePerkLevelUpReady` (pawn) and `ClientAddPerkUIEvent` (controller); they arrive (the player's log shows the level-up telemetry). `ForceNetUpdate` on pawn/controller/PlayerState right after them (v172) changes nothing. | Client-side: the aircraft HUD widget does not refresh from the RPC. Candidates in the SDK: `BravoHotelCharacter::IsMaxLevel`, `GetPerkMaxLevelCount`, the HUD `UpdatePerkInfo` (the player's log shows `UpdatePerkInfo()` DataTable warnings for `ExpertWeapon00`/`ExpertType01` rows missing in the composite table — that may be the actual cause: the HUD fails a row lookup and never draws). Needs the same DLL in client mode on a player's machine. |
| 4 | **White capsule says "already max level"**; black not tested; R/G/B work. | The v104 pill port aliases white→row 220000104 and black→220000105, repairs the buff-index cache and the loot weights; all three checks pass in the log (`[pill-data] ... aufgeloest`). The "max level" message therefore comes from a different check than the ones hooked. | SDK: the inventory slot calls `CanUseCapsuleItem` → `IBravoHotelPlayerControllerInterface::CanApplyActiveItem`; the character has `IsMaxLevel()` and `GetPerkMaxLevelCount(Slots)`. Hook `CanApplyActiveItem` on the host with `-rpcnames`-style logging to see which input fails for the white row (probably the skill-slot count for the "Renewal" ability 221000337). |
| 5 | **Movement corrections / jitter.** | Per round ~7,500 `ServerMove` in, ~760 `ClientAckGoodMove` out (that ratio is normal UE: acks are throttled to ~3/s) and ~60 `ClientAdjustPosition` corrections. Corrections are what the player feels. The client logs `Hit limit of 96 saved moves` in bursts (landing, match end). | Log the correction reasons: hook `UCharacterMovementComponent::ServerMoveHandleClientError` or count corrections per phase; check whether the host's simulation of the remote pawn lags (net tick 25/s vs client 60 moves/s) and whether `ServerMoveDual` is handled. |
| 6 | Kicked-door physics / pushing players through doors. | Same family as #1. | After #1. |
| 7 | Invisible weapon (rare): remote player's weapon mesh missing on other clients. | Attachment arrays replicate now; the remaining case is pickup ordering (channel opens before the mesh index is set). Never write `Character+0x3A4`. | On a new weapon for a remote pawn: `ForceNetUpdate` + the one-frame reference toggle used for the zone. Switch `-weaponretoggle` (not written yet). |

## What the SDK gives us

`Super-People-Preservation` (a separate project) contains a reflection dump of this exact exe: `SDK/metadata.json` (classes, properties with **offsets**, functions with parameters), `symbols.json`, `enums.json`, `include/SDK.hpp`. Since v173 all new offsets come from there instead of memory probing. Useful verified facts:

* GameMode vtable: `HandleStartingNewPlayer` slot 0x6D8; `RestartPlayer` slot 0x7E0 → BR impl `0x1CC38E0` (= our `GM_InitNewPlayer` hook target, so that hook runs on every respawn); `HasMatchEnded` slot 0x880 → `0x1CB3B30`; `GetBattleRoyaleState` `0x1CB2110` reads `GameState+0x9F2`; `EndMatch` `0x1CAC3F0`; `DoMoveToLobbyLevel` `0x1CEA190`; `ChoosePlayerStart` `0x1CF2D40`; `FindPlayerStart` event stub `0x47F3190`.
* `EBattleRoyaleState`: 0 None, 1 Waiting, 2 Ready, 3 CheckStartPlay, 4 Play, 5 MatchEnd (the `[gs] Match-Phase` value).
* Native dispatch: `UFunction+0xFC` holds an encoded native index; `0x2B128E0` decodes it via table `0x6B3C020` (how `CanEquipWeapon` wrapper `0x255F260` reaches impl `0x1C47910`). Use it to find a BlueprintNative implementation by name.
* `ACharacter::Mesh` +0x3A0; `USkinnedMeshComponent::VisibilityBasedAnimTickOption` +0x684, `bEnableUpdateRateOptimizations` bit 0x02 at +0x688. `USceneComponent::ComponentToWorld.Translation` is at +0x110 (what `ActorLocation()` reads); `RelativeLocation` +0x134.
* `ABravoHotelDoor`, `BravoHotelHIDestructibleComponent`, `BravoHotelWindow`: see open issues #1/#2.
* `ABravoHotelCharacter::ServerStartKickDoor(TargetDoor)` — the RPC carries the door actor; `ABravoHotelDoor::KickDoor(InstigatorLocation)`, `CanKickDoor()`.
* Only `Tablet_R/G/B/White/Black` exist in the object inventory — there is no gold capsule asset; "gold" is `Tablet_Black`.

## Findings by version (this restructure onwards)

**v170 — clean restructure of v169.** 12,800 lines → 4,037, 21 numbered sections (`//  == N.`), one `Config` switch table, English comments, German ASCII log text kept, panel wire format unchanged (`[cmd] <id> ok|fail <text>`, `[gg] #n t=<unix> CommitRequest[CurrencyGain] (...)`). Every hook/RVA/default of v169 kept; dropped only measurement code and OFF-by-default experiments (three log-only hooks: `DoReload`, `RemoveNetworkActor`, `ReplicateSingleActor` tally). Deliberate changes: `-nopelog` only silences the `[pe]` lines (the ProcessEvent hook stays, shot counting needs it); the magazine push runs for every remote pawn; ProcessEvent caches its verdict per `UFunction*` (was a name resolve per call at ~6,000 calls/s).

**v171.** `-noperkwake` (phase-change wake-up — turned out to fire too early), movement counters, `-rpcnames` diagnostics.

**v172.** Perk push right after the perk RPCs (`-noperkpush`); door/window push: cull 0 + flush + `ForceNetUpdate` for door/destructible actors used near a remote player and for all doors within 10 m of a kick, for 3 s (`-nodoorpush`); `-zonepolicy` experiment (reads/sets the graph's per-class routing byte — it was already 1 = RelevantAllConnections, so the switch stays off); zone-channel close diagnostic; graph per-class counters.

**v173.** **Blue zone fixed**: `FindBlueZone()` now runs before the first client joins. In every round where the zone failed, the player's GameState channel had opened before the DLL knew the zone reference; in the rounds where it worked the reference was known first. Also `-noanimtick` (forces `AlwaysTickPoseAndRefreshBones` on remote pawn meshes; the game rewrites the option to `OnlyTickMontagesWhenNotRendered` every frame, so this is probably neutral — kept, harmless) and the ack/correction split in the movement counters.

**v174.** Diagnostics for the two open replication bugs: `[cs]` callspace log per net function, per-class channel counters, `DoorState`/HP probe on pushed doors. No behaviour change vs v173.

## Switches

Every fix has an undo switch; new, unproven behaviour ships OFF. Defaults as of v174:

| switch | default | area |
|---|---|---|
| `-nomask` | mask ON | the standalone mask itself (turning it off breaks everything; debugging only) |
| `-noactormode`, `-noownmode`, `-noaircraftfix`, `-nobeginplayfix` | ON | `AActor::GetNetMode` answers for remote pawns / their owned actors / `DoInAircraft` / BeginPlay spawn timers |
| `-norpc`, `-remoteonly` | rpc ON, remoteonly OFF | RPC callspace: client/multicast RPCs get `CS_Remote` OR-ed on |
| `-allowreplay`, `-allowrebase`, `-nolevelvis`, `-noenctoken`, `-nopay` | replay off, rebase blocked, level refs forced, token ignored, pay patched | engine/game patches |
| `-nostreamremote`, `-nohostfirst`, `-hostpoint`, `-nosubscale`, `-nocommitfix`, `-noblockload`, `-nopulltiles`, `-noviewfix` | ON | world streaming around remote players, viewer fix |
| `-nodormblock`, `-dormlevel`, `-dormallowweapons`, `-nodormwake`, `-wakedoors`, `-nodormfix`, `-nowakeup` | block ON, wake ON | dormancy handling (never DormantAll/Partial for dynamic actors; `DORM_Initial` level actors woken) |
| `-nozonefix`, `-nozonerelevant`, `-nogskick`, `-nozonepin`, `-nozonetoggle`, `-zonepolicy` | ON except `-zonepolicy` | blue zone |
| `-noownrelevant` | ON | graph cull 0 for remote players' own actors |
| `-fpp`, `-tpp` | — | force view type |
| `-nospawnremote`, `-nolootscan`, `-nodirectspawn`, `-nogatebypass` | ON | loot spawning around remote players |
| `-nomagtrack`, `-nomagpush`, `-noservchanged`, `-noboltfinish`, `-noreloadfinish`, `-noemptypulse`, `-pulseonlyempty` | ON | weapons |
| `-nopelog`, `-rpcnames` | pelog ON, rpcnames OFF | ProcessEvent logging / RPC name census |
| `-noperkwake`, `-noperkpush` | ON | perk/level pushes (no visible effect yet, see issue #3) |
| `-nodoorpush`, `-noanimtick` | ON | door/window push, anim tick (issue #1) |
| `-nogoldgain`, `-loadout`, `-cmdfile[=path]` | gold ON, loadout OFF, cmdfile OFF | backend integration |
| `-nocrashlog`, `-nomapguard`, `-nochanguard`, `-nographguard` | ON | crash reporter and the three crash guards (keep them) |
| `-nopills` | ON | capsule compatibility (v104 port) |

## Log tags

`[+]`/`[i]` install, `[gs]` match phase, `[ls]` 20-second status line with every counter, `[bz]`/`[zc]`/`[zt]`/`[gz]` blue zone, `[pb]` possession, `[ws]`/`[ct]`/`[fx]` streaming, `[dm]`/`[dw]`/`[dz]` dormancy, `[lq]`/`[ls]`/`[sw]` loot, `[mag]`/`[mp]`/`[ep]`/`[sc]`/`[rr]` weapons, `[pe]`/`[rn]`/`[cs]` RPC diagnostics, `[pk]` perk push, `[dp]` door push, `[at]` anim tick, `[gg]` gold, `[cmd]` command file, `[pill]`/`[pill-data]` capsules, `[gd]`/`[cg]`/`[mg]`/`[ax]` guards and crash reporter.

## Host performance notes

Already done: verdict caches in ProcessEvent and per-class lookups, no diagnostics in the hot paths. Candidates if a host struggles with many players: `WeaponTick` every 30 ticks instead of 15; `LootSweep` restricted to tiles near remote pawns; `ZoneTick` gated on a dirty flag set by channel/graph events; `-nullrhi` hosting (default in the host scripts); `MaxClientRate` 150000 in `Engine.ini`.
