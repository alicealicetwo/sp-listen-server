# sp_listen — open issues (14.09.2026, state after v135)

Status of every known problem in the SUPER PEOPLE listen-server setup.
Companion documents in the same folder:

- `FINDING-dormancy-gate.md` — the DORM_Initial gate; read before touching replication
- `AI-ORIENTATION.md` — how the whole thing works, written for a fresh AI session
- `HANDOFF-sp_listen.md` — setup, build loop, conventions
- `ISSUESsp_listen.md` — the long historical log, one section per version

Confidence labels: **proven**, **likely**, **unknown**.

---

## The rule this session cost the most to relearn

**Never write the result field of an engine state machine — only the decision
in front of it.**

v130 wrote `CurrentEquipWeaponID` (character `+0x3A4`) for one frame to force a
RepNotify. The host crashed on the very next weapon pickup:

```
[wa]  neue Waffe BP-Weapon_KAR98_LV3_C ... (Pawn 0000016FC462AAB0)
[wp2] CurrentEquipWeaponID 367 ... fuer EINEN Tick veraendert
[ax]  *** Ausnahme 0xC0000005 an base+0x20CF35A
      0x20CF34A  mov rcx, [rdi + 0x3A0]      ; rdi == that same pawn
      0x20CF35A  subss xmm8, [rcx + 0x2E0]   ; <== fault
```

That ID is a live lookup key and the host caches the resolved data in the
neighbouring field `+0x3A0`. The zone *reference* was safe to toggle because
nothing caches off it. The two are not the same shape, and "it worked for the
zone" is not a reason. **v132 removed the write permanently**; the remaining
`AttachmentIndices` nudge is off by default (`-weaponpush` to enable).

A second, milder version of the same error: v123/v124 wrote `GameState+0x7D4`
and `+0x7F0` on every join. Those offsets belong to the **zone actor**, not the
GameState — the writes went into unrelated fields for one frame each. Fixed in
v125. **Confirm which class owns an offset before writing to it.**

---

## Instruments added this session

| Tag | What it shows |
|---|---|
| `[ax]` | every hard fault: address, all registers, real call chain — **works without `-log`** |
| `[cg]` | the channel guard catching an unusable actor before the engine dereferences it |
| `[zi]` | the zone actor's own name / index / phase list |
| `[zt]` | the zone reference toggle firing |
| `[ar]` | whether the zone is in the AlwaysRelevant node |
| `[mv]` | every server position correction, with base and `bHasBase` |
| `[sm]` | arriving `ServerMove` vs acks vs corrections |
| `[rr]` | every reload event with magazine / capacity / backpack |
| `[wa]` `[wd]` `[wx]` | weapon attachment slots, byte diff, stowed weapons |
| `[at]` | anything named Attachment/Sight/Scope/Item/Slot/Inventory/Equip, on **any** object |
| `[lt]` | ladder state machine read straight from the component |
| `[tl]` | which `SLV-` tiles the host holds near the remote player |

---

## Closed

| Issue | Fixed in | How |
|---|---|---|
| Magazine/reload display, ammo lost on weapon switch | v87/v88, per-player v112 | host never decrements; sp_listen counts shots and pushes `ClientSetMagazine` |
| Doors would not open, windows would not break | v96 | the DORM_Initial gate |
| Pills | v104 | ported from `PILLS-WORKING-HANDOFF.md` |
| Crashes unreadable without `-log` | v113/v114 | `[ax]` vectored reporter |
| Host crash `AV 0x40` / `0xffffffff` | v117 + v126 | see issue 1 |
| Blue zone data missing on the host | — | **it never was.** v124 read the wrong object; the host always had it |

---

## Open

### 1. Host crash — guarded, root cause still open

Three crashes, all the same call, all now with full chains:

```
our tick -> ServerReplicateActors 0x13C6C60 -> 0x13C2990 -> 0x13C3DB0
         -> 0x13C3E9F call 0x3F81A20   (rcx = Actor, rdx = Connection)
            -> 0x43E5D70 -> 0x43E7870 -> 0x2D1BCA0 FWeakObjectPtr::operator=
```

- actor `0x7F12038200388E04` — non-canonical, freed and reused
- actor **NULL** — died one level deeper on `Actor+0x40`
- actor's **class** = `0x656E6F4E` = the ASCII text `"None"`

**proven**: the graph hands the channel layer an actor whose *contents* are
dead. The pointer itself passes every address test, which is why v117 let the
third one through.

**v126 guard**: also checks the actor's vtable, its class, and the class's
vtable — all must be inside the game module. Mirrors the engine's own early-out
at `0x3F81BAD` (a bare `ret`). `-nochanguard`.

**refuted**: "actors are added and never removed" — removals run 4–8× more
often than adds (`AddNetworkActor 11712 / Remove 51262 / 93696`).

**unknown**: which graph list holds the dead entry. `[cg]` now logs the vtable
and class it rejected; that narrows it when it next fires.

### 2. Blue zone — narrowed to one place

**The host always has the data** (v125 corrected my offsets):

```
[zi] Zone ...: SelectedPlayZoneName='LV-OrbIsland_ALPHA_16_10_1' | InfoIndex=1 |
     AreaDescKey='Hidden Valley' | ClientPlayZonePhaseList Num=6 Max=6
```

Correct offsets, **on the zone actor, not the GameState**:

```
zone + 0x7C8  SelectedPlayZoneName       FName    RepNotify
zone + 0x7D4  SelectedPlayZoneInfoIndex  int32    RepNotify
zone + 0x7D8  AreaDescKey                FName    RepNotify
zone + 0x7E8  ClientPlayZonePhaseList    TArray   RepNotify   (Num +0x7F0)
GameState + 0x8D0  the zone reference             RepNotify
```

**The zone is in the AlwaysRelevant node** (`[ar]`, alongside `LV-OrbIsland_C`,
`BravoHotelWorldSettings`, `BattleRoyaleGameState`). So the graph holds it.

**What decides good vs bad rounds**: whether the zone's channel opens at all.

```
zone channel first, GameState 0.8 s later   -> worked
16 ms apart                                 -> worked
GameState first, zone 35 s later            -> broken
GameState first, zone 66 s later            -> broken
never (60 s, 18 ForceNetUpdates, 0 channels) -> broken
```

**proven**: `ForceNetUpdate` cannot repair it — it lifts the send-rate throttle,
it does not mark a property dirty. That is why v111 fired in every broken round
and changed nothing.

**v122's toggle works but cannot fire in the worst case**: its trigger *is* the
channel opening. When no channel ever opens, nothing happens. That is the flaw
to fix.

**Next**: per-connection relevancy for the zone — why does a connection refuse a
channel for an actor that is in the always-relevant list? Measure before
touching. Do not force a channel open from our own tick (rule 2 — that is what
produced the `0x40` crash).

### 3. Movement: rubber-banding, ladders, lobby jitter — one cause

The PC's own log, 36 times, at **19.2-second intervals to the millisecond**:

```
LogNetPlayerMovement: Warning: CreateSavedMove: Hit limit of 96 saved moves
```

A fixed cadence is a constant leak, not ping. The client holds every move until
the host acknowledges it; at 96 it discards the buffer and the prediction snaps
back. **proven** that this is the same cause for the rock rubber-band, the
ladder letting go after ~3 s, and the lobby jitter.

The ledger (`[sm]`), one round:

```
Zuege: 4306 angekommen, 604 bestaetigt, 90 korrigiert -> 3612 ohne Antwort (83%)
```

starting near 52% and climbing to 88%. **Caveat**: UE4 packs several moves into
one `ServerMove` call and one ack frees every move up to its timestamp, so
"ohne Antwort" is not literally lost moves. The direction is unambiguous; the
exact accounting is not.

Stone rubber-band, measured (`[mv]`):

```
ts=90.04  base = FoliageInstancedStaticMeshComponent  bHasBase=1  mode=1 (walking)
ts=93.82  base = none                                 bHasBase=0  mode=3 (falling)
ts=98.86  base = FoliageInstancedStaticMeshComponent  bHasBase=1  mode=1
ts=102.20 base = none                                 bHasBase=0  mode=3
```

Z drops 3848 → 3405 → 3117 → 2856. The rocks are **foliage instances**, and the
server does have them — it puts you on one, then decides you are falling.

**refuted**: the tile-LOD theory. Host and client both load `_High` and `_Low`
of the same tile, `_High` as close as 11 m, `_Props` at 31–59 m.

### 4. Ladders

`ServerTryUseLadder` arrives with a **valid** ladder pointer every time
(`BP-Ladder_Red_C`, `BP-Ladder_ChocoM_C`). 2–5 s later the **client** sends
`ServerTryExitLadder`. One attempt held 13.4 s. The handoff's own test saw the
identical ~3.6 s. **proven**: the reference is fine; this is issue 3.

`[cs]` came back completely empty — the character state machine is **not**
dispatched through ProcessEvent. `[lt]` (v122) reads it directly instead:
component at `Character+0x678`, `LadderState +0xD0`, `UsingLadder +0xD8`.

RVAs: `ServerTryUseLadder_Implementation` **0x2079CA0** (vtable `+0xE70`),
`_Validate` `+0xE68`, `execServerTryUseLadder` **0x236BB10**.

### 5. Invisible weapon — cause known, fix withdrawn

The PC log names it exactly:

```
LogSkinnedMeshComp: Warning: GetSocketByName(Sight_Basic): No SkeletalMesh
    for Component(SkelMesh) Actor(BP-Weapon_SCAR_LV3_C_...)
```

**proven**: the weapon actor exists on the client and its `SkelMesh` component
has **no mesh assigned**. Nothing to draw. Not an attachment problem.

Two RepNotify properties drive it: `AttachmentIndices` (weapon `+0x190`, TArray,
`Num +0x198`, `OnRep_ChangeAttachments`) and `CurrentEquipWeaponID` (character
`+0x3A4`, int32, `OnRep_ChangeEquipWeapon`).

The v130 attempt to force both crashed the host — see the rule at the top.
**Any retry must not write `+0x3A4`.** The channel churn is real and probably
the reason the properties arrive as opening state rather than as a change: one
AKM took **five** different channel objects in a round, and one channel object
was the zone's and later a rifle's.

### 6. Weapon attachments — separate bug, host may not be involved

**Alen's description**: the attachment goes onto the weapon, but not into the UI
and the slot, *as long as the weapon is not in your hand*. Holding it works.

**This is not the same bug as issue 5.**

Two instrumented rounds with sights actually being mounted produced **zero**
lines — `[wx]` empty, `[at]` empty, the word "Attachment" appears once in the
whole log (a startup name list). The round was real: 160 s, four weapon
switches, `[wa]` and `[pe]` both alive.

So no function named `Attachment` / `Sight` / `Scope` runs on the host at all.
v135 widened `[at]` to `Item`, `Slot`, `Inventory`, `Equip` — the generic
inventory names the reflection table actually has (`ServerMoveItem`,
`Request_EquipItem`, `AttachToWeaponBySlot`, `SetAttachmentSlot`). **Not yet
run.**

If that is empty too, attaching never reaches the host and this is client-side —
which closes it as *not our bug* rather than leaving it open.

### 7. Reload sometimes refused

`Rucksack` (`weapon+0xE50`) reads **0 on every weapon, always**. Shotguns still
reload shell by shell (0→1→2→3→4). A 30-round weapon started a reload at
`Magazin 0 / Kapazitaet 30, Rucksack 0` and nothing happened for 41 s.
Earlier rounds showed the host cancelling: `ServerStartReload` → `ServerStopReload`
125 ms later, twice, then `ClientStopReload`.

`[rr]` now prints the triple on every reload event. **unknown** whether the empty
backpack is the gate or a red herring.

### 8. Two players do not hear each other's shots

`MulticastFireProjectile` fires 1:1 per shot (**proven**); the RPC switch forces
multicasts remote (**proven**). Whether it reaches the other client is
**unknown** — every instrument so far watched only the joining player's own
weapon.

### 9. Window break sound

**proven**: windows are HISM actors; a break moves an instance between two
actors and **no RPC fires**. Client-side work, not replication.

### 10. Walking through a kicked door

**proven**: host-local physics, all `Callspace 2`. Replicating a physics pose.

### 11. #16 perk UI late / #8 lobby movement / #20 ping shows for host / black pill

Untouched or previously diagnosed; see history.

---

## Not code problems

- **Corrupt client pak** `pakchunk8500_s1-WindowsClient.pak` — 176 packages, 148
  of them building art. Copy the host's copy over.
- **Host bandwidth cap** `MaxClientRate` / `MaxInternetClientRate` = 50000 — see
  `Engine.ini.host-netrate.txt`.
- **Client log noise**: `SetActiveLevelCollection attempted to use an out of date
  NetDriver` ×1333. Unexplained, possibly downstream of issue 3.

---

## Switches

| Switch | Turns off |
|---|---|
| `-nochanguard` | v117/v126 channel guard (issue 1) |
| `-nocrashlog` | `[ax]` crash reporter |
| `-nomapguard` | v110 null-map guard |
| `-nozonetoggle` | v122–v125 zone reference/index/list toggle |
| `-nozonepin` | v120 zone dormancy pin (never fired; zone never sleeps) |
| `-nowatchattach` | the weapon/attachment instruments |
| `-weaponpush` | **opt-in** — the v130 remnant. Off because v130 crashed the host |
| `-nopills`, `-nodormwake`, `-wakedoors`, `-nogskick`, `-nomagtrack`, `-nomagpush`, `-nopelog`, `-norpc`, `-nodormfix` | as before |
