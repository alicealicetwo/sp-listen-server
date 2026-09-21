# Client measurement mode (`-spclient`)

The DLL can also run on a **player's** machine, in a read-only measurement mode. It hosts nothing and writes nothing into the game; it only hooks two functions and logs what they see. This was used (v162) to find why reloading was refused: `CurrentState` and `bWantsToFire` of the weapon are not replicated, so the host could never see them.

Hooked in client mode:

* `ABravoHotelCharacter::DoReload` (`0x1FF12C0`) — what the R key calls
* `ABravoHotelRangedWeapon::CanReload` (`0x1EF2F80`) — the check that says "no" on an empty magazine

## Steps

1. Build the DLL as usual; the first line of `sp_listen.log` must show the version you built.
2. Copy `dxgi.dll`, `DList.ini` and `sp_listen.dll` into the **player's** `Binaries\Win64` folder.
3. Start the client with `-spclient` appended to its command line. `sp_listen.log` then appears in the player's folder and starts with `[cx] Client-Messmodus aktiv: CanReload gehookt, DoReload gehookt`. If it says `NICHT gehookt`, the exe version is wrong.
4. In game: pick up a weapon, fill the magazine; fire a few rounds, press R (reloads); empty the magazine, press R (refused); press R twice more, switch weapons and back.
5. Send both logs: the player's (with the `[cx]` lines) and the host's.

Each `[cx]` line shows whether `CanReload` said yes or no and every value it derived that from: magazine/capacity, state (0 idle, 1 firing, 2 reloading, 3 bolt, 4 drawing), bolt flags, ready, reload flag, the `+0x21E0` gate, the equip bits and `bWantsToFire`. The value that flips between step 4.2 (yes) and 4.3 (no) is the cause.

Note: the client measurement code was removed from the source in the v170 restructure (it was measurement-only). `legacy/sp_listen_v169.cpp` still contains it (`g_clientMode`, `MyCanReload`, `MyDoReloadC`); the open issues #3 and #4 in `NOTES.md` (aircraft HUD, white capsule) will need the same approach, so it is worth porting the client-mode scaffold back as its own section.
