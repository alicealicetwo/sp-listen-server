// ============================================================================
//  sp_listen.cpp  (v136)  —  Listen-Server fuer BravoHotelClient 1.3.0.473797
//
//  v60: #15 is settled, and not by another round. Alen zipped the shipping exe
//       into the game folder, the zip staged in seconds, and the binary
//       answered the question directly.
//
//       The game keeps a native reflection table in .data. Property names are
//       obfuscated with byte[i] ^= (0xDD + i) and each descriptor is
//       { name, repnotify_name, uint64 flags @+0x10, ..., uint32 offset @+0x24 }
//       -- the same layout the blue-zone work used. 2634 descriptors decode
//       cleanly, and the weapon's are:
//
//           Magazine                  weapon+0x0DD0   flags 0x0120080100002034
//                                                     = CPF_Net (0x20)
//                                                     + CPF_RepNotify (bit 32)
//                                                     RepNotify = OnRep_Magazine
//           BackPackInTotalAmmoCount  weapon+0x0E50   CPF_Net
//           MagazineCapacity          weapon+0x1A28   NOT replicated
//
//       So weapon+0x1B0, which v59c through v59d watched, has no reflection
//       entry at all and is therefore never serialised. It is real, live
//       gameplay state -- -magtest proved the game counts on from a value
//       written there -- but nothing about it ever leaves the host. And Alen's
//       answer to the one open question closes it from the other side:
//
//           the PC's HUD did NOT show 99.
//
//       That is the whole of #15. The host's counter was right, nothing was
//       sent, and the client's display only ever refreshed on an equip or on
//       the 5-10 s ClientModifyInventoryItem -- exactly the symptom.
//
//       Confirmations, so none of this rests on one table:
//         * the weapon constructor 0x1FAFA70 zeroes [weapon+0xDD0] at
//           0x1FB0206 and carries the class size 0x1E00 as a literal;
//         * there is NO native write to +0xDD0 anywhere in .text -- every
//           writer is Blueprint going through the reflection system, which is
//           why no byte-pattern hook could ever have caught the write;
//         * GetCurrentWeapon (0x2017300) resolves Pawn+0x648 -> +0x710, so the
//           object [mag] reads is the weapon actor.
//
//       v60 does two things:
//       1. [mag] watches +0xDD0, +0xE50 and +0x1A28, with +0x1B0/+0x1B4 kept
//          alongside for one more round and the object pointer printed -- the
//          v59b [wp] window saw nothing move while firing and [mag] saw +0x1B0
//          move on the same action, so those two watchers were not on the same
//          object and the log should say so.
//       2. [mp] pushes the value with the game's own RPC. ClientSetMagazine
//          (UFunction getter 0x2526BF0, parameter struct { int32 NewMagazine; })
//          is the designed "server tells the owning client its magazine" path.
//          It is called on the game thread, inside the MaskOff window, only for
//          a remote player's weapon, only when the host's own value changed.
//          Rule 1 is intact: this is a declared Client RPC, not an engine state
//          machine's result field. Since v58u the weapon answers
//          NM_ListenServer, so GetFunctionCallspace routes it Remote, to the
//          owning connection -- the one that already delivers
//          ClientAddInventoryItem to the millisecond (v58v).
//          Switch -nomagpush turns the push off and leaves only the measurement.
//
//       Full write-up: STATIC-FINDINGS-ammo.md in the game folder.
//
//
//  v59d: the relevance theory is DEAD, and the log killed it cleanly.
//        [wl] measured the distance the graph uses:
//            Waffe bei {-38201,-175208,15265}, Spieler bei {-38178,-175249,15307}
//            -- Abstand 63 (1 m) | Cull 15000 -> 0
//        The weapon's location follows the player to within 0.6 m. It was never
//        outside the 150 m cull, so the cull never culled it. The repair ran
//        and changed nothing, which is the answer, not a failure: relevance is
//        ruled out, as dormancy and the missing channel were before it. The
//        repair stays in because it is free; it is not the fix.
//
//        [mag] found weapon+0x1B0 tracking the ammo across four rounds. v60
//        shows that reading was a coincidence of a live but private field --
//        see above.
//
//        The channel question from v59d stands: the weapon's channel is
//        (re-)assigned 13-18 times per round while the close-logger recorded no
//        SetChannelActor(channel, nullptr) at all, so these are reassignments,
//        not close/reopen cycles.
//
//
//  v59c: the magazine has an address. The v59b round found it in one line:
//        Pawn+0x648 is a PlayerInventoryComponent (the game names it), and on
//        the weapon actor two adjacent ints move like nothing else in 0x2000
//        bytes:
//            +0x1B0   1 -> 9 -> 16 -> 20   (the reload, across four polls)
//                    20 -> 12 -> 5         (firing)
//            +0x1B4   4 -> 24              (once, at the same moment)
//        [mag] now prints that pair with a millisecond stamp on every change,
//        so one round with Alen saying what the HUD showed settles what they
//        are.
//
//        Delivery is now the whole of #15, and the census names a candidate we
//        have beaten once before:
//            [cn] *** 1 x BP-Weapon_VECTOR_LV4_C (repliziert 1,
//                       immer-relevant 0, Dormancy 4, Cull 150 m)
//        150 m class cull and not always-relevant is the blue-zone bug (#10)
//        again. The graph keeps a PER-ACTOR copy of that cull distance in
//        FGlobalActorReplicationInfo (+0x90/+0x94) and checks it in the
//        replicate loop; zeroing that copy is what made the zone work.
//        Whether it bites for the weapon depends on something never measured:
//        the graph's distance is viewer -> ACTOR LOCATION, and an attached
//        weapon can keep a stale root location on the server. If it does, the
//        weapon is out of range nearly always, its channel closes and reopens
//        (the v59b log has eleven opens for one weapon), and the client only
//        ever sees the full state of a fresh channel -- which is precisely
//        "after 5-10 seconds it applies by itself" and "switch away and back
//        and it is right".
//        [wl] prints distance, flags and the graph's cull copy, and the repair
//        runs in the same round because it costs nothing if the distance turns
//        out to be zero. Switch -noownrelevant.
//
//  v59b: the DORM_Awake write crashed the host. It is OFF by default now.
//        v59 was the first build in which that write actually ran (see below),
//        and Alen's host died during a reload with the same access violation as
//        the two crashes before it -- read of address 0x40, RVAs 0x11E820A,
//        0x13BADC9, 0x1394877, 0x13AB3F7, 0x13B9734, 0x43D7DEE. That is the
//        branch disassembled in v58y: it tests byte [actor+0x20D] against 2
//        (DORM_DormantAll) and then dereferences the per-connection context at
//        [rbp+0x1D8], which is only set up inside the replication graph's own
//        per-connection pass. Our write flips actors into/out of that branch
//        from outside it.
//        The v59 log shows the thrash directly:
//            [da] ... Dormancy 2 -> DORM_Awake gesetzt -- #1
//            [wp] ...: +0x20D  byte 0 -> 1      <- us
//            [wp] ...: +0x20D  byte 1 -> 0      <- the engine, same tick
//        Writing NetDormancy is the v58h mistake again: never write the result
//        field of an engine state machine, always the decision in front of it.
//        Three of my interventions have now crashed his host for the same
//        reason. The switch is inverted: -dormawake turns it back on, nothing
//        turns it on by itself.
//
//        What v59 did measure, and it kills the v58z conclusion:
//          * the weapon's dormancy is NOT stably 2. It reads 2 at some polls and
//            0 at others, so "the weapon sleeps and that is all of #15" is too
//            simple.
//          * in the whole weapon object's first 0x800 bytes NOTHING moves while
//            shooting -- except our own +0x20D write and a flag at +0x9B. The
//            magazine count is not in that window.
//          * BHCharacterReplication does move: +0xC0 runs 2 -> 4 -> 6 -> 8 -> 9
//            and +0xD4 (ReplicatedStateID, the OnRep property from the handoff)
//            runs 1 -> 19 -> 2 -> 3.
//        So v59b widens the snapshot window from 0x800 to 0x2000 and adds a
//        third watcher, [wh], on Character+0x648 -- the weapon holder, proven by
//        GetCurrentWeapon being 19 bytes of "mov rcx,[rcx+0x648]". If the
//        magazine lives in the holder rather than the weapon, that is where the
//        diff will show up.
//
//  v59: v58w, v58x AND v58z all measured nothing, and it was one line.
//        The v58z round: no [da], no [wp] field diffs, no [cr], and DoReload
//        never fired -- while [ow] and [ch] from the level sweep worked fine.
//        Everything that went quiet had exactly one thing in common: a
//        liveness check I added in v58w.
//        IsObjectAlive walks the GUObjectArray and compares the item's first
//        pointer against the object. In THIS build that pointer sits at +8 and
//        is obfuscated, so the comparison never matches and the function
//        returns false for EVERY object. Our own calibration has been saying
//        so since v58g, once per round, in a line I kept reading past:
//            [ws] GUObjectArray-Layout ABWEICHEND -> nur Controller-Abgleich
//        The older code respected that flag (RememberRemote checks
//        g_objArrayState > 0 first). The checks I added in v58w did not. So
//        from v58w on, each of them returned false and silently switched off
//        the thing it was guarding -- the poke, the weapon watcher, the
//        replication-proxy watcher, and in v58z the DORM_Awake write that was
//        supposed to be the actual fix for #15. Three rounds of Alen's time
//        for a missing guard, and the log had been naming it the whole time.
//        v59 routes every one of them through MaybeAlive(), which only filters
//        when the layout is usable and otherwise gets out of the way, and
//        prints a loud one-off warning so this can never go quiet again.
//        So v58z's DORM_Awake fix has never actually run. It runs now.
//
//        Still open from that round and not explained by this: DoReload
//        (0x1FF12C0) never fired although Alen reloaded. That address is the
//        Blueprint-callable entry; the real reload path evidently runs
//        elsewhere. Not chased yet -- the field diffs come first.
//
//  v58z: the weapon is DORM_DormantAll -- that is the whole of #15.
//        The v58y round, with Alen picking a weapon up, reloading, switching
//        away and back, and emptying the magazine:
//            [ow] BP-Weapon_SKS_LV3_C (repliziert 1, Dormancy 2, Role 3,
//                                      RemoteRole 1, NetUpdateFrequency 100.0)
//            [ch] Kanal fuer BP-Weapon_SKS_LV3_C ... -- six times, same
//                 connection
//        Dormancy 2 is DORM_DormantAll: the actor replicates once and then
//        sleeps, and a sleeping actor sends no property changes at all. The
//        channel reopening six times is the sleep/wake cycle -- the client gets
//        the state as it was at each wake and nothing in between. Which is
//        exactly what Alen described: his copy of the magazine never moves, and
//        only a fresh equip refreshes the display from it.
//        We cannot call FlushNetDormancy for it -- that is the branch that
//        crashed the host twice (v58y). But NetDormancy is an INPUT field: the
//        replication path reads actor->NetDormancy every pass. So v58z writes
//        DORM_Awake into it for the remote player's own sleeping actors. A
//        one-byte write to a field we already read everywhere, no engine call,
//        and nothing that can reach the crashing branch. [da] lines,
//        -nodormawake.
//
//        AND MY OWN INSTRUMENT WAS BROKEN, which is why v58y produced not one
//        field diff. SafeCopy is a single memcpy inside one __try, so reading
//        0x600 bytes off an object that ends before the next page boundary
//        fails ENTIRELY -- and the old code then dropped the watcher and went
//        quiet without saying so. v58z reads in 0x40-byte chunks with a
//        validity mask, never drops the watcher over a failed read, and diffs
//        BYTES as well as int32s, because a magazine counter is very often a
//        uint8. The [cr] watcher now also says out loud when Pawn+0x40C8 is
//        null instead of staying silent -- v58y had no [cr] line at all and
//        there was no way to tell missing object from failed read.
//
//  v58y: THE SAME CRASH AGAIN -- and this time the branch is named.
//        v58w's liveness checks did not help, because the pointer was never
//        stale. Reading the crashing code properly:
//            cmp  byte [r9+0x20D], 2     ; actor->NetDormancy == DORM_DormantAll
//            jne  skip
//            mov  rdi,[rbp+0x1D8]        ; the PER-CONNECTION context
//            lea  rcx,[rdi+0x38]         ; rdi was NULL -> rcx = 0x38
//            call 0x11E8200              ; reads [rcx+8] = 0x40 -> access violation
//        That branch exists only for a dormant-all actor, and it assumes it is
//        running inside the replication graph's per-connection pass, where
//        +0x1D8 is set. We called into it from our own tick, outside that
//        pass, so the context was null. A live actor, a valid pointer, and a
//        code path that simply must not be entered from where we entered it.
//        v58k's wake-up never crashed because the actors it finds are
//        DORM_Initial (4); dormancy 2 and 3 were only ever swept up by
//        accident. So both places now refuse dormancy 2 and 3 outright.
//
//        And the poke itself is OFF by default now (-ownpoke re-enables it).
//        It was a v58t hypothesis, the v58v log already disproved the premise
//        behind it -- delivery is fine, the host simply never sends the
//        magazine -- and it has since cost Alen two host crashes while
//        measurably buying nothing. The read-only instruments it was bundled
//        with ([wp], [cr], [rl], [ow], [ch]) all stay and now run
//        independently of it: they are pure reads plus one leaf call.
//
//  v58x: the handoff, mined properly.
//        Alen asked for everything useful out of SP-hand-off-main. The two
//        files that carry real value are under
//        superpeople-research-handoff 5.6Sol pass 1/schemas/native-reflection/
//
//        native-properties.json -- 32 confirmed native properties with offsets.
//        The one that matters is the character's replication proxy
//        (Character+0x40C8, BHCharacterReplication, ReplicatedStateID +0xD4,
//        CPF_Net) -- v58w already watches it.
//
//        native-functions.json -- 1368 natives with owner, name and exec-thunk
//        RVA. Every one of them was previously an unnamed address to us. The
//        exec thunks are ProcessEvent wrappers; for a parameterless native the
//        real implementation is the call/jmp target right after the stack
//        bookkeeping. Resolved and verified by disassembly:
//            GetCurrentWeapon  0x235C540 -> 0x2017300   void* (character)
//            DoReload          0x2358EC0 -> 0x1FF12C0   void  (character)
//            GetPendingWeapon  0x235CFA0 -> 0x201D5B0
//            IsInAircraft      0x235EC50 -> 0x202F560
//            OnChangeCurrentWeapon 0x2361E10 -> 0x2040560
//        GetCurrentWeapon is nineteen bytes and hands over the design:
//            mov rcx,[rcx+0x648] ; test rcx,rcx ; jne 0x1C56650 ; xor eax,eax
//        so Character+0x648 is the weapon holder. Two things follow:
//          - the weapon watcher no longer waits for the level sweep to find a
//            weapon. It asks the game, four times a second, and follows every
//            weapon switch the instant it happens;
//          - DoReload is hooked (16-byte prologue, position independent,
//            ending on an instruction boundary) so the log carries the exact
//            millisecond the host executed a reload. [rl] lines.
//        Together with [wp] and [cr] one reload now pins the magazine field.
//
//        Two of our own leads are retired by the same handoff: in a run where
//        loot worked, ServerOnOverlapItemSpawnBox and
//        ServerRequestBuildingItemSpawnList were called ZERO times, so neither
//        is the loot route; and loot is not demand-spawned but map-wide at
//        match start, which is what v58s ended up producing. Their own loot
//        conclusion ("actors must answer NM_Standalone") is our v45/v46
//        finding -- their document cites this file by name.
//
//  v58w: HOST CRASH -- my bug, introduced in v58t. Fixed.
//        Alen opened a door on the PC and the host died:
//            EXCEPTION_ACCESS_VIOLATION reading address 0x00000040
//        The crash site is 0x11E820A, `mov eax,[rcx+8]` with rcx = 0x38 --
//        one instruction earlier the caller does `lea rcx,[rdi+0x38]`, so
//        rdi was NULL and the read landed on 0x40. Every frame above it is
//        replication-graph code (0x13BABE0 -> 0x1394820 -> 0x13AB3B7 ->
//        0x13B96A0) reached through the net driver at 0x43D7960, and the
//        instruction right before the call tests
//        `cmp byte [r9+0x20D], 2` = NetDormancy == DORM_DormantAll. So: the
//        graph was asked about an actor it had no entry for.
//        What fed it was PokeOwnedActors(). v58t collected actor pointers
//        during the level sweep, kept them FOREVER without ever re-checking
//        them, and called ForceNetUpdate on each one four times a second.
//        Opening a door destroys and respawns actors; the first pointer that
//        went stale handed the engine a dead object. A __try around the call
//        cannot save that -- the memory is still mapped, it is just no longer
//        that actor.
//        Fixed on four levels:
//          - nothing is collected unless it really replicates, and never the
//            short-lived classes (Projectile, Bullet, Camera, Emitter);
//          - every poke first checks the object is still alive in the
//            GUObjectArray (IsObjectAlive) and still replicates;
//          - a dormant actor is registered with AddNetworkActor BEFORE
//            FlushNetDormancy, so the graph has the map entry it looks up --
//            that missing entry is literally the null that crashed;
//          - anything that fails a check is dropped from the list instead of
//            being retried forever ([ow] ... verworfen N).
//        The weapon watcher checks liveness the same way.
//        If anything like this happens again: -noownpoke switches the whole
//        mechanism off and the host is safe immediately.
//
//        Also in v58w, from the handoff Alen sent (SP-hand-off-main,
//        schemas/native-reflection/native-properties.json): the character has
//        a dedicated replication proxy,
//            BravoHotelCharacter::CharacterReplication  +0x40C8
//            BHCharacterReplication::ReplicatedStateID  +0xD4, CPF_Net,
//                                                       OnRep_ReplicatedStateID
//        A separate object hanging off the character is exactly where a game
//        puts state it wants pushed to the owning client, and a magazine count
//        is that kind of state. It is a UObject, not an Actor, so it never has
//        a channel of its own -- it rides on the character's through
//        ReplicateSubobjects, which is also why none of our channel-level
//        instruments could ever have seen it. [cr] now diffs that object four
//        times a second exactly like [wp] does for the weapon.
//        The same handoff retires two of our own leads: its loot run shows
//        ServerOnOverlapItemSpawnBox and ServerRequestBuildingItemSpawnList
//        called ZERO times in a round where loot worked, so neither is the
//        loot route -- and loot is not demand-spawned at all, it spawns
//        map-wide at match start, which is what our own v58s fix ended up
//        doing.
//
//  v58v: two things the v58u log settled, and the one measurement left.
//
//        (1) THE DELIVERY IS NOT THE PROBLEM. Host and client timestamps line
//        up to the millisecond: the host sends ClientAddInventoryItem at
//        t=26791375 and ClientModifyInventoryItem at t=26794234, and the
//        client logs [AddReplicationItems] at 12:50:11.730 and
//        [ModifyReplicationItems] at 12:50:14.589 -- 2859 ms apart on both
//        sides. The perk RPCs (ClientPerkSpinEvent, ClientAddPerkUIEvent,
//        ClientAddPerkLevel) and ClientInAircraft all went out too. Nothing is
//        lost or late in transit. The host simply never sends the magazine.
//
//        (2) A SUSPICION I CARRIED SINCE v58j WAS WRONG, and it should not be
//        carried any further. "[rg] ServerReplicateActors ... Summe 0" is not
//        a symptom: UReplicationGraph::ServerReplicateActors 0x13C6C60 has
//        exactly one return path, at 0x13C7955, and it is `xor eax,eax` --
//        the function ALWAYS returns 0. The counter measured nothing. Same for
//        "14 ReplicateSingleActor calls": 0x13BE920 is not that function (it
//        takes 10 arguments), as v58k already noted. Both instruments are
//        meaningless and the conclusions drawn from them are withdrawn.
//
//        So the open question is narrow and physical: where does the magazine
//        count live, and does it ever leave the host? v58v measures exactly
//        that, three ways:
//          [ow] now prints bReplicates, RemoteRole and NetUpdateFrequency for
//               every actor the remote player owns. A weapon with
//               bReplicates = 0 cannot send a property to anyone, and then the
//               magazine must travel inside the inventory array instead -- a
//               completely different fix from a weapon that replicates but
//               whose channel never opens.
//          [ch] logs every channel opened for one of his own actors, so
//               "does the weapon reach him at all" stops being a guess.
//          [wp] snapshots the weapon object four times a second and prints
//               every 32-bit slot that changes to another small non-negative
//               number. Firing one magazine empty names the offset of the
//               magazine counter outright -- no reflection table, no guess.
//        -noweaponwatch turns the last one off.
//
//  v58u: the reload, described exactly enough to name the cause.
//        [stated] Alen: "Ich hebe eine Waffe auf, lade nach. Danach steht
//        immer noch null. Wenn ich aber die Waffe wechsle und wieder
//        zurueckwechsle, ist das Magazin voll. Dann schiesse ich von 20 auf
//        15, wechsle wieder und zurueck, und ich habe wieder 20 im Magazin
//        ohne nachzuladen."
//        Read that as two separate stores on the client:
//          A = the client's own copy of the inventory item
//          B = what the ammo widget shows
//        B is refreshed from A on every equip, and updated locally while you
//        shoot. The observations then say: A is FULL the whole time and never
//        changes. It is not updated by the reload (it was already full when
//        the weapon was picked up) and not by the shooting (20 comes back).
//        So nothing the host does to that item ever reaches the client, and
//        the only thing the client ever sees is the value the item was
//        created with. The host is fine because the host reads its own copy.
//        WHY: our per-actor net-mode hook answers NM_ListenServer only for
//        actors whose RemoteRole is AutonomousProxy -- the remote player's
//        PlayerController and Pawn. His WEAPON, his inventory items and his
//        PlayerState are not, so they were all answered NM_Standalone, and
//        every "we are a server, so tell the owning client" branch inside
//        those classes quietly did nothing. It is the same shape as
//        DoInAircraft, which we have been fixing one call site at a time --
//        this is the whole family at once.
//        v58u answers NM_ListenServer for actors that BELONG to a remote
//        player as well, found through the Owner chain built in v58t. The
//        hash set is only a fast filter (GetNetMode runs ~6000 times a
//        second); every hit is verified against the real Owner chain, so a
//        stale pointer cannot widen the answer. [om] lines name each class
//        and the calling site. -noownmode.
//        The perk UI (#16) sits on the same mechanism: the perk data lives on
//        the PlayerState, which was in exactly the same position.
//
//  v58t: [stated] Alen: floor loot works now. Next: the ammo counter (#15)
//        and the perk UI in the aircraft (#16).
//        The v58s log says these are NOT the streaming problem -- they are one
//        layer further in, and both have the same shape:
//          - The reload itself is fine. ServerReloadWeapon arrives on the host
//            (exec 0x248BF70, validate at character vtable +0x958) and
//            MulticastStopSimulatingReload goes back out.
//          - What arrives late is the new magazine COUNT. It travels as a
//            replicated property on the weapon / inventory-item actor, and
//            through ClientModifyInventoryItem -- which in the v58s log fired
//            exactly four times in the whole round, 6 to 9 seconds apart.
//            That IS the delay Alen sees.
//          - Same for the perk data on his PlayerState: [ac] confirms
//            ClientInAircraft goes out, so the boarding notification is fine;
//            what lags is the state behind it.
//        These actors are the remote player's own, and nothing we built so far
//        touches them: v58k's wake-up only looks at level-placed actors with
//        NetDormancy > DORM_Awake, and a weapon is spawned at runtime.
//        v58t finds them by their Owner chain (AActor::Owner +0x2B0, the chain
//        IsNetRelevantFor itself walks) during the level sweep, and then
//        pushes pawn, controller, PlayerState and everything they own out four
//        times a second -- FlushNetDormancy when dormant, then ForceNetUpdate,
//        on the game thread inside the MaskOff window where those are not the
//        no-ops the mask otherwise makes them (the v58c finding). [ow] lines,
//        -noownpoke.
//        Also: -ammolog now watches the perk, aircraft, magazine and
//        simulation RPCs as well, so one reload and one boarding in the same
//        round answer both questions with timestamps.
//
//  v58s: why standalone works and the listen server does not.
//        [stated] Alen: "when playing a Standalone match without a listen
//        server it's running fine." That is the whole answer, and the v58r log
//        shows exactly where the difference lives.
//
//        (1) THE REMOTE PLAYER WAS A SUB POINT. World Composition takes a
//        list of streaming points. Point 0 is the primary one; every point
//        after it is a SUB POINT, and the tile lambda 0x4752FC2 treats those
//        as second class -- for a level that is not loaded yet it multiplies
//        the distance by the sub-point scale clamped to [0.15, 0.45] for the
//        base level. In standalone there is exactly ONE point and it is the
//        player, so he gets full-distance base levels. Here the HOST was
//        point 0 -- sitting in the aircraft at z=74800 doing nothing -- and
//        the player actually on the ground was appended behind him, so he got
//        at most 45 %. The log is unambiguous: he stands on tile #1730,
//        Prio 600, LODIndex=0, whose level holds THREE actors. An HLOD proxy.
//        No buildings, therefore no item spawn boxes, therefore no loot -- and
//        nothing solid to collide with either, which is the falling-through.
//        v58s puts the remote players in first, as the primary point, and
//        appends the host behind them; while the host is above z=12000 (in
//        the aircraft) he gets no point at all. That is the standalone
//        situation exactly: one point, the player on the ground. It also
//        halves the tile load on a machine sitting at 97 % memory.
//        -nohostfirst restores the old order, -hostpoint always keeps the
//        host's point.
//
//        (2) AND v58r's PULL NEVER RAN. CommitTileStreamingState early-outs
//        before touching anything while the tile's evaluation byte is 3:
//            ecx = [tile+0xD8]; ecx--; je apply; ecx--; je apply;
//            cmp ecx,1; je RETURN
//        Every tile in every log reads Eval=3, so all 174 [fx] calls did
//        nothing whatsoever. v58s calls the three setters the commit body
//        itself calls once past that byte, in its order: vtable+0x268
//        SetShouldBeLoaded, 0x433E0F0 SetShouldBeVisible, 0x433DE00
//        SetLevelLODIndex(-1). Engine setters, which notify
//        UWorld::UpdateStreamingLevelShouldBeConsidered -- not raw field
//        writes, so the v58h lesson holds. Runs every ~0.5 s now instead of
//        every 2 s, and the commit hook uses the same 250 m radius so the
//        engine does not argue back over the same tiles.
//
//  v58r: the loot is not blocked, it does not exist. The v58q log answers the
//        question v58q was built to ask, and the answer clears every suspect
//        the last four builds chased:
//            [lq] Tore: keine World 0, kein GameMode 0, kein GameState 0,
//                       Match-Phase < 4 8, kein Owner 0, Owner nicht Authority 0
//        Eight attempts, all eight in the waiting lobby, none after. Not one
//        gate ever blocked a single source out on the map, because there were
//        no sources: "11 Actors im Umkreis 300 m geprueft, 0 davon sind
//        Loot-Quellen", for the whole round.
//        The level walk says why: 1098 levels holding 5409 actors between
//        them -- five per level, the signature of an HLOD proxy -- and 35
//        building actors in the entire world. The census around the landed
//        player: 4 actors for forty seconds, 488 at the very end, with
//        fences, door frames and static meshes but not one BPB- building.
//        And every tile in every log we have taken comes in a pair: a
//        Prio-600 tile that loads, and a Prio-500 tile over the same ground
//        that reads ShouldBeLoaded=0 / NichtGeladen, always.
//        The commit hook cannot reach those: it only sees tiles the engine
//        decides to commit, and a tile the engine never wants is never
//        committed -- which is exactly why [ct] korrigiert froze at 317.
//        So v58r calls UWorldComposition::CommitTileStreamingState ITSELF,
//        from the game thread, for every tile within 250 m of the remote
//        player that is not already loaded and visible at LOD -1, six per
//        sweep, with the last two arguments captured from the engine's own
//        calls. Engine entry point, not a write into engine state -- the
//        v58h lesson holds. [fx] lines, -nopulltiles.
//        Second instrument: [td] now prints the LoadedLevel and its actor
//        count for the tile the player is standing on. Two or three actors
//        means a proxy; a few hundred means the real level. That one number
//        decides whether this build is the fix or the next dead end.
//
//  v58q: two measured causes, two fixes.
//
//        (1) THE FLAG WE USED AS "THIS SOURCE HAS ALREADY SPAWNED" NEVER WAS
//        ONE. 0x2141FD0 read end to end:
//            mov  byte [comp+0x534], 0          <- cleared on entry, always
//            cmp  byte [comp+0x528], 0
//            jne  skip
//              eax = 0; ecx = 1
//              cmp  byte [comp+0x529], 0
//              cmove eax, ecx                   <- 1 when both mode bytes are 0
//              mov  byte [comp+0x534], al
//        So +0x534 means "spawn every category in this pass", it is recomputed
//        per call, and for mode 2 (which zeroes both mode bytes) it ends up 1
//        and stays 1. v58o read it as a done-flag and therefore skipped every
//        source the game had ever touched -- "schon vorher fertig" in the log.
//        v58q keeps its own fired-set instead.
//
//        (2) THE SIX GATES IN FRONT OF THE SPAWN, and which one blocks us.
//        0x2142DA0 (mode-2 path) checks, in this order: comp+0xB0 World,
//        World+0x1D0 AuthGameMode, GameMode+0x370 GameState, GameState+0x9F2
//        >= 4, comp+0xA8 Owner, Owner+0x20F Role == 3. The v58p log says 30
//        sources were handed to it and not one fired, so one of the six is
//        shut -- and guessing which is exactly what Alen asked me to stop
//        doing. v58q evaluates all six itself, counts them ([lq] Tore: ...),
//        and then calls the INNER function 0x2141FD0 directly, which has none
//        of them: zero the two mode bytes, set comp+0x51C = World+0x5C8, call.
//        Gate 4 is the one we do not step over -- no loot before the round.
//        -nogatebypass goes back to the outer call only.
//
//        (3) AND WHY HE FALLS THROUGH BUILDINGS. Same log, [cn]: he landed at
//        t=142 with 37 actors around him on the host, and 726 only at t=162 --
//        the round ended at t=164. For those twenty seconds the host has no
//        walls to collide with and no loot sources to fire, so the client gets
//        pushed through them. The tile he is standing on now gets bBlockOnLoad
//        in the commit arguments (at most 24 per round, only below z=12000,
//        only while the tile is not there yet). -noblockload.
//
//  v58p: no loot before the round. Alen saw pickups lying on the map while
//        everyone was still in the waiting lobby, and the v58o log names the
//        culprit line by line -- every [ph] entry reads "Match-Phase noch 1",
//        i.e. our own nested CheckSpawnByStandalone was lifting the phase gate
//        in the lobby and spawning the map before the match existed.
//        The lift was a v58l guess and v58m had already disproved it: the
//        phase was 4 at t=84 s with the host still at z=74800, long before
//        anybody touched the ground. So the gate is open by the time the first
//        player lands anyway -- lifting it buys nothing and only ever caused
//        this. Two changes:
//          - the phase lift is OFF by default (-stateforce re-enables it),
//          - both spawn paths (the nested building check and the direct
//            component fire) return early below phase 4; the watchdog counts
//            them as "in der Lobby uebersprungen".
//        That keeps the promise the other way round too: from the aircraft
//        phase onwards nothing is blocked, so whoever lands first still has
//        loot around him.
//
//  v58o: the spawn chain, read end to end in the binary instead of one log at
//        a time -- and with it, the reason our own spawning only ever produced
//        a fraction of the loot.
//
//          ABravoHotelBuilding::CheckSpawnByStandalone 0x1FA5E60
//            match phase 4 | first PlayerController | its pawn | < 300 m |
//            components registered | PC+0x1928
//          -> 0x1FA7980(building, FName area, 0.0f, mode 2)
//               not outside the zone | building+0x46A
//            -> comp = building->vtable[+0x660](
//                        UBravoHotelDetectItemSpawnBoxComponent)
//          -> 0x2142DA0(comp, delay, mode)
//               comp world | GameState 0x1CB2110 >= 4 | owner Role == Authority
//            -> 0x2141FD0(comp, mode)
//                 comp+0x534 = "this source has fired" -- the ONLY real guard
//              -> for every child component: 0x2177320(child, time, mode)
//                 = one pickup per spawn point
//
//        So a loot building is not "a child of ABravoHotelBuilding" -- it is
//        simply any actor carrying that component. Our walk was asking the
//        wrong question and found 38 to 47 actors in the whole world while the
//        host's own wave produced 2120 pickups from the same map.
//        v58o asks the game's question instead: every actor within 300 m of a
//        remote player is asked for the component, the ones whose +0x534 is
//        still clear are fired directly through 0x2142DA0 -- which takes the
//        COMPONENT, so it is class-agnostic and sits below the distance gate,
//        the PC flag and the phase gate (lifted here as well). [lq] lines
//        report how many actors were checked, how many are loot sources, how
//        many were still open and how many fired.
//        -nolootscan (measure nothing), -nodirectspawn (count only, do not
//        fire).
//
//  v58n: the phase theory is DEAD, and the v58m log killed it cleanly.
//        The match phase reached 4 at t=84 s, while the host was still at
//        z=74800 in the aircraft. The remote player landed at t=142. And the
//        loot still did not arrive until the host was on the ground at
//        t~190: pickups went 1177 -> 3297 and the census around the remote
//        player went 105 -> 592 in that one window. So a phase-4 gate is not
//        what holds the map's loot back, and forcing it (which does work --
//        40 buildings spawned for him at phase 1, see the [ph] lines) only
//        ever covered the handful of buildings our own sweep reaches.
//
//        Something else fires when the host touches down and spawns 2120
//        pickups across the map at once. Counters cannot name a code path,
//        so this build reads the one thing that can: the hook on
//        UNetDriver::AddNetworkActor is a 14-byte jmp, so _ReturnAddress()
//        inside it is the instruction after the caller's call. For every
//        BravoHotelPickup that gets registered, that address is turned into
//        an RVA and the distinct paths are logged ([lr] lines). One round,
//        and the function that creates the wave has a name -- after which it
//        can simply be called for the remote player.
//
//        The same build also evaluates, for the GAME's own building check,
//        the five gates it is about to evaluate itself, with the HOST's
//        controller: phase, no controller, no pawn, too far, flag. The [gs]
//        line now carries those counters plus the host's own +0x1928 flag, so
//        the host path's behaviour before and after touchdown is readable
//        directly instead of inferred.
//
//  v58m: the v58l round proved the chain works -- 50 pickups around the remote
//        player and 100 open channels while the host was still at z=69800 --
//        but it proved it in a round where the match phase happened to reach 4
//        on its own at t=80 s. The point of the phase lift is the OTHER case:
//        the first player to land should have loot even while the phase is
//        still 2 or 3. The code already does that (RunSpawnForRemotePawns
//        lifts GameState+0x9F2 for its one nested call), so v58m only makes it
//        say so: every time loot is actually created for the remote player
//        while the phase was still below 4, one [ph] line, plus a counter in
//        the 20 s summary. Nothing else changes.
//
//  v58l: v58k worked -- and the log shows the LAST gate, the one that has been
//        behind "it only starts working once the host is on the ground" all
//        along.
//
//        v58k's result first, because it is real: BravoHotelPickup went from
//        never appearing in UNetDriver::AddNetworkActor at all to
//        "1183 hinzugefuegt / 375 Kanal" -- 375 pickups actually sent to the
//        client -- and the doors and windows got channels too (13, 11, 18).
//        Total channels went from 320 to 841. Waking the dormant actors was
//        right and stays.
//
//        But the same log also shows this: while the host was still in the
//        air, the entire map held TWENTY pickups. In the twenty seconds
//        around the host's touchdown, 1137 more appeared at once -- and 231
//        of those stood within 200 m of the remote player, who was 1.5 km
//        away from the host at the time. A wave like that is not distance and
//        not streaming. It is the first gate of CheckSpawnByStandalone:
//            cmp byte [GameState+0x9F2], 4        (0x1FA5F6B)
//        the match phase. Every building on the map runs its 0.1 s spawn
//        timer the whole time and every one of them bails at that line until
//        the phase reaches 4 -- which in a two-player match happens when the
//        last player, the host, is down. Our nested call for the remote player
//        was bailing there too, which is why forcing his PC flag was never
//        enough.
//        So the nested call now lifts that gate as well, exactly the way it
//        lifts the PC flag: set it to 4, run the one check, put it back
//        (-nostateforce). And [gs] logs the phase every half second and the
//        moment it changes, so the next log proves or disproves this in one
//        line instead of another evening.
//
//  v58k: found it. The v58j table says BravoHotelPickup never passes through
//        UNetDriver::AddNetworkActor -- not once, while 48 of them stand
//        within 200 m of the player -- and the census says what the whole
//        missing family has in common:
//            BravoHotelPickup            repliziert 1, Dormancy 4, Cull 100 m
//            BP-Fence_C                  repliziert 1, Dormancy 4, Cull 100 m
//            BP-Wood_In_Door05_Frame_C   repliziert 1, Dormancy 4, Cull 100 m
//            BP-BrokenWindowHISMActor_C  repliziert 1, Dormancy 4, Cull 100 m
//        Dormancy 4 is DORM_Initial -- "the client already has this actor from
//        its own copy of the level, do not replicate it until something wakes
//        it" -- and the engine enforces that by keeping those actors out of
//        the network object list altogether (UNetDriver::AddNetworkActor ->
//        IsDormInitialStartupActor 0x3F93040). The only thing that ever brings
//        one in is AActor::FlushNetDormancy.
//        That assumption is true for a fence the client loaded itself. It is
//        false for every pickup the HOST spawned during the match, and false
//        for a window this client never saw break. Nothing in this setup calls
//        FlushNetDormancy for them, so they sit on the host, fully spawned,
//        and no client is ever told.
//        So the level walk now wakes them: every replicated actor with
//        NetDormancy > DORM_Awake within 300 m of a remote player gets
//        FlushNetDormancy plus AddNetworkActor, once each, inside the MaskOff
//        window where the driver is visible. -nowakeup.
//        The same round also settles the other open question, because the
//        graph reported "Summe 0" replicated actors all evening: [rg] now
//        prints how many connections the replication graph actually holds
//        (+0x40/+0x48) against how many the driver has.
//
//  v58j: the streaming question is CLOSED. The v58i log: 1245 of 3616 tiles
//        LoadedVisible and none stuck (0 Loading, 0 MakingVisible, 0 failed),
//        the tiles under the remote player at LODIndex -1, in UWorld::Levels,
//        with their levels holding 31..43 actors each -- and the census counts
//        64 BravoHotelPickup, a BP-Weapon_AKM_LV3_C and a BP-Weapon_M870_LV4_C
//        within 200 m OF HIM, on the host, next to 393 static meshes, 56
//        fences and a dozen door frames. The world is there and the loot is
//        there. He still sees none of it. It is replication.
//        (The host had 88 pickups around itself at the same moment, so the
//        spawning works on both sides. 10 GB of 15.6 GB RAM in use, 95% system
//        load -- tight, but nothing is being purged.)
//
//        So this build counts, per class, the three gates an actor passes on
//        its way to a client -- added to the graph, gathered for the
//        connection, given a channel -- and for the loot classes it also logs
//        the two things that can drop an actor inside ReplicateSingleActor:
//        the per-class policy byte and the cull distance. One round, one
//        table, and the gate that loses the pickups names itself.
//
//  v58i: v58h's tile forcing was wrong and is REMOVED. It wrote LevelLODIndex
//        and the visibility bits straight into the ULevelStreaming; the log
//        showed 861 of those writes in two minutes, because
//        UWorldComposition::CommitTileStreamingState (0x4756450) compares its
//        arguments against those very fields, saw a difference it had not
//        caused, and drove the level through an unload/reload every frame.
//        That cannot settle. The right place is the DECISION: the commit is
//        now hooked and, for a tile within 150 m of a remote player, told
//        "loaded, visible, no LOD" -- after which the engine's own state
//        machine does the rest, untouched. -nocommitfix.
//
//        And because two rounds have now been spent on plausible stories, this
//        build measures the thing itself instead of guessing at it. Every
//        ULevelStreaming carries its state machine at +0xC8 (Unloaded,
//        Loading, LoadedNotVisible, MakingVisible, LoadedVisible ...), its
//        target state at +0xC9 and its ULevel at +0x140. The [st] lines print
//        that for every detail tile around the remote player AND around the
//        host, plus how many actors that level holds and whether it is in
//        UWorld::Levels at all, plus a histogram over all ~3600 tiles. Four
//        different causes -- never requested, stuck loading, loaded but never
//        made visible, or visible and full (in which case streaming is
//        innocent and the fault is in replication) -- read differently in
//        those lines, so the next log picks one. [mem] adds the host's memory
//        use, because a machine that is purging levels as fast as it loads
//        them would look exactly like this.
//
//  v58h: the v58g round answered the open question and killed my own theory.
//        The viewpoint the replication graph uses for the client is CORRECT --
//        every [vw] line shows it sitting on his pawn, ~3 m off (the camera).
//        The census says what is really wrong: thirty seconds after he landed
//        the host had SIXTEEN actors within 200 m of him -- six post process
//        volumes, two emitters, a jeep, one static mesh. No buildings, no
//        doors, no glass, no loot spawners. And his tile (#1334) was sitting
//        at LODIndex 0: the host had the baked HLOD proxy of that square
//        instead of the real level, and a proxy has no gameplay actors at all.
//        Nothing can spawn, break or open in a world that is not there. It
//        also explains the oldest symptom exactly: once the HOST lands, its
//        own streaming pressure drops, the remote area finally gets the base
//        level, and loot appears.
//        So v58h forces it: every detail tile within 300 m of a remote player
//        gets LevelLODIndex = -1 plus bShouldBeLoaded/bShouldBeVisible right
//        after the engine made its own choice (-noforcetiles). The census now
//        runs for the host as well, so both halves of the map can be compared
//        in one log, and always prints the pickup/item classes separately.
//
//  v58g: the v58f round proved the loot is SPAWNED and never arrives.
//        271 buildings ran their spawn to the end around the remote player
//        (GameState spawn list 44 -> 271) and UNetDriver::AddNetworkActor took
//        ~930 new actors in the same 60 seconds -- so the items exist on the
//        host and are registered with the net driver. The player still saw an
//        empty map. What every missing thing has in common (loot, glass,
//        doors, vehicles) is that it is a GRID actor, and what works (blue
//        zone, his own pawn) needs no position at all. The grid gathers cells
//        around ONE point per connection: FNetViewer::ViewLocation, built in
//        UReplicationGraph::ServerReplicateActors from the connection's
//        ViewTarget and APlayerController::GetPlayerViewPoint. v58g hooks that
//        constructor (RVA::FNetViewer_Ctor), logs the point the graph really
//        uses per connection, and -- when the connection belongs to a
//        registered remote player and the point is more than 50 m off -- moves
//        it onto that player's pawn. -noviewfix measures without correcting.
//        Second instrument: the level walk now tallies the CLASSES of every
//        actor within 200 m of the remote player ([cn] lines, with each
//        class's replication settings from its CDO), which says once and for
//        all whether the pickups are there on the host.
//        The v58f BeginPlay gate stayed at "0 forced" all round -- net mode
//        was never the reason a timer was missing -- but it costs nothing and
//        stays in.
//
//  v58f: (1) the level-walk decode of v58e cleared the upper 32 bits of the
//            actor pointer ("not ecx" only complements the low dword, the
//            game keeps the high dword) -- 0 of 7675 slots decoded. Fixed.
//        (2) WHY no spawn check ever runs around the remote player: the
//            building / vehicle-spawner BeginPlay starts the 0.1 s spawn
//            timer only when AActor::GetNetMode() == NM_Standalone
//            (0x1FA5867, 0x22BFA6C). Those two call sites now always get
//            NM_Standalone, whatever the mask state. -nobeginplayfix.
//        (3) "spawned" is now measured by the GameState's spawned-building
//            list (+0xAD8) growing, not by a byte that never changes; the
//            watchdog also counts the game's own checks around the HOST so
//            the two sides can be compared.
//
//  v58: four things, all on the v55 source (the v56/v57 file on the other
//       machine is superseded by this one):
//
//       (1) HOST CRASH FIXED -- see below.
//       (2) ZONE: the replication graph (UBasicReplicationGraph from
//           Engine.ini) culls per CLASS with the CDO's NetCullDistanceSquared
//           (150 m for BP_BlueZone_C), snapshotted inside InitListen, and
//           keeps a per-actor copy of it. Writes to the live actor never
//           reached either. Now, when the zone actor enters the graph
//           (UNetDriver::AddNetworkActor hook): CDO + instance get
//           bAlwaysRelevant (-> AlwaysRelevant node, verified in
//           RouteAddNetworkActorToNodes 0x13C5B60) and the graph's per-actor
//           cull copy (FGlobalActorReplicationInfo+0x90/+0x94) is zeroed.
//           -nozonerelevant. (v57's dormancy flush is gone: the actor was
//           DORM_Awake all along.)
//       (3) LOOT far from the host (v56 redone): both CheckSpawnByStandalone
//           functions hooked; for the nested per-remote-pawn run,
//           UWorld::GetFirstPlayerController answers with the remote player's
//           controller (pawn, distance and the PC+0x1928 "levels loaded" gate
//           then all belong to that player). -nospawnremote.
//       (4) AIRCRAFT: DoInAircraft sends ClientInAircraft only under
//           NM_DedicatedServer; for the remote pawn we answer that at exactly
//           that call site. -noaircraftfix.
//
//       The crash: EXCEPTION_ACCESS_VIOLATION in TaskGraphThreadNP, 36 s
//       after the client joined. The faulting frame (RVA 0x2C42147 inside
//       0x2C412B0) is the garbage collector's reference walker
//       (TFastReferenceCollector::ProcessObjectArray -- it checks every
//       pointer against GUObjectAllocator's permanent pool at 0x762F3E0/
//       0x762F3E8 and then reads UObject::InternalIndex at +0xC). It died
//       walking a TArray<UObject*> whose data pointer was bent.
//
//       Who bent it: WE did, in v54. "+0x2E0" is not MinNetUpdateFrequency
//       in this build. AActor's fields are shuffled, and +0x2E0 is the data
//       pointer of a TArray<UObject*> (0x3F95390 does an AddUnique loop over
//       it, 0x3F96760 calls TArray::Empty on it). The v54 write of 10.0f
//       (0x41200000) replaced the low half of that pointer; the next GC pass
//       read Num elements from 0x226_41200000 and crashed. v55 survived on
//       08.09. only because the pointer half, read as a float, happened not
//       to fall into (0,10) that time.
//
//       The real net fields, from AActor's default initialiser at 0x3F92170:
//           NetCullDistanceSquared  +0x2D4  (225000000.0f -- v50 stays)
//           NetUpdateFrequency      +0x2A4  (100.0f)
//           MinNetUpdateFrequency   +0x218  (2.0f)
//           NetPriority             +0x298  (1.0f; GetNetPriority 0x3F87A80)
//           CustomTimeDilation      +0x29C  (1.0f)
//           NetDormancy             +0x20D  (1 = DORM_Awake)
//       The v54 block is now read-only and uses these offsets. Rule from
//       here on: never write to an actor field whose offset was inferred
//       from the UE4 declaration order -- only from the binary.
//
//  v55: the ROOT CAUSE of "everything is buggy away from the host". The
//       server streams World Composition tiles only around its own view point:
//       UWorld::Tick -> UWorldComposition::UpdateStreamingState() (0x4761A00)
//       walks GEngine->GetGamePlayer(World,i)->PlayerController -- LOCAL
//       players only -- and passes their view points to the array overload
//       at 0x4760F20. A remote player has no ULocalPlayer, so wherever the
//       host has not been, the sublevels are not loaded ON THE SERVER: no
//       loot spawners, no door actors, no breakable glass, no floor to stand
//       on. That is the loot, doors, windows and "bugged back" complaints in
//       one. The overload is now hooked: the pawns of remote controllers
//       (RemoteRole == ROLE_AutonomousProxy, remembered in PossessedBy) are
//       appended to the location list, so the host loads tiles around every
//       player. Costs RAM on the host (two loaded areas instead of one).
//       -nostreamremote restores v54. See the "STREAM ... REMOTE" block.
//
//  v54: the zone question is ANSWERED. The properties on the host really do
//       carry the data (measured: Index=1, AreaDescKey "The Vista",
//       ZoneName "LV-OrbIsland_ALPHA_24_23"), and the client only ever gets
//       them when the game changes one -- so this is a send-rate problem, not
//       a missing-data problem. v54 logs the actor's net fields (which also
//       checks the assumed +0x2DC/+0x2E0/+0x2E4 layout) and raises the rate
//       if it is implausibly low. Raising a rate can never deliver a wrong
//       value, unlike poking the game's own state.
//
//  v53: -ammolog. net.UseAdaptiveNetUpdateFrequency=0 did NOT fix the reload
//       delay, so that guess was wrong. This switch timestamps every call of
//       the inventory/weapon RPCs, which separates the two possibilities:
//       the update leaves the host late (server side) or it leaves at once
//       and the client's UI sits on it (client side).
//
//  v52: the v51 measurement used the SDK offsets and they were wrong (it read
//       index 15 where the host log said 0). The real ones come from the
//       engine's property table, which stores each RepNotify name next to its
//       offset: SelectedPlayZoneName +0x7C8, SelectedPlayZoneInfoIndex +0x7D4,
//       AreaDescKey +0x7D8. The SDK was uniformly 8 bytes low. Read-only.
//
//  v51: v50 plus one measurement. The zone OnReps now fire on the client
//       (OnRepSelectedPlayZoneName / ...InfoIndex / ...AreaDescKey), so v50
//       works -- but the first one lands 1:55 after the join, exactly when
//       the game picks the first real zone. The pre-match preview is still
//       missing. We now print the two zone properties periodically to find
//       out whether they already hold the preview at map load (then it is
//       dormancy) or stay empty (then the preview only ever travelled in the
//       one-shot multicast). Read-only.
//
//  v50: the zone fix is RELEVANCY, not dormancy. The v49 measurement found the
//       engine default 225000000.0f at exactly one offset in the live
//       BP_BlueZone_C object, so AActor::NetCullDistanceSquared is +0x2D4.
//       We raise it to 1e18 on that actor, which makes it replicate to every
//       client regardless of distance. Only the untouched engine default is
//       overwritten; a game-chosen value is left alone.
//
//  v49: measurement only -- no behaviour change against v48. The zone now
//       reaches the joining player after he lands but not while waiting for
//       the match, which points at distance based net relevancy. The engine
//       property names are not in the binary, so the offset of
//       NetCullDistanceSquared cannot be found statically; instead we search
//       the live BlueZone object for the engine default 225000000.0f and log
//       every hit, plus the flag bytes around 0x280. Nothing is written.
//
//  v48: TWO things.
//       (1) My own v47 bug: the blue zone work ran on the WATCHDOG thread and
//           called engine functions (FName::ToString, FMemory::Free,
//           ForceNetUpdate) while the game thread was replicating. That race
//           is what shoved the joining player into buildings and under the
//           map. It now runs inside MyTickFlush -- game thread, and inside
//           the MaskOff window so ForceNetUpdate is not a no-op.
//       (2) The host dropped 1292 object references in ONE round with
//           "Using None instead of replicated reference ... level not made
//           visible" -- 1004 of them the floor mesh SM-Luxury_Floor_02, plus
//           house floors, stairs and door frames. Those are the components a
//           character stands on, and UE4 sends a character's position
//           RELATIVE to that MovementBase. A 2-byte patch now always sends
//           the real reference. -nolevelvis restores the old behaviour.
//
//  v47: blue zone. The joining player gets no zone preview and permanent
//       storm effects because the ABravoHotelBlueZone actor never replicates
//       to him -- the host announces the zone at map load, ~18 s before his
//       game is even running, and the actor is then never woken because this
//       is a ReplicationGraph title whose graph class is missing. The actor
//       is located through UWorld::GameState (+0x1D8) by resolving class
//       names, then ForceNetUpdate() (VTable +0x638, which flushes net
//       dormancy) is called on it once a second. -nozonefix disables it.
//       See the "BLUE ZONE" block.
//
//  v46: the actor net mode is back, but NARROW: only actors whose RemoteRole
//       is ROLE_AutonomousProxy (the remote player's PlayerController and
//       Pawn) get NM_ListenServer. Everything else -- loot spawners, items,
//       GameMode, GameState, geometry -- keeps NM_Standalone.
//       Why: AController::IsLocalController (0x4198FB0) short-circuits to
//       TRUE whenever GetNetMode() == NM_Standalone. In v45 the host
//       therefore treated the JOINING player's controller "locally
//       controlled", ran the local-input movement path for his pawn, never
//       applied his ServerMove, and corrected him back every frame --
//       exactly the rubber-banding Alen saw. Under NM_ListenServer the
//       function falls through to "RemoteRole == AutonomousProxy -> false",
//       which is correct, while the host's own controller (not
//       AutonomousProxy) still answers true and keeps its character.
//
//  v45: TWO corrections, both from reading AActor::GetFunctionCallspace.
//       (1) FunctionCallspace is Absorbed=0, Remote=1, Local=2 and the
//           values are BIT FLAGS (the multicast path literally returns 3).
//           We had Local/Remote swapped since v37, so the RPC switch OR-ed
//           Local instead of Remote and never sent anything to the client.
//       (2) The global actor net mode hook (v42) is OFF by default again:
//           Alen verified the host has floor loot WITHOUT ?listen and none
//           WITH it, so presenting NM_ListenServer to every actor is what
//           cost the host its loot. Roles are kept correct by the
//           APawn::PossessedBy hook and RPC routing by the now-correct
//           switch, so the global hook is not needed. -actormode brings it
//           back for A/B testing.
//
//  v44: world origin pinned. The joining player was displaced by exactly one
//       world-origin offset (2.40 km) when vaulting, landed outside the
//       physics MBP bounds (collisions disabled = "out of map") and was
//       killed by the play zone. Cause: LV-OrbIsland uses World Composition
//       origin rebasing; a retail DEDICATED server never rebases, so all
//       server-side game code assumes local == absolute. Our listen host has
//       a local player and therefore rebases. UWorld::SetNewWorldOrigin
//       (0x4744230) is now a no-op on the host. -allowrebase restores v43.
//       See the "pin the world origin" block.
//
//  v43: replay recorder disabled. The host crashed the moment the aircraft
//       phase began -- the crash callstack is entirely the crash-dump replay
//       recorder (TickDemoRecord -> DeltaSerializeFastArrayProperty, null
//       FastArray). v42's role fix makes the joiner replicate fully, which
//       that recorder chokes on. UGameInstance::StartRecordingReplay
//       (0x426AF90) is neutered; the live netcode is untouched. -allowreplay
//       restores the old behaviour. See the "replay recorder" block.
//
//  v42: actor-level net mode. AActor::GetNetMode (base+0x3F92730) is hooked
//       so that actors see NM_ListenServer while the world stays masked as
//       standalone. Fixes the joiner's pawn arriving as ROLE_SimulatedProxy
//       (APawn::PossessedBy skipped SetAutonomousProxy under NM_Standalone),
//       RPC routing and IsLocalController. See the "ACTOR NET MODE" block.
//       New switches: -noactormode (behave like v41), -rpcfix (old v37 switch).
//
//  --- older history below ---------------------------------------------------
//
//  v8-Befund: GEngine ist jetzt KORREKT (VTable base+0x5A245E0,
//  NetDriverDefinitions Num=2). Crash kam vom Aufruf selbst:
//
//    0x46A3C90:  movsd xmm0,[r8] / mov eax,[r8+8] / lea rcx,[rdx+0x228]
//
//  -> r8/r9 sind ZEIGER auf FName-Strukturen (12 Byte!), keine Werte,
//     und rdx ist ein FWorldContext*, kein UWorld*.
//     Ich hatte den FName als Wert uebergeben -> [0x2e1] gelesen -> Crash.
//
//  v9 macht es wie die Engine selbst:
//    * FName = 12 Byte {ComparisonIndex, DisplayIndex, Number}, per ZEIGER
//      (bestaetigt durch  cmp [rbx+0x1a0],0  = NetDriverName(0x198)+8)
//    * Aufruf des UWorld*-Thunks 0x46A3BF0, der den WorldContext selbst holt
//    * WorldContext via 0x46AD580 (WorldList @GEngine+0xD88, ctx->World @+0x1A8)
//    * Driver aus ctx->ActiveNetDrivers (@ctx+0x228, Stride 16, Driver @+0)
//    * World->NetDriver = 0x58 (aus SDK), UNetDriver::World = 0x148
//    * InitListen bekommt den FNetworkNotify*-SUBOBJEKTZEIGER, nicht den
//      UWorld* — UWorld erbt zusaetzlich von FNetworkNotify. Der Subobjekt-
//      Offset wird zur Laufzeit gefunden: gesucht wird ein Slot im World,
//      dessen VTable UWorld::NotifyControlMessage (base+0x473A100) in den
//      ersten 8 Eintraegen enthaelt.
// ============================================================================
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <share.h>
#include <intrin.h>     // _ReturnAddress (v58 aircraft fix)
#include <cmath>        // sqrtf (v58 loot log)
#include <ctime>        // time() for the [gg] relay line (v147)

namespace RVA {
    constexpr uintptr_t LoadMap                = 0x0046B91F0; // this == GEngine
    constexpr uintptr_t LoadMap_CallListen     = 0x0046BC9B1; // E8 0A 15 A6 FC
    // ACHTUNG: 0x46A3BF0 ist die UPendingNetGame*-Ueberladung (nutzt 0x46AD580,
    // vergleicht ctx+0x1A8 = PendingNetGame). Die UWorld*-Variante ist 0x46A3C40,
    // sie nutzt 0x46AD690 und vergleicht ctx+0x280 = ThisCurrentWorld — genau das
    // Feld, aus dem LoadMap den World holt ([r13+0x280]).
    constexpr uintptr_t CreateNamedNetDrv_World= 0x0046A3C40; // (GEngine, UWorld*, FName*, FName*)
    constexpr uintptr_t GetWorldContextFromW   = 0x0046AD690; // (GEngine, UWorld*) -> FWorldContext*
    constexpr uintptr_t NotifyControlMessage   = 0x00473A100; // UWorld::NotifyControlMessage
    // UWorld::NotifyAcceptingConnection. Ueber die Logzeilen
    // "NotifyAcceptingConnection: Client refused" / "... Server %s accept"
    // gefunden; liegt direkt vor NotifyControlMessage (0x4739F50..0x473A0F9).
    // Liest an +0x11 ungeprueft World->NetDriver->ServerConnection.
    constexpr uintptr_t NotifyAcceptingConnection = 0x04739F50;
    // Die eine Stelle, an der UWorld::InternalGetNetMode NICHT wegoptimiert ist.
    // Beweis fuer die Konstantenfaltung (siehe Kommentar bei der Maske):
    //   4729C57  cmp qword [world+0x58], 0
    //   4729C5C  je  .demo
    //   4729C5E  mov eax, 3            <- NM_Client, hart einkompiliert
    constexpr uintptr_t NetModeFoldProof = 0x04729C5E;
    constexpr uintptr_t IpNetDriver_InitListen  = 0x00132C420; // UIpNetDriver::InitListen (Basis)
    constexpr uintptr_t DriverInitListen_Override = 0x0012B9DC0; // USteamNetDriver::InitListen, VTable-Index 83 (0x298)
    // In dieser Funktion entscheidet sich, ob Steam-P2P-Sockets oder echte
    // UDP-Sockets benutzt werden:
    //   12b9e09  test rax,rax          ; Steam-Socket-Subsystem
    //   12b9e0c  je   12b9ee4          ; null      -> Passthrough
    //   12b9e1c  call FURL::HasOption("bIsLanMatch")
    //   12b9e23  jne  12b9ee4          ; gesetzt   -> Passthrough
    //   12b9ee4  mov byte [rbx+0x7E0],1  ; bIsPassthrough = true
    //   12b9f12  jmp  UIpNetDriver::InitListen   (echter UDP-Socket)
    // Wir machen den ersten Sprung unbedingt -> immer echte IP-Sockets.
    constexpr uintptr_t SteamPassthrough_JE = 0x0012B9E0C;
    // FOnlineSubsystemSteam::InitSteamworksClient — Besitz-/Lizenzpruefung:
    //   12ba72f  mov  rax,[rcx]
    //   12ba732  call qword ptr [rax]      ; SteamApps()->BIsSubscribed()
    //   12ba734  test al,al
    //   12ba736  je   12ba7f4              ; 0 -> "Steam User is NOT subscribed, exiting."
    //   12ba73c  ...                       ; sonst -> "Steam User is subscribed %i"
    // Strings: RVA 0x58195A0 "Steam User is NOT subscribed, exiting."
    //          RVA 0x5819560 "Steam User is subscribed %i"
    // 6x NOP -> der Sprung entfaellt, es geht immer den "subscribed"-Weg.
    constexpr uintptr_t SteamSubscribedCheck_JE = 0x0012BA736;
    // UNetDriver::SetWorld — setzt World/WorldPackage/Notify UND registriert
    // den Treiber an den TickDispatch-Delegates der World. Zwingend noetig,
    // sonst wird TickDispatch nie gerufen und Pakete bleiben ungelesen.
    constexpr uintptr_t UNetDriver_SetWorld = 0x0043FFBB0;
    // AGameModeBase::FindPlayerStart_Implementation(AController*, const FString&)
    // Gefunden ueber die Log-Zeile "FindPlayerStart: PATHS NOT DEFINED or NO
    // PLAYERSTART with positive rating" (String RVA 0x6017B80, einzige lea-
    // Referenz bei 0x4249128; laut .pdata gehoert die zur Funktion
    // 0x4248DA0-0x42491D0). LV-OrbIsland hat keine PlayerStarts -- im Original
    // setzt der Dedicated Server die Positionen. Ohne Startpunkt spawnt UE am
    // Weltursprung, und der liegt bei OrbIsland weit draussen im Wasser.
    constexpr uintptr_t FindPlayerStart = 0x04248DA0;
    // AActor::GetFunctionCallspace -- ueber den VTable-Abzug von v36 gefunden:
    // Slot 67 der Actor-VTable. Nachbarn passen zur Deklarationsreihenfolge in
    // UObject.h (66 = ProcessEvent, 67 = GetFunctionCallspace, 68 = CallRemoteFunction).
    // Inhaltlich bestaetigt: prueft FUNC_Static (0x2000), laeuft die
    // SuperStruct-Kette (+0x48) hoch und testet FUNC_NetMulticast (bt 14),
    // FUNC_NetServer (0x200000) und FUNC_Net (bit 6) an UFunction+0xC8.
    constexpr uintptr_t GetFunctionCallspace = 0x03F8CF60;
    // UNetDriver::TickFlush -- hier laeuft ServerReplicateActors.
    // Gefunden ueber die Log-Zeile "UNetDriver::TickDispatch: Very long time
    // between ticks" (String RVA 0x606C170, lea-Referenz bei 0x4400F03 ->
    // Funktion 0x4400E10 = UNetDriver::TickDispatch). Der VTable-Eintrag, der
    // diese Funktion aufruft, ist Index 96 (UIpNetDriver::TickDispatch), also:
    //   [96] TickDispatch      0x013483D0  (5761 B)
    //   [97] PostTickDispatch  0x043F2FC0  ( 357 B)
    //   [98] TickFlush         0x04401050  (7102 B)   <-- der Brocken
    //   [99] PostTickFlush     0x043F3130  (  55 B)
    // 7102 Byte heisst: die Replikationsschleife ist VORHANDEN, nicht gestrippt.
    constexpr uintptr_t UNetDriver_TickFlush = 0x04401050;
    // UReplicationGraph::ServerReplicateActors -- Zieladresse zur Kontrolle,
    // damit wir niemals einen unerwarteten Zeiger aufrufen.
    constexpr uintptr_t RepGraph_ServerReplicateActors = 0x013C6A50;
    // FName::ToString(FString& out) -- rcx = FName*, rdx = FString*.
    // Confirmed by the block-decode inside it (ComparisonIndex>>0x10 = block,
    // &0xffff = offset, entry = [pool + block*8 + 0x10] + 2*offset).
    constexpr uintptr_t FName_ToString = 0x02A7A590;
    // FMemory::Free(void*) -- frees the FString buffer ToString allocated.
    constexpr uintptr_t FMemory_Free   = 0x0261AE00;

    // ---- v42: the actor-level net mode ---------------------------------
    // AActor::GetNetMode() -- a REAL, non-inlined function with 456 direct
    // callers (found through APawn::PossessedBy, see below). Unlike the
    // world-level UWorld::InternalGetNetMode (folded to "NetDriver ? NM_Client
    // : ..." at base+0x4729C57) this one is NOT folded:
    //     03F92746  movsd xmm0,[rbx+0x1C0]        ; NetDriverName (FName)
    //     03F9274E  mov  edx,0x11A                ; NAME_GameNetDriver
    //     03F92782  mov  rax,[rdi+0x58]           ; World->NetDriver
    //     03F927C9  jmp  qword ptr [rdx+0x278]    ; NetDriver->GetNetMode()  (virtual!)
    //     03F927D5  mov  rcx,[rdi+0x130]          ; else World->DemoNetDriver
    //     03F927FA  xor  eax,eax                  ; else NM_Standalone
    // The virtual target is UNetDriver::GetNetMode (base+0x43E77D0):
    //     IsServer() ? (GIsClient ? NM_ListenServer : NM_DedicatedServer) : NM_Client
    // i.e. with the driver visible every ACTOR already reports NM_ListenServer.
    // Only the WORLD reports NM_Client -- that is what the standalone mask
    // fixes (Kismet IsServer/IsStandalone, UWorld::IsNetMode). The price of the
    // mask was that actors now saw NM_Standalone, and that broke:
    //   * APawn::PossessedBy        -> SetAutonomousProxy(true) skipped, the
    //                                  joiner's own pawn arrived as a
    //                                  SimulatedProxy (no input, no camera)
    //   * AActor::GetFunctionCallspace -> every RPC "Local" (the v37 switch
    //                                  papered over that additively)
    //   * AController::IsLocalController -> true for the REMOTE player's PC
    // Hooking this one function gives actors NM_ListenServer again while the
    // world keeps looking standalone. That is exactly what a real listen
    // server looks like from the engine's point of view.
    constexpr uintptr_t AActor_GetNetMode = 0x03F92730;
    // APawn::PossessedBy(AController*) -- found as the caller of
    // AActor::SetAutonomousProxy (base+0x3F9FEC0, string "SetAutonomousProxy
    // called on a unreplicated actor"). Decoded body (UE 4.25 layout):
    //     044AE72D  call [rax+0x490]              ; SetOwner(NewController)
    //     044AE733  mov  rbp,[rbx+0x358]          ; OldController = Controller
    //     044AE740  mov  [rbx+0x358],rsi          ; Controller = NewController
    //     044AE747  call [rax+0x638]              ; ForceNetUpdate()
    //     044AE754  mov  rcx,[rax+0x390]          ; Controller->PlayerState
    //     044AE79D  call 0x48D4E60                ; APlayerController::StaticClass()
    //     044AE7AE..BE  IsChildOf via StructBaseChain (+0x38 chain, +0x40 num)
    //     044AE7C3  call 0x3F92730                ; GetNetMode()
    //     044AE7C8  test eax,eax / je 0x44AE816   ; NM_Standalone -> SKIP
    //     044AE7D1  call 0x3FA0EB0                ; SetReplicates(true)
    //     044AE7E0  call 0x3F9FEC0                ; SetAutonomousProxy(true,true)
    //     044AE825  call 0x48C7E30                ; ReceivePossessed/NotifyControllerChanged
    // Hooked only to LOG the result and, as a belt-and-braces measure, to
    // repeat the two calls if the pawn still came out as a SimulatedProxy.
    constexpr uintptr_t APawn_PossessedBy      = 0x044AE710;
    constexpr uintptr_t AActor_SetReplicates   = 0x03FA0EB0; // (AActor*, bool)
    constexpr uintptr_t AActor_SetAutonomousProxy = 0x03F9FEC0; // (AActor*, bool, bool bForceCompare)
    // Cached UClass* of APlayerController (what StaticClass() returns; the
    // function reads it from this global at 048D4E6A "mov rax,[rip+...]").
    constexpr uintptr_t APlayerController_ClassPtr = 0x07798F28;

    // ---- v43: kill the replay recorder on the host ---------------------
    // UGameInstance::StartRecordingReplay -- the function that logs
    // "StartRecordingReplay: A replay is already recording" (string RVA
    // 0x6014720) and creates the DemoNetDriver/DemoNetConnection. Single
    // caller at 0x2162C10; its return value is NOT used (void).
    //
    // WHY: the host crashed the instant the aircraft phase began. The whole
    // crash callstack is the DEMO-RECORD path, not the live netcode:
    //   0x4340D90 TickDemoRecord -> 0x41C3160 prioritize
    //   -> 0x41BEB20 DemoReplicateActor -> 0x41DDA60 TickDemoRecord replicate
    //   -> 0x41DE070 ReplicateProperties -> 0x244D950/0x244F3D0 FastArrayDelta
    //   -> 0x4567830 FRepLayout::DeltaSerializeFastArrayProperty
    //   faulting at 04567CAB "add r12,[rcx+8]" with rcx = [obj+0x808] == null.
    // The game keeps a rolling crash-dump replay (log: CVar
    // cr.UseDumpCrashRecordingReplay=1) and records ALL net actors. v42 makes
    // the joiner's actors replicate fully (correct roles), and one of their
    // FastArray properties has no shadow/delta state -> null deref, but ONLY
    // inside the recorder. The live client connection is fine (it welcomed and
    // reached the aircraft). A private listen server needs no replay, so we
    // simply skip recording -- that removes the entire crashing code path.
    //
    // Switch off without rebuilding:  -allowreplay
    constexpr uintptr_t GameInstance_StartRecordingReplay = 0x0426AF90;

    // ---- v44: pin the world origin (World Composition rebasing) --------
    // UWorld::SetNewWorldOrigin -- the single choke point for origin shifts,
    // found via the log strings "WORLD TRANSLATION BEGIN"/"END" (RVA 0x60FD340
    // / 0x60FD3B0, both referenced only from this function). Three callers:
    // UWorld::Tick (0x434127D, feeds RequestedOriginLocation) and two game
    // paths (0x213E4F0, 0x21814C0).
    //
    // WHY: LV-OrbIsland uses World Composition with origin rebasing. A retail
    // DEDICATED server has no local view, so its origin stays {0,0,0} and the
    // whole game is written against "server coordinates == absolute". Our
    // listen host HAS a local player, so it rebases -- the logs show the host
    // walking through {0,0,0} -> {-329462,16772,0} -> {-279027,49399,0} ->
    // {-228626,82002,0} while the client sat on its own {-224903,84413,0}.
    // From then on any raw FVector crossing the wire (vault target, zone
    // centre, spawn points -- anything not going through FRepMovement's
    // RebaseOntoZeroOrigin/LocalOrigin) is read in the wrong coordinate space.
    //
    // Measured proof from the 19:09 run: the host's own zone check printed
    //   CharacterLoc     X=-179193.375 Y=108295.867     (rebased/local)
    //   OuterCircleCenter X=-142355.812 Y=127502.656    (absolute)
    //   Distance          272789.562500
    // 272789.6 is the distance using the ABSOLUTE character position
    // (local + origin), matching to 0.1 units -- so the zone maths is right
    // and the character really was 2.7 km out. Subtracting one world-origin
    // offset puts him 460 m from the zone centre, i.e. exactly where he was
    // standing before the vault: the vault displaced him by one origin
    // (2.40 km). He then fell outside the physics MBP bounds ("Component
    // CollisionCylinder ... has physics bodies outside of MBP bounds,
    // collisions are disabled"), which is the "out of map", and the zone
    // killed him seconds later.
    //
    // FIX: keep the host's origin pinned at {0,0,0} so server-side local ==
    // absolute, exactly like the dedicated server the game was built against.
    // The CLIENT keeps rebasing normally -- that is what retail does too, and
    // replicated movement is origin-corrected by the engine on both ends.
    // Precision is a non-issue: the map spans ~4 km, WORLD_MAX is ~21 km.
    //
    // UWorld::OriginLocation          = +0x5E0 (FIntVector)
    // UWorld::RequestedOriginLocation = +0x5EC (FIntVector)
    // both read off the UWorld::Tick call site:
    //   04341260  movsd xmm0,[rsi+0x5EC] / mov eax,[rsi+0x5F4]
    //   04341276  lea rdx,[rbp-0x60] / mov rcx,rsi / call SetNewWorldOrigin
    //
    // Switch off without rebuilding:  -allowrebase
    constexpr uintptr_t UWorld_SetNewWorldOrigin = 0x04744230;

    // ---- v55: World Composition streaming around REMOTE players ---------
    // UWorld::Tick (0x4340D90) at 0x4341604: "mov rcx,[rsi+0x608] / test /
    // call 0x4761A00" = UWorldComposition::UpdateStreamingState(). That one
    // loops GEngine->GetNumGamePlayers(World) / GetGamePlayer(World,i) and
    // takes ->PlayerController (+0x38) of every LOCAL player, fetches a view
    // point per controller (0x45031A0) and then calls the array overload:
    //   0x4760F20  UpdateStreamingState(this, const FVector* Locations,
    //                  const FVector* Directions, int32 Num,
    //                  int32 Lod /*[rsp+20]*/, float DistanceScale /*[rsp+28]*/)
    // Remote players own no ULocalPlayer, so their tiles are never requested.
    // We hook the overload and append their pawns. -nostreamremote disables.
    constexpr uintptr_t WorldComp_UpdateStreamingState = 0x04760F20;
    // Game helper used by the local path: (APlayerController*, FVector* outLoc,
    // FVector* outDir). Only called for DIAGNOSTICS (first few calls), the
    // streaming itself uses the pawn's root location -- fewer assumptions.
    constexpr uintptr_t PC_StreamingViewPoint = 0x045031A0;
    // GUObjectArray, read off the inlined IsPendingKill at 0x4761B70:
    //   mov eax,[obj+0xC] / cmp eax,[rip+..]=0x762F71C / jge invalid
    //   chunk = [0x762F708][idx>>16] ; item = chunk + (idx&0xFFFF)*40
    //   flags = [item+0x20] ; PendingKill = bit 29 (0x20000000)
    constexpr uintptr_t GUObjectArray_Chunks   = 0x0762F708;
    constexpr uintptr_t GUObjectArray_NumElems = 0x0762F71C;

    // ---- v58: zone actor relevancy through the replication graph ------
    // FName::FName(FName* out, const WCHAR* str, EFindName type) -- 648 call
    // sites, always "lea rcx,[out] / lea rdx,[str] / mov r8d,1". With type 0
    // (FNAME_Find) an unknown string yields NAME_None instead of a new entry.
    constexpr uintptr_t FName_Ctor = 0x02A595B0;
    // UNetDriver::AddNetworkActor(this, AActor*): skips DORM_Initial startup
    // actors, then NetworkObjects->FindOrAdd and ReplicationDriver->AddNetworkActor
    // (vtable +0x2A0). Prolog 16 bytes, position independent.
    constexpr uintptr_t NetDriver_AddNetworkActor    = 0x043DE4B0;
    // UNetDriver::RemoveNetworkActor(this, AActor*): NetworkObjects->Remove,
    // RenamedStartupActors.Remove(name), ReplicationDriver->RemoveNetworkActor (+0x2A8).
    constexpr uintptr_t NetDriver_RemoveNetworkActor = 0x043FA590;

    // ---- v58: aircraft boarding RPC for remote pawns -------------------
    // ABravoHotelCharacter::DoInAircraft (0x1FEC960) does
    //     if (GetNetMode() == NM_DedicatedServer) { InternalInAircraft(); ClientInAircraft(); }
    //     else                                    { InternalInAircraft(); }
    // i.e. the ClientInAircraft RPC is only ever sent by a dedicated server.
    // The GetNetMode call sits at 0x1FEC9DC and returns to 0x1FEC9E1; for the
    // REMOTE pawn we answer NM_DedicatedServer at exactly that return address.
    constexpr uintptr_t DoInAircraft_GetNetModeRet = 0x01FEC9E1;

    // ---- v58 (re-done v56): floor loot around REMOTE pawns -------------
    // ABravoHotelBuilding::CheckSpawnByStandalone(this) = 0x1FA5E60 and
    // ABravoHotelVehicleSpawnActor::CheckSpawnByStandalone(this) = 0x22BEF20.
    // Both: GetWorld, GameState (BattleRoyaleState +0x9F2 == 4),
    // PC = UWorld::GetFirstPlayerController (0x4732E90 -- the HOST only),
    // pawn = thunk 0x21F6110(PC, false), then |pawn - actor|^2 < 30000^2 ...
    // The thunk is an interface adjustor: add rcx,0x6F0 / mov rax,[rcx] /
    // rex.W jmp [rax+0x38]  (7+3+4 = 14 bytes, then int3 int3).
    constexpr uintptr_t Building_CheckSpawn = 0x01FA5E60;   // prolog 15 bytes
    constexpr uintptr_t Vehicle_CheckSpawn  = 0x022BEF20;   // prolog 16 bytes
    // ---- v58f: WHY the check never runs for buildings around the remote ---
    // ABravoHotelBuilding::BeginPlay (0x1FA57F0) and
    // ABravoHotelVehicleSpawnActor::BeginPlay (0x22BF900) only start the
    // looping 0.1 s timer that calls CheckSpawnByStandalone when
    //     AActor::GetNetMode() == NM_Standalone          (0x1FA5867 / 0x22BFA6C)
    // (the building one additionally skips buildings whose location is already
    // in GameState+0xAD0, the "spawned" list, and the timer handle lives at
    // building+0x470). A building whose BeginPlay happens while our mask is
    // lifted therefore NEVER gets a spawn timer. Those two return addresses
    // get an unconditional NM_Standalone (see MyActorGetNetMode).
    constexpr uintptr_t BuildingBeginPlay_GetNetModeRet = 0x01FA586C;
    constexpr uintptr_t VehicleBeginPlay_GetNetModeRet  = 0x022BFA71;
    // GameState of an actor: GEngine->GetWorldContextFromWorld(actor)->+0x1D8
    // (0x424C130, used by CheckSpawnByStandalone itself). The "spawned
    // buildings" TArray<FVector2D> sits at GameState+0xAD0 (Num +0xAD8) and
    // grows by one every time the spawn path completes -- our success signal.
    constexpr uintptr_t Actor_GameState     = 0x0424C130;
    constexpr uintptr_t GameState_SpawnedNum = 0xAD8;
    // The match phase CheckSpawnByStandalone tests FIRST (0x1FA5F6B):
    //   cmp byte [GameState+0x9F2], 4  -- everything else is behind it.
    constexpr uintptr_t GameState_Phase      = 0x9F2;
    // Building spawn flags (+0x468..+0x46B, all 1 in the constructor 0x1FA5150):
    // 0x1FA7980 refuses mode 2 ("standalone") when +0x46A == 0. +0x470 is the
    // timer handle; +0x409 the "inside zone" byte GameState+0x818 pairs with.
    constexpr uintptr_t Building_SpawnFlags = 0x468;
    constexpr uintptr_t Building_SpawnTimer = 0x470;
    constexpr uintptr_t Building_InZone     = 0x409;
    // v58o: AActor::GetComponentByClass vtable slot.
    constexpr size_t    AActor_GetComponentByClass_VT = 0x660;
    // v58q CORRECTION: +0x534 is NOT "this source has already fired". The
    // inner spawn function 0x2141FD0 clears it on entry and then sets it to
    // (+0x528 == 0 && +0x529 == 0) -- for mode 2 that is always 1, and it
    // stays 1 forever afterwards. So reading it as a done-flag made us skip
    // every source the game had ever touched ("schon vorher fertig" in the
    // v58p log). It is really "spawn all categories in this pass".
    constexpr uintptr_t SpawnComp_All                 = 0x534;

    // ---- v58g: the viewpoint the replication graph uses per client --------
    // UBasicReplicationGraph::ServerReplicateActors 0x13C6A50 (vtable +0x2F0)
    // only drains ActorsWithoutNetConnection (graph+0x5C0/+0x5C8) and tail-
    // jumps into UReplicationGraph::ServerReplicateActors 0x13C6C60. That one
    // walks the graph's connections (graph+0x40 Data / +0x48 Num; each
    // UNetReplicationGraphConnection holds its UNetConnection at +0x30) and:
    //   conn->ViewTarget(+0x110) = conn->PlayerController(+0x38)
    //                            ? PC->GetViewTarget()   [PC vtable +0x700]
    //                            : conn->OwningActor(+0x118)
    //   skip the connection when conn->State(+0x1BC)==USOCK_Closed(1)
    //   skip the connection when ViewTarget is null
    //   FNetViewer(out, conn, 0.0f) = 0x43DB130 for the connection and each
    //   child connection.
    // FNetViewer: +0x00 Connection, +0x08 InViewer, +0x10 ViewTarget,
    //             +0x18 ViewLocation (FVector), +0x24 ViewDir; size 0x30.
    // ViewLocation starts as ViewTarget->RootComponent(+0x158)->Translation
    // (+0x110) and is then overwritten by PC->GetPlayerViewPoint [vt +0x760].
    // THAT location is the only position the grid node knows about, so it
    // decides which cells a client is sent -- i.e. everything that is not
    // bAlwaysRelevant: floor loot, breakable glass, doors, vehicles, other
    // players. The zone works today because it sits in the always-relevant
    // node and needs no location at all. Prolog 15 bytes, position-independent.
    constexpr uintptr_t FNetViewer_Ctor = 0x043DB130;

    // ---- v58i: the level streaming state machine, read out of the code -----
    // UWorldComposition::UpdateStreamingState (array form, 0x4760F20) first
    // has every tile evaluated in parallel (0x4759660 -> lambda 0x4752FC2) and
    // then commits each result through
    //   UWorldComposition::CommitTileStreamingState(comp, UWorld*, int TileIdx,
    //       bool bShouldBeLoaded, bool bShouldBeVisible, bool bShouldBlockOnLoad,
    //       int LODIndex, bool a8, bool a9) = 0x4756450
    // which calls the REAL setters on the tile's ULevelStreaming:
    //   vtable +0x268 SetShouldBeLoaded, 0x433E0F0 SetShouldBeVisible,
    //   0x433DE00 SetLevelLODIndex, 0x433E400, 0x433E180.
    // v58s: and BEFORE any of that it early-outs on the tile's evaluation
    // byte (tile+0xD8):
    //     ecx = [tile+0xD8]; ecx--; je apply; ecx--; je apply;
    //     cmp ecx,1; je RETURN            <- Eval == 3 does nothing at all
    // Every tile in every log we have taken reads Eval=3, so v58r's 174 calls
    // into the commit did literally nothing. The three setters below are what
    // the commit body calls once it gets past that byte, in this order, so we
    // call them ourselves instead.
    constexpr uintptr_t LevelStreaming_SetShouldBeVisible = 0x0433E0F0;
    constexpr uintptr_t LevelStreaming_SetLevelLODIndex   = 0x0433DE00;
    constexpr size_t    LevelStreaming_SetShouldBeLoadedVT = 0x268;
    // Each of those notifies UWorld::UpdateStreamingLevelShouldBeConsidered
    // (0x474A7C0) -- which is why writing those fields directly (v58h) was
    // wrong: the commit compares its arguments against the current values,
    // sees a difference that it did not cause, and drives the level through a
    // pointless unload/reload every single frame. Hooking the commit and
    // changing its ARGUMENTS instead leaves the engine's own state machine
    // intact. Prolog 22 bytes, position independent.
    constexpr uintptr_t WorldComp_CommitTile = 0x04756450;

    // ---- v58j: the three stages an actor passes on its way to a client -----
    // The v58i log settled the streaming question for good: 1245 tiles are
    // LoadedVisible, the tiles under the remote player are LODIndex -1 and in
    // UWorld::Levels, and the census counts 64 BravoHotelPickup plus two
    // weapons within 200 m OF HIM, on the host. The loot exists, right where
    // he stands, and he does not see it. So it is replication, and these are
    // the three gates it has to pass:
    //   1. UNetDriver::AddNetworkActor  (already hooked) -- is it in the graph
    //   2. UReplicationGraph::ReplicateSingleActor 0x13BE920 -- did the graph
    //      gather it for this connection this frame. rcx = graph, rdx = actor;
    //      ten arguments in total (four registers, six on the stack).
    //      Inside it, two gates can still drop the actor:
    //        0x13BECB4 a per-CLASS policy lookup (map at graph+0x240,
    //                  0x1395990 -- cached, virtual fallback; the byte it
    //                  returns must be 1)
    //        0x13BED8F the cull distance check against the viewer
    //      and only then does it create the channel:
    //        0x43C03B0 create channel, then
    //   3. UActorChannel::SetChannelActor 0x41AC3C0 (rcx channel, rdx actor)
    //      -- the actor is now really being sent to that connection.
    // Counting all three per class says in one round which gate loses the
    // pickups, instead of another theory about why they might be lost.
    constexpr uintptr_t RepGraph_ReplicateSingleActor = 0x013BE920;   // prolog 15
    constexpr uintptr_t ActorChannel_SetChannelActor  = 0x041AC3C0;   // prolog 16
    constexpr uintptr_t RepGraph_ClassPolicyLookup    = 0x01395990;   // (map, UClass*)
    constexpr uintptr_t RepGraph_ClassPolicyMap       = 0x240;        // graph + this
    // ULevelStreaming, from SetLevelLODIndex / SetShouldBeVisible / 0x475A1D0:
    //   +0xC0 LevelLODIndex, +0xC8 CurrentState, +0xC9 TargetState,
    //   +0xD0 bit0 bShouldBeVisible / bit1 bShouldBeLoaded / bit4 bBlockOnLoad,
    //   +0x140 LoadedLevel, +0x148 PendingUnloadLevel, +0x28 the world.
    // CurrentState is UE4's ECurrentState: 0 Removed, 1 Unloaded,
    // 2 FailedToLoad, 3 Loading, 4 LoadedNotVisible, 5 MakingVisible,
    // 6 LoadedVisible, 7 MakingInvisible (proven by SetLevelLODIndex turning
    // 2 into 1, exactly as the engine turns FailedToLoad back into Unloaded).
    // UWorld::GetFirstPlayerController(this) = 0x4732E90, 21 bytes:
    //   cmp dword [rcx+0x278],0 / jle ret0 / mov rcx,[rcx+0x270] / jmp FWeakObjectPtr::Get
    // (ControllerList: Data +0x270, Num +0x278). We re-implement it in the hook,
    // the jmp is relative and cannot be copied into a trampoline.
    constexpr uintptr_t World_GetFirstPlayerController = 0x04732E90;
    // The interface adjustor the spawn check uses to get a PlayerController's
    // character: add rcx,0x6F0 / mov rax,[rcx] / rex.W jmp [rax+0x38] (v58n).
    constexpr uintptr_t Pawn_Thunk = 0x021F6110;

    // ---- v58o: the loot spawn chain, read end to end -----------------------
    //  ABravoHotelBuilding::CheckSpawnByStandalone 0x1FA5E60
    //    gates: GameState+0x9F2 == 4 | first PlayerController | its pawn |
    //           distance < 300 m | all spawn components registered (0x1FA5C60)
    //           | PC+0x1928
    //    -> clears its timer (+0x470), takes the area name from the pawn and
    //  0x1FA7980(building, FName area, 0.0f, mode 2)
    //    gates: not outside the zone ([GameState-ish+0x818] && building+0x409)
    //           | mode 2 needs building+0x46A
    //    -> comp = building->vtable[+0x660](UBravoHotelDetectItemSpawnBoxComponent)
    //       building+0x398 = the area name as FString
    //  0x2142DA0(comp, float delay, uint8 mode)
    //    gates: comp+0xB0 world | GameState 0x1CB2110(gs) >= 4 |
    //           owner(comp+0xA8)->Role(+0x20F) == 3 | per-mode byte +0x528/+0x529
    //    -> comp+0x51C = GameState+0x5C8, then
    //  0x2141FD0(comp, uint8 mode)
    //    -> comp+0x534 = "this component has spawned" (the only real guard)
    //    -> for every CHILD component (0x413EBB0 = GetChildrenComponents):
    //         0x2177320(child, comp+0x51C, mode)      <- one pickup per point
    //
    //  So a "loot building" is simply any actor carrying a
    //  UBravoHotelDetectItemSpawnBoxComponent (StaticClass 0x23C5C40, cached at
    //  0x735FF40). That test is what the game itself uses, it does not depend
    //  on our class-hierarchy detection, and comp+0x534 says whether that
    //  source has already fired. ABravoHotelPickup itself: StaticClass
    //  0x24520C0, class cache 0x73654C0, instance size 0xA30.
    constexpr uintptr_t ItemSpawnBoxComp_StaticClass = 0x023C5C40;
    constexpr uintptr_t ItemSpawnComp_Spawn          = 0x02142DA0;  // (comp, delay, mode)
    constexpr uintptr_t Pickup_StaticClass           = 0x024520C0;
    // v58q: the inner function, the one that actually creates the pickups.
    // 0x2141FD0(comp, mode): clears comp+0x534, recomputes it as
    // (+0x528 == 0 && +0x529 == 0), walks GetChildrenComponents and calls
    // 0x2177320(child, comp+0x51C, mode) for every child = one pickup per
    // spawn point. It has NO world / gamemode / phase / role gate -- all six
    // of those sit in 0x2142DA0 in front of it. Signature (comp, uint8 mode).
    constexpr uintptr_t ItemSpawnComp_SpawnInner     = 0x02141FD0;
    // v58w, from the other team's reflection database (native-properties.json
    // in SP-hand-off-main): BravoHotelCharacter::CharacterReplication is an
    // object reference at +0x40C8, and the object it points at
    // (BHCharacterReplication) carries ReplicatedStateID at +0xD4 with
    // OnRep_ReplicatedStateID and CPF_Net set.
    constexpr uintptr_t Character_CharacterReplication = 0x40C8;

    // ---- v58x: from the handoff's native-function database ---------------
    //  SP-hand-off-main/.../schemas/native-reflection/native-functions.json
    //  lists 1368 natives with owner, name and exec-thunk RVA. The thunks are
    //  ProcessEvent wrappers; for the parameterless ones the real
    //  implementation is the call/jmp target right after the stack bookkeeping,
    //  which is what these are. Verified by disassembly, not taken on trust.
    //
    //      GetCurrentWeapon        exec 0x235C540  ->  0x2017300
    //      GetCurrentRangedWeapon  exec 0x235C3D0  ->  0x2015BF0
    //      GetPendingWeapon        exec 0x235CFA0  ->  0x201D5B0
    //      OnChangeCurrentWeapon   exec 0x2361E10  ->  0x2040560
    //      IsInAircraft            exec 0x235EC50  ->  0x202F560
    //      GetIsFiredRemaining     exec 0x235CAA0  ->  0x201A4E0
    //      DoReload                exec 0x2358EC0  ->  0x1FF12C0
    //      DoReloadImmediately     exec 0x2358EE0  ->  0x1FF13E0
    //
    //  GetCurrentWeapon is nineteen bytes and says where the weapon lives:
    //      mov rcx,[rcx+0x648] ; test rcx,rcx ; jne 0x1C56650 ; xor eax,eax
    //  so Character+0x648 is the weapon holder and 0x1C56650 resolves it.
    //  That replaces the level sweep as the way to find the remote player's
    //  weapon -- it is immediate, exact, and survives every weapon switch.
    constexpr uintptr_t Character_GetCurrentWeapon = 0x02017300;  // void* (character)
    constexpr uintptr_t Character_DoReload         = 0x01FF12C0;  // void  (character)
    constexpr uintptr_t Character_WeaponHolder     = 0x648;       // object GetCurrentWeapon reads
    // -----------------------------------------------------------------------
    //  v60: read out of the binary's own native reflection table, not measured
    //  (full write-up in STATIC-FINDINGS-ammo.md in the game folder). Property
    //  names live in .data obfuscated with byte[i] ^= (0xDD + i); each
    //  descriptor is { name, repnotify_name, uint64 flags @+0x10, ...,
    //  uint32 offset @+0x24 } -- the same layout the blue-zone work used.
    //
    //      Magazine                  weapon+0x0DD0   CPF_Net | CPF_RepNotify
    //      BackPackInTotalAmmoCount  weapon+0x0E50   CPF_Net
    //      MagazineCapacity          weapon+0x1A28   not replicated
    //      WeaponInfo +0xE78, WeaponReplicatedComponent +0xC38,
    //      WeaponBaseData +0x1AB8, LastFireTime +0xAF8
    //
    //  So +0x1B0, which every build since v59c watched, has NO reflection
    //  entry and is therefore never serialised -- that is the whole of #15,
    //  and the -magtest round confirms it from the other side: 99 went into
    //  +0x1B0 and the PC's HUD did not move. The weapon constructor 0x1FAFA70
    //  zeroes [weapon+0xDD0] at 0x1FB0206 and carries the class size 0x1E00 as
    //  a literal, so both the offset and the object are confirmed.
    //  There is no native write to +0xDD0 anywhere in .text: every writer is
    //  Blueprint going through the reflection system, so it can be read but
    //  never caught with a byte-pattern hook.
    constexpr uintptr_t Weapon_Magazine         = 0x00DD0;
    constexpr uintptr_t Weapon_BackPackAmmo     = 0x00E50;
    constexpr uintptr_t Weapon_MagazineCapacity = 0x01A28;
    // ClientSetMagazine(int32 NewMagazine) -- the game's own "server tells the
    // owning client its magazine" RPC, declared on the weapon. This RVA is the
    // lazy UFunction getter: no arguments, returns UFunction*, caches into the
    // global at 0x736E600 on first call. The parameter struct is a single
    // int32 at offset 0.
    constexpr uintptr_t Weapon_GetClientSetMagazineFn = 0x02526BF0;
    // AActor::ProcessEvent. v36 pinned GetFunctionCallspace to vtable slot 67,
    // and UObject.h declares ProcessEvent immediately before it -- slot 66.
    // Kept as a value so the slot can be sanity-checked before it is called.
    constexpr uintptr_t AActor_ProcessEvent     = 0x03F98AF0;   // 15-byte prologue
    constexpr uintptr_t VT_ProcessEvent         = 66 * 8;      // 0x210
    // AActor::GetNetConnection, vtable slot -- v58g read it off the graph's
    // ActorsWithoutNetConnection pass. Used here read-only, to log whether the
    // weapon can even resolve an owning connection.
    constexpr uintptr_t VT_GetNetConnection     = 0x4F0;
    // Fields 0x2142DA0 touches on the way through (all read off its code):
    constexpr uintptr_t SpawnComp_World   = 0x0B0;   // UActorComponent::World
    constexpr uintptr_t SpawnComp_Owner   = 0x0A8;   // UActorComponent::Owner
    constexpr uintptr_t SpawnComp_Time    = 0x51C;   // = World->TimeSeconds
    constexpr uintptr_t SpawnComp_ModeA   = 0x528;   // mode 2 zeroes both
    constexpr uintptr_t SpawnComp_ModeB   = 0x529;
    constexpr uintptr_t World_AuthGameMode = 0x1D0;  // null on a client
    constexpr uintptr_t GameMode_GameState = 0x370;
    constexpr uintptr_t World_TimeSeconds  = 0x5C8;
    constexpr uintptr_t WeakObjectPtr_Get              = 0x02D26750;

    // ---- v58: replication-graph internals (UBasicReplicationGraph) ---------
    // vtable @0x5856A88: +0x2A0 AddNetworkActor (0x13AB0B0), +0x300
    // RouteAddNetworkActorToNodes (0x13C5B60: bAlwaysRelevant -> AlwaysRelevant-
    // Node +0x5A8 else grid +0x5A0), +0x338 InitGlobalActorClassSettings.
    // FGlobalActorReplicationInfoMap sits at graph+0xC0; Get(map, AActor* const&)
    // = 0x1395A80 returns the FGlobalActorReplicationInfo whose Settings hold
    // CullDistance at +0x90 and CullDistanceSquared at +0x94 (both written by
    // the game's own graph at 0x139C620..0x139C634).
    constexpr uintptr_t BasicGraph_InitGlobalActorClassSettings = 0x013B54C0;
    // -----------------------------------------------------------------------
    //  v71, taken from SP-hand-off-main/windows-implementation-opus-pass-1 and
    //  checked against THIS binary by their verify_host_rvas.txt (prologue
    //  bytes listed there match ours):
    //      URealReplicationGraph::StaticClass             0x0139D260
    //      UBasicReplicationGraph::StaticClass            0x013CA4C0
    //      UReplicationGraph::StaticClass                 0x013CB400
    //      UReplicationGraph::ServerReplicateActors       0x013C6A50
    //      URealReplicationGraph::ServerReplicateActors   0x013C6C60 (0xD2C B)
    //
    //  Our graph's vtable+0x2F0 holds 0x13C6A50 -- the BASE gather. The game's
    //  own server used BravoHotelReplicationGraph, which a client build does
    //  not carry; the nearest class that IS here is URealReplicationGraph, and
    //  it overrides ServerReplicateActors with a different 0xD2C-byte version.
    //  Ours is the one that has been gathering nobody but the pawn, the
    //  GameState and the zone all evening.
    // -----------------------------------------------------------------------
    constexpr uintptr_t RealRepGraph_StaticClass     = 0x0139D260;
    constexpr uintptr_t BasicRepGraph_StaticClass    = 0x013CA4C0;
    constexpr uintptr_t RepGraphBase_StaticClass     = 0x013CB400;
    constexpr uintptr_t RealRepGraph_ServerReplicate = 0x013C6C60;
    // v73: UActorChannel::ReplicateActor(this) -- one argument, the channel.
    // RVA from the other team's verify_host_rvas.txt and confirmed here by
    // disassembly (0x13A5 bytes, reads Connection at channel+0xB0). This is
    // the function the graph would call for an actor it gathered; calling it
    // ourselves for ONE actor sidesteps the gather that returns nobody.
    constexpr uintptr_t UActorChannel_ReplicateActor = 0x041A79B0;
    // -----------------------------------------------------------------------
    //  v76, all of this disassembled from the binary, none of it guessed:
    //
    //  The grid node's vtable is base+0x5855EE0 (read off the live node by
    //  [gc]); slot +0x288 is GatherActorListsForConnection = 0x13B12F0 and
    //  +0x290 is PrepareForReplication = 0x13BBCA0.
    //
    //  Inside the gather (0x13B12F0):
    //      [node+0x58] CellSize, [node+0x5C]/[node+0x60] SpatialBias,
    //      [node+0x64] ConnectionMaxZ (constructor 0x13A5200 sets 2097152.0,
    //                  so the Z check never rejects anybody here),
    //      [node+0x210]/[node+0x218] the Grid columns (our [gc] round shows
    //                  220 then 255 columns, so cells ARE being built),
    //      cellX = (ViewLocation.X - Bias.X) / CellSize, cellY likewise.
    //
    //  And the very first thing it does:
    //      mov  rax, [params+0xC8]      ; viewers, stride 0x30
    //      lea  r12, [params+8]         ; inline buffer if the array is empty
    //      movsxd rax, [params+0xD0]    ; viewer COUNT
    //      ... cmp r12, rax ; je <return>      <-- no viewers, nothing gathered
    //
    //  So if that count is zero the node returns without looking at a single
    //  cell, however healthy the grid is. That is the one remaining branch
    //  that fits everything we have measured, and it is one integer to read.
    // -----------------------------------------------------------------------
    constexpr uintptr_t GridNode_Gather      = 0x013B12F0;
    constexpr uintptr_t GridNode_VTable      = 0x05855EE0;
    constexpr uintptr_t GlobalActorInfoMap_Get                  = 0x01395A80;

    // ---- v58c: dormancy wake-ups must reach the driver -------------------
    // AActor::FlushNetDormancy (0x3F891D0) and AActor::ForceNetUpdate
    // (0x3F896C0) end in "drv = AActor::GetNetDriver(); if (drv) drv->..."
    // and AActor::GetNetDriver (0x3F8E300) reads World->NetDriver (+0x58),
    // which the standalone mask keeps at null. So every dormancy flush --
    // a window breaking, a door opening, the zone waking up, a weapon's ammo
    // changing on a dormant-wanting actor -- silently ended right there and
    // the replication graph never heard about it. Both are now hooked and run
    // inside a MaskOff window (same trick as MyTickFlush).
    //   FlushNetDormancy prolog 17 B: 40 53 / 48 83 EC 20 / 48 8B D9 / 33 D2 / 8B 89 C0 01 00 00
    //   ForceNetUpdate   prolog 16 B: 40 53 / 48 83 EC 20 / 80 B9 0F 02 00 00 03 / 48 8B D9
    constexpr uintptr_t AActor_FlushNetDormancy = 0x03F891D0;
    constexpr uintptr_t AActor_ForceNetUpdate   = 0x03F896C0;

    // ---- v58d: world composition sub-point streaming ---------------------
    // Tile evaluation lambda 0x4752FC2 (ParallelFor from UpdateStreamingState):
    // every location after the first is a "sub point": ignored when closer
    // than s.SubPointLevelStreamingCullingDistance (60000, @0x6F7B12C) to the
    // primary, otherwise its streaming distance is multiplied by
    // s.SubPointLevelStreamingDistanceScale (0.5, @0x6F7B128). Both are CVar
    // backed floats in .data (registered at 0xEE2B70 / 0xEE2CC0, names XOR
    // obfuscated). Related ints: s.EnableLevelStreamingSubPoint @0x778ADC8,
    // s.HideWCLODLevelPriority @0x778ADC0 (help "Disable : 0 Low : 500, High : 600").
    // World composition layout used by the lambda: Tiles TArray at comp+0x48
    // (stride 0xE0: +0x2C/+0x30 int position, +0x38/+0x3C bounds min,
    // +0x44/+0x48 bounds max, +0x18/+0xA0 LOD counts, +0xAC priority, +0xD8
    // last evaluation result), TilesStreaming TArray at comp+0x168
    // (ULevelStreaming*: +0xC0 LevelLODIndex, +0xD0 bit0 bShouldBeLoaded /
    // bit4 bShouldBeVisible), min change interval (double) at comp+0x178.
    // ---- v58e: level actor lists (handoff "ACTOR-POINTERS-ARE-OBFUSCATED") --
    // UWorld::Levels TArray<ULevel*> at World+0x1F0/+0x1F8 (SetNewWorldOrigin
    // walks it); ULevel::Actors at Level+0x210/+0x218, but each slot is
    // obfuscated:  v=~raw; rol 1; xor 0x6B4D7C809C6BBCDF; low32 = ~low32
    // (upper 32 cleared); add 0x185100513866A525; ror 6  -> AActor*.
    // The matching encode sits at base+0x3F80FD5 (adds the two's complement).
    constexpr uintptr_t UWorld_Levels       = 0x1F0;
    constexpr uintptr_t ULevel_Actors       = 0x210;
    constexpr uint64_t  ActorPtr_XorKey     = 0x6B4D7C809C6BBCDFULL;
    constexpr uint64_t  ActorPtr_AddKey     = 0x185100513866A525ULL;
    constexpr uintptr_t CVar_SubPointDistanceScale = 0x06F7B128;
    constexpr uintptr_t CVar_SubPointCullDistance  = 0x06F7B12C;
    constexpr uintptr_t CVar_EnableSubPoint        = 0x0778ADC8;
    constexpr uintptr_t CVar_HideWCLODLevelPriority = 0x0778ADC0;

    // ---- v48: stop dropping references into streaming sublevels --------
    // UPackageMapClient::SerializeObject, the branch that decides whether an
    // object reference may be sent to a client. Found via its warning string
    // "Using None instead of replicated reference to %s because the level it's
    // in has not been made visible" (RVA 0x606DD80, single lea at 0x43FEF81,
    // function 0x43FEC70-0x43FF262). The decision chain ends with:
    //   043FEF3F  cmp rax,[rcx+0x50]        ; Level == World->PersistentLevel?
    //   043FEF43  je  0x43FEFB8             ; persistent -> send normally
    //   043FEF45  test byte [rax+0x20C],0x10; per-level "client has it" flag
    //   043FEF4C  jne 0x43FEFB8             ; set -> send normally
    //   ...                                  ; else warn and send None
    // (the +0x50 confirms rcx is a UWorld: PersistentLevel is at +0x50)
    //
    // WHY: the host log carries 1292 of these warnings in a single round --
    // 1004x SM-Luxury_Floor_02 alone, plus house floors, tennis ground, stairs
    // and door frames. Those are exactly the components a character STANDS on.
    // UE4 replicates a character's position relative to its MovementBase, so
    // when the base reference is dropped and replaced by None, the receiving
    // side applies the relative offset against nothing -- which is precisely
    // "I am suddenly inside a building or under the map while walking
    // normally". The client does have those sublevels streamed in locally; the
    // server just does not believe it does.
    //
    // Patch: make the jump at 0x43FEF4C unconditional (75 6A -> EB 6A), so the
    // real reference is always sent. If the client genuinely lacks the level,
    // the reference simply stays unmapped on its side -- which is no worse than
    // the None it gets today, and UE4 resolves it once the level streams in.
    //
    // Switch off without rebuilding:  -nolevelvis
    constexpr uintptr_t PackageMap_LevelVisibleJne = 0x043FEF4C;

    // v142: NMT_Hello with an EncryptionToken (UWorld::NotifyControlMessage).
    //
    // The lobby's START GAME path (match_success from the backend) only lets
    // the client travel when BOTH Key and Token are set -- client log 15.09.:
    //   "encrypt Key is empty!!. Matching succeeded, but can't access the game."
    //   "encrypt Token is empty!!. Matching succeeded, but can't access the game."
    // The client then sends the token in NMT_Hello. On the host:
    //   473A92D  cmp dword [rbp-0x78], 1     ; EncryptionToken.Num() <= 1 ?
    //   473A931  jg  473A940                 ; token present -> delegate path
    //   473A933  mov rcx, rsi                ; connection
    //   473A936  call 43D27E0                ; UNetConnection::SendChallengeControlMessage()
    //   473A93B  jmp 473AB73
    //   473A940  ... FNetDelegates::OnReceivedNetworkEncryptionToken -> the
    //            GameInstance, whose default answers Failure
    //            ("ReceivedNetworkEncryptionToken not implemented", RVA 0x6014B20)
    //            or, unbound, "No delegate available ... disconnecting."
    // The real DS module (BravoHotelServer) that answered the token is not in
    // the client build.
    //
    // Patch: NOP the jg (7F 0D -> 90 90), so a token takes the same path as no
    // token -- the path every manual "open ip:port" join already uses.
    // Why the client is fine with that: it only switches encryption on when
    // the server sends NMT_EncryptionAck (21), which only the delegate path
    // does. UPendingNetGame::NotifyControlMessage (0x44AC330, jump table at
    // 0x44AD4AC) handles NMT_Challenge (case 0x44AC744) without any encryption
    // check and answers with NMT_Login. Both sides stay unencrypted.
    //
    // Switch off without rebuilding:  -noenctoken
    constexpr uintptr_t Hello_TokenCheck = 0x0473A92D;

    // v146: in-round gold payments on a listen server.
    // The Class Selection window (Direct 700 / Random 100 gold), and every other
    // gold or material purchase, goes through
    //   CurrencyPay  0x21EF0B0 (this, amount, actionCode)   and
    //   MaterialPay  0x21EFAD0 (this, amount, materialIdx, actionCode).
    // Both log "CommitRequest[CurrencyPay] ..." and hand the request to a
    // delegate the dedicated-server module binds (it POSTs /ds/api/currency-pay
    // and later delivers the result via 0x220D240 / 0x220D980). The client exe
    // has no such module, so the delegate is unbound ("wasn't bind function!!")
    // and the tail decides what to do on its own:
    //   21EFA69  call GetNetMode             ; 0x3F92730
    //   21EFA70  je   SUCCESS                ; NM_Standalone -> dev mode, pay OK
    //   21EFA77  jne  +4  (bl = "ok so far") ; else: only a ScenarioGameMode
    //   21EFA7D  ... IsA(AScenarioGameMode)  ;       gets the immediate result
    //   21EFA9B  SUCCESS: mov dl,1 ; call 0x220D240(this, true, amount, action)
    // On a listen server (NM_ListenServer, BattleRoyale gamemode) NOTHING
    // delivers the result -> the class change / purchase waits forever: the
    // player freezes (client log: "CreateSavedMove: Hit limit of 96 saved
    // moves"). Fix: make 21EFA77 (and 21F0457 for MaterialPay) jump straight
    // to SUCCESS, i.e. the unbound path behaves exactly like Standalone.
    //   75 04 -> 75 22   (jne +4 -> jne +0x22, both land on 'mov dl,1')
    // The lobby balance is not touched by this (no backend call) -- the in-round
    // deduction is done by 0x220D240 as in Standalone.
    // Switch off without rebuilding:  -nopay
    constexpr uintptr_t CurrencyPay_Jne = 0x021EFA77;
    constexpr uintptr_t MaterialPay_Jne = 0x021F0457;

    // v147: gold looted in the round -> account ("[gg]").
    // The round's gold lives on the PlayerState: PS = [PC+0x390],
    // gold = [[PS+0x5E0]+0xE0]. Every change on the host goes through
    //   SetGold(PS, value)  0x222F200   (InitNewPlayer's AccountGold, loot
    //                                    pickups, purchases -- all of them)
    //   GetGold(PS)         0x2228420   = [[PS+0x5E0]+0xE0]
    // Purchases arrive via the pay result 0x220D240(PC, ok, amount, action),
    // which SetGold(GetGold()-amount)s. Retail settled the round on the DS at
    // the end; the host never logs a CurrencyGain, so we do the bookkeeping:
    // start = gold right after InitNewPlayer, end = gold at Logout,
    // looted = end - start + paid  (paid was already relayed as CurrencyPay).
    // GM_Logout is the ABattleRoyaleGameMode override that raises
    // TriggerEvent[PlayerDisconnected] (this=GameMode, rdx=Controller).
    constexpr uintptr_t PS_SetGold    = 0x0222F200;
    constexpr uintptr_t PC_PayResult  = 0x0220D240;
    constexpr uintptr_t GM_Logout     = 0x01CB9110;
    // v149: the round's gold ledger. OnPayResult (0x220D240, action 5) does
    //   r9 = [PS+0x6E8]; entry = { int32 value = -amount; uint8 type = 10 (ChangeDeck) };
    //   call 0x21659D0(r9, &entry)
    // and 0x21659D0 keeps ONE row per type in a TArray<{int32 value; uint8 type}>
    // at ledger+0xF0 (Data) / +0xF8 (Num) / +0xFC (Max): same type -> value is
    // added, else appended. Those rows are exactly what the client prints as
    // "MatchEndResult / ChangeDeck:-200 / AcquireCoin:20 / RandomGold:17"
    // (ENormalType: 6 DropCoin, 7 AcquireCoin, 10 ChangeDeck, 13 RandomGold,
    // 14 RandomRankGold). So the settlement reads the ledger straight off the
    // PlayerState at Logout -- no RPC, no ProcessEvent hook needed.
    constexpr uintptr_t Ledger_AddEntry = 0x021659D0;
    // v152: ABattleRoyaleGameMode::InitTableSetting(int SelectBlueZoneIndex)
    // (vtable slot +0xA40, base 0x59E7E20). Picks the play zone row from
    // TBL-PlayZone_OrbIsland: rows of the map (non-dev) are collected, and
    //   idx >= 0 and a row with ID == 520100000 + idx exists -> that row,
    //   otherwise idx >= 0                                   -> candidates[idx],
    //   otherwise (idx == -1, what this build always passes) -> rand() over ALL
    // 156 rows -- 16/24/40/64/80/100-player zones alike. Retail's dedicated
    // server got the index from matchmaking; we get a 16-player zone (550 m,
    // 60 s delay) one round and an 80-player one the next. v152 supplies the
    // index itself (see g_zonePlayers).
    constexpr uintptr_t GM_InitTableSetting = 0x01CB7D80;
    // v154: AActor::SetNetDormancy(ENetDormancy) -- the ONLY writer of +0x20D at
    // runtime besides the DORM_Initial constructors. Prologue 15 B:
    //   48 89 5C 24 18 | 56 | 48 83 EC 30 | 8B F2 | 48 8B D9
    // Body: NetDriverName check, GetNetMode()==Client -> return, destroyed ->
    // return, FindNamedNetDriver(World, NetDriverName) -> null -> return,
    // write +0x20D, driver->NotifyActorDormancyChange(actor, old) (0x43F1710),
    // and for Awake/Never: World->AddNetworkActor + FlushActorDormancy.
    constexpr uintptr_t AActor_SetNetDormancy = 0x03FA09C0;
    // v162, client measurement mode
    constexpr uintptr_t Weapon_CanReload    = 0x01EF2F80;   // ABravoHotelRangedWeapon::CanReload
    constexpr int       PlayZone_IdBase     = 520100000;   // 0x1F0018A0 in the lambda

    // v144: carry the joining player's gold/level/outfit into the round.
    // ABattleRoyaleGameMode::InitNewPlayer -- the ROUND gamemode's override
    // (BP-BattleRoyaleGM_C). It reads the account fields off the connection's
    // stored options and applies them (host log confirmed this is the one that
    // runs for a network join; v143 hooked 0x1D04E40, the *login* gamemode's,
    // which never fires on a join).
    //   1CC3AD4  mov    rsi,[obj+0x1418]    ; Options.Data (rdx = obj)
    //   1CC3ACD  movsxd r12,[obj+0x1420]    ; Options.Num
    //   1CC3DCD  call   0x424C550          ; GetIntOption(Options,"AccountGold",0)
    //   1CC3DE9  call   0x222F200          ; applies gold; level -> [obj+0x5D8]+0x278
    // A network join carries none of it (retail got it from the DS via the
    // token). We inject the options here before the original reads them.
    constexpr uintptr_t GM_InitNewPlayer = 0x001CC38E0;
    // FMemory thunks -- so the FString buffer we hand the game is freed with the
    // same allocator it uses (0x16FC4D0 = Malloc(rcx=size,align0->0x26278E0),
    // 0x16FC4F0 = Realloc(rcx=ptr,rdx=size,align0->0x2629940)).
    constexpr uintptr_t FMemory_Realloc    = 0x0016FC4F0;
}
namespace PSO {
    // v147: PlayerController -> PlayerState -> wallet -> gold (see RVA::PS_SetGold).
    constexpr uintptr_t PC_PlayerState = 0x390;
    constexpr uintptr_t PS_Wallet      = 0x5E0;
    constexpr uintptr_t Wallet_Gold    = 0xE0;
    constexpr uintptr_t PS_Ledger      = 0x6E8;   // v149, see RVA::Ledger_AddEntry
    constexpr uintptr_t Ledger_Rows    = 0xF0;    // TArray<{int32 value; uint8 type}>
}
namespace OPT {
    // Where the join options FString sits on the object InitNewPlayer receives.
    constexpr uintptr_t Options_Data = 0x1418;   // wchar_t*
    constexpr uintptr_t Options_Num  = 0x1420;   // int32 (incl. NUL)
    constexpr uintptr_t Options_Max  = 0x1424;   // int32
}
namespace OFF {
    constexpr uintptr_t UWorld_NetDriver      = 0x058;  // SDK
    // UWorld::DemoNetDriver -- aus der Netzmodus-Faltung an base+0x4729C65
    // abgelesen: "mov rcx,[rcx+0x130] / test rcx,rcx / mov rax,[rcx] /
    // call [rax+0x278]".  Der zweite Zweig ist NICHT wegoptimiert.
    constexpr uintptr_t UWorld_DemoNetDriver  = 0x130;
    // VTable-Slot, den dieser Zweig aufruft -- der Netzmodus des Demo-Treibers.
    constexpr uintptr_t NetDriver_GetNetMode_VT = 0x278;   // Index 79
    constexpr uintptr_t UNetDriver_World      = 0x148;
    constexpr uintptr_t UNetDriver_Name       = 0x198;  // FName, 12 Byte
    constexpr uintptr_t UEngine_NetDriverDefs = 0xD48;  // Data +0xD48, Num +0xD50
    constexpr uintptr_t Ctx_World             = 0x280;  // ThisCurrentWorld (0x1A8 = PendingNetGame!)
    constexpr uintptr_t Ctx_ActiveNetDrivers  = 0x228;  // Data +0x228, Num +0x230
    constexpr uintptr_t InitListen_VT         = 0x298;  // Index 83; 0x288 waere InitBase!
    constexpr size_t    NamedNetDriverStride  = 0x10;   // {UNetDriver*, FNetDriverDefinition*}
    // FLevelCollection-Array der World (aus SetActiveLevelCollection @0x4743C90):
    //   Data +0x200, Num +0x208, Elementgroesse 0x88
    //   im Element: +0x08 GameState, +0x10 NetDriver, +0x18 DemoNetDriver, +0x20 PersistentLevel
    constexpr uintptr_t UWorld_LevelCollections   = 0x200;
    constexpr size_t    LevelCollectionStride     = 0x88;
    constexpr uintptr_t LevelCollection_NetDriver = 0x010;
    // aus UWorld::SetActiveLevelCollection (base+0x4743CEE) abgelesen
    constexpr uintptr_t LevelCollection_DemoNetDriver = 0x018;
    constexpr size_t    TickFlush_VT = 98;   // Index in der Treiber-VTable
    // UFunction::FunctionFlags -- verifiziert an UNetDriver::ProcessRemoteFunction
    // base+0x43F38E9: "test dword ptr [rdi+0xC8], 0x4000" (FUNC_NetMulticast).
    constexpr uintptr_t UFunction_FunctionFlags = 0x0C8;
    constexpr uintptr_t UStruct_SuperStruct     = 0x048;
    // aus der Schleife in TickFlush (0x4401534: cmp [rsi+0xA0],0 / [rsi+0x98])
    // v58g, read out of UReplicationGraph::ServerReplicateActors / FNetViewer
    constexpr uintptr_t UNetConn_PlayerController = 0x038;
    constexpr uintptr_t UNetConn_ViewTarget       = 0x110;
    constexpr uintptr_t UNetConn_OwningActor      = 0x118;
    constexpr uintptr_t UNetConn_State            = 0x1BC;   // USOCK_Closed == 1
    constexpr uintptr_t LevelStreaming_LODIndex   = 0x0C0;
    constexpr uintptr_t LevelStreaming_CurState   = 0x0C8;
    constexpr uintptr_t LevelStreaming_TgtState   = 0x0C9;
    constexpr uintptr_t LevelStreaming_Flags      = 0x0D0;
    constexpr uintptr_t LevelStreaming_LoadedLvl  = 0x140;
    constexpr uintptr_t FNetViewer_ViewTarget     = 0x010;
    constexpr uintptr_t FNetViewer_ViewLocation   = 0x018;
    constexpr uintptr_t UNetDriver_ClientConnData = 0x098;
    constexpr uintptr_t UNetDriver_ClientConnNum  = 0x0A0;
    // Aus UNetDriver::InitBase (0x43E9CE0) und SetReplicationDriver (0x43FFA60):
    //   043E9EA8  mov rbx,[rdi+0x180]   ReplicationDriverClass  (null -> Fehlerzeile)
    //   043FFA6E  mov rcx,[rcx+0x708]   ReplicationDriver
    constexpr uintptr_t UNetDriver_RepDriverClass = 0x180;
    constexpr uintptr_t UNetDriver_RepDriver      = 0x708;
    // UReplicationDriver-VTable, Slot fuer ServerReplicateActors(float).
    // Belegt durch die Instruktion bei 0x13C6A6D: "movaps xmm6, xmm1" --
    // xmm1 ist bei __fastcall das zweite Argument, und nur diese eine Funktion
    // im Bereich 0x250-0x320 nimmt einen float. Nachbarn passen ebenfalls:
    // +0x2F8 = PostTickDispatch (wird von UNetDriver::PostTickDispatch gerufen),
    // +0x268 = SetRepDriverWorld (wird von UNetDriver::SetWorld gerufen).
    constexpr size_t    RepDriver_ServerReplicateActors_VT = 0x2F0;
    // (Der Pawn-Weg ueber SDK-Offsets ist mit v24 raus: AController::Pawn 0x320
    //  war falsch und hat den Client abstuerzen lassen. Der SDK-Dump ist fuer
    //  AController unbrauchbar.)

    // ---- v42: offsets read straight out of the engine code above ----------
    // AActor::RemoteRole   -- SetAutonomousProxy: "mov byte [rcx+0x191],dl"
    // AActor::Role         -- SetReplicates:      "cmp byte [rcx+0x20F],3" (ROLE_Authority)
    // AActor::bReplicates  -- SetAutonomousProxy: "test byte [rcx+0x289],1"
    // APawn::Controller    -- PossessedBy:        "mov [rbx+0x358],rsi"
    // UObject::ClassPrivate-- PossessedBy:        "mov rdx,[rdi+0x20]" (shifted layout!)
    // UStruct base chain   -- PossessedBy:        "+0x38 array / +0x40 NumBasesMinusOne"
    constexpr uintptr_t AActor_RemoteRole   = 0x191;
    constexpr uintptr_t AActor_Role         = 0x20F;
    constexpr uintptr_t AActor_bReplicates  = 0x289;   // bit 0
    constexpr uintptr_t APawn_Controller    = 0x358;
    constexpr uintptr_t UObject_Class       = 0x020;
    // UWorld::GameState. Taken from the SDK's UWorld layout, which is
    // trustworthy for THIS class because three of its entries are ones we
    // verified ourselves in the binary: PersistentLevel +0x50, NetDriver
    // +0x58 and DemoNetDriver +0x130 all match.
    constexpr uintptr_t UWorld_GameState    = 0x1D8;
    // AActor::ForceNetUpdate, VTable slot. Verified inside APawn::PossessedBy:
    //   044AE73D  mov rax,[rbx] / mov [rbx+0x358],rsi   ; Controller = New
    //   044AE747  call qword ptr [rax+0x638]            ; ForceNetUpdate()
    // which matches UE4's PossessedBy exactly (SetOwner, Controller,
    // ForceNetUpdate). ForceNetUpdate calls FlushNetDormancy() internally when
    // the actor is dormant, so it both wakes and re-replicates in one call.
    constexpr uintptr_t AActor_ForceNetUpdate_VT = 0x638;
    // AActor::NetCullDistanceSquared. Located at runtime by the v49 measurement:
    // the live BP_BlueZone_C object contained the engine default 225000000.0f
    // (= 15000^2) at EXACTLY ONE offset in the whole 0x100..0x1200 window. The
    // same dump confirmed our other offsets into that object line up -- the
    // flag byte printed at 0x289 came back 0x01, i.e. bReplicates, as expected.
    constexpr uintptr_t AActor_NetCullDistanceSquared = 0x2D4;
    constexpr uintptr_t AActor_NetDormancy  = 0x20D;   // 1 = DORM_Awake (v58g)
    // AActor::Owner, the chain AActor::IsNetRelevantFor walks (v58t).
    constexpr uintptr_t AActor_Owner        = 0x2B0;
    // AController::PlayerState, read out of APawn::PossessedBy 0x44AE710:
    //     044AE754  mov rcx,[rax+0x390]   ; Controller->PlayerState
    constexpr uintptr_t AController_PlayerState = 0x390;
    constexpr uintptr_t UStruct_BaseChain   = 0x038;
    constexpr uintptr_t UStruct_NumBasesM1  = 0x040;
    constexpr uintptr_t AActor_GetWorld_VT  = 0x150;   // "call [rax+0x150]" in GetNetMode
    // World Composition origin, read off the UWorld::Tick call site (see the
    // UWorld_SetNewWorldOrigin comment above).
    constexpr uintptr_t UWorld_OriginLocation          = 0x5E0;  // FIntVector
    constexpr uintptr_t UWorld_RequestedOriginLocation = 0x5EC;  // FIntVector
    // v55 streaming, all read off the game's own code:
    //   UWorldComposition::GetWorld() = Cast<UWorld>(Outer), Outer at +0x28
    //   0x45031A0: ViewTarget->[+0x158] (RootComponent) ->[+0x110] = the
    //   FTransform translation of the component (ComponentToWorld at +0x100)
    //   FUObjectItem is 40 bytes here, Object at +0, Flags at +0x20 (2306
    //   sites read [item+0x20]; 0x40000000 is set/cleared as RootSet).
    constexpr uintptr_t UWorldComposition_World = 0x028;
    constexpr uintptr_t AActor_RootComponent    = 0x158;
    constexpr uintptr_t USceneComp_Translation  = 0x110;
    constexpr uintptr_t UObject_InternalIndex   = 0x00C;
    constexpr uintptr_t UObjectItem_Size        = 40;
    constexpr uintptr_t UObjectItem_Flags       = 0x020;
    // v58, all read off the binary:
    //   AActor::bAlwaysRelevant       bit 0 of +0x1CC  (AActor::IsNetRelevantFor
    //       0x3F92D10 tests it first; UBasicReplicationGraph::InitGlobalActorClass-
    //       Settings 0x13B54C0 tests +0x1CC and +0x214 and zeroes the class cull
    //       distance when either is set)
    //   AActor::bOnlyRelevantToOwner  bit 0 of +0x214
    //   AActor::Owner                 +0x2B0  (IsOwnedBy chain in IsNetRelevantFor)
    //   UClass::ClassDefaultObject    +0x138  (InitGlobalActorClassSettings:
    //       "mov rdi,[rsi+0x138] / test rdi,rdi / jne", else GetDefaultObject vt+0x398)
    //   UObject::NamePrivate          +0x10, 12 bytes (UNetDriver::RemoveNetworkActor
    //       0x43FA590 copies actor+0x10..0x1B as the FName for RenamedStartupActors)
    constexpr uintptr_t AActor_bAlwaysRelevant      = 0x1CC;   // bit 0
    constexpr uintptr_t AActor_bOnlyRelevantToOwner = 0x214;   // bit 0
    constexpr uintptr_t UClass_DefaultObject        = 0x138;
    constexpr uintptr_t UObject_Name                = 0x010;   // FNameRaw, 12 bytes
    enum : int { NM_Standalone = 0, NM_DedicatedServer = 1, NM_ListenServer = 2, NM_Client = 3 };
    enum : uint8_t { ROLE_None = 0, ROLE_SimulatedProxy = 1, ROLE_AutonomousProxy = 2, ROLE_Authority = 3 };
}

// ------------------------------------------------------------------- Logging
static FILE* g_log = nullptr; static int g_lines = 0;
// v88: the v87 round ran out of log before the player ever held a weapon --
// the loot/streaming diagnostics ([cn] 624, [pl] 235, [wh] 201, [ls] 98 ...)
// filled all 4000 lines during the drop. Two changes:
//   1. the overall limit goes up,
//   2. each diagnostic tag gets its own quota, so no single chatty instrument
//      can starve the ammo evidence. The ammo/weapon tags are exempt.
static const int MAX_LOG_LINES = 30000;
static const int TAG_QUOTA     = 400;      // per [xx] tag, diagnostics only

struct TagCount { char tag[3]; int n; };
static TagCount g_tagCount[64];
static int      g_tagUsed = 0;

// Tags that must never be throttled: everything about ammo, weapons and the
// RPC traffic we are actually hunting, plus errors and the banner.
static bool TagExempt(const char* t) {
    static const char* keep[] = { "cmd", "ms", "mag", "mp", "pe", "am", "ow", "wk", "rs", "gx", "cz", "sl", "cv", "ch", "bk", "gg", "gt", "an", "pill-data", "pill-consume", "loot-pool", "loot-repair", "pill", "dw", "dm", "dr", "pk", "ps", "gz", "jo", "mg", "zf", "ww", "gn", "gc", "ax", "ld", "cg", "zl", "zc", "zr", "tl", "zp", "mv", "cs", "cx", "zt", "lt", "zi", "wa", "wd", "rr", "sm", "wp2", "wx", "ar", "at", "gd", "et", "lo", "cp", "gg", "vt", "sc", "wz", "bx", "cx", "!", "+", "i", "e" };
    for (int i = 0; i < (int)(sizeof(keep) / sizeof(keep[0])); ++i)
        if (strcmp(t, keep[i]) == 0) return true;
    return false;
}

static void L(const char* f, ...) {
    if (!g_log) return;
    if (g_lines == MAX_LOG_LINES) { fputs("\n[!] Log-Limit erreicht.\n", g_log); fflush(g_log); g_lines++; return; }
    if (g_lines > MAX_LOG_LINES) return;

    // Per-tag quota. The format string starts with the literal tag in every
    // call site, so it can be read straight off 'f' without formatting first.
    if (f[0] == '[') {
        char tag[4] = { 0, 0, 0, 0 };
        int k = 0;
        while (k < 3 && f[1 + k] && f[1 + k] != ']') { tag[k] = f[1 + k]; ++k; }
        if (f[1 + k] == ']' && !TagExempt(tag)) {
            TagCount* slot = nullptr;
            for (int i = 0; i < g_tagUsed; ++i)
                if (strcmp(g_tagCount[i].tag, tag) == 0) { slot = &g_tagCount[i]; break; }
            if (!slot && g_tagUsed < 64 && k <= 2) {
                slot = &g_tagCount[g_tagUsed++];
                slot->tag[0] = tag[0]; slot->tag[1] = tag[1]; slot->tag[2] = 0;
                slot->n = 0;
            }
            if (slot) {
                if (slot->n > TAG_QUOTA) return;                 // silently dropped
                if (slot->n == TAG_QUOTA) {
                    ++slot->n;
                    fprintf(g_log, "[!] Tag [%s] hat sein Kontingent von %d Zeilen erreicht -- wird ab jetzt unterdrueckt.\n",
                            tag, TAG_QUOTA);
                    fflush(g_log); ++g_lines;
                    return;
                }
                ++slot->n;
            }
        }
    }

    va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a);
    fputc('\n', g_log); fflush(g_log); g_lines++;
}
static uintptr_t g_base = 0, g_end = 0;

static bool SafeCopy(const void* s, void* d, size_t n) {
    __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void*    SafePtr(const void* a) { void* v=nullptr; return SafeCopy(a,&v,8)? v : nullptr; }
static int32_t  SafeI32(const void* a) { int32_t v=0;     return SafeCopy(a,&v,4)? v : 0; }
static bool InModule(const void* p) { return (uintptr_t)p >= g_base && (uintptr_t)p < g_end; }
// v71: recognise a replication graph by its ServerReplicateActors slot instead
// of by the Basic graph's InitGlobalActorClassSettings. The old test rejected
// URealReplicationGraph and would have quietly disabled [gn], the cull repair
// and the zone's graph fixes the moment we switched class.
static bool IsKnownRepGraph(const void* vt) {
    if (!vt || (uintptr_t)vt < g_base || (uintptr_t)vt >= g_end) return false;
    void* fn = nullptr;
    __try { memcpy(&fn, (const uint8_t*)vt + 0x2F0, 8); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return fn == (void*)(g_base + 0x013C6A50) || fn == (void*)(g_base + 0x013C6C60);
}

// ------------------------------------------------------------- UE4-Typen
struct FString  { wchar_t* Data; int32_t Num; int32_t Max; };
struct FNameRaw { uint32_t Comparison, Display, Number; };   // 12 Byte
typedef bool  (__fastcall* tCreateNamedNetDriverW)(void* engine, void* world, FNameRaw* name, FNameRaw* def);
typedef void* (__fastcall* tGetWorldContext)(void* engine, void* world);
typedef bool  (__fastcall* tInitListen)(void* drv, void* notify, void* url, bool reuse, FString* err);
typedef void  (__fastcall* tSetWorld)(void* drv, void* world);

// ------------------------------------------------------- Patch-Hilfsmittel
static bool Poke(void* at, const void* src, size_t n) {
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(at, src, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}
static uint8_t* MakeCapture(uintptr_t target, const uint8_t* expect, size_t len) {
    if (memcmp((void*)target, expect, len) != 0) return nullptr;
    auto* st = (uint8_t*)VirtualAlloc(nullptr, 0x100, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!st) return nullptr;
    memset(st, 0xCC, 0x100);
    size_t k=0;
    st[k++]=0x48; st[k++]=0x89; st[k++]=0x0D;
    *(int32_t*)(st+k) = (int32_t)(0x80 - (k+4)); k+=4;         // mov [rip+d], rcx -> +0x80
    memcpy(st+k, (void*)target, len); k+=len;
    st[k++]=0xFF; st[k++]=0x25; *(uint32_t*)(st+k)=0; k+=4;
    uint64_t back = target + len; memcpy(st+k, &back, 8);
    *(void**)(st+0x80) = nullptr;
    uint8_t patch[32]; memset(patch, 0x90, sizeof(patch));
    patch[0]=0xFF; patch[1]=0x25; *(uint32_t*)(patch+2)=0;
    void* d = st; memcpy(patch+6, &d, 8);
    return Poke((void*)target, patch, len) ? st : nullptr;
}
static uint8_t* g_capEngine = nullptr;
static uintptr_t g_notifyOff = 0;

// Erzwingt echte UDP-Sockets statt Steam-P2P:
//   je 0x12B9EE4   (0F 84 D2 00 00 00)  ->  jmp 0x12B9EE4  (E9 D3 00 00 00 90)
static bool ForceIpSockets() {
    uintptr_t at = g_base + RVA::SteamPassthrough_JE;
    static const uint8_t expect[6] = { 0x0F, 0x84, 0xD2, 0x00, 0x00, 0x00 };
    static const uint8_t patch [6] = { 0xE9, 0xD3, 0x00, 0x00, 0x00, 0x90 };
    auto* p = (uint8_t*)at;
    L("[i] Steam-Passthrough-Sprung: %02X %02X %02X %02X %02X %02X",
      p[0],p[1],p[2],p[3],p[4],p[5]);
    if (memcmp(p, expect, 6) != 0) {
        if (memcmp(p, patch, 6) == 0) { L("[i] bereits gepatcht"); return true; }
        L("[!] unerwartete Bytes -> NICHT gepatcht (Steam-P2P bleibt aktiv!)");
        return false;
    }
    if (!Poke(p, patch, 6)) { L("[!] Patch fehlgeschlagen"); return false; }
    L("[+] SteamNetDriver auf Passthrough gezwungen -> echte UDP-Sockets");
    return true;
}

// v48: always send object references into streaming sublevels instead of
// replacing them with None (see the RVA comment for the full reasoning).
//   jne 0x43FEFB8  (75 6A)  ->  jmp 0x43FEFB8  (EB 6A)
static bool g_levelVis = true;   // -nolevelvis turns this off
static bool ForceLevelReferences() {
    uintptr_t at = g_base + RVA::PackageMap_LevelVisibleJne;
    static const uint8_t expect[2] = { 0x75, 0x6A };
    static const uint8_t patch [2] = { 0xEB, 0x6A };
    auto* p = (uint8_t*)at;
    L("[lv] Level-Sichtbarkeitssprung: %02X %02X", p[0], p[1]);
    if (memcmp(p, expect, 2) != 0) {
        if (memcmp(p, patch, 2) == 0) { L("[lv] bereits gepatcht"); return true; }
        L("[!] [lv] unerwartete Bytes -> NICHT gepatcht");
        return false;
    }
    if (!Poke(p, patch, 2)) { L("[!] [lv] Patch fehlgeschlagen"); return false; }
    L("[+] Referenzen in Streaming-Sublevels werden immer gesendet (kein 'None' mehr)");
    return true;
}

// v142: accept a join that carries an EncryptionToken (see RVA::Hello_TokenCheck).
//   cmp dword [rbp-0x78],1 / jg +0x0D  ->  cmp dword [rbp-0x78],1 / nop nop
static bool g_ignoreEncToken = true;   // -noenctoken turns this off
static bool IgnoreEncryptionToken() {
    uintptr_t at = g_base + RVA::Hello_TokenCheck;
    // cmp dword [rbp-0x78],1 | jg +0x0D | mov rcx,rsi | call ...
    static const uint8_t expect[10] = { 0x83, 0x7D, 0x88, 0x01, 0x7F, 0x0D, 0x48, 0x8B, 0xCE, 0xE8 };
    static const uint8_t patched[10] = { 0x83, 0x7D, 0x88, 0x01, 0x90, 0x90, 0x48, 0x8B, 0xCE, 0xE8 };
    auto* p = (uint8_t*)at;
    L("[et] Token-Pruefung in NMT_Hello: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
      p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9]);
    if (memcmp(p, expect, 10) != 0) {
        if (memcmp(p, patched, 10) == 0) { L("[et] bereits gepatcht"); return true; }
        L("[!] [et] unerwartete Bytes -> NICHT gepatcht (Joins aus der Lobby-Suche werden abgewiesen)");
        return false;
    }
    static const uint8_t nops[2] = { 0x90, 0x90 };
    if (!Poke(p + 4, nops, 2)) { L("[!] [et] Patch fehlgeschlagen"); return false; }
    L("[+] [et] EncryptionToken im NMT_Hello wird ignoriert -> Join aus der Lobby-Suche laeuft unverschluesselt wie 'open ip:port'");
    return true;
}

// v146: deliver the result of in-round gold/material payments at once (see
// RVA::CurrencyPay_Jne). Both sites:  test bl,bl / jne +4  ->  jne +0x22.
static bool g_payPatch = true;   // -nopay turns this off
static bool PatchPayResult(const char* name, uintptr_t rva) {
    auto* p = (uint8_t*)(g_base + rva);
    // test bl,bl | jne +4 | xor edx,edx | jmp +0x23 | mov rax,[reg]
    static const uint8_t expect[8]  = { 0x84, 0xDB, 0x75, 0x04, 0x33, 0xD2, 0xEB, 0x23 };
    static const uint8_t patched[8] = { 0x84, 0xDB, 0x75, 0x22, 0x33, 0xD2, 0xEB, 0x23 };
    uint8_t* at = p - 2;   // rva points at the jne; the test sits 2 bytes before
    L("[cp] %s: %02X %02X %02X %02X %02X %02X %02X %02X", name,
      at[0],at[1],at[2],at[3],at[4],at[5],at[6],at[7]);
    if (memcmp(at, expect, 8) != 0) {
        if (memcmp(at, patched, 8) == 0) { L("[cp] %s bereits gepatcht", name); return true; }
        L("[!] [cp] %s: unerwartete Bytes -> NICHT gepatcht (Klassenwahl/Kauf mit Gold bleibt haengen)", name);
        return false;
    }
    static const uint8_t rel = 0x22;
    if (!Poke(at + 3, &rel, 1)) { L("[!] [cp] %s: Patch fehlgeschlagen", name); return false; }
    L("[+] [cp] %s: Ergebnis wird sofort geliefert (wie Standalone)", name);
    return true;
}
static void PatchPayResults() {
    PatchPayResult("CurrencyPay", RVA::CurrencyPay_Jne);
    PatchPayResult("MaterialPay", RVA::MaterialPay_Jne);
}

// v143: carry gold / level / outfit into the round. -----------------------
//
// A network join's URL has no loadout (the vanilla client leaves it out; retail
// let the dedicated server fetch it via the encryption token, the path v142
// bypasses). But the host's InitNewPlayer reads AccountGold/AccountLevel off
// the connection options and the leading option as the outfit JSON -- exactly
// like dev Standalone mode. So we inject those options here, from a file the
// backend writes next to the DLL:
//
//   loadouts.txt   one line per player:   <UID>\t<join_options>
//   join_options = <pc_info json>?AccountGold=G?AccountLevel=L?CountryCode=CC
//                  (served by the backend at /ds/api/listen/loadout/<uid>)
//
// The options FString lives on the object InitNewPlayer receives (rdx) at
// +0x1418 (Data) / +0x1420 (Num, incl. NUL) / +0x1424 (Max). We prepend
// "<join_options>?" so the JSON becomes option 1 and the account fields are
// present; the game then applies them itself. Switch: -loadout (default OFF).
typedef void* (__fastcall* tInitNewPlayer)(void*, void*, void*, void*, void*, void*);
static tInitNewPlayer g_origInitNewPlayer = nullptr;
static bool g_loadout = false;                 // -loadout turns it on
static wchar_t g_loadoutPath[MAX_PATH] = {0};  // <dll folder>\loadouts.txt

// Case-insensitive search for "UID=" in a wide options string; copies the hex
// value (until '?' or end) into out as ASCII. Returns false if not found.
static bool ExtractUidFromOptions(const wchar_t* opts, int num, char* out, int outsz) {
    if (!opts || num <= 4 || outsz < 2) return false;
    for (int i = 0; i + 4 <= num; ++i) {
        wchar_t a = opts[i], b = opts[i+1], c = opts[i+2], d = opts[i+3];
        if ((a=='U'||a=='u') && (b=='I'||b=='i') && (c=='D'||c=='d') && d=='=') {
            int j = i + 4, k = 0;
            while (j < num && opts[j] && opts[j] != '?' && k < outsz - 1) {
                wchar_t ch = opts[j++];
                if (ch < 32 || ch > 126) return false;      // uid is plain hex
                out[k++] = (char)ch;
            }
            out[k] = 0;
            return k > 0;
        }
    }
    return false;
}

// Reads loadouts.txt, returns the join_options for uid into out (ASCII).
// The file is re-read on every join -- it is tiny and joins are rare.
static bool LookupLoadout(const char* uid, char* out, int outsz) {
    if (!g_loadoutPath[0] || !uid || !uid[0]) return false;
    FILE* f = _wfsopen(g_loadoutPath, L"r", _SH_DENYNO);
    if (!f) return false;
    static char line[9000];
    bool found = false;
    size_t ulen = strlen(uid);
    while (fgets(line, (int)sizeof(line), f)) {
        // line = "<uid>\t<options...>"
        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        if (strncmp(line, uid, ulen) == 0 && line[ulen] == 0) {
            char* val = tab + 1;
            // strip trailing newline / CR
            size_t vl = strlen(val);
            while (vl && (val[vl-1] == '\n' || val[vl-1] == '\r')) val[--vl] = 0;
            if (vl > 0 && (int)vl < outsz) { memcpy(out, val, vl + 1); found = true; }
            break;
        }
    }
    fclose(f);
    return found;
}

// Rewrite the object's options FString at +0x1418 to
//     "?" + <opts> + <original>
// reallocating with the game's own allocator so the game frees it correctly.
//
// The leading '?' is not cosmetic. The game's option extractor (0x424ED40,
// used by InitNewPlayer to find the outfit JSON) first checks
// Options.Left(1) == "?" and returns an EMPTY string otherwise -- v144 injected
// "{json}?..." with no leading '?', and the host log duly showed
// "JsonObjectStringToUStruct - Unable to parse json=[]". With the '?' in front
// it strips it and takes everything up to the next '?' = our JSON.
// The original already starts with '?' ("?UserName=..."); if it ever does not,
// a separator '?' is inserted.
static bool InjectOptions(void* obj, const char* asciiOpts) {
    if (!obj || !asciiOpts) return false;
    auto* pData = (wchar_t**)((uint8_t*)obj + OPT::Options_Data);
    auto* pNum  = (int32_t*)((uint8_t*)obj + OPT::Options_Num);
    auto* pMax  = (int32_t*)((uint8_t*)obj + OPT::Options_Max);
    wchar_t* origData; int origNum;
    if (!SafeCopy(pData, &origData, 8)) return false;
    if (!SafeCopy(pNum, &origNum, 4)) return false;
    if (!origData || origNum < 1 || origNum > 8192) return false;
    static wchar_t orig[8200];
    if (origNum > (int)(sizeof(orig)/sizeof(orig[0]))) return false;
    if (!SafeCopy(origData, orig, (size_t)origNum * 2)) return false;
    orig[origNum - 1] = 0;

    int prefixLen = (int)strlen(asciiOpts);
    if (prefixLen < 1 || prefixLen > 8000) return false;
    bool needSep = (orig[0] != L'?');                       // original lacks its own '?'
    // layout: '?' + opts + [ '?' ] + original(incl NUL)
    int head = 1 + prefixLen + (needSep ? 1 : 0);
    int newNum = head + origNum;

    typedef void* (__fastcall* tRealloc)(void*, size_t, uint32_t);
    auto Realloc = (tRealloc)(g_base + RVA::FMemory_Realloc);
    wchar_t* nd = nullptr;
    __try { nd = (wchar_t*)Realloc(origData, (size_t)newNum * 2, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!nd) return false;

    // nd holds the original (Realloc preserved it). Slide it back, write head.
    memmove(nd + head, nd, (size_t)origNum * 2);
    int k = 0;
    nd[k++] = L'?';
    for (int i = 0; i < prefixLen; ++i) nd[k++] = (wchar_t)(uint8_t)asciiOpts[i];
    if (needSep) nd[k++] = L'?';
    nd[newNum - 1] = 0;

    *pData = nd; *pNum = newNum; *pMax = newNum;
    return true;
}

// v151: force the round's view type (FPP / TPP). --------------------------
// GameViewType lives on the game state at +0x4B0 (uint8 EGameViewType:
// 0 Unknown, 1 TPP, 2 FPP; MatchingType is the next byte, +0x4B1 -- verified
// through 0x1D076A0, which turns +0x4B1 into "solo"/"duo"/"trio"/"squad", and
// 0x2136F10, which copies +0x4B0/+0x4B1 into the telemetry base as
// gameviewtype/gamemode). It is replicated (OnRep_GameViewType on the
// clients). The gamemode keeps its own copy at +0x9E8, which the StartGame
// event logs ("GameViewType:0" on every host so far). No native code writes
// either -- retail's dedicated server got the value from the backend through
// GameSettingString, whose field names this build hides (v150's
// {"GameViewType":2} parsed without complaint and changed nothing). So the
// DLL writes both fields itself, every net tick, for as long as they differ:
//   -fpp  -> 2      -tpp -> 1      (neither: untouched, as before)
static int g_forceView = 0;
static int g_vtWrites = 0;
extern void* g_world;   // defined with the world capture, further down
static bool SafeWriteU8(void* at, uint8_t v) {
    __try { *(volatile uint8_t*)at = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void ViewTypeTick() {
    if (!g_forceView || !g_world) return;
    const uint8_t want = (uint8_t)g_forceView;
    void* gs = SafePtr((uint8_t*)g_world + OFF::UWorld_GameState);
    void* gm = SafePtr((uint8_t*)g_world + RVA::World_AuthGameMode);
    if (gs) {
        uint8_t cur = 0xFF; SafeCopy((uint8_t*)gs + 0x4B0, &cur, 1);
        if (cur != want && cur != 0xFF) {
            bool ok = SafeWriteU8((uint8_t*)gs + 0x4B0, want);
            if (++g_vtWrites <= 6) L("[vt] GameState %p GameViewType %d -> %d %s", gs, cur, want, ok ? "" : "(Schreiben fehlgeschlagen)");
        }
    }
    if (gm) {
        uint8_t cur = 0xFF; SafeCopy((uint8_t*)gm + 0x9E8, &cur, 1);
        if (cur != want && cur != 0xFF) {
            bool ok = SafeWriteU8((uint8_t*)gm + 0x9E8, want);
            if (++g_vtWrites <= 6) L("[vt] GameMode %p GameViewType %d -> %d %s", gm, cur, want, ok ? "" : "(Schreiben fehlgeschlagen)");
        }
    }
}

// v154: keep dynamic actors AWAKE -- the weapon / attachment / reload root cause.
// The [wd] watch of 16.09. shows it for every weapon of the remote player:
//     +0x20C 03020001 -> 03020201   3.5 s after K2_OnEquip
// i.e. the game's own Blueprint calls SetNetDormancy(DORM_DormantAll) on a
// weapon a few seconds after it is equipped (and on PlayerStates, drop boxes,
// fences ... -- see the [dm] lines). From then on every change -- a sight
// mounted (AttachmentIndices in the WeaponAttachmentComponent, +0x1E00 of the
// weapon, NOT +0x190 of the weapon as v130-v135 assumed), a reload (Magazine
// +0xDD0), a bolt cycle -- only reaches the client if the game's
// FlushNetDormancy wakes the actor. Under the standalone mask that call is a
// no-op, and the v62 crash guard refuses to open the window for exactly these
// DormantAll actors. So the client keeps the state it received before the
// actor fell asleep, until the next weapon switch makes the game set the
// dormancy back and a full update goes out. That is precisely the reported
// behaviour: attachments show only when mounted while the weapon is in hand
// (still awake in its first 3.5 s), the magazine display freezes until a
// switch, the weapon on the back never updates.
// v154 refuses the request at its source: AActor::SetNetDormancy with
// DormantAll/DormantPartial returns without doing anything (no write, no
// NotifyActorDormancyChange, so the graph never learns of a dormancy it would
// have to be woken from). Level-placed actors (bNetStartup) keep the game's
// dormancy unless -dormlevel is given; v96 already wakes them for the graph.
// Actors that arrive already dormant (class default) are set to DORM_Awake in
// AddNetworkActor, same single-byte write as v96.
// Switches: -nodormblock (v153 behaviour), -dormlevel (block for level actors too).
static void* MakeTrampoline(uintptr_t target, const uint8_t* expect, size_t len);
static bool InstallJmp(const char* what, uintptr_t rva, const void* dest, const uint8_t* expect);
static bool ClassNameOf(void* obj, wchar_t* out, int cap);
static bool g_dormBlock      = true;
static bool g_dormBlockLevel = false;
static bool g_dormAllowWeapons = false;   // v156: -dormallowweapons (A/B: weapons sleep as before v154)
static volatile LONG g_dormBlocked = 0, g_dormBlockedStartup = 0, g_dormBlockedAdd = 0;
typedef void (__fastcall* tSetNetDormancy)(void* actor, uint8_t dorm);
static tSetNetDormancy g_origSetNetDormancy = nullptr;
static void __fastcall MySetNetDormancy(void* actor, uint8_t dorm) {
    if (g_dormBlock && actor && (dorm == 2 || dorm == 3)) {
        uint8_t flags = 0;
        const bool startup = SafeCopy((uint8_t*)actor + 0x2D0, &flags, 1) && (flags & 2);
        bool skip = false;
        if (g_dormAllowWeapons) { wchar_t wn[128] = L""; skip = ClassNameOf(actor, wn, 128) && wcsstr(wn, L"Weapon") != nullptr; }
        if ((!startup || g_dormBlockLevel) && !skip) {
            LONG n = InterlockedIncrement(&g_dormBlocked);
            if (n <= 40) {
                wchar_t cn[128] = L"?"; ClassNameOf(actor, cn, 128);
                uint8_t cur = 0xFF; SafeCopy((uint8_t*)actor + 0x20D, &cur, 1);
                L("[dz] SetNetDormancy(%d) fuer %ls %p unterdrueckt (war %d, %s) -- Actor bleibt wach, seine Aenderungen erreichen den Client weiter -- #%ld",
                  dorm, cn, actor, cur, startup ? "im Level platziert" : "dynamisch", n);
            }
            return;
        }
        InterlockedIncrement(&g_dormBlockedStartup);
    }
    if (g_origSetNetDormancy) g_origSetNetDormancy(actor, dorm);
}
static void InstallDormBlock() {
    static const uint8_t prolog[15] = { 0x48,0x89,0x5C,0x24,0x18, 0x56, 0x48,0x83,0xEC,0x30, 0x8B,0xF2, 0x48,0x8B,0xD9 };
    g_origSetNetDormancy = (tSetNetDormancy)MakeTrampoline(g_base + RVA::AActor_SetNetDormancy, prolog, sizeof(prolog));
    if (!g_origSetNetDormancy) { L("[!] [dz] SetNetDormancy-Trampolin FEHLGESCHLAGEN -- Dormancy-Sperre AUS"); g_dormBlock = false; return; }
    if (InstallJmp("AActor::SetNetDormancy", RVA::AActor_SetNetDormancy, (void*)&MySetNetDormancy, prolog))
        L("[+] [dz] Dormancy-Sperre aktiv: DormantAll/DormantPartial wird fuer %s abgewiesen", g_dormBlockLevel ? "ALLE Actors" : "dynamische Actors (Level-Actors wie bisher)");
    else { L("[!] [dz] SetNetDormancy-Hook fehlgeschlagen -- Dormancy-Sperre AUS"); g_origSetNetDormancy = nullptr; g_dormBlock = false; }
}

// v152: choose the play zone by player count. -------------------------------
// See RVA::GM_InitTableSetting. The row IDs per MaxPlayerCount come straight
// out of TBL-PlayZone_OrbIsland.json (ID - 520100000):
//   16 -> cqr_a (550 m first circle, 60 s delay, 6 phases)
//   24 -> cqr_b      40 -> cqr_d      64 -> cqr_e (1.9 km, 90 s, 9 phases)
//   80 -> cqr_c     100 -> cqr_f (one row)
// Default 64: the host fills the plane with ~50 AI, so the 64-player timeline
// fits. Switches:  -zoneplayers=N  (16/24/40/64/80/100; 0 = leave the game's
// random pick alone)   -zoneid=N  (one fixed row, N = ID or ID-520100000)
static void* MakeTrampoline(uintptr_t target, const uint8_t* expect, size_t len);
static bool InstallJmp(const char* what, uintptr_t rva, const void* dest, const uint8_t* expect);
// v153: scale the zone with the passengers. While the match has not started
// (GameState+0x9F2 < 3) the DLL counts the PlayerStates on the host every
// ~2 s (humans + host + AI) and, whenever that count crosses a tier, re-runs
// InitTableSetting with a row from the tier's table -- the same call the
// game makes at map load, which re-applies the row to the zone actor and
// re-notifies the clients (OnRepSelectedPlayZoneName). Tiers are
// "row:maxPassengers" pairs, default retail-like:
//   -zonetiers=16:16,24:24,40:40,64:64,80:999
// A fixed -zoneplayers=N or -zoneid=N switches the scaling off (v152
// behaviour); -zoneplayers=0 leaves everything to the game.
static int g_zonePlayers = 0;      // 0 = scale by tiers (v153); N = fixed table (v152)
static int g_zoneId      = -1;
static bool g_zoneScale  = true;   // false once -zoneplayers=N / -zoneid=N is given
struct ZoneTier { int rows; int maxPassengers; };
static ZoneTier g_zoneTiers[8] = { {16,16},{24,24},{40,40},{64,64},{80,999} };
static int      g_zoneTierN    = 5;
static int      g_zoneTierCur  = -1;   // rows of the tier last applied
static int      g_zoneReruns   = 0;
static void ParseZoneTiers(const wchar_t* spec) {
    int n = 0;
    while (spec && *spec && n < 8) {
        int rows = _wtoi(spec); const wchar_t* c = wcschr(spec, L':'); if (!c) break;
        int mx = _wtoi(c + 1);
        if (rows > 0 && mx > 0) { g_zoneTiers[n].rows = rows; g_zoneTiers[n].maxPassengers = mx; ++n; }
        const wchar_t* k = wcschr(c, L','); if (!k) break; spec = k + 1;
    }
    if (n > 0) g_zoneTierN = n;
}
static int ZoneTierFor(int passengers) {   // -> rows (16/24/40/64/80/100)
    for (int i = 0; i < g_zoneTierN; ++i) if (passengers <= g_zoneTiers[i].maxPassengers) return g_zoneTiers[i].rows;
    return g_zoneTiers[g_zoneTierN - 1].rows;
}
static const int kZone16[]  = { 273,274,275,276,277,278,279,280,281,282,283,284,285,286,287,288,289,290,291,292,293,294,295,296,297,298,299,300,301,302,307,311,312,313 };
static const int kZone24[]  = { 243,244,245,246,247,248,249,250,251,252,253,254,255,256,257,258,259,260,261,262,263,264,265,266,267,268,269,270,271,272,310,331,332,333,334,335,336,337,338,339,340,341,351 };
static const int kZone40[]  = { 223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,305,309,324,325,326,327,328,329,330,419 };
static const int kZone64[]  = { 203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,304,308,323,414,417,418 };
static const int kZone80[]  = { 314,315,316,317,318,319,320,321,322,342,349,350,409,410,411,412,413,425,426,427,428,429 };
static const int kZone100[] = { 303 };
static bool ZoneList(int players, const int** list, int* n) {
    switch (players) {
    case 16:  *list = kZone16;  *n = (int)(sizeof(kZone16)/sizeof(int));  return true;
    case 24:  *list = kZone24;  *n = (int)(sizeof(kZone24)/sizeof(int));  return true;
    case 40:  *list = kZone40;  *n = (int)(sizeof(kZone40)/sizeof(int));  return true;
    case 64:  *list = kZone64;  *n = (int)(sizeof(kZone64)/sizeof(int));  return true;
    case 80:  *list = kZone80;  *n = (int)(sizeof(kZone80)/sizeof(int));  return true;
    case 100: *list = kZone100; *n = (int)(sizeof(kZone100)/sizeof(int)); return true;
    }
    return false;
}
static int ZonePickFrom(int players) {        // -> index for InitTableSetting, or -1
    const int* list = nullptr; int n = 0;
    if (!ZoneList(players, &list, &n) || n <= 0) return -1;
    static bool seeded = false;
    if (!seeded) { srand((unsigned)(time(nullptr) ^ GetCurrentProcessId())); seeded = true; }
    return list[rand() % n];
}
static int ZonePick() {                       // -> index for InitTableSetting, or -1
    if (g_zoneId >= 0) return g_zoneId >= RVA::PlayZone_IdBase ? g_zoneId - RVA::PlayZone_IdBase : g_zoneId;
    if (g_zoneScale) { g_zoneTierCur = ZoneTierFor(1); return ZonePickFrom(g_zoneTierCur); }   // map load: host alone
    const int* list = nullptr; int n = 0;
    if (!ZoneList(g_zonePlayers, &list, &n) || n <= 0) return -1;
    static bool seeded = false;
    if (!seeded) { srand((unsigned)(time(nullptr) ^ GetCurrentProcessId())); seeded = true; }
    return list[rand() % n];
}
typedef void* (__fastcall* tInitTableSetting)(void*, int, void*, void*, void*, void*);
static tInitTableSetting g_origInitTableSetting = nullptr;
static void* __fastcall MyInitTableSetting(void* gm, int idx, void* a3, void* a4, void* a5, void* a6) {
    int use = idx;
    if (idx < 0) {
        int pick = ZonePick();
        if (pick >= 0) {
            use = pick;
            L("[bz] Zonenwahl: Spiel wollte Index %d (Zufall ueber alle 156 Zeilen) -> v152 waehlt Zeile ID %d (%s)",
              idx, RVA::PlayZone_IdBase + pick,
              g_zoneId >= 0 ? "-zoneid" : g_zoneScale ? "Stufe fuer 1 Passagier, waechst mit den Beitritten" : "Spielerzahl-Tabelle");
        } else {
            L("[bz] Zonenwahl: Index %d bleibt (-zoneplayers=0 oder unbekannte Spielerzahl)", idx);
        }
    } else {
        L("[bz] Zonenwahl: Spiel gibt Index %d selbst vor -- unveraendert", idx);
    }
    return g_origInitTableSetting ? g_origInitTableSetting(gm, use, a3, a4, a5, a6) : nullptr;
}
static void InstallZonePick() {
    // 48 89 5C 24 18  mov [rsp+18],rbx | 55 56 57 | 41 54 41 55 41 56  -- 14 B, on a boundary
    static const uint8_t prolog[14] = { 0x48,0x89,0x5C,0x24,0x18, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55, 0x41,0x56 };
    g_origInitTableSetting = (tInitTableSetting)MakeTrampoline(g_base + RVA::GM_InitTableSetting, prolog, sizeof(prolog));
    if (!g_origInitTableSetting) { L("[!] [bz] InitTableSetting-Trampolin FEHLGESCHLAGEN -> Zone bleibt Zufall"); return; }
    if (InstallJmp("ABattleRoyaleGameMode::InitTableSetting", RVA::GM_InitTableSetting, (void*)&MyInitTableSetting, prolog))
        L("[+] [bz] Zonenwahl aktiv: %s", g_zoneId >= 0 ? "feste Zeile (-zoneid)" : "Zufall innerhalb der Spielerzahl-Tabelle");
    else { L("[!] [bz] InitTableSetting-Hook fehlgeschlagen -> Zone bleibt Zufall"); g_origInitTableSetting = nullptr; }
}

static void* DecodeActorSlot(uint64_t raw);
static bool  ClassIsPlayerState(void* a, void* cls);
static bool  InModule(const void* p);
static int CountPlayerStates() {
    if (!g_world) return -1;
    void**  levels = (void**)SafePtr((uint8_t*)g_world + RVA::UWorld_Levels);
    int32_t nLv    = SafeI32((uint8_t*)g_world + RVA::UWorld_Levels + 8);
    if (!levels || nLv <= 0 || nLv > 4096) return -1;
    int n = 0;
    for (int li = 0; li < nLv; ++li) {
        uint8_t* lvl = (uint8_t*)SafePtr(levels + li);
        if (!lvl) continue;
        uint64_t* acts = (uint64_t*)SafePtr(lvl + RVA::ULevel_Actors);
        int32_t   nA   = SafeI32(lvl + RVA::ULevel_Actors + 8);
        if (!acts || nA <= 0 || nA > 200000) continue;
        for (int ai = 0; ai < nA; ++ai) {
            uint64_t raw = 0;
            if (!SafeCopy(acts + ai, &raw, 8)) break;
            if (raw == 0) continue;
            void* a = DecodeActorSlot(raw);
            if ((uintptr_t)a < 0x10000 || (uintptr_t)a > 0x00007FFFFFFFFFFFULL) continue;
            void* vt = SafePtr(a);
            if (!vt || !InModule(vt)) continue;
            void* cls = SafePtr((uint8_t*)a + OFF::UObject_Class);
            if (cls && ClassIsPlayerState(a, cls)) ++n;
        }
    }
    return n;
}
// Game thread (MyTickFlush, MaskOff window), ~every 2 s until the match starts.
static void ZoneScaleTick() {
    if (!g_zoneScale || !g_origInitTableSetting || !g_world) return;
    void* gs = SafePtr((uint8_t*)g_world + OFF::UWorld_GameState);
    void* gm = SafePtr((uint8_t*)g_world + RVA::World_AuthGameMode);
    if (!gs || !gm) return;
    uint8_t state = 0xFF; SafeCopy((uint8_t*)gs + 0x9F2, &state, 1);
    if (state == 0xFF || state >= 3) return;          // 3 = countdown over, plane next: too late
    int passengers = CountPlayerStates();
    if (passengers <= 0) return;
    int rows = ZoneTierFor(passengers);
    if (rows == g_zoneTierCur) return;
    int pick = ZonePickFrom(rows);
    if (pick < 0) return;
    g_zoneTierCur = rows;
    L("[bz] Zonenstufe: %d Passagiere -> %d-Spieler-Tabelle, Zeile ID %d wird NEU eingespielt (InitTableSetting erneut, Match-Phase %d, #%d)",
      passengers, rows, RVA::PlayZone_IdBase + pick, state, ++g_zoneReruns);
    __try { g_origInitTableSetting(gm, pick, nullptr, nullptr, nullptr, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { L("[!] [bz] Ausnahme beim erneuten InitTableSetting -- Zonenstufen AUS"); g_zoneScale = false; }
}

// v147: gold looted in the round -> account. ------------------------------
// See RVA::PS_SetGold. One slot per joined player, keyed by PlayerController
// (and its PlayerState for the SetGold hook). Settled at Logout as one line
// in this log that tools/sp-listen-alive.js forwards to the backend:
//   [gg] #<n> t=<unix> CommitRequest[CurrencyGain] (UID:..,CurrencyIndex:230000001,Amount:<looted>,ActionCode:0)
// A loss (end < start - paid, e.g. gold dropped on death) is only logged, never
// booked -- better a little inflation than an account emptied by one death.
// Switch off without rebuilding:  -nogoldgain
static void* MakeTrampoline(uintptr_t target, const uint8_t* expect, size_t len);
static bool InstallJmp(const char* what, uintptr_t rva, const void* dest, const uint8_t* expect);
typedef void  (__fastcall* tSetGold)(void*, int, void*, void*, void*, void*);
typedef void* (__fastcall* tPayResult)(void*, uint8_t, int, int, void*, void*);
typedef void* (__fastcall* tLogout)(void*, void*, void*, void*, void*, void*);
static tSetGold   g_origSetGold   = nullptr;
static tPayResult g_origPayResult = nullptr;
static tLogout    g_origLogout    = nullptr;
static bool g_goldGain = true;    // -nogoldgain turns this off
struct GoldTrack { void* pc; void* ps; char uid[48]; int start; int last; int paid; int changes; bool active;
                   int coins; int bonus; bool resultSeen; };   // v148: from ShowMatchEndFinalResult
static GoldTrack g_gold[64];
static int g_ggSeq = 0;

static int GoldRead(void* ps) {
    void* w = ps ? SafePtr((uint8_t*)ps + PSO::PS_Wallet) : nullptr;
    return w ? SafeI32((uint8_t*)w + PSO::Wallet_Gold) : -1;
}
static GoldTrack* GoldByPc(void* pc) {
    for (auto& g : g_gold) if (g.active && g.pc == pc) return &g;
    return nullptr;
}
static GoldTrack* GoldByPs(void* ps) {
    for (auto& g : g_gold) if (g.active && g.ps == ps) return &g;
    return nullptr;
}
// Called after InitNewPlayer ran (AccountGold applied). uid may be empty for
// the host's own local player -- such a player is not tracked.
static void GoldRegister(void* pc, const char* uid) {
    if (!g_goldGain || !pc || !uid || !uid[0]) return;
    void* ps = SafePtr((uint8_t*)pc + PSO::PC_PlayerState);
    int gold = GoldRead(ps);
    if (!ps || gold < 0) { L("[gg] UID %hs: kein PlayerState/Gold lesbar (ps=%p) -- nicht verfolgt", uid, ps); return; }
    if (GoldTrack* old = GoldByPc(pc)) {   // InitNewPlayer twice for one PC (reconnect?)
        L("[gg] UID %hs: PC %p erneut initialisiert (Start %d, jetzt %d) -- Verfolgung neu", uid, pc, old->start, gold);
        old->active = false;
    }
    GoldTrack* slot = nullptr;
    for (auto& g : g_gold) if (!g.active) { slot = &g; break; }
    if (!slot) { L("[gg] UID %hs: keine freie Spur (64 aktiv?)", uid); return; }
    memset(slot, 0, sizeof(*slot));
    slot->pc = pc; slot->ps = ps; slot->start = gold; slot->last = gold; slot->active = true;
    strncpy_s(slot->uid, uid, _TRUNCATE);
    L("[gg] UID %hs: Start %d Gold (PC %p, PS %p)", uid, gold, pc, ps);
}
static void __fastcall MySetGold(void* ps, int value, void* a3, void* a4, void* a5, void* a6) {
    if (GoldTrack* g = GoldByPs(ps)) {
        if (g->changes < 8) L("[gg] UID %hs: Gold %d -> %d", g->uid, g->last, value);
        else if (g->changes == 8) L("[gg] UID %hs: weitere Aenderungen still", g->uid);
        g->changes++;
        g->last = value;
    }
    if (g_origSetGold) g_origSetGold(ps, value, a3, a4, a5, a6);
}
static void* __fastcall MyPayResult(void* pc, uint8_t ok, int amount, int action, void* a5, void* a6) {
    if (ok && amount > 0) {
        if (GoldTrack* g = GoldByPc(pc)) { g->paid += amount; L("[gg] UID %hs: bezahlt %d (Aktion %d), gesamt %d", g->uid, amount, action, g->paid); }
    }
    return g_origPayResult ? g_origPayResult(pc, ok, amount, action, a5, a6) : nullptr;
}
// v149: reads the gold ledger rows off the PlayerState (see RVA::Ledger_AddEntry).
// Returns the number of rows (0 = none/unreadable); coins = DropCoin+AcquireCoin,
// bonus = RandomGold+RandomRankGold; desc lists every row for the log.
static int GoldReadLedger(void* ps, int* coins, int* bonus, char* desc, int descCap) {
    static const char* names[17] = { "None","RankPoint","KillPoint","DMGPoint","SurvivalPoint","SupplyBoxOpen","DropCoin",
        "AcquireCoin","DropRecipe","AcquireRecipe","ChangeDeck","Resuscitation","RequestResuscitation","RandomGold",
        "RandomRankGold","SelectDeckMode","ChangeDeckList" };
    desc[0] = 0; *coins = 0; *bonus = 0;
    void* led = ps ? SafePtr((uint8_t*)ps + PSO::PS_Ledger) : nullptr;
    if (!led) { sprintf_s(desc, (size_t)descCap, "kein Ledger-Objekt an PS+0x%zX", (size_t)PSO::PS_Ledger); return 0; }
    void* rows = SafePtr((uint8_t*)led + PSO::Ledger_Rows);
    int num = SafeI32((uint8_t*)led + PSO::Ledger_Rows + 8);
    int max = SafeI32((uint8_t*)led + PSO::Ledger_Rows + 12);
    if (num <= 0 || num > 64 || max < num) { sprintf_s(desc, (size_t)descCap, "Ledger %p leer/unplausibel (Num %d, Max %d)", led, num, max); return 0; }
    struct Row { int32_t value; uint8_t type; uint8_t pad[3]; } r[64];
    if (!rows || !SafeCopy(rows, r, (size_t)num * sizeof(Row))) { sprintf_s(desc, (size_t)descCap, "Ledger-Zeilen %p nicht lesbar", rows); return 0; }
    int len = 0;
    for (int i = 0; i < num; ++i) {
        const char* nm = r[i].type < 17 ? names[r[i].type] : "?";
        if (len < descCap - 40) len += sprintf_s(desc + len, (size_t)(descCap - len), "%s%s(%d)=%d", i ? " " : "", nm, r[i].type, r[i].value);
        if (r[i].type == 6 || r[i].type == 7)   *coins += r[i].value;
        if (r[i].type == 13 || r[i].type == 14) *bonus += r[i].value;
    }
    return num;
}

static void GoldSettle(void* pc) {
    GoldTrack* g = GoldByPc(pc);
    if (!g) return;
    int end = GoldRead(g->ps);
    if (end < 0) end = g->last;
    // v149: the ledger on the PlayerState is the authoritative round result.
    { int lc = 0, lb = 0; char ld[512];
      int rows = GoldReadLedger(g->ps, &lc, &lb, ld, sizeof(ld));
      if (rows > 0) { g->coins = lc; g->bonus = lb; g->resultSeen = true; L("[gg] UID %hs: Ledger (%d Zeilen): %s", g->uid, rows, ld); }
      else            L("[gg] UID %hs: Ledger nicht lesbar: %s", g->uid, ld); }
    // Account gold that changed in the round (so far always 0: pickups do not
    // touch it) plus the round's own result: coins picked up + random bonus.
    long long looted = (long long)end - g->start + g->paid + g->coins + g->bonus;
    g->active = false;
    if (!g->resultSeen)
        L("[gg] UID %hs: weder Ledger noch ShowMatchEndFinalResult -> nur Kontogold-Differenz", g->uid);
    if (looted > 0) {
        ++g_ggSeq;
        L("[gg] UID %hs: Start %d, Ende %d, bezahlt %d, Muenzen %d, Bonus %d -> Beute %lld (wird verbucht)",
          g->uid, g->start, end, g->paid, g->coins, g->bonus, looted);
        L("[gg] #%d t=%llu CommitRequest[CurrencyGain] (UID:%hs,CurrencyIndex:230000001,Amount:%lld,ActionCode:0)",
          g_ggSeq, (unsigned long long)time(nullptr), g->uid, looted);
    } else if (looted < 0) {
        L("[gg] UID %hs: Start %d, Ende %d, bezahlt %d, Muenzen %d, Bonus %d -> Verlust %lld (NICHT verbucht)", g->uid, g->start, end, g->paid, g->coins, g->bonus, -looted);
    } else {
        L("[gg] UID %hs: Start %d, Ende %d, bezahlt %d, Muenzen %d, Bonus %d -> keine Beute", g->uid, g->start, end, g->paid, g->coins, g->bonus);
    }
}
// v148: the round's result. The host tells the client what it earned through
// the client RPC ABravoHotelPlayerController::ShowMatchEndFinalResult (client
// log: "MatchEndResult / ChangeDeck:-700 / AcquireCoin:25 / RandomGold:17").
// The parameter struct carries an array of {ENormalType, value} rows; the
// enum (reflection table): 6 DropCoin, 7 AcquireCoin, 10 ChangeDeck,
// 13 RandomGold, 14 RandomRankGold. The row layout is not known statically,
// so the decoder looks for a TArray inside the parameters whose rows read as
// small enum values with plausible numbers, tries 8/12/16/20/24-byte rows and
// logs the raw bytes as well, so a wrong guess can be corrected from the log.
// Coins picked up in the round are the "+N Gold" on the HUD (1:1).
static bool GoldDecodeResult(const uint8_t* prm, size_t prmLen, int* coins, int* bonus, char* desc, int descCap) {
    static const char* names[17] = { "None","RankPoint","KillPoint","DMGPoint","SurvivalPoint","SupplyBoxOpen","DropCoin",
        "AcquireCoin","DropRecipe","AcquireRecipe","ChangeDeck","Resuscitation","RequestResuscitation","RandomGold",
        "RandomRankGold","SelectDeckMode","ChangeDeckList" };
    for (size_t q = 0; q + 16 <= prmLen; q += 8) {
        const uint8_t* arr = *(const uint8_t* const*)(prm + q);
        int num = *(const int32_t*)(prm + q + 8);
        int max = *(const int32_t*)(prm + q + 12);
        if (!arr || num < 1 || num > 32 || max < num || max > 256) continue;
        static const int sizes[5] = { 8, 12, 16, 20, 24 };
        for (int si = 0; si < 5; ++si) {
            const int es = sizes[si];
            uint8_t rows[32 * 24];
            if (!SafeCopy(arr, rows, (size_t)num * es)) break;   // unreadable -> not an array
            for (int tw = 1; tw <= 4; tw <<= 1) {                 // type as u8 / u16 / u32
                for (int vo = 4; vo + 4 <= es; vo += 4) {         // value offset
                    bool ok = true; int c = 0, b = 0, seen7or13 = 0; int len = 0;
                    for (int i = 0; i < num && ok; ++i) {
                        const uint8_t* r = rows + i * es;
                        uint32_t t = tw == 1 ? r[0] : tw == 2 ? *(const uint16_t*)r : *(const uint32_t*)r;
                        int32_t v = *(const int32_t*)(r + vo);
                        if (t > 16 || v < -10000000 || v > 10000000) { ok = false; break; }
                        if (t == 7) { c += v; seen7or13 = 1; }
                        if (t == 13 || t == 14) { b += v; seen7or13 = 1; }
                        if (len < descCap - 40) len += sprintf_s(desc + len, (size_t)(descCap - len), "%s%s=%d", i ? " " : "", names[t], v);
                    }
                    if (ok && seen7or13) { *coins = c; *bonus = b;
                        len += sprintf_s(desc + len, (size_t)(descCap - len), "  [Feld +0x%zX, %d Zeilen x %d Byte, Typ %d Byte, Wert +%d]", q, num, es, tw, vo);
                        return true; }
                }
            }
        }
    }
    desc[0] = 0;
    return false;
}
static void GoldOnMatchEndResult(void* obj, void* params) {
    GoldTrack* g = GoldByPc(obj);
    if (!g) g = GoldByPs(obj);
    if (!g) { L("[gg] ShowMatchEndFinalResult auf %p -- kein verfolgter Spieler", obj); return; }
    uint8_t prm[0x80]; size_t got = 0;
    for (size_t k = 0x80; k >= 0x20 && !got; k -= 0x20) if (params && SafeCopy(params, prm, k)) got = k;
    if (!got) { L("[gg] UID %hs: ShowMatchEndFinalResult ohne lesbare Parameter", g->uid); return; }
    char hex[0x80 * 3 + 1]; int hl = 0;
    for (size_t i = 0; i < got && i < 0x60; ++i) hl += sprintf_s(hex + hl, sizeof(hex) - hl, "%02X%s", prm[i], (i % 8 == 7) ? "  " : " ");
    L("[gg] UID %hs: ShowMatchEndFinalResult Parameter (%zu Byte): %s", g->uid, got, hex);
    int coins = 0, bonus = 0; char desc[512];
    if (GoldDecodeResult(prm, got, &coins, &bonus, desc, sizeof(desc))) {
        g->coins = coins; g->bonus = bonus; g->resultSeen = true;
        L("[gg] UID %hs: Rundenergebnis %s -> Muenzen %d, Bonusgold %d", g->uid, desc, coins, bonus);
    } else {
        g->resultSeen = true;
        L("[gg] UID %hs: Rundenergebnis NICHT entschluesselt -- Rohbytes oben pruefen, Muenzen bleiben 0", g->uid);
    }
}
struct FnVerdict;   // defined with the attachment watch (v134)
static uint8_t FnIsMatchEndResult(void* fn);   // 1 = ShowMatchEndFinalResult, 2 = other

static void* __fastcall MyLogout(void* gm, void* pc, void* a3, void* a4, void* a5, void* a6) {
    __try { GoldSettle(pc); } __except (EXCEPTION_EXECUTE_HANDLER) { L("[gg] Ausnahme beim Abrechnen (PC %p)", pc); }
    return g_origLogout ? g_origLogout(gm, pc, a3, a4, a5, a6) : nullptr;
}
static void InstallGoldGain() {
    static const uint8_t sgProlog[16] = { 0x40,0x53, 0x48,0x83,0xEC,0x20, 0x48,0x8B,0x81,0xE0,0x05,0x00,0x00, 0x48,0x8B,0xD9 };
    static const uint8_t prProlog[16] = { 0x40,0x55, 0x53, 0x56, 0x57, 0x41,0x55, 0x41,0x56, 0x41,0x57, 0x48,0x8D,0x6C,0x24,0xD9 };
    static const uint8_t loProlog[14] = { 0x48,0x89,0x5C,0x24,0x18, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55, 0x41,0x56 };
    g_origSetGold   = (tSetGold)  MakeTrampoline(g_base + RVA::PS_SetGold,   sgProlog, sizeof(sgProlog));
    g_origPayResult = (tPayResult)MakeTrampoline(g_base + RVA::PC_PayResult, prProlog, sizeof(prProlog));
    g_origLogout    = (tLogout)   MakeTrampoline(g_base + RVA::GM_Logout,    loProlog, sizeof(loProlog));
    if (!g_origSetGold || !g_origPayResult || !g_origLogout) {
        L("[!] [gg] Trampolin FEHLGESCHLAGEN (SetGold %p, PayResult %p, Logout %p) -> Beute wird NICHT verbucht",
          (void*)g_origSetGold, (void*)g_origPayResult, (void*)g_origLogout);
        g_origSetGold = nullptr; g_origPayResult = nullptr; g_origLogout = nullptr; g_goldGain = false;
        return;
    }
    bool ok = InstallJmp("PlayerState::SetGold", RVA::PS_SetGold, (void*)&MySetGold, sgProlog)
           && InstallJmp("PlayerController::OnPayResult", RVA::PC_PayResult, (void*)&MyPayResult, prProlog)
           && InstallJmp("ABattleRoyaleGameMode::Logout", RVA::GM_Logout, (void*)&MyLogout, loProlog);
    if (ok) L("[+] [gg] Beute-Abrechnung aktiv: Start bei InitNewPlayer, Ende bei Logout, Zeile CommitRequest[CurrencyGain] in dieser Datei");
    else    L("[!] [gg] Hook unvollstaendig -- Beute wird evtl. NICHT verbucht");
}

static int g_loCalls = 0;
static void* __fastcall MyInitNewPlayer(void* a1, void* obj, void* a3, void* a4,
                                        void* a5, void* a6) {
    char uidForGold[48] = {0};   // v147: remembered for GoldRegister below
    if ((g_loadout || g_goldGain) && obj) {
        if (g_loCalls < 20) { ++g_loCalls; L("[lo] InitNewPlayer #%d obj=%p", g_loCalls, obj); }
        __try {
            auto* pData = (wchar_t**)((uint8_t*)obj + OPT::Options_Data);
            auto* pNum  = (int32_t*)((uint8_t*)obj + OPT::Options_Num);
            wchar_t* data; int num;
            if (SafeCopy(pData, &data, 8) && SafeCopy(pNum, &num, 4)
                && data && num >= 4 && num <= 8192) {
                static wchar_t buf[8200];
                int n = num < (int)(sizeof(buf)/sizeof(buf[0])) ? num : 0;
                if (n && SafeCopy(data, buf, (size_t)n * 2)) {
                    buf[n - 1] = 0;
                    char uid[64] = {0};
                    // Skip if already injected (options begin with our JSON '{').
                    bool gotUid = (buf[0] != L'{') && ExtractUidFromOptions(buf, n, uid, sizeof(uid));
                    if (g_loCalls <= 20 && !gotUid)
                        L("[lo]   keine UID in den Optionen (erste 60 Zeichen: %.60ls)", buf);
                    if (gotUid) strncpy_s(uidForGold, uid, _TRUNCATE);
                    if (gotUid && g_loadout) {
                        char opts[9000];
                        if (LookupLoadout(uid, opts, sizeof(opts))) {
                            if (InjectOptions(obj, opts))
                                L("[lo] UID %hs -> Loadout eingespielt (%d Zeichen Optionen, +%zu)",
                                  uid, num, strlen(opts));
                            else
                                L("[lo] UID %hs: Injektion fehlgeschlagen (Optionen unveraendert)", uid);
                        } else {
                            L("[lo] UID %hs: kein Eintrag in loadouts.txt", uid);
                        }
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            L("[lo] Ausnahme beim Einspielen -- Optionen unveraendert");
        }
    }
    void* ret = g_origInitNewPlayer ? g_origInitNewPlayer(a1, obj, a3, a4, a5, a6) : nullptr;
    // v147: AccountGold is applied inside the original -> read the start value now.
    if (g_goldGain && uidForGold[0]) {
        __try { GoldRegister(obj, uidForGold); } __except (EXCEPTION_EXECUTE_HANDLER) { L("[gg] Ausnahme beim Registrieren"); }
    }
    return ret;
}

// Entfernt die Steam-Besitzpruefung, damit das Spiel auch auf Accounts
// startet, die SUPER PEOPLE nicht in der Bibliothek haben:
//   je 0x12BA7F4  (0F 84 B8 00 00 00)  ->  6x NOP
// WICHTIG: laeuft frueh beim Engine-Start. Die DLL muss VOR dem
// OnlineSubsystem-Init drin sein, also direkt beim Prozessstart injizieren.
static bool SkipOwnershipCheck() {
    uintptr_t at = g_base + RVA::SteamSubscribedCheck_JE;
    static const uint8_t expect[6] = { 0x0F, 0x84, 0xB8, 0x00, 0x00, 0x00 };
    static const uint8_t patch [6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    auto* p = (uint8_t*)at;
    L("[i] BIsSubscribed-Sprung: %02X %02X %02X %02X %02X %02X",
      p[0],p[1],p[2],p[3],p[4],p[5]);
    if (memcmp(p, expect, 6) != 0) {
        if (memcmp(p, patch, 6) == 0) { L("[i] bereits gepatcht"); return true; }
        L("[!] unerwartete Bytes -> Besitzpruefung bleibt aktiv");
        return false;
    }
    if (!Poke(p, patch, 6)) { L("[!] Patch fehlgeschlagen"); return false; }
    L("[+] Steam-Besitzpruefung entfernt -> Spiel laeuft auch ohne Lizenz");
    return true;
}

// FNetworkNotify-Subobjekt im UWorld finden: VTable, die NotifyControlMessage
// in den ersten 8 Eintraegen hat (die primaere UWorld-VTable hat sie erst bei ~92).
static uintptr_t FindNotifyOffset(void* world) {
    uintptr_t want = g_base + RVA::NotifyControlMessage;
    for (uintptr_t o = 8; o < 0x400; o += 8) {
        void* vt = SafePtr((uint8_t*)world + o);
        if (!vt || !InModule(vt)) continue;
        for (int k = 0; k < 8; ++k)
            if ((uintptr_t)SafePtr((uint8_t*)vt + k*8) == want) {
                L("  FNetworkNotify-Subobjekt: World+0x%llX (VTable %p, NCM bei Index %d)",
                  (unsigned long long)o, vt, k);
                return o;
            }
    }
    return 0;
}

// ------------------------------------------------------------- Watchdog
//  Haelt World->NetDriver gesetzt. Ohne das crasht der Client-Verbindungs-
//  aufbau in UWorld::NotifyAcceptingConnection, weil dort ungeprueft
//  World->NetDriver->ServerConnection (+0x90) gelesen wird.
void*         g_world = nullptr;   // non-static: ViewTypeTick (v151) is declared earlier
static void*  g_drv   = nullptr;
static HANDLE g_watchdog = nullptr;

// ===========================================================================
//  STANDALONE-MASKE  (v33)  --  der eigentliche Fund dieser Runde
//
//  BEFUND
//  Die exe ist ein CLIENT-Target (WITH_SERVER_CODE=0). Damit ist
//  IsRunningClientOnly() zur Uebersetzungszeit konstant true, und
//  UWorld::InternalGetNetMode() ist zusammengefaltet zu:
//
//      if (NetDriver)     return NM_Client;          // Konstante!
//      if (DemoNetDriver) return Demo->GetNetMode();
//      return NM_Standalone;
//
//  Nachgewiesen an base+0x4729C57 (einzige nicht wegoptimierte Kopie):
//      cmp qword ptr [world+0x58], 0
//      je  .demo
//      mov eax, 3          <- NM_Client, als Konstante, ohne jeden Aufruf
//
//  FOLGE
//  Sobald ein NetDriver an der Welt haengt, meldet GetNetMode() dem ganzen
//  Spiel NM_Client. Alles, was "nur auf dem Server" laeuft, faellt still aus.
//  Dazu gehoert das Spawnen des Bodenloots -- genau deshalb ist im Standalone
//  Loot da und mit Listen-Server nicht, auf dem HOST selbst.
//  Ausserdem dreht AActor::GetFunctionCallspace() durch: RPCs, die der Client
//  schickt, will der Host zurueckschicken statt sie auszufuehren.
//
//  WARUM MAN DAS NICHT PATCHEN KANN
//  Die 3 ist eine Konstante, und an praktisch allen Aufrufstellen hat der
//  Compiler den Vergleich ganz wegoptimiert zu "NetDriver != null". Es gibt
//  keine Funktion, die man umbiegen koennte -- ich habe die ganze .text
//  danach abgesucht, es existiert genau eine einzige Kopie.
//
//  DESHALB ANDERSHERUM
//  Wir nehmen der WELT den Treiber weg, nicht dem Treiber die Welt:
//  UWorld::NetDriver (+0x58) und die NetDriver der LevelCollections werden
//  auf null gesetzt. Fuer die Spiellogik ist die Welt damit exakt das, was
//  sie im Standalone war. Der Treiber bleibt vollstaendig intakt:
//    * Driver->World (+0x148) zeigt weiter auf die Welt
//    * die vier Tick-Delegates sind ueber SetWorld registriert -> er tickt
//    * er steht in FWorldContext::ActiveNetDrivers -> ForEachNetDriver findet
//      ihn, neu gespawnte Actors werden weiter zur Replikation angemeldet
//    * UWorld::IsServer() liefert OHNE NetDriver true -> Level-Actors behalten
//      ROLE_Authority, der GameMode wird gespawnt (base+0x47386E0-Muster)
//
//  Die LevelCollections muessen mit, weil UWorld::SetActiveLevelCollection
//  (base+0x4743C90) deren NetDriver jeden Tick nach World->NetDriver kopiert.
//
//  NUR dort, wo die Engine selbst World->NetDriver ungeprueft dereferenziert,
//  blenden wir ihn kurz wieder ein:
//    * UWorld::NotifyAcceptingConnection -- daran ist v15 gecrasht
//    * UWorld::NotifyControlMessage      -- Login/Join des Clients
//    * UNetDriver::TickFlush             -- der ganze Replikationsdurchlauf
//
//  Abschalten ohne Neubau: Spiel mit  -nomask  starten (Verhalten wie v32).
// ===========================================================================
static bool          g_mask       = true;   // Maske gewuenscht (per -nomask aus)
static bool          g_maskActive = false;  // Maske scharf (erst nach Listen)
static volatile LONG g_maskDepth  = 0;      // >0 = Treiber gerade eingeblendet
static volatile LONG g_maskOffCnt = 0;

static void SafeStore(void* at, void* v) {
    __try { *(void**)at = v; } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Setzt World->NetDriver UND die NetDriver aller LevelCollections.
// Bewusst ohne VirtualProtect: beides liegt auf dem Heap und ist schreibbar,
// und die Funktion laeuft bis zu 60x pro Sekunde.
static void SetWorldDriver(void* d) {
    if (!g_world) return;
    SafeStore((uint8_t*)g_world + OFF::UWorld_NetDriver, d);
    void*  lcData = SafePtr((uint8_t*)g_world + OFF::UWorld_LevelCollections);
    int32_t lcNum = SafeI32((uint8_t*)g_world + OFF::UWorld_LevelCollections + 8);
    for (int32_t i = 0; lcData && i < lcNum && i < 8; ++i)
        SafeStore((uint8_t*)lcData + (size_t)i * OFF::LevelCollectionStride
                  + OFF::LevelCollection_NetDriver, d);
}
static void MaskOff() {                      // Treiber einblenden
    if (!g_maskActive) return;
    InterlockedIncrement(&g_maskOffCnt);
    if (InterlockedIncrement(&g_maskDepth) == 1) SetWorldDriver(g_drv);
}
static void MaskOn() {                       // Treiber wieder verstecken
    if (!g_maskActive) return;
    if (InterlockedDecrement(&g_maskDepth) == 0) SetWorldDriver(nullptr);
}

// ===========================================================================
//  ATTRAPPE ALS DEMO-NETDRIVER  (v34)  --  liefert endlich NM_ListenServer
//
//  WAS v33 UEBRIG GELASSEN HAT
//  Mit der Maske meldet GetNetMode() NM_Standalone. Das repariert alles, was
//  "nur auf dem Server" laufen soll (Loot, Fahrzeuge, Levelsichtbarkeit),
//  aber AActor::GetFunctionCallspace() liefert bei NM_Standalone fuer JEDE
//  Funktion "Local". Der Host fuehrt Client-RPCs also selbst aus, statt sie
//  zu verschicken. Deshalb bekam der PC nie ClientRestart -- und ohne das
//  setzt der PlayerController dort keinen Pawn und kein Kameraziel:
//  Minimap sagt "im Flugzeug", das Bild zeigt den Nullpunkt, also Meer.
//
//  DER HEBEL
//  Die Faltung an base+0x4729C57 hat DREI Zweige, und nur der erste ist
//  konstant gefaltet:
//
//      if (NetDriver)     return NM_Client;              // Konstante 3
//      if (DemoNetDriver) return DemoNetDriver->GetNetMode();   // ECHTER
//                                                              // VTable-Ruf!
//      return NM_Standalone;
//
//  Der zweite Zweig fragt ein Objekt. Wir haengen dort eine Attrappe ein,
//  deren VTable an Index 79 (Byte-Offset 0x278) NM_ListenServer (2) liefert.
//  Damit meldet die Engine im ganzen Spiel korrekt "Listen-Server", ohne dass
//  eine einzige Instruktion gepatcht wird:
//
//      bIsServer = true       -> Client-RPCs gehen RAUS statt lokal zu laufen
//                             -> Multicasts gehen lokal UND raus
//                             -> eingehende Server-RPCs werden ausgefuehrt
//                                statt an den Absender zurueckgeschickt
//
//  Die Sende-Maschinerie dafuer ist im Client-Build vorhanden -- geprueft:
//  UNetDriver::ProcessRemoteFunction (base+0x43F3660, 1765 Byte) enthaelt die
//  Schleife ueber ClientConnections (+0x98 Data / +0xA0 Num).
//
//  WIE DIE ATTRAPPE GEBAUT IST
//    * 0x4000 Byte, komplett genullt -> jedes Feld liest sich als 0.
//      Wichtig, weil UNetDriver::IsServer() nicht virtuell ist, sondern
//      ServerConnection (+0x90) direkt liest: 0 heisst "ich bin Server".
//      IsRecording() liest ClientConnections.Num() (+0xA0): 0 heisst "nein".
//    * die ersten 0x40 Byte sind der UObjectBase-Kopf des ECHTEN Treibers
//      (ObjectFlags, InternalIndex, ClassPrivate, ...). Damit sieht der
//      Garbage Collector ein gueltiges Objekt und markiert den echten
//      Treiber, der ohnehin lebt.
//    * eigene VTable mit 0x400 Eintraegen, alle auf "xor eax,eax; ret",
//      nur Index 79 auf "mov eax,2; ret". Jeder unerwartete virtuelle Ruf
//      liefert also 0/false/null statt in fremden Code zu springen.
//
//  NEBENEFFEKT, ERWUENSCHT: das Spiel sieht einen vorhandenen DemoNetDriver
//  und legt keinen echten Replay-Recorder mehr an ("A replay is already
//  recording"). Damit kann uns der Replay-Treiber den Zweig auch nicht mehr
//  unter den Fuessen wegziehen.
//
//  Abschalten ohne Neubau:  -nodecoy
// ===========================================================================
//  ---------------------------------------------------------------------------
//  ERGEBNIS DES FELDVERSUCHS (v34, 22:54) -- DIE ATTRAPPE IST STANDARDMAESSIG AUS
//
//  Sie hat getan, was sie sollte. Im Log stand:
//      [dc] Netzmodus laut Faltung: NM_ListenServer (2) -- Attrappe
//
//  Und trotzdem ist der Host danach nicht mehr hochgekommen: nach
//  "Bringing World LV-OrbIsland up for play" fehlten Possess, InitNewPlayer,
//  CharacterSpawned und der Wechsel WaitingToStart -> InProgress komplett.
//
//  Der Grund steht im Binary. Die Engine erkennt eine Replay-Wiedergabe an
//  genau dieser Konstellation -- Beispiel base+0x43CA770:
//
//      cmp qword [world+0x058], 0     ; NetDriver
//      jne .nein
//      cmp qword [world+0x130], 0     ; DemoNetDriver
//      je  .nein
//      ...                            ; -> "wir spielen ein Replay ab"
//
//  Mit maskiertem NetDriver UND gesetztem DemoNetDriver ist das exakt der
//  Zustand "Replay laeuft". Damit haelt sich das halbe Spiel fuer eine
//  Aufzeichnung und laesst den lokalen Spieler gar nicht erst entstehen.
//  Diese Pruefung ruft keine virtuelle Funktion auf -- es gibt an der
//  Attrappe also nichts, womit man sie beantworten koennte.
//
//  Die Attrappe bleibt im Code, weil der Netzmodus-Beweis wertvoll ist, aber
//  sie ist aus. Einschalten nur zum Experimentieren:  -decoy
//  ---------------------------------------------------------------------------
static bool  g_useDecoy = false;
static void* g_decoy    = nullptr;
// v40: present NM_ListenServer to the host only while it handles the client's
// join, so Login/SpawnPlayActor bind the PlayerController to the connection
// correctly. Outside that window the standalone mask stays in charge.
static bool g_joinDecoy = false;        // -joindecoy
static bool g_logCtrlMsg = false;       // reuse -rpclog to also log control-message ids


static bool BuildDecoy(void* realDrv) {
    auto* code = (uint8_t*)VirtualAlloc(nullptr, 0x1000, MEM_COMMIT|MEM_RESERVE,
                                        PAGE_EXECUTE_READWRITE);
    if (!code) { L("  [dc] kein Speicher fuer Stubs"); return false; }
    memset(code, 0xCC, 0x1000);
    uint8_t* stubZero = code;            // xor eax,eax ; ret
    stubZero[0]=0x31; stubZero[1]=0xC0; stubZero[2]=0xC3;
    uint8_t* stubTwo  = code + 0x10;     // mov eax,2 ; ret   -> NM_ListenServer
    stubTwo[0]=0xB8; *(uint32_t*)(stubTwo+1)=2u; stubTwo[5]=0xC3;

    const size_t SLOTS = 0x400;
    auto** vt = (void**)VirtualAlloc(nullptr, SLOTS*8, MEM_COMMIT|MEM_RESERVE,
                                     PAGE_READWRITE);
    if (!vt) { L("  [dc] kein Speicher fuer VTable"); return false; }
    for (size_t i = 0; i < SLOTS; ++i) vt[i] = stubZero;
    vt[OFF::NetDriver_GetNetMode_VT / 8] = stubTwo;

    auto* obj = (uint8_t*)VirtualAlloc(nullptr, 0x4000, MEM_COMMIT|MEM_RESERVE,
                                       PAGE_READWRITE);
    if (!obj) { L("  [dc] kein Speicher fuer Attrappe"); return false; }
    memset(obj, 0, 0x4000);
    if (realDrv) SafeCopy(realDrv, obj, 0x40);   // UObjectBase-Kopf uebernehmen
    *(void**)obj = vt;                           // eigene VTable einsetzen

    g_decoy = obj;
    L("  [dc] Attrappe %p, VTable %p, Slot 0x%llX -> mov eax,2",
      (void*)obj, (void*)vt, (unsigned long long)OFF::NetDriver_GetNetMode_VT);
    return true;
}

// UWorld::SetActiveLevelCollection (base+0x4743C90) kopiert JEDEN Tick:
//     mov rdi,[coll+0x10] / mov [world+0x058],rdi     <- NetDriver
//     mov rax,[coll+0x18] / mov [world+0x130],rax     <- DemoNetDriver
// Deshalb muss die Attrappe auch in die Collections, sonst wird sie 60x pro
// Sekunde wieder ausgetragen (im v34-Log als "[dc] wieder eingehaengt" zu sehen).
static void SetDemoDecoy(bool on) {
    if (!g_world || !g_decoy) return;
    void* v = on ? g_decoy : nullptr;
    SafeStore((uint8_t*)g_world + OFF::UWorld_DemoNetDriver, v);
    void*  lcData = SafePtr((uint8_t*)g_world + OFF::UWorld_LevelCollections);
    int32_t lcNum = SafeI32((uint8_t*)g_world + OFF::UWorld_LevelCollections + 8);
    for (int32_t i = 0; lcData && i < lcNum && i < 8; ++i)
        SafeStore((uint8_t*)lcData + (size_t)i * OFF::LevelCollectionStride
                  + OFF::LevelCollection_DemoNetDriver, v);
}

// Was sieht das Spiel jetzt als Netzmodus? Bildet die Faltung exakt nach.
static const char* NetModeName() {
    if (!g_world) return "?";
    void* nd = SafePtr((uint8_t*)g_world + OFF::UWorld_NetDriver);
    if (nd) return "NM_Client (3) -- Treiber gerade eingeblendet";
    void* dd = SafePtr((uint8_t*)g_world + OFF::UWorld_DemoNetDriver);
    if (!dd) return "NM_Standalone (0)";
    if (dd == g_decoy) return "NM_ListenServer (2) -- Attrappe";
    return "?? fremder DemoNetDriver";
}

// ------------------------------------------------- FNetworkNotify-Umleitung
//  Kein Code-Patch: wir tauschen zwei Eintraege in der VTable des
//  FNetworkNotify-Subobjekts (World+0x48). Beide Ziele werden vorher an ihrer
//  bekannten Adresse verifiziert -- passt sie nicht, wird nichts angefasst.
typedef int  (__fastcall* tNotifyAccept)(void* notify);
typedef void (__fastcall* tNotifyCtrl)(void* notify, void* conn, uint8_t msg, void* bunch);
static tNotifyAccept g_origNotifyAccept = nullptr;
static tNotifyCtrl   g_origNotifyCtrl   = nullptr;

static int __fastcall MyNotifyAccept(void* notify) {
    MaskOff();
    int r = g_origNotifyAccept ? g_origNotifyAccept(notify) : 0;
    MaskOn();
    return r;
}
// UE4 control-message ids. NMT_Login=5, NMT_Join=9, NMT_JoinSplit=10 -- these
// are the ones during which the server sets up the client's PlayerController.
// Only NMT_Join (9) actually spawns the play actor and binds the PC to the
// connection. NMT_Login (5) dereferences World->NetDriver, so it MUST keep the
// real driver -- nulling it there was what crashed the host in v40.
static bool IsJoinMsg(uint8_t m) { return m == 9; }

static void __fastcall MyNotifyCtrl(void* notify, void* conn, uint8_t msg, void* bunch) {
    // v142: every join attempt starts with NMT_Hello (0). Logged so a failed
    // lobby join can be told apart from "the client never arrived".
    if (msg == 0) {
        static int hellos = 0;
        if (hellos < 200) {
            ++hellos;
            L("[et] NMT_Hello #%d von Verbindung %p (Token-Patch %s)", hellos, conn,
              g_ignoreEncToken ? "AN" : "AUS -- ein Token wird abgewiesen");
        }
    }
    if (g_logCtrlMsg) {
        static int seen[256] = {0};
        if (msg < 256 && !seen[msg]) { seen[msg] = 1; L("[ctrl] control message id %u", (unsigned)msg); }
    }
    // Join window: give the host NM_ListenServer (decoy) instead of the real
    // driver, so the PlayerController<->connection binding is done correctly.
    if (g_joinDecoy && g_decoy && IsJoinMsg(msg)) {
        // Present NM_ListenServer for the NMT_Join spawn only. If the call
        // faults, put the real driver back immediately so the host survives
        // instead of running on with a null driver (the v40 crash cascade).
        bool faulted = false;
        SetWorldDriver(nullptr);          // NetDriver = null
        SetDemoDecoy(true);               // DemoNetDriver = decoy -> NM_ListenServer
        __try {
            if (g_origNotifyCtrl) g_origNotifyCtrl(notify, conn, msg, bunch);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            faulted = true;
        }
        SetDemoDecoy(false);
        if (faulted) {
            SetWorldDriver(g_drv);        // hand the real driver back -> survive
            g_joinDecoy = false;          // do not try again this session
            L("[jd] join msg %u faulted -> real driver restored, join-decoy disabled", (unsigned)msg);
        } else {
            L("[jd] handled join msg %u with NM_ListenServer decoy", (unsigned)msg);
        }
        return;
    }
    MaskOff();
    if (g_origNotifyCtrl) g_origNotifyCtrl(notify, conn, msg, bunch);
    MaskOn();
}

static void HookNotify(void* world) {
    if (!g_notifyOff) { L("  [nm] kein FNetworkNotify-Offset -> Maske waere unsicher"); return; }
    void** vt = (void**)SafePtr((uint8_t*)world + g_notifyOff);
    if (!vt || !InModule(vt)) { L("  [nm] Notify-VTable unbrauchbar"); return; }
    void* wantA = (void*)(g_base + RVA::NotifyAcceptingConnection);
    void* wantC = (void*)(g_base + RVA::NotifyControlMessage);
    for (int k = 0; k < 8; ++k) {
        void* cur = SafePtr(vt + k);
        if (cur == wantA && !g_origNotifyAccept) {
            g_origNotifyAccept = (tNotifyAccept)cur;
            void* mine = (void*)&MyNotifyAccept;
            L(Poke(vt + k, &mine, 8)
              ? "  [nm] NotifyAcceptingConnection (Index %d) umgehaengt"
              : "  [nm] NotifyAcceptingConnection (Index %d) NICHT schreibbar", k);
        } else if (cur == wantC && !g_origNotifyCtrl) {
            g_origNotifyCtrl = (tNotifyCtrl)cur;
            void* mine = (void*)&MyNotifyCtrl;
            L(Poke(vt + k, &mine, 8)
              ? "  [nm] NotifyControlMessage (Index %d) umgehaengt"
              : "  [nm] NotifyControlMessage (Index %d) NICHT schreibbar", k);
        }
    }
    if (!g_origNotifyAccept) L("  [nm] NotifyAcceptingConnection NICHT gefunden");
    if (!g_origNotifyCtrl)   L("  [nm] NotifyControlMessage NICHT gefunden");
}

// ===========================================================================
//  MESSUNG: laeuft TickFlush ueberhaupt?  (v27)
//
//  Stand der Dinge: der Client oeffnet 4 Kanaele und behaelt sie. Das sind
//  Kontrollkanal plus die Actors der Verbindung selbst. Weltobjekte und
//  GameState kommen nie an. Die Nachregistrierung der Objektliste (v26) hat
//  daran nichts geaendert -- gemessen an derselben Zahl:
//      "UNetConnection::Close: ... Channels: 4"
//
//  Auffaellig ist die Sprunghaftigkeit: einmal kam der erste Zustand nach
//  20 s, einmal nach 93 s. Das sieht nicht nach einem Filter aus, der sauber
//  "nein" sagt, sondern danach, dass die Replikation nur ganz selten laeuft.
//
//  ServerReplicateActors steckt in UNetDriver::TickFlush, und die Funktion ist
//  mit 7102 Byte voll vorhanden (siehe RVA-Kommentar oben) -- die Schleife ist
//  also NICHT wegkompiliert. Bleibt die Frage, wie oft sie drankommt.
//
//  Deshalb: VTable-Eintrag 98 des Treibers auf uns umbiegen, mitzaehlen,
//  Original aufrufen. Kein Code-Patch, kein Trampolin, kein Risiko.
//
//  Auswertung im Log:
//    ~30 Aufrufe/s  -> Replikation laeuft, die Relevanzpruefung filtert
//    deutlich weniger -> der Treiber wird nicht getickt, das ist der Fehler
// ===========================================================================
typedef void (__fastcall* tTickFlush)(void* drv, float dt);
static tTickFlush g_origTickFlush = nullptr;
static volatile LONG g_tfCount = 0;

typedef int32_t (__fastcall* tRepServerReplicate)(void* repDriver, float dt);

// Der fehlende Aufruf. In UE 4.25 steht er in UNetDriver::TickFlush so:
//
//    #if WITH_SERVER_CODE
//        if (IsServer() && ClientConnections.Num() > 0) {
//            if (ReplicationDriver) ReplicationDriver->ServerReplicateActors(DeltaSeconds);
//            else                   ServerReplicateActors(DeltaSeconds);
//        }
//    #endif
//
// Beide Zweige liegen im selben WITH_SERVER_CODE-Block. Ist er wegkompiliert,
// wird der Graph zwar erzeugt und mit der Welt verbunden, aber nie gefragt.
// Genau das holen wir hier nach.
//
// Sicherungen, in dieser Reihenfolge:
//   1. nur fuer unseren GameNetDriver, nicht fuer den PendingNetDriver
//   2. nur wenn mindestens eine Client-Verbindung anliegt (wie im Original)
//   3. ReplicationDriver muss gesetzt sein
//   4. der VTable-Eintrag muss EXAKT auf die erwartete Adresse zeigen --
//      sonst wird nichts aufgerufen, sondern nur protokolliert
// Der eigentliche Replikationsdurchlauf, herausgezogen, damit MyTickFlush die
// Maske sauber um ALLES herumlegen kann -- auch um die frueheren Ausstiege.
static void ReplicateNow(void* drv, float dt) {
    if (drv != g_drv) return;
    if (SafeI32((uint8_t*)drv + OFF::UNetDriver_ClientConnNum) <= 0) return;

    void* rd = SafePtr((uint8_t*)drv + OFF::UNetDriver_RepDriver);
    if (!rd) return;
    void** vt = (void**)SafePtr(rd);
    if (!vt || !InModule(vt)) return;

    void* fn = SafePtr((uint8_t*)vt + OFF::RepDriver_ServerReplicateActors_VT);
    // v71: BOTH implementations are legitimate. Until now only the base class's
    // address was accepted, so switching the graph class would have silently
    // stopped us driving replication at all.
    const bool knownSRA = (fn == (void*)(g_base + RVA::RepGraph_ServerReplicateActors))
                       || (fn == (void*)(g_base + RVA::RealRepGraph_ServerReplicate));
    if (!knownSRA) {
        static int warned = 0;
        if (!warned++) L("[rg] VTable+0x2F0 = base+0x%llX -- keine bekannte ServerReplicateActors -> KEIN Aufruf",
                         (unsigned long long)((uintptr_t)fn - g_base));
        return;
    }

    int32_t n = ((tRepServerReplicate)fn)(rd, dt);

    static int logged = 0; static int32_t sum = 0; static int calls = 0;
    ++calls; sum += n;
    if (logged < 12 && (calls % 60) == 0) {
        // v58k: does the graph know the client at all? UReplicationGraph keeps
        // its UNetReplicationGraphConnection list at +0x40 (data) / +0x48
        // (num) -- read out of its own ServerReplicateActors 0x13C6C60, which
        // walks exactly that array and takes each connection's UNetConnection
        // from +0x30. An empty list means the graph replicates nothing at all,
        // however well everything else is set up.
        const int32_t graphConns = SafeI32((uint8_t*)rd + 0x48);
        const int32_t drvConns   = SafeI32((uint8_t*)drv + OFF::UNetDriver_ClientConnNum);
        void** cl = (void**)SafePtr((uint8_t*)rd + 0x40);
        void*  c0 = (cl && graphConns > 0) ? SafePtr(cl) : nullptr;
        L("[rg] ServerReplicateActors: %d Aufrufe, zuletzt %d Actors, Summe %d | Verbindungen im Graphen %d, im Treiber %d, erste Graph-Verbindung %p -> UNetConnection %p",
          calls, n, sum, graphConns, drvConns, c0,
          c0 ? SafePtr((uint8_t*)c0 + 0x30) : nullptr);
        ++logged;
    }
}

static void ReplicateWeaponIfDirty();    // v73, defined with the weapon block

// ---------------------------------------------------------------------------
//  v76: how many viewers does the spatial gather actually get?
//  Read-only wrapper on the grid node's vtable slot +0x288. Calls the original
//  untouched and logs the viewer count it was handed. Same poke technique as
//  the RemoveNetworkActor diagnostic, and the slot is only touched if it still
//  holds 0x13B12F0.
// ---------------------------------------------------------------------------
typedef void (__fastcall* tGridGather)(void* node, void* params);
static tGridGather   g_origGridGather = nullptr;
static volatile LONG g_gatherCalls = 0;

static bool FNameToStr(void* fnamePtr, wchar_t* out, int cap);   // v91, defined further down
static bool IsDoorWindowClass(void* cls);                        // v97, defined further down
static volatile LONG g_drLogged = 0;
static volatile LONG g_pkLogged  = 0;   // v100, #16 perk UI -- perk words
static volatile LONG g_pk2Logged = 0;   // v102, everything else, separate budget

static void __fastcall MyGridGather(void* node, void* params) {
    LONG n = InterlockedIncrement(&g_gatherCalls);
    // v77: the INPUT is proven fine -- exactly one viewer, at Alen's real
    // position, all the way down to the ground (v76 round: {163220, 336818,
    // 275}). So the question is the OUTPUT: how many actors does the node put
    // into the gathered lists? We do not know that struct's layout, so instead
    // of guessing an offset we snapshot the parameter block, let the original
    // run, and report which int32 slots grew. The same before/after technique
    // that found the magazine field. Purely read-only.
    // v93: the v77..v92 [gg] measurement was WRONG and its "gathered nothing"
    // verdict has to be thrown out. It snapshotted the PARAMETER BLOCK, but
    // the gather does not count into Params at all. Static analysis of
    // ActorListFrequencyBuckets::Gather (0x13AFA50) shows where the counter
    // really lives:
    //
    //      Out = Params[0xE8];              // FGatheredReplicationActorLists*
    //      AddList(Out, bucket);            // 0x13AB280
    //      Out[0x50]++;                     // <- the counter, in OUT, not Params
    //
    // So every "KEIN Zaehler ist gestiegen" line so far measured memory the
    // gather never touches. Measure the real thing.
    const bool watch = (n <= 6) || (n % 900 == 0);
    void*   outObj   = watch ? SafePtr((uint8_t*)params + 0xE8) : nullptr;
    int32_t outBefore = outObj ? SafeI32((uint8_t*)outObj + 0x50) : 0;
    int32_t outRaw[0x18];
    if (outObj)
        for (int i = 0; i < 0x18; ++i) outRaw[i] = SafeI32((uint8_t*)outObj + i * 4);

    if (g_origGridGather) g_origGridGather(node, params);

    if (!watch) return;
    void*   vptr = SafePtr((uint8_t*)params + 0xC8);
    int32_t vnum = SafeI32((uint8_t*)params + 0xD0);
    float vx = 0.f, vy = 0.f, vz = 0.f;
    if (vnum > 0) {
        const uint8_t* v = vptr ? (const uint8_t*)vptr : ((const uint8_t*)params + 8);
        SafeCopy(v + 0x18, &vx, 4); SafeCopy(v + 0x1C, &vy, 4); SafeCopy(v + 0x20, &vz, 4);
    }
    const int32_t outAfter = outObj ? SafeI32((uint8_t*)outObj + 0x50) : 0;
    L("[gg] Gather #%ld: Zuschauer %d | Blickpunkt {%.0f, %.0f, %.0f} | Ausgabe-Objekt %p:"
      " Listen %d -> %d",
      n, vnum, vx, vy, vz, outObj, outBefore, outAfter);
    if (outObj && outAfter == outBefore) {
        L("[gg]   Der Zaehler im Ausgabe-Objekt ist NICHT gestiegen -- diesmal an der"
          " richtigen Stelle gemessen. Weitere Felder, die sich geaendert haben:");
        int shown = 0;
        for (int i = 0; i < 0x18 && shown < 10; ++i) {
            const int32_t after = SafeI32((uint8_t*)outObj + i * 4);
            if (after == outRaw[i]) continue;
            ++shown;
            L("[gg]     Out+0x%02X: %d -> %d", i * 4, outRaw[i], after);
        }
        if (!shown) L("[gg]     keines -- das Ausgabe-Objekt bleibt voellig unberuehrt");
    }

    // v91: the other half of the streaming-level question. The collection
    // gather (0x13AF790) drops any level whose FName is not in the set at
    // Params+0xF0 -- ClientVisibleLevelNames, which the client fills by
    // sending ServerUpdateLevelVisibility. Read that set here: a TSet's
    // sparse array is {data, Num, Max} at its start and its elements are
    // 16 bytes beginning with the FName. If this set is empty, every
    // streaming-level list in every cell is thrown away and that alone
    // explains loot, doors and windows. Pure reads.
    // v92: CORRECTION. ClientVisibleLevelNamesRef is a C++ REFERENCE, i.e. a
    // pointer to the set, so Params+0xF0 holds the SET'S ADDRESS -- it is not
    // the set itself. v91 read Params+0xF0 as the set and Params+0xF8 as its
    // Num, but +0xF8 is LastCheckedVisibleLevel (a cached FName), which is 0
    // until the first successful lookup. The "set is empty" line v91 printed
    // was therefore MY bug, not a finding. Dereference once more.
    void*   visSet = SafePtr((uint8_t*)params + 0xF0);
    void*   visArr = visSet ? SafePtr(visSet) : nullptr;
    int32_t visNum = visSet ? SafeI32((uint8_t*)visSet + 8) : -1;
    if (!visArr || visNum < 0 || visNum > 100000) {
        L("[cv] Set %p -> Daten %p Num %d -- so nicht lesbar, Rohbild des Sets:",
          visSet, visArr, visNum);
        for (uintptr_t o = 0; o <= 0x40 && visSet; o += 8)
            L("[cv]   Set+0x%02llX  %p  (als int32: %d / %d)", (unsigned long long)o,
              SafePtr((uint8_t*)visSet + o),
              SafeI32((uint8_t*)visSet + o), SafeI32((uint8_t*)visSet + o + 4));
    } else if (visNum == 0) {
        L("[cv] *** ClientVisibleLevelNames ist LEER -- der Client hat dem Host KEIN"
          " einziges Streaming-Level als sichtbar gemeldet. Damit wirft die Zelle jede"
          " Level-Liste weg. Das ist die Ursache. ***");
    } else {
        L("[cv] ClientVisibleLevelNames: %d Eintraege", visNum);
        int named = 0;
        for (int i = 0; i < visNum && named < 15; ++i) {
            uint8_t* e = (uint8_t*)visArr + (size_t)i * 16;
            wchar_t ln[128] = L"?";
            if (!FNameToStr(e, ln, 128)) continue;
            ++named;
            L("[cv]   %ls", ln);
        }
        if (!named) L("[cv]   keiner der Eintraege liess sich als Name aufloesen --"
                      " das Set liegt anders im Speicher als angenommen");
    }
}

static void HookGridGather() {
    if (g_origGridGather || !g_drv) return;
    void* graph = SafePtr((uint8_t*)g_drv + OFF::UNetDriver_RepDriver);
    if (!graph) return;
    void* grid = SafePtr((uint8_t*)graph + 0x5A0);
    void** vt = grid ? (void**)SafePtr(grid) : nullptr;
    if (!vt || !InModule(vt)) return;
    void* cur = SafePtr((uint8_t*)vt + 0x288);
    if (cur != (void*)(g_base + RVA::GridNode_Gather)) {
        L("[gg] Slot +0x288 des GridNode zeigt auf base+0x%llX statt 0x13B12F0 -- nicht angefasst",
          (unsigned long long)((uintptr_t)cur - g_base));
        g_origGridGather = (tGridGather)cur;          // stop retrying
        return;
    }
    void* mine = (void*)&MyGridGather;
    if (Poke((uint8_t*)vt + 0x288, &mine, 8)) {
        g_origGridGather = (tGridGather)cur;
        L("[gg] GridNode-VTable +0x288 (Gather, %p) laeuft jetzt ueber uns -- nur Lesen", cur);
    }
}

// v83: declared early so the hook installer can see them; the body lives with
// the weapon block, after g_watchWeapon / g_remote / ResolveFuncName exist.
typedef void (__fastcall* tPE)(void* obj, void* fn, void* params);
static tPE           g_origProcessEvent = nullptr;
static volatile LONG g_peLogged = 0;
static volatile LONG g_ldLogged = 0;   // v115: ladder/climb instrument, own budget
static volatile LONG g_graphGuardHits = 0;   // v136: graph per-actor guard
static volatile LONG g_graphGuardSeen = 0;
static volatile LONG g_zrLogged = 0;   // v119: zone/phase events on GameState + zone actor
// v121 -- three decisive instruments, see the header comment at each use site.
static volatile LONG g_mvLogged = 0;    // [mv] movement corrections
static volatile LONG g_csLogged = 0;    // [cs] character state machine
static volatile LONG g_cxLogged = 0;    // [cx] channel census
// Our own channel -> actor map, filled from MySetChannelActor. The engine's
// ActorChannels map is keyed by a WEAK pointer (object index + serial), and
// this build's FUObjectItem is obfuscated, so a weak key cannot be resolved
// back to an actor. Remembering the pairing ourselves costs one array.
struct ChanPair { void* chan; void* actor; };
static ChanPair      g_chanMap[512];
static int           g_chanMapN = 0;
static CRITICAL_SECTION g_chanCs; static bool g_chanCsReady = false;
static void ChanMapPut(void* ch, void* actor) {
    if (!ch) return;
    if (!g_chanCsReady) { InitializeCriticalSection(&g_chanCs); g_chanCsReady = true; }
    EnterCriticalSection(&g_chanCs);
    for (int i = 0; i < g_chanMapN; ++i)
        if (g_chanMap[i].chan == ch) { g_chanMap[i].actor = actor; LeaveCriticalSection(&g_chanCs); return; }
    if (g_chanMapN < 512) { g_chanMap[g_chanMapN].chan = ch; g_chanMap[g_chanMapN].actor = actor; ++g_chanMapN; }
    LeaveCriticalSection(&g_chanCs);
}
static void* ChanMapGet(void* ch) {
    if (!ch || !g_chanCsReady) return nullptr;
    void* r = nullptr;
    EnterCriticalSection(&g_chanCs);
    for (int i = 0; i < g_chanMapN; ++i) if (g_chanMap[i].chan == ch) { r = g_chanMap[i].actor; break; }
    LeaveCriticalSection(&g_chanCs);
    return r;
}
// v116: the 14.09. crash was a DANGLING ACTOR inside the graph's own
// replication pass. Count both removal paths against AddNetworkActor -- if
// actors go in and never come out, a streamed-out level leaves freed pointers
// in the graph and the next pass dereferences one.
static volatile LONG g_removeDrv   = 0;
static volatile LONG g_removeGraph = 0;
static bool          g_peLog = true;                 // -nopelog
static void __fastcall MyProcessEvent(void* obj, void* fn, void* params);

static void ZoneTick();   // v48: runs on the GAME thread, see below
static void ServerChangedTick();   // v157: bIsServerChanged for the remote players' held weapons
static void BoltFixTick();         // v160: the empty magazine and its unfinished chambering action
static void BoltFinishTick();      // v164: finish the chambering action the way a dedicated server does
static void EmptyPulseTick();      // v165: give the client its reload permission back when the magazine is empty
static void LadderTick();     // v122: polls the ladder component directly
// v121: g_remote is declared much further down, so the channel census asks
// through these three instead.
static bool IsRemotePawnActor(void* a);
static bool IsRemoteWeaponActor(void* a);
static bool IsRemoteControllerActor(void* a);

static void LootSweep();                 // v58e, defined with the loot block
static void PillTick();                  // v104, defined with the pill block
static void PlayerStateCensus();         // v103, #16
static void ForceTilesAroundRemote();     // v58s, defined with the streaming block
static void PokeOwnedActors();            // v58t, defined with the loot block
static bool PokeOneActor(void* a);         // v58t, defined with the wake-up block
// v167: die Befehlsdatei des Panels. Definiert weiter unten, neben der
// Reflexionssuche, die sie braucht.
static void CmdFileTick();

static void __fastcall MyTickFlush(void* drv, float dt) {
    InterlockedIncrement(&g_tfCount);
    const bool ours = (drv == g_drv);
    if (ours) MaskOff();                 // Engine-Innenleben: Treiber sichtbar
    if (g_origTickFlush) g_origTickFlush(drv, dt);
    ReplicateNow(drv, dt);
    if (ours) ReplicateWeaponIfDirty();   // v73, same tick, frame is fresh
    // v48: the blue zone work MUST happen here, not in the watchdog thread.
    // In v47 it ran on the watchdog and called engine functions (FName::ToString,
    // FMemory::Free, ForceNetUpdate) from a foreign thread while the game thread
    // was replicating -- that is a data race against the whole engine and it
    // showed up as the joining player being shoved into buildings and under the
    // map. Here we are on the game thread AND inside the MaskOff window, so
    // World->NetDriver is visible and ForceNetUpdate can actually do its job
    // (with the mask on, GetNetDriver() returns null and it is a no-op).
    if (ours) ZoneTick();
    if (ours) ViewTypeTick();            // v151: -fpp / -tpp
    if (ours) { static int zs = 0; if (++zs >= 120) { zs = 0; ZoneScaleTick(); } }   // v153, ~every 2 s
    if (ours) { static int sc = 0; if (++sc >= 6) { sc = 0; ServerChangedTick(); } }   // v157, ~10x/s
    if (ours) { static int bx = 0; if (++bx >= 6) { bx = 0; BoltFixTick(); } }        // v160, ~10x/s
    if (ours) { static int bf = 0; if (++bf >= 3) { bf = 0; BoltFinishTick(); } }     // v164, ~20x/s
    if (ours) { static int ep = 0; if (++ep >= 3) { ep = 0; EmptyPulseTick(); } }     // v165, ~20x/s
    if (ours) { static int op = 0; if (++op >= 15) { op = 0; PokeOwnedActors(); } }        // v58t, ~every 0.25 s
    if (ours) { static int fx = 0; if (++fx >= 30) { fx = 0; ForceTilesAroundRemote(); } }  // v58s, ~every 0.5 s
    if (ours) { static int sw = 0; if (++sw >= 120) { sw = 0; LootSweep(); } }   // v58e, ~every 2 s
    if (ours) { static int pc = 0; if (++pc >= 150) { pc = 0; PlayerStateCensus(); } }  // v103, ~2.5 s
    if (ours) { static int pi = 0; if (++pi >= 60) { pi = 0; PillTick(); } }            // v104, ~1 s, both self-disarming
    if (ours) { static int cf = 0; if (++cf >= 30) { cf = 0; CmdFileTick(); } }         // v167, ~2x/s
    if (ours) MaskOn();                  // fuer die Spiellogik wieder weg
}

static void HookTickFlush(void* drv) {
    void** vt = (void**)SafePtr(drv);
    if (!vt) { L("  [tf] Treiber-VTable unlesbar"); return; }
    void*  cur  = SafePtr(vt + OFF::TickFlush_VT);
    void*  want = (void*)(g_base + RVA::UNetDriver_TickFlush);
    L("  [tf] VTable[%zu] = %p (erwartet %p)", OFF::TickFlush_VT, cur, want);
    if (cur != want) { L("  [tf] passt nicht -> NICHT gehakt"); return; }
    g_origTickFlush = (tTickFlush)cur;
    void* mine = (void*)&MyTickFlush;
    if (Poke(vt + OFF::TickFlush_VT, &mine, 8)) L("  [tf] TickFlush wird gezaehlt");
    else                                        L("  [tf] VTable nicht beschreibbar");
}

// ===========================================================================
//  DIFFERENZMESSUNG AM TREIBEROBJEKT  (v28)
//
//  Warum so und nicht anders: ich habe dreimal versucht, ServerReplicateActors
//  ueber die VTable zu identifizieren, und dreimal danebengegriffen (einmal
//  Treiber- und Verbindungs-VTable verwechselt, einmal einen Teilstring statt
//  des Stringanfangs gesucht). Geratene Offsets haben in diesem Projekt schon
//  einen Absturz gekostet -- also diesmal ohne.
//
//  Idee: UNetDriver zaehlt intern mit. ReplicationFrame wird bei JEDEM
//  Replikationsdurchlauf um 1 erhoeht. Wir muessen das Feld nicht kennen --
//  wir suchen es. Dazu alle 5 s die ersten 0x600 Byte des Treibers
//  fotografieren und protokollieren, welche 4-Byte-Felder sich geaendert
//  haben und um wieviel.
//
//  Der Vergleich macht die Aussage:
//     Phase A -- noch kein Client verbunden: ServerReplicateActors steigt
//                sofort aus (ClientConnections.Num() == 0)
//     Phase B -- Client verbunden: jetzt muesste die Schleife durchlaufen
//
//  Kommt in Phase B KEIN neuer Zaehler dazu, laeuft die Replikation gar nicht
//  -- dann ist der Aufruf in TickFlush wegkompiliert (WITH_SERVER_CODE) und
//  wir muessen ihn selbst nachbauen. Kommen neue Zaehler dazu, laeuft sie und
//  filtert, und wir wissen ausserdem, welches Feld ReplicationFrame ist.
// ===========================================================================
static uint8_t g_snap[0xA00];
static bool    g_snapValid = false;

// Einmalig: alle Slots des Treibers auflisten, die auf ein UObject zeigen
// (erste 8 Byte = VTable, und die muss im Modul liegen). Damit finden wir
// UNetDriver::ReplicationDriver ohne einen einzigen geratenen Offset:
// der Slot, der NUR mit ReplicationGraph-Eintrag in der Engine.ini belegt ist,
// ist es.
static void DumpObjectPointers(void* obj, size_t size) {
    L("[ptr] Treiber @%p -- Slots, die auf ein UObject zeigen (Fenster 0x%zX):",
      obj, size);
    // erst alle sammeln, dann zaehlen wie oft jede VTable vorkommt:
    // ein einzeln vorkommendes Objekt ist der bessere Kandidat.
    static uintptr_t offs[256], ptrs[256], vts[256];
    int n = 0;
    for (size_t o = 0; o + 8 <= size && n < 256; o += 8) {
        void* p = SafePtr((uint8_t*)obj + o);
        if (!p || (uintptr_t)p < 0x10000) continue;
        void* vt = SafePtr(p);
        if (!vt || !InModule(vt)) continue;
        offs[n] = o; ptrs[n] = (uintptr_t)p; vts[n] = (uintptr_t)vt; ++n;
    }
    for (int i = 0; i < n; ++i) {
        int same = 0;
        for (int k = 0; k < n; ++k) if (vts[k] == vts[i]) ++same;
        L("   +0x%03llX -> %p   VTable base+0x%llX%s",
          (unsigned long long)offs[i], (void*)ptrs[i],
          (unsigned long long)(vts[i] - g_base),
          same == 1 ? "   <== einmalig" : "");
    }
    if (!n) L("   keine gefunden");
}

// Liegt ein ReplicationGraph am Treiber? Und wenn ja: seine VTable im
// interessanten Bereich auflisten. Den Slot fuer ServerReplicateActors lese
// ich aus dieser Liste ab, statt ihn zu berechnen -- zwei Anhaltspunkte deuten
// auf 0x2F0 (SetRepDriverWorld 0x268 aus UNetDriver::SetWorld, PostTickDispatch
// 0x2F8 aus UNetDriver::PostTickDispatch), aber geraten wird hier nicht mehr.
static void DumpRepDriver() {
    if (!g_drv) return;
    void* cls = SafePtr((uint8_t*)g_drv + OFF::UNetDriver_RepDriverClass);
    void* rd  = SafePtr((uint8_t*)g_drv + OFF::UNetDriver_RepDriver);
    L("[rd] ReplicationDriverClass(+0x180) = %p", cls);
    L("[rd] ReplicationDriver     (+0x708) = %p", rd);
    if (!cls) {
        L("[rd] -> Klasse nicht geladen. Die Engine.ini-Sektionen fehlen oder");
        L("[rd]    wurden vom Spiel beim letzten Beenden wieder entfernt.");
        return;
    }
    if (!rd) { L("[rd] -> Klasse da, aber kein Objekt erzeugt."); return; }
    void** vt = (void**)SafePtr(rd);
    if (!vt || !InModule(vt)) { L("[rd] VTable unbrauchbar"); return; }
    L("[rd] VTable %p (base+0x%llX), Bereich 0x250-0x320:",
      vt, (unsigned long long)((uintptr_t)vt - g_base));
    for (uintptr_t off = 0x250; off <= 0x320; off += 8) {
        void* f = SafePtr((uint8_t*)vt + off);
        if (!f || !InModule(f)) { L("   +0x%03llX  (leer/ausserhalb)", (unsigned long long)off); continue; }
        uint8_t b[4]; SafeCopy(f, b, 4);
        L("   +0x%03llX -> base+0x%08llX   erste Bytes %02X %02X %02X %02X",
          (unsigned long long)off, (unsigned long long)((uintptr_t)f - g_base),
          b[0], b[1], b[2], b[3]);
    }
}

static void DiffDriver(const char* phase) {
    if (!g_drv) return;
    uint8_t cur[sizeof(g_snap)];
    if (!SafeCopy(g_drv, cur, sizeof(cur))) { L("[diff] Treiber unlesbar"); return; }
    if (!g_snapValid) { memcpy(g_snap, cur, sizeof(cur)); g_snapValid = true;
                        L("[diff] %s: Ausgangsbild genommen", phase);
                        DumpObjectPointers(g_drv, sizeof(cur));
                        DumpRepDriver();
                        return; }
    int shown = 0;
    L("[diff] %s:", phase);
    for (size_t o = 0; o + 4 <= sizeof(cur); o += 4) {
        int32_t a = *(int32_t*)(g_snap + o), b = *(int32_t*)(cur + o);
        if (a == b) continue;
        if (++shown > 30) { L("   ... weitere ausgelassen"); break; }
        L("   +0x%03zX  %11d -> %11d   (%+d)", o, a, b, b - a);
    }
    if (!shown) L("   keine Aenderung");
    memcpy(g_snap, cur, sizeof(cur));
}

// ===========================================================================
//  NACHREGISTRIERUNG DER WELT  (v26)
//
//  Befund: der Client oeffnet nur 4 Kanaele (Log beim Trennen:
//  "UNetConnection::Close: ... Channels: 4"). Das sind Kontrollkanal plus die
//  Actors, die der Verbindung selbst gehoeren -- PlayerController, PlayerState,
//  Pawn. Kein GameState, keine Weltobjekte. Genau deshalb fehlt clientseitig
//  "Match State Changed", der Ping steht auf Weltzeit 0, und das Ladewidget
//  laeuft in seinen 240-s-Timeout.
//
//  Verdacht: UNetDriver::SetWorld baut die NetworkObjectList aus den Actors,
//  die es in dem Moment gibt (AddInitialObjects). Wir rufen SetWorld an der
//  LoadMap-Stelle -- also BEVOR UWorld::InitializeActorsForPlay laeuft und die
//  Weltobjekte ueberhaupt existieren. Beim Original ist die Reihenfolge
//  identisch, aber dort haengt danach noch die ganze Registrierungsmaschinerie
//  dran, die wir moeglicherweise nicht vollstaendig treffen.
//
//  Gegenprobe ohne Risiko: SetWorld ein zweites Mal aufrufen, wenn die Welt
//  fertig ist. Die Funktion setzt die Liste zurueck und baut sie aus ALLEN
//  vorhandenen Actors neu auf. Zu diesem Zeitpunkt ist noch kein Client
//  verbunden, es kann also nichts unterbrochen werden.
//
//  Wirkt es: der Client bekommt mehr als 4 Kanaele und das Spiel startet.
//  Wirkt es nicht: die Liste war nicht das Problem, dann ist die Relevanz-
//  pruefung dran. Beides sagt uns die Kanalzahl beim naechsten Trennen.
// ===========================================================================
static void ReRegisterWorld(const char* when) {
    if (!g_world || !g_drv) return;
    auto setWorld = (tSetWorld)(g_base + RVA::UNetDriver_SetWorld);
    setWorld(g_drv, g_world);

    // SetWorld fasst World->NetDriver und die LevelCollections nicht an --
    // die setzen wir sicherheitshalber nach.
    int32_t lcNum = SafeI32((uint8_t*)g_world + OFF::UWorld_LevelCollections + 8);
    // Bei aktiver Maske MUSS der Treiber danach wieder verschwinden, sonst
    // sieht die Spiellogik ab hier wieder NM_Client und das Loot bleibt weg.
    SetWorldDriver(g_maskActive ? nullptr : g_drv);

    L("[re] %s: SetWorld erneut gerufen, World->NetDriver=%p, %d Collections%s",
      when, SafePtr((uint8_t*)g_world + OFF::UWorld_NetDriver), lcNum,
      g_maskActive ? "  (Maske aktiv -> absichtlich null)" : "");
}

enum : uint32_t { FUNC_Net = 0x00000040, FUNC_NetMulticast = 0x00004000,
                  FUNC_NetClient = 0x01000000, FUNC_NetServer = 0x00200000 };
// *** CORRECTED IN v45 -- these were inverted from v37 up to v44. ***
// Read straight out of AActor::GetFunctionCallspace (base+0x3F8CF60):
//   03F8D0D9  mov eax,2      <- FUNC_Static / no world / Standalone+Authority
//   03F8D330  mov eax,1      <- FUNC_NetRequest (FunctionFlags bit 8)
//   03F8D1A2  mov eax,r14d   <- r14 = 2 if Role>=Authority else 0 (Absorbed)
//   03F8D18A  mov eax,3      <- NetMulticast on a server, RemoteRole != None
// The 3 proves these are BIT FLAGS and that Local is 2, not 1: a multicast
// has to run locally AND go out, i.e. Local|Remote == 3.
// Because they were swapped, "r | CS_Remote" OR-ed LOCAL onto the result for
// all of v37..v44 -- the RPC switch never sent a single function to the
// client, it only forced local execution. That is why the camera stayed
// broken in v41 even though the switch reported itself as active.
enum : int      { CS_Absorbed = 0, CS_Remote = 1, CS_Local = 2 };

typedef int (__fastcall* tCallspace)(void* actor, void* fn, void* stack);

// ---- v39: name the routed RPCs -----------------------------------------
// Switch -rpclog turns this on. It resolves the UFunction name via the
// engine's own FName::ToString and logs each DISTINCT client/multicast RPC
// once, so we can see whether ClientRestart / ClientRetryClientRestart is
// actually routed to the client. Everything runs under SEH; a bad guess can
// therefore never crash the game, it just yields no name.
typedef void (__fastcall* tFNameToString)(void* fnameptr, void* fstringOut);
typedef void (__fastcall* tFree)(void* ptr);
static bool  g_rpcLog = false;
// This build has a shifted UObject layout, so the NamePrivate offset is not
// the stock 0x18. We probe a handful at runtime and cache the first that
// yields printable text.
static int   g_nameOff = -1;
static const int g_nameOffTry[] = { 0x18, 0x10, 0x20, 0x28, 0x0C, 0x30 };

static tCallspace g_origCallspace = nullptr;
// v45: ON by default again -- and this time it actually works, because the
// Local/Remote constants above were corrected. With the actor net mode hook
// off (the v45 default) the host keeps its standalone behaviour and this
// switch is what additionally sends client/multicast RPCs to the real client.
static bool       g_rpcFix = true;
static bool       g_noRpcSwitch = false;   // -norpc was given explicitly
static bool       g_rpcRemoteOnly = false;   // -remoteonly
static volatile LONG g_rpcClient = 0, g_rpcMulti = 0, g_rpcSeen = 0;

// weiter unten definiert (bei FindPlayerStart bzw. der VTable-Messung)
static void* g_lastGoodStart = nullptr;
static void  DumpActorVTable(void* actor);
// v47: blue zone replication (defined further down, next to ResolveFuncName)
static void          ZoneTick();
static void          LootSweep();          // v58e, defined with the loot block
static const char*   RoleName(uint8_t r);
static bool          g_zoneFix   = true;      // -nozonefix
static void*         g_blueZone  = nullptr;
static uintptr_t g_zoneRefOff = 0;   // v106: GameState slot holding the zone reference
// v111: PROVEN by the [jo] log of 13.09. On a bad join the GameState channel
// opened 51 SECONDS before the BlueZone channel, with the zone reference still
// unresolvable; on a good join the zone channel had opened 0.4 s earlier and
// the reference read back as set. That is the object-reference race, measured.
// The zone reference lives at GameState+0x8D0 in this build.
static volatile bool g_zoneChannelJustOpened = false;
static volatile LONG g_zoneChannelOpens = 0;   // v118
static volatile LONG g_zoneResendN = 0;
static volatile LONG g_zoneKicks = 0;
// v58: zone class made always-relevant BEFORE the replication graph snapshots
// the per-class settings (see PrepareZoneClassBeforeListen / MyAddNetworkActor)
static bool          g_zoneRelevant  = true;   // -nozonerelevant
static bool          g_zoneCdoPatched = false; // CDO patched before InitListen
static volatile LONG g_zoneAddFixes  = 0;      // instances fixed up in AddNetworkActor
static volatile LONG g_addActorCalls = 0;
static bool          g_dormWake = true;        // v96, -nodormwake
static bool          g_pills    = true;        // v104, -nopills (pill block is further down)
// v109: with 16 players the host crashes often. v96 wakes ~3700 level actors,
// and every one of them is now permanently awake against 16 connections instead
// of 1 -- that is the single biggest load change made today. The histogram says
// WHAT is being woken; -wakedoors narrows it to the classes that are actually
// interactive, so scenery (fences and the like) stops costing replication.
static bool          g_wakeDoorsOnly = false;    // -wakedoors
static void*         g_wakeCls[64];
static volatile LONG g_wakeClsN[64];
static int           g_wakeClsCount = 0;
static CRITICAL_SECTION g_wakeCs;
static bool          g_wakeCsReady = false;

static void WakeTally(void* cls) {
    if (!cls || !g_wakeCsReady) return;
    EnterCriticalSection(&g_wakeCs);
    for (int i = 0; i < g_wakeClsCount; ++i)
        if (g_wakeCls[i] == cls) { ++g_wakeClsN[i]; LeaveCriticalSection(&g_wakeCs); return; }
    if (g_wakeClsCount < 64) { g_wakeCls[g_wakeClsCount] = cls; g_wakeClsN[g_wakeClsCount] = 1; ++g_wakeClsCount; }
    LeaveCriticalSection(&g_wakeCs);
}
static volatile LONG g_dormWoken = 0;

// v97: doors and windows work now, but two things are still off -- a window
// breaks silently, and walking through a kicked-open door is rough. Both look
// like the same family: the ACTOR replicates, but a one-shot event on it does
// not arrive. To see which RPCs actually fire on these actors we need a cheap
// filter, and v96 already identifies them for us: remember the CLASS pointer
// of every door/window/fence we wake, then ProcessEvent only has to compare a
// pointer against a short table.
static void* g_dwCls[48];
static int   g_dwClsN = 0;
static int   g_dwDoorN = 0;    // v99: doors alone must not fill the table
static void* g_dwSeen[256];
static int   g_dwSeenN = 0;

static bool IsDoorWindowClass(void* cls) {
    if (!cls) return false;
    for (int i = 0; i < g_dwClsN; ++i) if (g_dwCls[i] == cls) return true;
    return false;
}

// Called once per class (not once per actor) from the wake path.
static void NoteDoorWindowClass(void* cls, const wchar_t* name) {
    if (!cls || !name) return;
    if (g_dwClsN >= 48) return;
    const bool isDoorName = (wcsstr(name, L"Door") != nullptr);
    const bool isWindow   = wcsstr(name, L"Window") || wcsstr(name, L"Glass")
                         || wcsstr(name, L"Break")  || wcsstr(name, L"Destruct")
                         || wcsstr(name, L"Shatter");
    if (!isDoorName && !isWindow) return;
    // v99: in v98 the table held 16 entries and every one of them was a door
    // FRAME, so not a single window class was ever watched -- the window test
    // could not have shown up no matter what Alen did. Doors get a budget of
    // their own so windows always have room.
    if (isDoorName && !isWindow && g_dwDoorN >= 16) return;
    for (int i = 0; i < g_dwClsN; ++i) if (g_dwCls[i] == cls) return;
    g_dwCls[g_dwClsN++] = cls;
    if (isDoorName && !isWindow) ++g_dwDoorN;
    L("[dr] Klasse %ls wird ab jetzt auf RPCs beobachtet (#%d, %s)",
      name, g_dwClsN, (isDoorName && !isWindow) ? "Tuer" : "Fenster/Zerbrechliches");
}

// v99: and it must not depend on the DORM_Initial wake either -- a window that
// is already awake never reached that code path. Look at every class the driver
// registers, once per class.
static bool ClassNameOf(void* obj, wchar_t* out, int cap);   // v99, defined further down

static void ScanClassForDoorWindow(void* actor) {
    if (!actor) return;
    void* acls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
    if (!acls) return;
    for (int i = 0; i < g_dwSeenN; ++i) if (g_dwSeen[i] == acls) return;
    if (g_dwSeenN >= 256) return;
    g_dwSeen[g_dwSeenN++] = acls;
    wchar_t an[128] = L"?";
    if (ClassNameOf(actor, an, 128)) NoteDoorWindowClass(acls, an);
}

// v96: 56 distinct actors are spatialised while the driver registers 1722.
// The gap is the whole issue, so name both sides. This is the registration
// side: a class histogram of everything UNetDriver::AddNetworkActor sees,
// kept by class POINTER (cheap -- this runs thousands of times) and resolved
// to names only when it is printed.
static void*         g_anCls[96];
static volatile LONG g_anClsN[96];
static int           g_anClsCount = 0;
static CRITICAL_SECTION g_anCs;
static bool          g_anCsReady = false;

static void AddNetTally(void* actor) {
    if (!actor || !g_anCsReady) return;
    void* cls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
    if (!cls) return;
    EnterCriticalSection(&g_anCs);
    for (int i = 0; i < g_anClsCount; ++i)
        if (g_anCls[i] == cls) { ++g_anClsN[i]; LeaveCriticalSection(&g_anCs); return; }
    if (g_anClsCount < 96) { g_anCls[g_anClsCount] = cls; g_anClsN[g_anClsCount] = 1; ++g_anClsCount; }
    LeaveCriticalSection(&g_anCs);
}
static void*         g_zoneInfoPtr   = nullptr;   // graph info of the zone actor (v58c self-heal)
static volatile LONG g_zoneReadds    = 0;
static volatile LONG g_flushCalls = 0, g_forceCalls = 0, g_flushWindow = 0;   // v58c dormancy hooks
static bool          g_dormFix = true;            // -nodormfix
static bool          g_subScale = true;           // -nosubscale (v58d: sub-point streaming scale 0.5 -> 1.0)
// v58s: who is the PRIMARY streaming point. In a standalone match there is
// exactly one point and it is the player on the ground -- which is why
// standalone has loot and the listen server does not. Remote players first,
// host behind them, and no host point at all while he is in the aircraft.
static bool          g_remoteFirst = true;        // -nohostfirst restores the old order
static bool          g_hostPoint   = false;       // -hostpoint: always keep the host's own point
static volatile LONG g_subScaleWrites = 0;
static bool ZoneGraphCullToZero(void* drv, void* actor, const char* when);   // defined with the v58 zone block
static void ZoneGraphSelfHeal();
static void* FirstRemoteController();   // v55 block
// v58: aircraft boarding RPC (see MyActorGetNetMode)
static bool          g_aircraftFix   = true;   // -noaircraftfix
static volatile LONG g_aircraftSubst = 0;
// v58 (re-done v56): floor loot around remote pawns (see the LOOT block)
static bool          g_spawnRemote   = true;   // -nospawnremote
static volatile LONG g_spawnChecks   = 0, g_spawnCalls = 0;
static volatile LONG g_spawnBuilding = 0, g_spawnVehicle = 0, g_spawnFlagForced = 0, g_spawnGuardFlips = 0;
static bool          g_stateForce = false;         // v58p: off by default, -stateforce turns it back on
static volatile LONG g_spawnPhaseForced = 0, g_spawnBeforePhase = 0, g_spawnLobbySkipped = 0;
// v58p: the match phase values, read off the live game: 1 = waiting lobby,
// 2 and 3 = the countdown right before the drop, 4 = match running (aircraft
// up). The v58m log settled what the gate is worth: the phase went to 4 at
// t=84 s while the host was still at z=74800, i.e. long before anyone lands.
// So the game's own gate is already open by the time the first player hits
// the ground -- lifting it buys nothing and only ever produced loot lying
// around in the lobby. Nothing is spawned below the game's own value now.
static const uint8_t kPhaseMatchRunning = 4;
static void* GameStateOf(void* actor);   // defined further down, with the loot helpers

// Is the match far enough along that spawning loot is right? Reports the
// phase it saw, so the caller can lift a 3 to a 4 without reading it twice.
static bool MatchStarted(void* actorInWorld, uint8_t* phaseOut) {
    uint8_t* gs = (uint8_t*)GameStateOf(actorInWorld);
    uint8_t ph = 0;
    if (!gs || !SafeCopy(gs + RVA::GameState_Phase, &ph, 1)) { if (phaseOut) *phaseOut = 0; return false; }
    if (phaseOut) *phaseOut = ph;
    return ph >= kPhaseMatchRunning;
}
static volatile LONG g_sweepBuildings = 0, g_sweepNear = 0, g_sweepCalls = 0;   // v58e level sweep
// v58f: BeginPlay net-mode gate of the spawn timers (see RVA::BuildingBeginPlay_GetNetModeRet)
static bool          g_beginPlayFix  = true;   // -nobeginplayfix
static volatile LONG g_bpGateCalls = 0, g_bpGateForced = 0;
static volatile LONG g_spawnNearHost = 0;      // hooked building checks within 400 m of the host
static volatile LONG g_sweepTimer = 0, g_sweepNoTimer = 0, g_sweepUnreg = 0, g_spawnListGrew = 0;
static float         g_lastHostLoc[3] = { 0.f, 0.f, 0.f };
static void HookGraphRemove();                            // v58e diag, defined with the zone block
static void HookSpatialMove();                            // v69, the host-crash guard
static void LogPipeline();                                // v58j
static void* RealFirstPlayerController();                 // v58n, the HOST's PC
// v58n: which gate does the GAME's own building check fail, and where does the
// wave of pickups at the host's touchdown actually come from?
static volatile LONG g_gPhase = 0, g_gNoPc = 0, g_gNoPawn = 0, g_gFar = 0, g_gFlag = 0, g_gPass = 0;
static bool      g_lootRet = true;            // -nolootret
static void*     g_pickupClass = nullptr;
static uintptr_t g_lootRets[12] = { 0 };
static LONG      g_lootRetHits[12] = { 0 };
static int       g_lootRetN = 0;
static void      LogLootPaths();
static void TallyClass(void* cls);                        // v58g census, defined with the viewer block
static void TallyClassHost(void* cls);
static void LogTally(int radius);
static volatile LONG g_censusActors = 0;
// v58g: the viewpoint the replication graph uses per client connection
static volatile LONG g_viewCalls = 0, g_viewFixes = 0, g_viewRemote = 0;
static float         g_lastViewLoc[3] = { 0.f, 0.f, 0.f };
// v42: actor net mode hook state (used by the watchdog, defined further down)
typedef int (__fastcall* tGetNetMode)(void* actor);
static tGetNetMode   g_origGetNetMode = nullptr;
// v46: ON again, but NARROW -- only actors with RemoteRole ==
// ROLE_AutonomousProxy get NM_ListenServer (see MyActorGetNetMode). The wide
// version of v42..v44 broke the host's floor loot; switching it off entirely
// in v45 brought the loot back but left the remote player rubber-banding,
// because AController::IsLocalController short-circuits to TRUE under
// NM_Standalone and the host then ran the local-input movement path for the
// client's pawn.  -noactormode disables it (v45 behaviour).
static bool          g_actorMode      = true;      // -actormode / -noactormode
static volatile LONG g_nmCalls = 0, g_nmSubst = 0;
// v44: world-origin pinning state (used by the watchdog, defined further down)
static bool          g_noRebase      = true;       // -allowrebase
static volatile LONG g_rebaseBlocked = 0;
// v55: remote-player streaming state (used by the watchdog, defined further down)
static bool          g_streamRemote  = true;       // -nostreamremote
static volatile LONG g_streamCalls = 0, g_streamExtra = 0;
static float         g_lastRemoteLoc[3] = { 0.f, 0.f, 0.f };
static int           g_remoteCount = 0;            // registered remote pawns

static DWORD WINAPI WatchdogThread(LPVOID) {
    int ticks = 0, logged = 0;
    bool reRegistered = false;
    void* last = (void*)1;
    while (ticks < 3600) {
        Sleep(500); ++ticks;
        if (!g_world || !g_drv) continue;

        // Nach 5 s ist InitializeActorsForPlay durch und alle Weltobjekte
        // existieren -- jetzt die NetworkObjectList neu aufbauen.
        if (!reRegistered && ticks >= 10) {
            reRegistered = true; ReRegisterWorld("t=5s");
            // Einmalig: VTable eines echten Actors ausschreiben (siehe MESSUNG oben)
            DumpActorVTable(g_lastGoodStart);
        }

        if ((ticks % 10) == 0) {                             // alle 5 s
            char phase[64];
            sprintf_s(phase, "t=%.0fs", ticks*0.5);
            DiffDriver(phase);
        }
        if (g_origTickFlush && (ticks % 20) == 0) {          // alle 10 s
            static LONG prev = 0;
            LONG now = g_tfCount;
            L("[tf] t=%.0fs  TickFlush gesamt %ld, letzte 10 s: %ld  (~%.1f/s)",
              ticks*0.5, now, now - prev, (now - prev) / 10.0);
            prev = now;
        }
        void* cur = SafePtr((uint8_t*)g_world + OFF::UWorld_NetDriver);
        if (cur != last && ++logged <= 20) {
            L("[wd] t=%.1fs  World->NetDriver = %p%s", ticks*0.5, cur,
              (g_maskActive && cur == nullptr) ? "  (Maske: so soll es sein)" : "");
            last = cur;
        }
        if (g_origCallspace && (ticks % 20) == 0) {
            static LONG pc = 0, pm = 0;
            LONG c = g_rpcClient, m = g_rpcMulti;
            L("[rpc] t=%.0fs  zusaetzlich verschickt -- Client-RPCs: %ld (+%ld), Multicasts: %ld (+%ld), Netzfunktionen gesamt %ld",
              ticks*0.5, c, c-pc, m, m-pm, g_rpcSeen);
            pc = c; pm = m;
        }
        if (g_maskActive && (ticks % 20) == 0) {
            static LONG prevOff = 0; LONG now = g_maskOffCnt;
            L("[nm] t=%.0fs  Maske aktiv, Einblendungen letzte 10 s: %ld, Tiefe %ld",
              ticks*0.5, now - prevOff, g_maskDepth);
            prevOff = now;
        }
        // (v48: ZoneTick moved to MyTickFlush -- game thread. Only reporting here.)
        if (g_zoneFix && (ticks % 40) == 0 && g_blueZone) {    // every 20 s
            L("[bz] t=%.0fs  ForceNetUpdate auf BlueZone %p: %ld Aufrufe bisher | Zonen-Kanal geoeffnet: %ld x (v118)",
              ticks*0.5, g_blueZone, g_zoneKicks, g_zoneChannelOpens);
            L("[gd] t=%.0fs  Actor-Schleife des Graphen: %ld Durchlaeufe, davon %ld tote Actors abgefangen",
              ticks*0.5, g_graphGuardSeen, g_graphGuardHits);
        }
        if (g_noRebase && (ticks % 40) == 0) {                // every 20 s
            int32_t o[3] = { 0, 0, 0 };
            SafeCopy((uint8_t*)g_world + OFF::UWorld_OriginLocation, o, sizeof(o));
            L("[or] t=%.0fs  World-Origin {%d, %d, %d}, blockierte Anfragen: %ld",
              ticks*0.5, o[0], o[1], o[2], g_rebaseBlocked);
        }
        if (g_streamRemote && (ticks % 40) == 0) {            // every 20 s
            static LONG prevC = 0, prevE = 0;
            LONG c = g_streamCalls, e = g_streamExtra;
            L("[ws] t=%.0fs  Streaming-Updates %ld (+%ld), Remote-Anker angehaengt %ld (+%ld), registriert %d, letzter Anker {%.0f, %.0f, %.0f}",
              ticks*0.5, c, c - prevC, e, e - prevE, g_remoteCount,
              g_lastRemoteLoc[0], g_lastRemoteLoc[1], g_lastRemoteLoc[2]);
            prevC = c; prevE = e;
        }
        if ((ticks % 40) == 0) {                              // every 20 s (v58)
            L("[ls] t=%.0fs  Spawn-Pruefungen gesamt %ld (davon nahe Host %ld), fuer Remote-Pawns %ld (Gebaeude %ld, Fahrzeuge %ld, Flag erzwungen %ld, Phase angehoben %ld, davon Loot VOR Phase 4 %ld, in der Lobby uebersprungen %ld, Timer geloescht %ld, GESPAWNT %ld) | [sw] Sweeps %ld, Gebaeude im Walk %ld, nahe Remote %ld (Timer %ld / ohne %ld / unreg. %ld) | [bp] BeginPlay-Gate %ld, erzwungen %ld | [ac] DoInAircraft->Dedicated %ld | [bz] AddNetworkActor %ld (Remove: Treiber %ld / Graph %ld), Zonen-Instanz-Fixes %ld, CDO-Patch %s, Re-Adds %ld | [dm] FlushNetDormancy %ld (im Fenster %ld), ForceNetUpdate %ld | [vw] FNetViewer %ld (Remote %ld, korrigiert %ld), letzter Blickpunkt {%.0f, %.0f, %.0f} | [dw] DORM_Initial -> Awake %ld | [cn] Actors um den Remote-Spieler %ld",
              ticks*0.5, g_spawnCalls, g_spawnNearHost, g_spawnChecks, g_spawnBuilding, g_spawnVehicle, g_spawnFlagForced, g_spawnPhaseForced, g_spawnBeforePhase, g_spawnLobbySkipped, g_spawnGuardFlips, g_spawnListGrew,
              g_sweepCalls, g_sweepBuildings, g_sweepNear, g_sweepTimer, g_sweepNoTimer, g_sweepUnreg,
              g_bpGateCalls, g_bpGateForced,
              g_aircraftSubst, g_addActorCalls, g_removeDrv, g_removeGraph, g_zoneAddFixes, g_zoneCdoPatched ? "ja" : "nein", g_zoneReadds,
              g_flushCalls, g_flushWindow, g_forceCalls,
              g_viewCalls, g_viewRemote, g_viewFixes,
              g_lastViewLoc[0], g_lastViewLoc[1], g_lastViewLoc[2], g_dormWoken, g_censusActors);
            if (void* rpc = FirstRemoteController()) {
                uint8_t fl = 0; SafeCopy((uint8_t*)rpc + 0x1928, &fl, 1);
                L("[ls] t=%.0fs  Remote-PC %p: InBoundLevelsLoaded-Flag (+0x1928) = %d", ticks*0.5, rpc, fl);
                // v101: #16. Alen's description is exact -- the perk NOTIFICATION
                // arrives in the aircraft (so the RPC lands), but the UI only shows
                // the perk once he jumps out. The notification is an RPC; the UI
                // reads the perk list off the PlayerState, which is replicated
                // STATE. So the RPC gets through and the state does not, until
                // something at the jump forces an update.
                //
                // The obvious suspect is dormancy, and we have history there: if
                // the PlayerState sits at DORM_DormantAll, every perk change calls
                // FlushNetDormancy, which the v62 crash guard refuses for exactly
                // Dormancy 2 and 3 -- so the update never goes out. Watch its
                // dormancy and relevance, and log only when they change.
                void* ps = SafePtr((uint8_t*)rpc + OFF::AController_PlayerState);
                if (ps) {
                    uint8_t d = 0, ar = 0, rp = 0;
                    SafeCopy((uint8_t*)ps + OFF::AActor_NetDormancy, &d, 1);
                    SafeCopy((uint8_t*)ps + OFF::AActor_bAlwaysRelevant, &ar, 1);
                    SafeCopy((uint8_t*)ps + 0x289, &rp, 1);
                    static void*   prevPs = nullptr;
                    static uint8_t prevD = 0xFF, prevAr = 0xFF;
                    if (ps != prevPs || d != prevD || (ar & 1) != (prevAr & 1)) {
                        prevPs = ps; prevD = d; prevAr = ar;
                        L("[pk] t=%.0fs  PlayerState %p: NetDormancy %d (1=Awake, 2=DormantAll,"
                          " 4=Initial), immer-relevant %d, repliziert %d",
                          ticks*0.5, ps, d, ar & 1, rp & 1);
                        if (d == 2 || d == 3)
                            L("[pk] *** Die PlayerState SCHLAEFT. Jede Perk-Aenderung ruft"
                              " FlushNetDormancy, und die wird fuer Dormancy 2/3 vom"
                              " v62-Crashschutz abgewiesen -- damit geht der neue Perk-Stand"
                              " nie raus. Genau das passt zum Symptom. ***");
                    }
                }
            }
        }
        // v58l: the match phase and the two "levels loaded" flags, every half
        // second, and immediately whenever the phase changes. This is the line
        // that says WHEN the map's loot is allowed to exist at all.
        if (g_world) {
            static int prevPhase = -1;
            static LONG phaseTick = 0;
            uint8_t* gs = (uint8_t*)SafePtr((uint8_t*)g_world + OFF::UWorld_GameState);
            uint8_t ph = 0;
            if (gs && SafeCopy(gs + RVA::GameState_Phase, &ph, 1)) {
                const bool changed = ((int)ph != prevPhase);
                if (changed || (++phaseTick % 40) == 0) {
                    void*   rpc = FirstRemoteController();
                    void*   hpc = RealFirstPlayerController();
                    uint8_t rf = 0, hf = 0;
                    if (rpc) SafeCopy((uint8_t*)rpc + 0x1928, &rf, 1);
                    if (hpc) SafeCopy((uint8_t*)hpc + 0x1928, &hf, 1);
                    L("[gs] t=%.0fs  Match-Phase = %d%s | HOST-PC %p Flag 0x1928=%d, z=%.0f | Remote-PC %p Flag=%d, z=%.0f | Phase angehoben %ld | Spiel-Check scheitert an: Phase %ld, kein PC %ld, kein Pawn %ld, zu weit %ld, Flag %ld -- bestanden %ld",
                      ticks*0.5, ph, changed ? "  *** GEWECHSELT ***" : "",
                      hpc, hf, g_lastHostLoc[2], rpc, rf, g_lastRemoteLoc[2], g_spawnPhaseForced,
                      g_gPhase, g_gNoPc, g_gNoPawn, g_gFar, g_gFlag, g_gPass);
                    prevPhase = ph;
                }
            }
        }
        if ((ticks % 40) == 0) { LogPipeline(); LogLootPaths(); }   // v58j/v58n
        if ((ticks % 40) == 0) {                               // every 20 s (v58i)
            // Is the host simply out of memory? A machine that is thrashing
            // purges streamed levels as fast as it loads them, which would look
            // exactly like the empty world around the remote player.
            PROCESS_MEMORY_COUNTERS pmc; memset(&pmc, 0, sizeof(pmc)); pmc.cb = sizeof(pmc);
            MEMORYSTATUSEX ms; memset(&ms, 0, sizeof(ms)); ms.dwLength = sizeof(ms);
            const bool okP = GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) != 0;
            const bool okM = GlobalMemoryStatusEx(&ms) != 0;
            int32_t nLv = g_world ? SafeI32((uint8_t*)g_world + RVA::UWorld_Levels + 8) : -1;
            L("[mem] t=%.0fs  Spiel belegt %llu MB (Spitze %llu MB), Auslagerung %llu MB | System: %llu von %llu MB frei (%lu%% belegt) | Levels in der Welt: %d",
              ticks*0.5,
              okP ? (unsigned long long)(pmc.WorkingSetSize / (1024*1024)) : 0ULL,
              okP ? (unsigned long long)(pmc.PeakWorkingSetSize / (1024*1024)) : 0ULL,
              okP ? (unsigned long long)(pmc.PagefileUsage / (1024*1024)) : 0ULL,
              okM ? (unsigned long long)(ms.ullAvailPhys / (1024*1024)) : 0ULL,
              okM ? (unsigned long long)(ms.ullTotalPhys / (1024*1024)) : 0ULL,
              okM ? (unsigned long)ms.dwMemoryLoad : 0UL, nLv);
        }
        if (g_origGetNetMode && (ticks % 20) == 0) {          // every 10 s
            static LONG pc = 0, ps = 0;
            LONG c = g_nmCalls, s = g_nmSubst;
            L("[am] t=%.0fs  AActor::GetNetMode: %ld calls (+%ld), %ld answered NM_ListenServer (+%ld)%s",
              ticks*0.5, c, c - pc, s, s - ps, g_actorMode ? "" : "  [-noactormode]");
            pc = c; ps = s;
        }
        // Die Attrappe muss haengen bleiben. Raeumt das Spiel sie weg (Replay
        // beenden, Kartenwechsel), haengen wir sie wieder ein und sagen es.
        if (g_decoy) {
            void* dd = SafePtr((uint8_t*)g_world + OFF::UWorld_DemoNetDriver);
            if (dd != g_decoy) {
                static int reput = 0;
                if (reput < 20)
                    L("[dc] t=%.1fs  DemoNetDriver war %p -> Attrappe wieder eingehaengt",
                      ticks*0.5, dd);
                ++reput;
                SetDemoDecoy(true);
            }
            if ((ticks % 40) == 0) L("[dc] t=%.0fs  Netzmodus: %s", ticks*0.5, NetModeName());
        }
    }
    return 0;
}

// ===========================================================================
//  SPAWNPUNKT FUER NACHZUEGLER
//
//  Befund aus dem Client-Log (06.09.):
//    * beim Login des zweiten Spielers 2x "FindPlayerStart: PATHS NOT DEFINED
//      or NO PLAYERSTART with positive rating"
//    * der Client landet daraufhin bei (0,0,0) -- im Screenshot: Wasser bis zum
//      Horizont, die Insel als halb gestreamte Broecken in weiter Ferne
//    * damit liegt ALLES ausserhalb der Netz-Relevanzreichweite: der GameState
//      kommt nicht an, "Match State Changed" fehlt beim Client komplett, der
//      Ping steht auf 17131901897 (= Weltzeit 0), und der Ladebildschirm loest
//      sich erst per Notbremse auf:
//      "Auto-Destroy Loading Screen with Cheat(0) or Timeout(240 sec)"
//
//  ANSATZ (v24). Zwei Sackgassen vorher, beide protokolliert:
//    v22 gab null zurueck, wenn kein Host-Pawn da war ->
//        "Fatal error: Couldn't spawn player: Failed to find PlayerStart"
//    v23 holte den Pawn ueber SDK-Offsets -> AController::Pawn 0x320 ist falsch,
//        gelesen wurde 0xFFFFFFFF00000000 (zwei gepackte int32) ->
//        EXCEPTION_ACCESS_VIOLATION beim Lesen von 0x158.
//        Der SDK-Dump ist fuer AController unbrauchbar: Member unsortiert,
//        Strukturgroessen wie 0x2d0036a8.
//
//  Deshalb jetzt ohne jeden geratenen Offset: Das Original findet fuer den HOST
//  einen gueltigen Startpunkt (Log: "Original liefert 0000018D9CFFCAE0"), nur
//  fuer Nachzuegler nicht. Wir merken uns den letzten gueltigen Rueckgabewert
//  und reichen ihn weiter, wenn das Original null liefert. Jeder Zeiger wird
//  vorher geprueft (VTable muss ins Modul zeigen).
// ===========================================================================
typedef void* (__fastcall* tFindPlayerStart)(void*, void*, void*);
static tFindPlayerStart g_origFindPlayerStart = nullptr;

// Sieht das nach einem lebenden UObject aus? Erste 8 Byte sind die VTable und
// muessen ins Modul zeigen. Verhindert genau den Absturz von v23, wo
// 0xFFFFFFFF00000000 (zwei gepackte int32) als Actor durchgereicht wurde.
static bool LooksLikeActor(void* a) {
    if (!a || (uintptr_t)a < 0x10000) return false;
    void* vt = SafePtr(a);
    return vt && InModule(vt);
}

// ===========================================================================
//  MESSUNG: AActor-VTable ausschreiben  (v36, aendert nichts am Spiel)
//
//  Warum. Der letzte fehlende Baustein ist AActor::GetFunctionCallspace().
//  Diese Funktion entscheidet fuer jedes RPC "lokal ausfuehren / verschicken /
//  verwerfen". Solange sie NM_Standalone sieht, sagt sie zu allem "lokal" --
//  deshalb kommt ClientRestart nie beim Client an und dessen Kamera haengt an
//  nichts ([UpdateCamera] CameraAssistant3P is detached!!).
//
//  Statisch habe ich sie nicht gefunden: MSVC testet UFunction::FunctionFlags
//  (Offset +0xC8, verifiziert an UNetDriver::ProcessRemoteFunction
//  base+0x43F38E9 "test dword ptr [rdi+0xC8], 0x4000") teils byteweise, teils
//  ueber ein geladenes Register, und beide Suchmuster liefen ins Leere.
//
//  Also andersherum: die Funktion ist virtuell und steht damit in JEDER
//  AActor-VTable. Wir haben zur Laufzeit einen echten Actor -- den zuletzt
//  gefundenen PlayerStart aus dem FindPlayerStart-Haken. Dessen VTable
//  schreiben wir einmal komplett als base+RVA ins Log, dann suche ich den
//  Eintrag offline im Binary heraus. Kein Patch, kein Risiko.
// ===========================================================================
static void DumpActorVTable(void* actor) {
    if (!LooksLikeActor(actor)) { L("[vt] kein brauchbarer Actor -> kein Abzug"); return; }
    void** vt = (void**)SafePtr(actor);
    if (!vt || !InModule(vt)) { L("[vt] VTable unbrauchbar"); return; }
    L("[vt] Actor %p, VTable %p (base+0x%llX) -- Abzug der ersten 260 Slots:",
      actor, (void*)vt, (unsigned long long)((uintptr_t)vt - g_base));
    char line[256]; line[0] = 0;
    int inLine = 0;
    for (int i = 0; i < 260; ++i) {
        void* f = SafePtr(vt + i);
        char cell[40];
        if (f && InModule(f))
            sprintf_s(cell, "%3d:%08llX ", i, (unsigned long long)((uintptr_t)f - g_base));
        else
            sprintf_s(cell, "%3d:-------- ", i);
        strcat_s(line, cell);
        if (++inLine == 6) { L("[vt] %s", line); line[0] = 0; inLine = 0; }
    }
    if (inLine) L("[vt] %s", line);
}


static void* __fastcall MyFindPlayerStart(void* gameMode, void* controller,
                                          void* incomingName) {
    void* r = g_origFindPlayerStart
            ? g_origFindPlayerStart(gameMode, controller, incomingName)
            : nullptr;
    static int n = 0, m = 0;

    if (LooksLikeActor(r)) {                 // Engine wurde selbst fuendig
        g_lastGoodStart = r;
        if (n < 10) { L("[fps] Original liefert %p -> gemerkt", r); ++n; }
        return r;
    }
    if (LooksLikeActor(g_lastGoodStart)) {   // Nachzuegler: letzten guten nehmen
        if (m < 20) { L("[fps] Original liefert %p -> ersetzt durch %p",
                        r, g_lastGoodStart); ++m; }
        return g_lastGoodStart;
    }
    if (m < 20) { L("[fps] Original liefert %p, kein Ersatz vorhanden", r); ++m; }
    return r;                                // nie etwas Erfundenes zurueckgeben
}

// ===========================================================================
//  RPC-WEICHE  (v37)  --  der letzte fehlende Baustein
//
//  BEFUND
//  Mit der Maske sieht die Spiellogik NM_Standalone. Das ist fuer alles
//  richtig, was "nur auf dem Server" laufen soll -- aber
//  AActor::GetFunctionCallspace() liefert bei NM_Standalone zu JEDER Funktion
//  "Local". Der Host fuehrt Client-RPCs also selbst aus, statt sie zu
//  verschicken. Sichtbar am Client: die Kamera haengt an nichts
//  ("[UpdateCamera] CameraAssistant3P is detached!!"), weil ClientRestart
//  nie ankommt.
//
//  DIE FUNKTION
//  base+0x3F8CF60, ueber den VTable-Abzug (Slot 67) gefunden. Inhalt passt:
//      03F8CF8B  test dword ptr [rdx+0xC8], 0x2000   ; FUNC_Static
//      03F8CFAB  call [rax+0x150]                    ; GetWorld()
//      03F8D155  mov rax,[rsi+0x48] ...              ; SuperStruct-Kette hoch
//      03F8D172  bt  eax, 0xE                        ; FUNC_NetMulticast
//      03F8D118  test dword ptr [rsi+0xC8], 0x200000 ; FUNC_NetServer
//
//  DER EINGRIFF -- bewusst NUR ADDITIV
//  Wir rufen das Original und ODERn bei Client- und Multicast-RPCs das
//  Remote-Bit dazu. Das lokale Verhalten bleibt damit exakt wie bisher, es
//  geht nur zusaetzlich etwas raus. Kein Wegnehmen, kein Umschreiben.
//
//  Warum nicht sauber "nur Remote": ob ein Actor einem entfernten oder dem
//  lokalen Spieler gehoert, entscheidet AActor::GetNetConnection() -- eine
//  weitere virtuelle Funktion, deren Slot ich nicht kenne. Wuerde ich pauschal
//  nur Remote zurueckgeben, verloere der HOST seine eigenen Client-RPCs
//  (inklusive seiner eigenen Kamera). "Original ODER Remote" kann das nicht
//  passieren: fuer hosteigene Actors findet ProcessRemoteFunction keine
//  Verbindung, meldet das und verwirft -- genau wie vorher.
//
//  Die Sendeseite ist vorhanden, geprueft: UNetDriver::ProcessRemoteFunction
//  (base+0x43F3660) enthaelt die Schleife ueber ClientConnections
//  (+0x98 Data / +0xA0 Num).
//
//  Abschalten ohne Neubau:  -norpc
// ===========================================================================
// v51: resolve an FName that sits at a KNOWN address (not a UObject's name).
// Same machinery as ResolveFuncName, just without the offset probing.
static bool FNameToStr(void* fnamePtr, wchar_t* out, int cap) {
    auto toStr  = (tFNameToString)(g_base + RVA::FName_ToString);
    auto freeFn = (tFree)(g_base + RVA::FMemory_Free);
    int32_t cmpIdx = SafeI32(fnamePtr);
    if (cmpIdx <= 0 || cmpIdx > 0x2000000) return false;   // implausible index
    FString fs{ nullptr, 0, 0 };
    bool ok = false;
    __try {
        toStr(fnamePtr, &fs);
        if (fs.Data && fs.Num > 1 && fs.Num < 200) {
            int k = 0;
            for (; k < fs.Num - 1 && k < cap - 1; ++k) out[k] = fs.Data[k];
            out[k] = 0;
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (fs.Data) { __try { freeFn(fs.Data); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    return ok;
}

// Try to resolve a UFunction's name into `out` (wide). Returns true on success.
// SEH-guarded end to end; probes candidate NamePrivate offsets until one gives
// printable text, then caches it in g_nameOff.
static bool ResolveFuncName(void* fn, wchar_t* out, int cap) {
    if (!fn || !InModule((void*)SafePtr(fn))) { /* vtable sanity */ }
    auto toStr = (tFNameToString)(g_base + RVA::FName_ToString);
    auto freeFn = (tFree)(g_base + RVA::FMemory_Free);

    // offsets to try: the cached one first, then the candidate list
    int order[8]; int n = 0;
    if (g_nameOff >= 0) order[n++] = g_nameOff;
    for (int i = 0; i < (int)(sizeof(g_nameOffTry)/sizeof(int)); ++i) order[n++] = g_nameOffTry[i];

    for (int i = 0; i < n; ++i) {
        int off = order[i];
        // Pre-check the FName's ComparisonIndex so we don't feed ToString junk.
        int32_t cmpIdx = SafeI32((uint8_t*)fn + off);
        if (cmpIdx <= 0 || cmpIdx > 0x2000000) continue;   // implausible index
        FString fs{ nullptr, 0, 0 };
        bool ok = false;
        __try {
            toStr((uint8_t*)fn + off, &fs);
            if (fs.Data && fs.Num > 1 && fs.Num < 200) {
                wchar_t c0 = fs.Data[0];
                if (c0 > 32 && c0 < 127) {                 // printable ASCII start
                    int k = 0;
                    for (; k < fs.Num - 1 && k < cap - 1; ++k) out[k] = fs.Data[k];
                    out[k] = 0;
                    ok = true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (fs.Data) { __try { freeFn(fs.Data); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        if (ok) { g_nameOff = off; return true; }
    }
    return false;
}

// ===========================================================================
//  BLUE ZONE  (v47)  --  why the joiner has no zone display and permanent storm
//
//  BEFUND
//  The host sets the zone at map load, ~18 s before the joining client's game
//  is even running (host log 17:49:45):
//      InitTableSetting BlueZoneName: LV-OrbIsland_ALPHA_64_05
//      ClientNotifySelectedPlayZoneInfoIndex NewSelectedPlayZoneInfoIndex:3,
//          NewAreaDescKey:Fortress Gate, CQR:cqr_e
//      UpdateAreaDescInfo ... PlayZoneInfosNum:4
//  The client log contains none of that, only "UW-MapImageWidget - Retry Set
//  Map Info" 392 times. Those log lines are NOT in the exe (searched as UTF-16
//  and ANSI) -- they come from Blueprint, so there is nothing to patch there.
//
//  What IS native are the properties. Per the SDK, ABravoHotelBlueZone carries
//      SelectedPlayZoneInfoIndex   with OnRepSelectedPlayZoneInfoIndex
//      AreaDescKey                 with OnRepAreaDescKey
//  and ClientNotifySelectedPlayZoneInfoIndex is a NetMulticast. Both the
//  property names ("SelectedPlayZoneInfoIndex" @0x59F08B8, "AreaDescKey"
//  @0x59F08F0) do exist in the binary as UProperty names.
//
//  So a late joiner does NOT depend on the missed multicast: the moment the
//  BlueZone actor replicates to him, the initial property state arrives and
//  the OnRep functions fire. The client log shows no such OnRep and no
//  BlueZone actor at all, while 145 channels are open otherwise -- so the
//  actor specifically is not being replicated.
//
//  The leading reason is net dormancy. This is a ReplicationGraph title whose
//  graph class (BravoHotelServer.BravoHotelReplicationGraph) is missing from
//  the client build ("ReplicationDriverClass is null"). A ReplicationGraph
//  game lets actors sleep via NetDormancy and relies on the graph to wake
//  them; with no graph, nothing ever does.
//
//  ANSATZ
//  Find the BlueZone actor through UWorld::GameState (+0x1D8) by scanning the
//  GameState for object pointers and resolving each one's class name -- no
//  guessed member offset anywhere. Then call AActor::ForceNetUpdate() on it
//  once a second while a client is connected. ForceNetUpdate flushes net
//  dormancy and marks the actor for immediate replication, which is exactly
//  the wake-up the missing graph would have done.
//
//  Everything is logged the first few times, so if this is NOT enough the next
//  round has the actor pointer, its class name, Role/RemoteRole and the
//  replication flag byte to work from.
//
//  Abschalten ohne Neubau:  -nozonefix
// ===========================================================================
static bool ClassNameOf(void* obj, wchar_t* out, int cap) {
    void* cls = SafePtr((uint8_t*)obj + OFF::UObject_Class);
    if (!cls || !SafePtr(cls)) return false;
    return ResolveFuncName(cls, out, cap);   // resolves any UObject's FName
}

// v118: the Outer chain of an actor as text -- "Level <name> in <world/package>".
// An actor in a streaming sublevel is only gathered for a connection once the
// client has reported that level visible (ClientVisibleLevelNames); an actor
// in PersistentLevel always is. So this one line decides whether the zone can
// ever reach a fresh connection.
static void LevelPathOf(void* actor, wchar_t* out, int cap) {
    out[0] = 0;
    void* lvl = actor ? SafePtr((uint8_t*)actor + 0x28) : nullptr;       // Outer = ULevel
    void* wld = lvl   ? SafePtr((uint8_t*)lvl   + 0x28) : nullptr;       // Outer = UWorld
    wchar_t ln[96] = L"?", wn[96] = L"?";
    if (lvl) FNameToStr((uint8_t*)lvl + OFF::UObject_Name, ln, 96);
    if (wld) FNameToStr((uint8_t*)wld + OFF::UObject_Name, wn, 96);
    swprintf_s(out, cap, L"Level %ls in %ls (Level %p)", ln, wn, lvl);
}

static bool g_zoneScanDone = false;   // v107: the heavy one-time work is finished

static void FindBlueZone() {
    // v107: THIS WAS A RACE IN OUR OWN CODE, and it is the reason the [gz]
    // diagnostic came back empty.
    //
    // g_blueZone is set from TWO places: this GameState scan, and
    // opportunistically in MyAddNetworkActor when the zone actor registers.
    // The old guard was "if (g_blueZone || !g_world) return;", so whenever
    // AddNetworkActor happened to win the race, this function returned on its
    // first line and the GameState was NEVER scanned -- no "[bz] BlueZone
    // gefunden", no zone-reference offset, and none of the work below. Which
    // path wins varies from round to round. That is exactly a
    // sometimes-yes-sometimes-no behaviour, produced by us.
    //
    // Now the scan always runs; only the one-time heavy work is gated.
    if (!g_world) return;
    void* gs = SafePtr((uint8_t*)g_world + OFF::UWorld_GameState);
    if (!gs) return;
    // v107: announce on CHANGE, not once. A lobby GameState being swapped for
    // the match GameState would otherwise be invisible -- and the zone plus the
    // match info both hang off this object.
    static void* lastGs = nullptr;
    if (gs != lastGs) {
        lastGs = gs;
        wchar_t gsn[128];
        if (ClassNameOf(gs, gsn, 128)) L("[bz] GameState %p, Klasse %ls", gs, gsn);
        else                           L("[bz] GameState %p (Klassenname nicht aufloesbar)", gs);
    }
    wchar_t nm[128];
    static int listed = 0;
    for (size_t o = 0; o + 8 <= 0xA00; o += 8) {
        void* p = SafePtr((uint8_t*)gs + o);
        if (!p || (uintptr_t)p < 0x10000) continue;
        void* vt = SafePtr(p);
        if (!vt || !InModule(vt)) continue;          // must look like a UObject
        if (!ClassNameOf(p, nm, 128)) continue;
        if (wcsstr(nm, L"BlueZone") || wcsstr(nm, L"PlayZone")) {
            g_blueZone = p;
            uint8_t role = 0, remote = 0, rep = 0;
            SafeCopy((uint8_t*)p + OFF::AActor_Role,        &role,   1);
            SafeCopy((uint8_t*)p + OFF::AActor_RemoteRole,  &remote, 1);
            SafeCopy((uint8_t*)p + OFF::AActor_bReplicates, &rep,    1);
            g_zoneRefOff = (uintptr_t)o;      // v106: the replicated reference's slot
            if (g_zoneScanDone) { g_blueZone = p; return; }   // v107: offset refreshed, rest already done
            g_zoneScanDone = true;
            L("[bz] *** BlueZone gefunden *** GameState+0x%03llX -> %p  Klasse %ls",
              (unsigned long long)o, p, nm);
            {   // v118
                // (g_remote is declared further down; the pawn's level is
                //  logged from MyAddNetworkActor instead -- see [zl] there)
                wchar_t a[256], b[256];
                LevelPathOf(p, a, 256); LevelPathOf(gs, b, 256);
                L("[zl] Die referenzierte Zone: %ls", a);
                L("[zl] Die GameState:          %ls", b);
            }
            L("[bz]     Role=%s RemoteRole=%s bReplicates=%d (Flagbyte 0x289 = 0x%02X)",
              RoleName(role), RoleName(remote), rep & 1, rep);
            {   // v58: did the instance inherit the CDO flag?
                uint8_t ar = 0, oo = 0;
                SafeCopy((uint8_t*)p + OFF::AActor_bAlwaysRelevant,      &ar, 1);
                SafeCopy((uint8_t*)p + OFF::AActor_bOnlyRelevantToOwner, &oo, 1);
                L("[bz]     bAlwaysRelevant=%d (Flagbyte 0x1CC = 0x%02X)  bOnlyRelevantToOwner=%d  -> %s",
                  ar & 1, ar, oo & 1,
                  (ar & 1) ? "AlwaysRelevant-Knoten, keine Distanzgrenze" : "Gitterknoten, 150 m Klassen-Cull (Fix hat NICHT gegriffen)");
            }
            // ---- v49: measurement only, nothing is written -----------------
            // The zone reaches the joining player only AFTER he lands, which
            // points at distance based net relevancy: the actor sits at the
            // play zone on the island while the pre-match waiting area is
            // somewhere else. AActor::IsNetRelevantFor ends in a distance test
            // against NetCullDistanceSquared, whose engine default is
            // 15000^2 = 225000000.0f. The engine property names are NOT in the
            // binary (searched), so the offset cannot be recovered statically.
            // Instead we look for that default value inside the live object and
            // report every hit. With the offset confirmed we can raise it next
            // round and make the zone relevant everywhere.
            {
                int found = 0;
                for (size_t off = 0x100; off + 4 <= 0x1200; off += 4) {
                    float f = 0.0f;
                    if (!SafeCopy((uint8_t*)p + off, &f, 4)) continue;
                    if (f == 225000000.0f) {
                        L("[bz]     NetCullDistanceSquared-Kandidat bei +0x%03llX (225000000.0)",
                          (unsigned long long)off);
                        if (++found >= 8) break;
                    }
                }
                if (!found) L("[bz]     kein 225000000.0 im Objekt -- Klasse ueberschreibt den Default");
                uint8_t fl[0x20] = {0};
                if (SafeCopy((uint8_t*)p + 0x280, fl, sizeof(fl))) {
                    char line[128]; line[0] = 0;
                    for (int i = 0; i < 0x20; ++i) {
                        char c[8]; sprintf_s(c, "%02X ", fl[i]); strcat_s(line, c);
                    }
                    L("[bz]     Flagbytes 0x280-0x29F: %s", line);
                }
            }
            return;
        }
        if (listed < 30) { L("[bz]   GameState+0x%03llX -> %ls", (unsigned long long)o, nm); ++listed; }
    }
}

// ===========================================================================
//  v50: make the zone actor relevant EVERYWHERE
//
//  With v48 the zone appeared on the joining client after he landed but never
//  while he waited for the match -- and in the very next round it did not
//  appear at all. That "sometimes yes, sometimes no" is the signature of
//  distance based net relevancy: AActor::IsNetRelevantFor ends in a test
//  against NetCullDistanceSquared, so the BlueZone actor only replicates once
//  the client happens to get close enough to it. Where he lands decides it.
//
//  ForceNetUpdate does NOT bypass that test -- it flushes dormancy and
//  schedules an update, then the relevancy filter still runs. The actual lever
//  is the cull distance itself, whose offset the v49 measurement pinned down.
//
//  We only ever overwrite the untouched engine default. If the game has chosen
//  its own value we leave it alone and say so, because in that case the
//  assumption behind the offset would be wrong and writing would be a guess.
// ===========================================================================
static void EnsureZoneRelevance() {
    if (!g_blueZone) return;
    const float kHuge = 1.0e18f;             // sqrt = 1e9 units; the map is ~8 km
    float cull = 0.0f;
    if (!SafeCopy((uint8_t*)g_blueZone + OFF::AActor_NetCullDistanceSquared, &cull, 4)) return;
    if (cull == kHuge) return;               // already ours
    if (cull != 225000000.0f) {
        static int warned = 0;
        if (!warned++)
            L("[bz] NetCullDistanceSquared = %.1f -- nicht der Engine-Default, wird NICHT angefasst", cull);
        return;
    }
    if (!SafeCopy(&kHuge, (uint8_t*)g_blueZone + OFF::AActor_NetCullDistanceSquared, 4)) return;
    static int done = 0;
    if (!done++)
        L("[bz] *** NetCullDistanceSquared +0x2D4: 225000000.0 -> 1e18 -- BlueZone ueberall netzrelevant ***");

    // ---- v58: the net rate fields, READ ONLY ---------------------------------
    // v54 assumed the UE4 declaration order (+0x2DC/+0x2E0/+0x2E4) and wrote
    // 10.0f into +0x2E0. In this build that is the data pointer of a
    // TArray<UObject*> inside AActor; the write bent it and the garbage
    // collector crashed on it ~50 s later (see the v58 header). The real
    // offsets come from AActor's default initialiser at RVA 0x3F92170:
    //     NetUpdateFrequency      +0x2A4   default 100.0f
    //     MinNetUpdateFrequency   +0x218   default 2.0f
    //     NetPriority             +0x298   default 1.0f
    //     NetDormancy             +0x20D   default 1 (DORM_Awake)
    // Nothing is written any more. The values are logged once so the next
    // run shows whether this actor's rate is even the problem.
    {
        static int once = 0;
        if (!once) {
            float freq = 0.0f, minf = 0.0f, prio = 0.0f;
            uint8_t dorm = 0;
            if (SafeCopy((uint8_t*)g_blueZone + 0x2A4, &freq, 4) &&
                SafeCopy((uint8_t*)g_blueZone + 0x218, &minf, 4) &&
                SafeCopy((uint8_t*)g_blueZone + 0x298, &prio, 4) &&
                SafeCopy((uint8_t*)g_blueZone + 0x20D, &dorm, 1)) {
                L("[bz] Netzfelder (echte Offsets): NetUpdateFrequency(+0x2A4)=%.2f  Min(+0x218)=%.2f  NetPriority(+0x298)=%.2f  NetDormancy(+0x20D)=%d  -- nur gelesen, nichts geschrieben",
                  freq, minf, prio, dorm);
                once = 1;
            }
        }
    }
}

// ForceNetUpdate() on the zone actor: flushes dormancy and schedules it for
// the next replication pass. Cheap, and the only lever that does not need a
// guessed member offset.
// ---------------------------------------------------------------------------
//  v106: the blue zone and the match info are sometimes missing in the
//  prematch lobby -- and ALWAYS missing together.
//
//  That correlation is the whole clue. sp_listen finds the zone by scanning
//  GameState+0x00..0xA00 for it (see FindBlueZone), i.e. the client does not
//  discover the zone on its own: it receives it as a REPLICATED OBJECT
//  REFERENCE stored on the GameState, alongside the match info fields. One
//  object, one failure, both symptoms.
//
//  A replicated object reference only resolves on the client if the referenced
//  actor has a channel on that connection AT THE MOMENT the reference arrives.
//  If the GameState replicates its zone pointer before the zone actor has been
//  channelled, the client stores null -- and since that property does not change
//  again, it is never resent. That is exactly a "sometimes yes, sometimes no"
//  race, and it fits both halves of the symptom.
//
//  HYPOTHESIS, not yet proven. The fix is the cheapest possible test: nudge the
//  GameState as well as the zone, so whichever order the race happens to take,
//  the client gets a fresh copy of the reference once the zone is channelled.
//  ForceNetUpdate is an INPUT nudge, not a state-machine write (rule 1), it runs
//  in the same game-thread window the zone kick already uses, and it goes
//  through the same v62-guarded hook. Capped so it cannot run all match.
//
//  [gz] says whether the hypothesis was right: if the zone reference on the
//  GameState is non-null on the host the whole time, the host side is fine and
//  the loss is in delivery -- which is what the nudge targets.
//
//  Off with -nogskick.
// ---------------------------------------------------------------------------
static bool          g_gsKick = true;            // -nogskick
static bool          g_zonePin = true;           // v120, -nozonepin
static bool          g_zoneToggle  = true;        // v122, -nozonetoggle
static int           g_togglePhase = 0;
static void*         g_toggleSaved = nullptr;
static volatile LONG g_zoneToggles = 0;
static const int32_t ZONE_IDX_NONE  = (-2147483647 - 1);  // v123 sentinel
static int32_t       g_zoneIdxSaved = ZONE_IDX_NONE;
static int32_t       g_zoneListSaved = 0;         // v124
static volatile LONG g_zonePinHits = 0;
static volatile LONG g_gsKicks = 0;

static void KickGameState() {
    if (!g_gsKick || !g_world) return;
    if (InterlockedCompareExchange(&g_gsKicks, 0, 0) >= 120) return;   // ~2 min of prematch
    void* gs = SafePtr((uint8_t*)g_world + OFF::UWorld_GameState);
    if (!gs) return;
    void** vt = (void**)SafePtr(gs);
    if (!vt || !InModule(vt)) return;
    void* fn = SafePtr((uint8_t*)vt + OFF::AActor_ForceNetUpdate_VT);
    if (!fn || !InModule(fn)) return;
    __try { ((void (__fastcall*)(void*))fn)(gs); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    LONG n = InterlockedIncrement(&g_gsKicks);
    if (n <= 5 || (n % 30) == 0) {
        // Is the reference actually present on the host right now?
        void* ref = g_zoneRefOff ? SafePtr((uint8_t*)gs + g_zoneRefOff) : nullptr;
        L("[gz] GameState %p angestossen (#%ld) -- Zonen-Referenz bei +0x%03llX = %p %s",
          gs, n, (unsigned long long)g_zoneRefOff, ref,
          (g_zoneRefOff && ref == g_blueZone && ref) ? "(zeigt auf die BlueZone)"
                                                     : (g_zoneRefOff ? "(NICHT die BlueZone)" : "(Offset noch unbekannt)"));
    }
}

static void KickBlueZone() {
    // v107: the log said "ForceNetUpdate auf BlueZone: 0 Aufrufe bisher" for a
    // whole round while ZoneTick was plainly running, so one of these guards
    // was rejecting silently. Say which.
    static int complained = 0;
    if (!g_blueZone) return;
    void** vt = (void**)SafePtr(g_blueZone);
    if (!vt || !InModule(vt)) {
        if (complained++ < 3) L("[bz] KickBlueZone: VTable %p des Zonen-Actors liegt nicht im Modul", vt);
        return;
    }
    void* fn = SafePtr((uint8_t*)vt + OFF::AActor_ForceNetUpdate_VT);
    if (!fn || !InModule(fn)) {
        if (complained++ < 3)
            L("[bz] KickBlueZone: VTable+0x%llX ist kein ForceNetUpdate (%p)",
              (unsigned long long)OFF::AActor_ForceNetUpdate_VT, fn);
        return;
    }
    __try { ((void (__fastcall*)(void*))fn)(g_blueZone); InterlockedIncrement(&g_zoneKicks); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Called from MyTickFlush -- game thread, driver visible. Rate limited to
// roughly once a second (TickFlush runs at ~60 Hz).
static void ZoneTick() {
    if (!g_zoneFix || !g_world || !g_drv) return;
    HookGraphRemove();                       // v58e, once
    HookSpatialMove();                       // v69, once
    if (SafeI32((uint8_t*)g_drv + OFF::UNetDriver_ClientConnNum) <= 0) return;

    // ================= v122: THE ZONE FIX, and why this is the right one ====
    // Every round that worked had the zone channel open within a fraction of a
    // second of the GameState channel; every round that failed had it open tens
    // of seconds later:
    //     14.09. 16:2x  BlueZone 8769046, GameState 8769875  ( 0.8 s)  -> WORKED
    //     14.09. 15:0x  GameState 6185562, BlueZone 6185578  (16 ms )  -> WORKED
    //     14.09. 16:05  GameState 6395671, BlueZone 6461468  (  66 s)  -> BROKEN
    //     14.09. 16:4x  GameState 7408203, BlueZone 7443265  (  35 s)  -> BROKEN
    // That is exactly how an unresolved object reference behaves: the client
    // stores null on arrival, and because the property never CHANGES again it
    // is never sent again. ForceNetUpdate cannot repair it -- it only lifts the
    // send-rate throttle, it does not mark a property dirty. That is why v111
    // fired on every broken round and changed nothing.
    // So make the property genuinely change: write null, let one replication
    // pass go out, write the zone back. Two single-pointer writes to a field we
    // already read every tick, and the null window is ONE frame (~16 ms)
    // because this block sits in front of the once-a-second gate. The client
    // then receives a real delta whose reference resolves, and OnRep_BlueZone
    // fires. Off with -nozonetoggle.
    if (g_zoneToggle && g_zoneRefOff && g_blueZone && g_world) {
        void* gs = SafePtr((uint8_t*)g_world + OFF::UWorld_GameState);
        if (gs) {
            if (g_togglePhase == 1) {                    // frame 2: put it back
                // v123: the index too -- see the note below.
                if (g_zoneIdxSaved != ZONE_IDX_NONE && g_blueZone)
                    SafeCopy(&g_zoneIdxSaved, (uint8_t*)g_blueZone + 0x7D4, 4);
                if (g_zoneListSaved > 0 && g_blueZone) {    // v125: zone actor
                    SafeCopy(&g_zoneListSaved, (uint8_t*)g_blueZone + 0x7F0, 4);
                    g_zoneListSaved = 0;
                }
                // v125: the zone actor's own properties changed, so it must be
                // told to send them, not just the GameState.
                if (g_blueZone) KickBlueZone();
                void* z = g_toggleSaved;
                if (z && SafeCopy(&z, (uint8_t*)gs + g_zoneRefOff, 8)) {
                    void** vt = (void**)SafePtr(gs);
                    void*  fn = vt && InModule(vt) ? SafePtr((uint8_t*)vt + OFF::AActor_ForceNetUpdate_VT) : nullptr;
                    if (fn && InModule(fn)) { __try { ((void (__fastcall*)(void*))fn)(gs); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
                    L("[zt] Zonen-Referenz zurueckgeschrieben (%p), SelectedPlayZoneInfoIndex wieder %d"
                      " -- der Client bekommt jetzt eine ECHTE Aenderung und kann sie aufloesen (#%ld)",
                      z, (int)(g_zoneIdxSaved == ZONE_IDX_NONE ? -999 : g_zoneIdxSaved), g_zoneToggles);
                    g_zoneIdxSaved = ZONE_IDX_NONE;
                }
                g_togglePhase = 0;
                g_toggleSaved = nullptr;
            } else if (g_zoneChannelJustOpened) {        // frame 1: blank it
                g_zoneChannelJustOpened = false;
                void* cur = SafePtr((uint8_t*)gs + g_zoneRefOff);
                if (cur) {
                    g_toggleSaved = cur;
                    void* nul = nullptr;
                    if (SafeCopy(&nul, (uint8_t*)gs + g_zoneRefOff, 8)) {
                        // v123: SelectedPlayZoneInfoIndex (GameState+0x7D4) is
                        // the number that says WHICH circle is active, and the
                        // map draws from it. The only thing that announces it is
                        // ClientNotifySelectedPlayZoneInfoIndex -- and this
                        // round's log caught it firing at t=9943859 with
                        // Callspace 2 (Local), 28 SECONDS before the client's
                        // channels even opened. A Client RPC on the zone actor
                        // cannot route anywhere because the zone has no owning
                        // connection, so a player who joins later is never told
                        // the index by RPC at all; he depends entirely on this
                        // property replicating. Give it the same one-frame
                        // change so it is genuinely resent.
                        // v125: on the ZONE actor, not the GameState.
                        int32_t idx = 0;
                        if (SafeCopy((uint8_t*)g_blueZone + 0x7D4, &idx, 4)) {
                            g_zoneIdxSaved = idx;
                            int32_t bump = idx + 1;
                            SafeCopy(&bump, (uint8_t*)g_blueZone + 0x7D4, 4);
                        }
                        // v124: ClientPlayZonePhaseList (+0x7E8, TArray) is the
                        // one that carries the circle geometry. Shortening it by
                        // one element for a single frame is enough to make the
                        // comparison see a change; the array data itself is
                        // never touched, only the count, and only when there is
                        // more than one element to spare.
                        int32_t ln = 0;
                        if (SafeCopy((uint8_t*)g_blueZone + 0x7F0, &ln, 4) && ln >= 2) {
                            g_zoneListSaved = ln;
                            int32_t shorter = ln - 1;
                            SafeCopy(&shorter, (uint8_t*)g_blueZone + 0x7F0, 4);
                        }
                        g_togglePhase = 1;
                        InterlockedIncrement(&g_zoneToggles);
                        L("[zt] Zonen-Kanal ist offen -> Referenz fuer EINEN Frame auf null gesetzt,"
                          " damit die Eigenschaft wirklich als geaendert gilt (#%ld)", g_zoneToggles);
                    }
                }
            }
        }
    }

    static int frames = 0;
    if (++frames < 60) return;
    frames = 0;
    FindBlueZone();

    // ============ v125: I READ THE WRONG OBJECT, and here is the proof =====
    // v124 read SelectedPlayZoneName / InfoIndex / ClientPlayZonePhaseList off
    // the GameState and got name='?', index=0, list Num=0, and I took that as
    // "the host has no play zone". The host's own game log says otherwise:
    //     InitTableSetting BlueZoneName: LV-OrbIsland_ALPHA_80_01
    //     ClientNotifySelectedPlayZoneInfoIndex NewSelectedPlayZoneInfoIndex:3
    //     UpdateAreaDescInfo ... SelectedPlayZoneInfoIndex:3
    //     PlayZoneInfosNum:4
    // So the selection succeeds. The offsets were simply not GameState offsets.
    // Decoding the whole descriptor cluster around them settles which class owns
    // them -- its neighbours are GetPainCausingComponent, InBlueZone,
    // InBlueZoneCharacters, OutBlueZoneCharacters, SetVisibleBlueZone,
    // SetCharacterLocation. That is the BLUE ZONE ACTOR, not the GameState:
    //     SelectedPlayZoneName      zone+0x7C8  FName
    //     SelectedPlayZoneInfoIndex zone+0x7D4  int32
    //     AreaDescKey               zone+0x7D8  FName
    //     ClientPlayZonePhaseList   zone+0x7E8  TArray (Num +0x7F0, Max +0x7F4)
    // Only the zone REFERENCE lives on the GameState (+0x8D0), and that one was
    // right because FindBlueZone located the zone through it.
    if (g_blueZone) {
        static int ziTick = 0;
        if ((++ziTick % 5) == 1) {
            wchar_t zn[128] = L"?", ak[128] = L"?";
            FNameToStr((uint8_t*)g_blueZone + 0x7C8, zn, 128);
            FNameToStr((uint8_t*)g_blueZone + 0x7D8, ak, 128);
            int32_t idx = -1, listNum = -1, listMax = -1;
            SafeCopy((uint8_t*)g_blueZone + 0x7D4, &idx, 4);
            void* listData = SafePtr((uint8_t*)g_blueZone + 0x7E8);
            SafeCopy((uint8_t*)g_blueZone + 0x7F0, &listNum, 4);
            SafeCopy((uint8_t*)g_blueZone + 0x7F4, &listMax, 4);
            L("[zi] Zone %p: SelectedPlayZoneName='%ls' | InfoIndex=%d | AreaDescKey='%ls' |"
              " ClientPlayZonePhaseList Data=%p Num=%d Max=%d%s",
              g_blueZone, zn, idx, ak, listData, listNum, listMax,
              (listNum <= 0) ? "   <<< LEER -- dann hat der Host wirklich keine Phasenliste >>>" : "");
        }
    }
    // v120: THE FIX. The 14.09. 16:xx round proved the zone channel opens, is
    // torn down, and reopens (three different channel objects, one connection).
    // Each teardown loses the phase multicasts (ClientNotifyPhaseChangedEvent /
    // ...StateChangedEvent, both Callspace 3 = multicast) that only reach a
    // connection whose zone channel is open at that instant -- which is why the
    // map (GameState *properties*) works but the in-world wall and the phase
    // progression do not. An always-relevant world actor's channel is town down
    // when it goes dormant; the game puts world actors to DORM_DormantAll after
    // the initial burst. So pin the zone permanently awake: pure single-byte
    // writes, the exact v96 technique, no engine call. Off with -nozonepin.
    if (g_zonePin && g_blueZone) {
        uint8_t d = 0;
        if (SafeCopy((uint8_t*)g_blueZone + OFF::AActor_NetDormancy, &d, 1) && d > 1) {
            const uint8_t awake = 1;   // DORM_Awake
            SafeCopy(&awake, (uint8_t*)g_blueZone + OFF::AActor_NetDormancy, 1);
            LONG n = InterlockedIncrement(&g_zonePinHits);
            if (n <= 10 || (n % 50) == 0)
                L("[zp] Zone %p war NetDormancy=%d -> auf DORM_Awake zurueckgeschrieben (#%ld),"
                  " damit der Kanal nicht schliesst", g_blueZone, d, n);
        }
    }
    // v121: CHANNEL CENSUS. The 14.09. log showed the same actor getting a
    // channel again and again with a different channel object each time -- the
    // blue zone three times, one M416 six times, and one channel object
    // (0x...5E380D00) was the zone's and later the rifle's, so channels are
    // being freed and recycled. That single fact would explain the zone that
    // never appears, the phases that never arrive and the weapon that goes
    // invisible until you switch away and back. What was missing is the other
    // half: when does a channel DISAPPEAR. UActorChannel::SetChannelActor is
    // only called on OPEN, so hooking it can never show a close.
    // So read the truth directly: UNetConnection::ActorChannels is a TMap whose
    // element array is at Connection+0x430 and count at +0x438 (from the crash
    // disassembly: "lea rcx,[r15+0x430]" then "add rsi,[r15+0x430]", stride
    // 0x20, value at +8). Walk it every 5 s and say which of the actors we care
    // about currently hold a channel. A "war da, ist weg" line is the close.
    {
        static int cxTick = 0;
        if ((++cxTick % 5) == 0) {
            void** conns = (void**)SafePtr((uint8_t*)g_drv + OFF::UNetDriver_ClientConnData);
            const int32_t nc = SafeI32((uint8_t*)g_drv + OFF::UNetDriver_ClientConnNum);
            for (int ci = 0; ci < nc && ci < 4; ++ci) {
                void* conn = SafePtr(conns + ci);
                if (!conn) continue;
                uint8_t* elems = (uint8_t*)SafePtr((uint8_t*)conn + 0x430);
                const int32_t num = SafeI32((uint8_t*)conn + 0x438);
                if (!elems || num <= 0 || num > 20000) continue;
                bool zone = false, pawn = false, weap = false, ctrl = false;
                int live = 0;
                for (int i = 0; i < num; ++i) {
                    void* ch = SafePtr(elems + (size_t)i * 0x20 + 8);
                    if (!ch) continue;
                    ++live;
                    void* a = ChanMapGet(ch);
                    if (!a) continue;
                    if (a == g_blueZone) zone = true;
                    if (IsRemotePawnActor(a))       pawn = true;
                    if (IsRemoteControllerActor(a)) ctrl = true;
                    if (IsRemoteWeaponActor(a))     weap = true;
                }
                static bool prevZone = false, prevWeap = false; static int prevLive = -1;
                const bool changed = (zone != prevZone) || (weap != prevWeap)
                                  || (prevLive < 0) || ((live > prevLive ? live - prevLive : prevLive - live) > 20);
                if (changed || (g_cxLogged % 12) == 0) {
                    InterlockedIncrement(&g_cxLogged);
                    L("[cx] Verbindung %p: %d Kanaele offen | BlueZone %s | Pawn %s | Waffe %s | Controller %s%s",
                      conn, live,
                      zone ? "JA" : "NEIN", pawn ? "JA" : "NEIN",
                      weap ? "JA" : "NEIN", ctrl ? "JA" : "NEIN",
                      (prevZone && !zone) ? "   <<< ZONEN-KANAL IST WEG >>>"
                                          : ((prevWeap && !weap) ? "   <<< WAFFEN-KANAL IST WEG >>>" : ""));
                } else InterlockedIncrement(&g_cxLogged);
                prevZone = zone; prevWeap = weap; prevLive = live;
            }
        }
    }
    EnsureZoneRelevance();   // v50: classic-path relevancy (the graph has its own copy, see below)
    KickBlueZone();
    KickGameState();         // v106: the zone reference and the match info ride on the GameState
    // v111: a zone channel just opened somewhere -- re-announce the GameState so
    // its zone reference goes out again now that it can actually resolve. Same
    // game-thread window, same ForceNetUpdate path, no engine state written.
    if (g_zoneChannelJustOpened && !g_zoneToggle) {   // v122: the toggle above owns this flag now
        g_zoneChannelJustOpened = false;
        void* gs2 = SafePtr((uint8_t*)g_world + OFF::UWorld_GameState);
        if (gs2) {
            void** vt2 = (void**)SafePtr(gs2);
            void*  fn2 = vt2 && InModule(vt2) ? SafePtr((uint8_t*)vt2 + OFF::AActor_ForceNetUpdate_VT) : nullptr;
            if (fn2 && InModule(fn2)) {
                __try { ((void (__fastcall*)(void*))fn2)(gs2); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
                LONG n = InterlockedIncrement(&g_zoneResendN);
                void* ref2 = g_zoneRefOff ? SafePtr((uint8_t*)gs2 + g_zoneRefOff) : nullptr;
                if (n <= 10)
                    L("[zf] Zonen-Kanal ist aufgegangen -> GameState erneut angestossen (#%ld),"
                      " Referenz bei +0x%03llX = %p %s", n, (unsigned long long)g_zoneRefOff, ref2,
                      (ref2 && ref2 == g_blueZone) ? "(gesetzt -- loest jetzt auf)" : "(noch nicht gesetzt)");
            }
        }
    }
    // v58e: the v58c order was wrong -- the first ZoneTick called CullToZero,
    // whose Get() CREATED a fresh (unrouted) entry and then remembered that as
    // "ours", so the self-heal never fired. Now: if we have never seen the
    // actor enter the graph, fix the entry once; otherwise compare and re-add.
    if (g_blueZone && g_zoneRelevant) {
        if (!g_zoneInfoPtr) ZoneGraphCullToZero(g_drv, g_blueZone, "ZoneTick (kein Add gesehen)");
        else                ZoneGraphSelfHeal();
    }
    // ---- v52: measurement with the REAL offsets --------------------------
    // v51 used the SDK offsets and they are wrong for this build: it reported
    // index 15 while the host log said 0. The correct ones come from the
    // engine's own property table, which stores each RepNotify name next to
    // its offset (entry+0x1C):
    //     SelectedPlayZoneName       +0x7C8   (FName)
    //     SelectedPlayZoneInfoIndex  +0x7D4   (int32)
    //     AreaDescKey                +0x7D8   (FName)
    // The SDK was uniformly 8 bytes low on all of them.
    //
    // This finally answers the open question: if these already carry the
    // preview the host announces at map load (e.g. index 0 / "Space Center"),
    // then the data is replicated state and the actor just is not replicating
    // early enough. If they are empty until the first zone is picked, the
    // preview only ever travelled inside the Blueprint multicast and has to be
    // replayed instead. Read-only.
    {
        static int slow = 0;
        if (g_blueZone && (++slow % 20) == 0) {
            int32_t idx = SafeI32((uint8_t*)g_blueZone + 0x7D4);
            wchar_t key[128], zone[128];
            bool k = FNameToStr((uint8_t*)g_blueZone + 0x7D8, key,  128);
            bool z = FNameToStr((uint8_t*)g_blueZone + 0x7C8, zone, 128);
            L("[bz] Zonendaten: Index(+0x7D4)=%d  AreaDescKey(+0x7D8)=%ls  ZoneName(+0x7C8)=%ls",
              idx, k ? key : L"<leer>", z ? zone : L"<leer>");
        }
    }
}

// Remember which UFunction pointers we already named, so each RPC is logged
// exactly once instead of every frame.
static void* g_rpcSeenFns[512];
static int   g_rpcSeenN = 0;
static bool RpcAlreadyLogged(void* fn) {
    for (int i = 0; i < g_rpcSeenN; ++i) if (g_rpcSeenFns[i] == fn) return true;
    if (g_rpcSeenN < 512) g_rpcSeenFns[g_rpcSeenN++] = fn;
    return false;
}

// ---- v53: watch list for the reload investigation --------------------------
// The -rpclog run proved there is no reload/ammo RPC at all; ammo travels via
// the inventory RPCs and replicated properties. What is still unknown is WHERE
// the 5-10 seconds go: does the update leave the host late, or does it leave
// immediately and the client's UI sits on it? -ammolog answers that by
// timestamping every single call of the inventory/weapon RPCs, so the delay can
// be measured against the moment the reload key was pressed.
static void* g_watchFns[64];
static int   g_watchN  = 0;
static bool  g_ammoLog = false;
static bool IsWatchedFn(void* fn) {
    for (int i = 0; i < g_watchN; ++i) if (g_watchFns[i] == fn) return true;
    return false;
}
static void ClassifyFn(void* fn, const wchar_t* name) {
    if (!g_ammoLog || g_watchN >= 64) return;
    // v58t: the perk/aircraft names too -- #16 is the same question as #15
    // ("does the update leave the host late, or does the client sit on it?")
    // and one reload plus one boarding then answer both in the same log.
    if (wcsstr(name, L"Inventory") || wcsstr(name, L"Ammo")      ||
        wcsstr(name, L"Reload")    || wcsstr(name, L"Weapon")    ||
        wcsstr(name, L"Perk")      || wcsstr(name, L"Aircraft")  ||
        wcsstr(name, L"Simulat")   || wcsstr(name, L"Magazine")) {
        g_watchFns[g_watchN++] = fn;
        L("[ammo] beobachte ab jetzt: %ls", name);
    }
}

static void LogRpcName(void* fn, const char* kind) {
    if ((!g_rpcLog && !g_ammoLog) || RpcAlreadyLogged(fn)) return;
    wchar_t name[128];
    if (ResolveFuncName(fn, name, 128)) {
        if (g_rpcLog) L("[rpcname] %s  %ls", kind, name);
        ClassifyFn(fn, name);
    } else if (g_rpcLog) {
        L("[rpcname] %s  <unresolved fn=%p>", kind, fn);
    }
}

// Logged on EVERY call of a watched function, with a millisecond stamp.
static void LogAmmoCall(void* fn) {
    if (!g_ammoLog || !IsWatchedFn(fn)) return;
    static int n = 0;
    if (n >= 300) return;                       // keep the log readable
    ++n;
    wchar_t name[128];
    if (ResolveFuncName(fn, name, 128))
        L("[ammo] t=%lu ms  %ls", (unsigned long)GetTickCount(), name);
}

// ===========================================================================
//  v58t: KEEP THE REMOTE PLAYER'S OWN ACTORS AWAKE
// ===========================================================================
//  #15 (the ammo counter stays at 0 until the weapon is switched, then catches
//  up after 5-10 s) and #16 (the perk UI in the aircraft updates late) are the
//  same shape of problem as the loot was, one layer further in. The reload
//  itself works: the v58s log shows ServerReloadWeapon arriving and
//  MulticastStopSimulatingReload going back out. What arrives late is the new
//  magazine COUNT, and that is a replicated property on the weapon /
//  inventory-item actor -- an actor that belongs to the remote player and to
//  nobody else. Same for the perk data on his PlayerState.
//  In the v58s log ClientModifyInventoryItem -- the RPC that carries a changed
//  inventory item -- fired four times in the whole round, 6 to 9 seconds
//  apart. That is exactly the delay Alen sees.
//  These actors never pass our v58k wake-up, because that only looks at
//  level-placed actors with NetDormancy > DORM_Awake, and a weapon is spawned
//  at runtime. So: find them by their Owner chain (Owner +0x2B0, the same
//  chain AActor::IsNetRelevantFor walks) and keep them updating.
static void* g_ownedActors[256];
static int   g_ownedCount = 0;
static bool  g_ownPoke    = false;         // v58y: OFF by default, -ownpoke enables it
static volatile LONG g_ownFound = 0, g_ownPoked = 0, g_ownDropped = 0;   // v58w

// v58u: a fast membership filter over the same actors, for the net-mode hook.
// AActor::GetNetMode is called ~6000 times a second, so it cannot walk an
// Owner chain every time. One hash probe rejects everything that is not in the
// set; a hit is then verified against the real Owner chain, so a stale entry
// (a freed pointer reused by another object) can never produce a wrong answer.
static void*         g_ownedSet[2048];
static bool          g_ownMode = true;     // -noownmode
static volatile LONG g_ownModeSubst = 0;
static void OwnedSetClear() { memset(g_ownedSet, 0, sizeof(g_ownedSet)); }
static void OwnedSetAdd(void* a) {
    if (!a) return;
    uintptr_t h = ((uintptr_t)a >> 4) * 2654435761u;
    for (int i = 0; i < 8; ++i) {
        const int slot = (int)((h + i) & 2047);
        if (!g_ownedSet[slot] || g_ownedSet[slot] == a) { g_ownedSet[slot] = a; return; }
    }
}
static bool OwnedSetMayHave(void* a) {
    uintptr_t h = ((uintptr_t)a >> 4) * 2654435761u;
    for (int i = 0; i < 8; ++i) {
        const int slot = (int)((h + i) & 2047);
        if (!g_ownedSet[slot]) return false;
        if (g_ownedSet[slot] == a) return true;
    }
    return false;
}

static bool OwnedByRemote(void* a);   // v58t, defined once g_remote is known

static int __fastcall MyGetFunctionCallspace(void* actor, void* fn, void* stack) {
    int r = g_origCallspace ? g_origCallspace(actor, fn, stack) : CS_Local;
    if (!g_rpcFix || !g_maskActive || !g_drv || !fn) return r;
    // Nur wenn wirklich ein Client dranhaengt -- sonst nichts anfassen.
    if (SafeI32((uint8_t*)g_drv + OFF::UNetDriver_ClientConnNum) <= 0) return r;

    uint32_t flags = (uint32_t)SafeI32((uint8_t*)fn + OFF::UFunction_FunctionFlags);
    // Die Netzflags stehen auf der obersten Funktion der Kette -- wie im Original.
    void* f = fn;
    for (int i = 0; i < 4; ++i) {
        void* sup = SafePtr((uint8_t*)f + OFF::UStruct_SuperStruct);
        if (!sup || !LooksLikeActor(sup)) break;
        f = sup;
        flags |= (uint32_t)SafeI32((uint8_t*)f + OFF::UFunction_FunctionFlags);
    }
    if (!(flags & FUNC_Net)) return r;
    InterlockedIncrement(&g_rpcSeen);

    if (flags & FUNC_NetMulticast) { InterlockedIncrement(&g_rpcMulti); LogRpcName(fn, "multicast"); LogAmmoCall(fn); return r | CS_Remote; }
    if (flags & FUNC_NetClient)    {
        InterlockedIncrement(&g_rpcClient); LogRpcName(fn, "client"); LogAmmoCall(fn);
        // -remoteonly: verhalten wie ein echter Listen-Server -- Client-RPCs
        // werden NUR verschickt, nicht auch lokal ausgefuehrt. Dann rendert
        // der Host nicht mehr die Klassenauswahl des Clients mit (doppelte
        // "Change Class"-Leiste, koreanische Platzhalter, generische Icons).
        // Preis: der Host verliert seine EIGENEN Client-RPCs, also auch seine
        // eigene Kamera. Nur sinnvoll, wenn der Laptop reiner Server ist.
        return g_rpcRemoteOnly ? CS_Remote : (r | CS_Remote);
    }
    return r;   // Server-RPCs bleiben unangetastet: lokal ausfuehren ist richtig
}

// ===========================================================================
//  ACTOR NET MODE  (v42)  --  why the joiner's camera sat under the map
//
//  FINDING
//  The standalone mask (v33) nulls World->NetDriver so that the WORLD reports
//  NM_Standalone (Blueprint IsServer, UWorld::IsNetMode, ...). But actors do
//  not ask the world. AActor::GetNetMode (base+0x3F92730) is a real function
//  and reads World->NetDriver itself:
//      NetDriver ? NetDriver->GetNetMode()            // virtual -> NM_ListenServer
//                : DemoNetDriver ? Demo->GetNetMode() : NM_Standalone
//  With the mask active it therefore returns NM_Standalone to every actor,
//  and three pieces of engine code that are essential for a remote player
//  silently take the "single player" branch:
//
//    APawn::PossessedBy (base+0x44AE7C3):
//        if (Cast<APlayerController>(Controller) && GetNetMode() != NM_Standalone)
//        {   SetReplicates(true); SetAutonomousProxy(true);   }      <- SKIPPED
//    -> the joiner's own pawn is replicated to him as ROLE_SimulatedProxy.
//       On his machine that means: no ServerMove (he cannot walk), the
//       character movement runs SimulatedTick like a stranger's pawn, and
//       every game check of the form "GetLocalRole() == ROLE_AutonomousProxy"
//       (camera attach, input setup, local-player HUD) is false. The
//       spring-arm CameraAssistant3P never gets attached -> the camera sits at
//       its relative offset near the origin, i.e. under the map.
//
//    AActor::GetFunctionCallspace (base+0x3F8D108 calls GetNetMode):
//        NM_Standalone -> every RPC is "Local"                      <- v37 hack
//
//    AController::IsLocalController (base+0x4198FB0):
//        NM_Standalone -> true, even for the REMOTE player's controller
//    -> the host ran the joiner's "locally controlled" code itself (the
//       duplicated class-selection bar, the Korean placeholders, ...).
//
//  FIX
//  Hook AActor::GetNetMode. Whenever the original answers NM_Standalone
//  while the mask is active and the actor lives in our listen world, answer
//  NM_ListenServer instead. That is precisely what the unpatched function
//  would return if the driver were visible (UNetDriver::GetNetMode at
//  base+0x43E77D0 = IsServer() ? (GIsClient ? NM_ListenServer :
//  NM_DedicatedServer) : NM_Client). The world keeps reporting standalone,
//  so loot / vehicles / level visibility stay as in v33.
//
//  CONSEQUENCES
//    * PossessedBy sets ROLE_AutonomousProxy on the joiner's pawn.
//    * GetFunctionCallspace routes RPCs like a real listen server:
//      client RPCs of remote-owned actors go out ONLY, host-owned ones run
//      locally ONLY, multicasts both. The additive v37 switch is therefore
//      OFF by default now (-rpcfix turns it back on; it is also re-enabled
//      automatically if this hook cannot be installed).
//    * IsLocalController is false for the remote PC on the host.
//
//  Switch off without rebuilding:  -noactormode
// ===========================================================================
// (tGetNetMode / g_origGetNetMode / g_actorMode / g_nmCalls / g_nmSubst are
//  declared above, next to the other watchdog forward declarations.)
static int __fastcall MyActorGetNetMode(void* actor) {
    int r = g_origGetNetMode ? g_origGetNetMode(actor) : OFF::NM_Standalone;
    InterlockedIncrement(&g_nmCalls);
    // v58f: the spawn timers of buildings / vehicle spawners are only started
    // in BeginPlay when GetNetMode() == NM_Standalone (RVA comment). Whatever
    // the mask state at that moment, those two call sites get "standalone".
    if (g_beginPlayFix && g_world && g_drv) {
        const uintptr_t ra = (uintptr_t)_ReturnAddress();
        const bool bld = ra == g_base + RVA::BuildingBeginPlay_GetNetModeRet;
        if (bld || ra == g_base + RVA::VehicleBeginPlay_GetNetModeRet) {
            InterlockedIncrement(&g_bpGateCalls);
            if (r != OFF::NM_Standalone) {
                LONG n = InterlockedIncrement(&g_bpGateForced);
                if (n <= 8) L("[bp] BeginPlay von %s %p sah NetMode %d (Maske gerade aus) -> NM_Standalone, damit der Spawn-Timer startet (#%ld)",
                              bld ? "Gebaeude" : "Fahrzeug-Spawner", actor, r, n);
                return OFF::NM_Standalone;
            }
        }
    }
    if (!g_actorMode || !g_maskActive || !g_world || !g_drv) return r;
    if (r != OFF::NM_Standalone) return r;          // driver visible or demo -> already right
    // *** v46: ONLY actors that belong to a remote autonomous client. ***
    // v42..v44 answered NM_ListenServer to EVERY actor -- that cost the host
    // its floor loot. v45 answered NM_Standalone to every actor -- then the
    // remote player could not walk. RemoteRole separates the two groups:
    //   remote player's PlayerController + Pawn  -> ROLE_AutonomousProxy
    //   loot spawners, items, GameMode, GameState, geometry -> anything else
    // The host's OWN PlayerController is not AutonomousProxy either (proven by
    // v44, where every actor was NM_ListenServer and the host still controlled
    // its character), so it keeps answering NM_Standalone and
    // AController::IsLocalController keeps returning true for it.
    //
    // *** v58u: AND the actors those two OWN. ***
    // Alen described the reload precisely: pick a weapon up, reload -- the
    // counter still says 0. Switch weapon away and back and the magazine is
    // full. Shoot 20 -> 15, switch away and back, and it says 20 again
    // without reloading. So the client's own copy of the item is never
    // updated after it is created: the widget is refreshed from that copy on
    // every equip, and the shooting in between is a local prediction that is
    // thrown away. The host is fine because the host reads its copy directly.
    // Why the update never leaves the host: the weapon, the inventory items
    // and the PlayerState are NOT AutonomousProxy, so the test below sent
    // them down the NM_Standalone path -- and every "we are a server, so tell
    // the owning client" branch in those classes then does nothing at all.
    // That is the same shape as DoInAircraft, which we already fix one call
    // site at a time. So actors that belong to a remote player answer
    // NM_ListenServer too. The set is only a fast filter; the Owner chain is
    // verified for real on every hit, so a stale pointer cannot widen this.
    // -noownmode.
    uint8_t remote = 0;
    if (!SafeCopy((uint8_t*)actor + OFF::AActor_RemoteRole, &remote, 1)) return r;
    const bool isAutonomous = (remote == OFF::ROLE_AutonomousProxy);
    bool isOwnedByRemote = false;
    if (!isAutonomous) {
        if (!g_ownMode || g_remoteCount == 0) return r;
        if (!OwnedSetMayHave(actor))           return r;
        if (!OwnedByRemote(actor))             return r;   // verified, not just hashed
        isOwnedByRemote = true;
    }
    // Only actors of OUR world. GetWorld() is the same virtual the original
    // used one instruction earlier, so calling it again is safe.
    void** vt = (void**)SafePtr(actor);
    if (!vt || !InModule(vt)) return r;
    void* fn = SafePtr((uint8_t*)vt + OFF::AActor_GetWorld_VT);
    if (!fn || !InModule(fn)) return r;
    void* world = nullptr;
    __try { world = ((void* (__fastcall*)(void*))fn)(actor); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return r; }
    if (world != g_world) return r;
    InterlockedIncrement(&g_nmSubst);
    // v58: ABravoHotelCharacter::DoInAircraft only sends the ClientInAircraft
    // RPC when GetNetMode() == NM_DedicatedServer; under NM_ListenServer it
    // runs InternalInAircraft locally and the OWNING CLIENT never hears about
    // the boarding (handoff finding, confirmed in the binary at 0x1FEC9DC..
    // 0x1FECA0A). Both branches do the same server-side work, so for the
    // remote pawn -- and only at that one call site -- we answer "dedicated".
    // The jmp at the function start keeps the caller's return address, so
    // _ReturnAddress() here is the address after the original call.
    if (isOwnedByRemote) {
        LONG n = InterlockedIncrement(&g_ownModeSubst);
        if (n <= 15) {
            wchar_t cn[128] = L"?";
            void* cls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
            if (cls) ResolveFuncName(cls, cn, 128);
            L("[om] %ls %p gehoert dem Remote-Spieler -> NM_ListenServer statt NM_Standalone (Aufrufer +0x%llX) -- #%ld",
              cn, actor, (unsigned long long)((uintptr_t)_ReturnAddress() - g_base), n);
        }
        return OFF::NM_ListenServer;
    }
    if (g_aircraftFix &&
        (uintptr_t)_ReturnAddress() == g_base + RVA::DoInAircraft_GetNetModeRet) {
        LONG n = InterlockedIncrement(&g_aircraftSubst);
        if (n <= 5) L("[ac] DoInAircraft fuer Remote-Pawn %p -> NM_DedicatedServer, damit ClientInAircraft rausgeht (#%ld)", actor, n);
        return OFF::NM_DedicatedServer;
    }
    return OFF::NM_ListenServer;
}

// ---- APawn::PossessedBy: verify (and if needed repeat) the role setup ------
typedef void (__fastcall* tPossessedBy)(void* pawn, void* controller);
typedef void (__fastcall* tSetReplicates)(void* actor, bool b);
typedef void (__fastcall* tSetAutoProxy)(void* actor, bool b, bool forceCompare);
static tPossessedBy g_origPossessedBy = nullptr;

// Same test the engine does inline in PossessedBy: IsChildOf(APlayerController)
// through the FStructBaseChain. All reads are guarded, a miss just says "no".
static bool IsPlayerController(void* obj) {
    void* pcClass = SafePtr((void*)(g_base + RVA::APlayerController_ClassPtr));
    if (!obj || !pcClass) return false;
    void* cls = SafePtr((uint8_t*)obj + OFF::UObject_Class);
    if (!cls) return false;
    int32_t want = SafeI32((uint8_t*)pcClass + OFF::UStruct_NumBasesM1);
    int32_t have = SafeI32((uint8_t*)cls     + OFF::UStruct_NumBasesM1);
    if (want < 0 || want > have || want > 64) return false;
    void* chain = SafePtr((uint8_t*)cls + OFF::UStruct_BaseChain);
    if (!chain) return false;
    return SafePtr((uint8_t*)chain + (size_t)want * 8) == (uint8_t*)pcClass + OFF::UStruct_BaseChain;
}

static const char* RoleName(uint8_t r) {
    switch (r) { case 0: return "None"; case 1: return "SimulatedProxy";
                 case 2: return "AutonomousProxy"; case 3: return "Authority"; default: return "?"; }
}

static void RegisterRemotePawn(void* pawn, void* controller);   // v55, below

static void __fastcall MyPossessedBy(void* pawn, void* controller) {
    if (g_origPossessedBy) g_origPossessedBy(pawn, controller);
    static int n = 0;
    uint8_t remote = 0, role = 0, rep = 0;
    SafeCopy((uint8_t*)pawn + OFF::AActor_RemoteRole,  &remote, 1);
    SafeCopy((uint8_t*)pawn + OFF::AActor_Role,        &role,   1);
    SafeCopy((uint8_t*)pawn + OFF::AActor_bReplicates, &rep,    1);
    const bool pc = IsPlayerController(controller);
    // v55: a REMOTE player's controller has RemoteRole == ROLE_AutonomousProxy
    // (the host's own controller does not -- that is what v46 rests on).
    // Remember its pawn as a streaming anchor.
    if (pc && g_streamRemote) {
        uint8_t ctrlRemote = 0;
        SafeCopy((uint8_t*)controller + OFF::AActor_RemoteRole, &ctrlRemote, 1);
        if (ctrlRemote == OFF::ROLE_AutonomousProxy) {
            RegisterRemotePawn(pawn, controller);
            L("[ws] Remote-Spieler: Controller %p (RemoteRole=%s) besitzt Pawn %p -> Streaming-Anker #%d",
              controller, RoleName(ctrlRemote), pawn, g_remoteCount);
        } else if (n < 40) {
            L("[ws] Controller %p hat RemoteRole=%s -> kein Streaming-Anker (Host selbst)",
              controller, RoleName(ctrlRemote));
        }
    }
    if (n >= 40) return;
    L("[pb] PossessedBy pawn=%p ctrl=%p (%s) -> Role=%s RemoteRole=%s bReplicates=%d  netmode=%s",
      pawn, controller, pc ? "PlayerController" : "other", RoleName(role), RoleName(remote), rep & 1,
      g_maskActive ? (g_actorMode && g_origGetNetMode ? "ListenServer (hook)" : "Standalone (mask)") : "driver visible");
    ++n;
    // Belt and braces: if the engine still skipped the role setup, do it now,
    // exactly the two calls the original makes at 044AE7D1 / 044AE7E0.
    if (g_maskActive && pc && role == OFF::ROLE_Authority && remote != OFF::ROLE_AutonomousProxy) {
        auto setRep  = (tSetReplicates)(g_base + RVA::AActor_SetReplicates);
        auto setAuto = (tSetAutoProxy) (g_base + RVA::AActor_SetAutonomousProxy);
        __try {
            setRep(pawn, true);
            setAuto(pawn, true, true);
            SafeCopy((uint8_t*)pawn + OFF::AActor_RemoteRole, &remote, 1);
            L("[pb]   role setup was skipped -> repeated it, RemoteRole now %s", RoleName(remote));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            L("[pb]   repeat of the role setup FAULTED -> left as is");
        }
    }
}

// ---- v43: neuter the replay recorder --------------------------------------
// The hook is a no-op body: it just returns, so no DemoNetDriver is created
// and TickDemoRecord (the crashing path) never runs. Declared with no args --
// on x64 the caller cleans the stack and the ignored parameters sit in
// registers/shadow space, so returning immediately is safe.
static bool g_blockReplay = true;   // -allowreplay turns this off
static volatile LONG g_replayBlocked = 0;
static void __fastcall MyStartRecordingReplay(void* /*gameInstance*/) {
    InterlockedIncrement(&g_replayBlocked);
    static int n = 0;
    if (n++ < 5) L("[rp] StartRecordingReplay unterdrueckt (kein Crash-Replay-Recorder auf dem Host)");
    // deliberately do NOT call the original -> no recording, no TickDemoRecord
}

// ---- v44: pin the world origin -------------------------------------------
// Never shift the origin. We also copy OriginLocation over
// RequestedOriginLocation so UWorld::Tick sees "nothing requested" and stops
// calling us every frame. Returning false is the same answer the engine gives
// itself when a level is pending visibility, so every caller already handles it.
static bool __fastcall MySetNewWorldOrigin(void* world, void* newOrigin) {
    InterlockedIncrement(&g_rebaseBlocked);
    int32_t cur[3] = { 0, 0, 0 }, req[3] = { 0, 0, 0 };
    SafeCopy((uint8_t*)world + OFF::UWorld_OriginLocation, cur, sizeof(cur));
    SafeCopy(newOrigin, req, sizeof(req));
    SafeCopy(cur, (uint8_t*)world + OFF::UWorld_RequestedOriginLocation, sizeof(cur));
    static int n = 0;
    if (n < 10) {
        L("[or] Origin-Rebasing blockiert: World %p bleibt bei {%d, %d, %d}, angefordert war {%d, %d, %d}",
          world, cur[0], cur[1], cur[2], req[0], req[1], req[2]);
        ++n;
    }
    return false;
}

// ===========================================================================
//  v55: STREAM WORLD-COMPOSITION TILES AROUND THE REMOTE PLAYERS TOO
// ===========================================================================
// Why everything is "buggy far from the host": the server only requests the
// streaming sublevels around its OWN view point (see RVA::WorldComp_Update-
// StreamingState). Where the host has never been, the tiles are not loaded on
// the server -- so there are no loot spawners, no door actors, no breakable
// glass and no floor to stand on, while the client has all of it locally.
// Every replicated interaction with those actors then fails or lags.
//
// Fix: remember the pawns of remote PlayerControllers (from PossessedBy, the
// controller's RemoteRole is ROLE_AutonomousProxy exactly for remote players,
// the host's own controller is not) and append their root-component location
// to the array the engine passes to the overload. Both arrays are rebuilt on
// the stack; the original then loads and unloads tiles for ALL anchors.
typedef void (__fastcall* tUpdStream)(void* comp, const float* locs, const float* dirs,
                                      int32_t num, int32_t lod, float scale);
typedef void (__fastcall* tPcViewPoint)(void* pc, float* outLoc, float* outDir);
static tUpdStream g_origUpdStream = nullptr;

// v112: the ammo fix was built and tested with TWO players and it shows. The
// watched weapon was a single global taken from g_remote[0], so shot counting,
// the magazine watcher and the ClientSetMagazine push only ever applied to ONE
// player -- everybody else kept the broken pre-v87 behaviour ("won't reload,
// switch away and back gives a full mag"). The registry also held only 8
// remotes while 16 people were playing. Both are per-player now.
struct RemotePawn {
    void*   pawn;
    void*   controller;
    void*   weapon;        // v112: this player's current weapon
    int32_t lastMag, lastBp, lastA, lastB;
};
static RemotePawn g_remote[24];
static bool IsRemotePawnActor(void* a) {
    if (!a) return false;
    for (int i = 0; i < g_remoteCount; ++i) if (g_remote[i].pawn == a) return true;
    return false;
}
static bool IsRemoteWeaponActor(void* a) {
    if (!a) return false;
    for (int i = 0; i < g_remoteCount; ++i) if (g_remote[i].weapon == a) return true;
    return false;
}
static bool IsRemoteControllerActor(void* a) {
    if (!a) return false;
    for (int i = 0; i < g_remoteCount; ++i) if (g_remote[i].controller == a) return true;
    return false;
}

// ===========================================================================
//  v122 -- the ladder, read where it actually lives
//
//  v121 tried to catch the state machine through ProcessEvent and got NOTHING:
//  [cs] stayed empty all round, so CurrentState / ChangeCharacterState are not
//  dispatched as UFunctions at all -- they are native calls. Polling is the
//  only way to see them.
//
//  The handoff and the binary agree on where to look:
//     Character + 0x678   ULadderComponent
//     component + 0xD0    LadderState   (enum, RepNotify OnRep_LadderState)
//     component + 0xD8    UsingLadder   (object pointer, RepNotify)
//  ELadderState: 0 LS_NotLadder, 1 LS_UseLadder, 2 LS_SnapToLadder,
//                3 LS_ClimbLadder, 4 LS_OnLadder, 5 LS_Sliding, 6 LS_DownLadder
//
//  The [ld] log shows the pattern to explain: ServerTryUseLadder arrives with a
//  valid ladder, and 2-5 s later the CLIENT sends ServerTryExitLadder. Once, it
//  held for 13.4 s. So something ends the climb early. This says which state it
//  dies in, and whether the host ever leaves LS_UseLadder for LS_ClimbLadder.
// ===========================================================================
static bool MaybeAlive(void* obj);      // v122: defined a few lines further down
static const wchar_t* LadderStateName(uint8_t v) {
    switch (v) {
        case 0: return L"LS_NotLadder";   case 1: return L"LS_UseLadder";
        case 2: return L"LS_SnapToLadder";case 3: return L"LS_ClimbLadder";
        case 4: return L"LS_OnLadder";    case 5: return L"LS_Sliding";
        case 6: return L"LS_DownLadder";  default: return L"(unbekannt)";
    }
}
static volatile LONG g_ladLogged = 0;
static void LadderTick() {
    if (g_ladLogged > 600) return;
    for (int i = 0; i < g_remoteCount && i < 4; ++i) {
        void* pawn = g_remote[i].pawn;
        if (!pawn || !MaybeAlive(pawn)) continue;
        void* comp = SafePtr((uint8_t*)pawn + 0x678);       // ULadderComponent
        if (!comp || !MaybeAlive(comp)) continue;
        uint8_t st = 0;
        void*   using_ = SafePtr((uint8_t*)comp + 0xD8);
        if (!SafeCopy((uint8_t*)comp + 0xD0, &st, 1)) continue;
        static uint8_t prevSt[4]   = { 255, 255, 255, 255 };
        static void*   prevUse[4]  = {};
        static DWORD   since[4]    = {};
        if (st != prevSt[i] || using_ != prevUse[i]) {
            const DWORD now = GetTickCount();
            wchar_t lc[128] = L"";
            if (using_ && MaybeAlive(using_)) ClassNameOf(using_, lc, 128);
            InterlockedIncrement(&g_ladLogged);
            L("[lt] t=%lu ms  Spieler %d: LadderState %d %ls -> %d %ls  (hielt %lu ms) | UsingLadder %p %ls",
              (unsigned long)now, i, prevSt[i], LadderStateName(prevSt[i]),
              st, LadderStateName(st),
              (unsigned long)(since[i] ? now - since[i] : 0),
              using_, lc[0] ? lc : L"(keine)");
            prevSt[i] = st; prevUse[i] = using_; since[i] = now;
        }
    }
}
static int        g_objArrayState = 0;   // 0 unknown, 1 usable, -1 layout mismatch

// Same test the engine inlines everywhere (0x4761B70): look the object up in
// GUObjectArray, reject an empty or reused slot and the PendingKill /
// Unreachable flags (0x30000000).
// v59: THIS IS WHY v58w, v58x AND v58z ALL MEASURED NOTHING.
//  IsObjectAlive walks the GUObjectArray and compares the item's first pointer
//  against the object. In THIS build that pointer sits at +8 and is obfuscated
//  (the handoff's FUObjectItem note, S-box at 0x2B14D40), so the comparison
//  never matches and the function returns false for EVERY object. Our own
//  calibration has been saying so since v58g, once per round, in one line:
//      [ws] GUObjectArray-Layout ABWEICHEND -> nur Controller-Abgleich
//  The older code respected that flag (see RememberRemote). The liveness
//  checks I added in v58w did not -- so from v58w on, every one of them
//  returned false and silently switched off the thing it was guarding: the
//  poke, the weapon watcher, the replication-proxy watcher, and in v58z the
//  DORM_Awake write. Three builds measuring nothing, for one missing guard.
//  MaybeAlive is what everything added after v58g must use: it only filters
//  when the layout is actually usable, and otherwise gets out of the way.
static bool IsObjectAlive(void* obj);
static bool MaybeAlive(void* obj) {
    if (!obj) return false;
    return (g_objArrayState > 0) ? IsObjectAlive(obj) : true;
}

static bool IsObjectAlive(void* obj) {
    if (!obj) return false;
    int32_t idx = SafeI32((uint8_t*)obj + OFF::UObject_InternalIndex);
    int32_t num = SafeI32((void*)(g_base + RVA::GUObjectArray_NumElems));
    if (idx < 0 || idx >= num) return false;
    void* chunks = SafePtr((void*)(g_base + RVA::GUObjectArray_Chunks));
    if (!chunks) return false;
    uint8_t* chunk = (uint8_t*)SafePtr((uint8_t*)chunks + (size_t)(idx >> 16) * 8);
    if (!chunk) return false;
    uint8_t* item = chunk + (size_t)(idx & 0xFFFF) * OFF::UObjectItem_Size;
    if (SafePtr(item) != obj) return false;
    uint32_t flags = (uint32_t)SafeI32(item + OFF::UObjectItem_Flags);
    return (flags & 0x30000000u) == 0;
}

// Called from MyPossessedBy (game thread). Keyed by controller, so a respawn
// or a vehicle simply replaces the pawn.
static void* FirstRemoteController() { return g_remoteCount > 0 ? g_remote[0].controller : nullptr; }
// Does this actor's Owner chain end at a remote pawn or controller?
static bool OwnedByRemote(void* a) {
    void* cur = a;
    for (int depth = 0; depth < 6 && cur; ++depth) {
        for (int i = 0; i < g_remoteCount; ++i) {
            if (cur == g_remote[i].pawn || cur == g_remote[i].controller) return depth > 0;
        }
        cur = SafePtr((uint8_t*)cur + OFF::AActor_Owner);
    }
    return false;
}

static void RegisterRemotePawn(void* pawn, void* controller) {
    for (int i = 0; i < g_remoteCount; ++i)
        if (g_remote[i].controller == controller) { g_remote[i].pawn = pawn; return; }
    if (g_remoteCount < 24) { g_remote[g_remoteCount++] = { pawn, controller, nullptr, -1, -1, -1, -1 }; }
    else { g_remote[7] = { pawn, controller }; }
}

static bool Finite3(const float* v) {
    for (int i = 0; i < 3; ++i) if (!(v[i] == v[i]) || v[i] > 1e9f || v[i] < -1e9f) return false;
    return true;
}

// Root-component location of a registered pawn, or false if the pawn is gone
// or no longer belongs to that controller.
static bool RemotePawnLocation(const RemotePawn& rp, float* out) {
    if (!rp.pawn) return false;
    if (g_objArrayState > 0) { if (!IsObjectAlive(rp.pawn)) return false; }
    if (SafePtr((uint8_t*)rp.pawn + OFF::APawn_Controller) != rp.controller) return false;
    uint8_t* root = (uint8_t*)SafePtr((uint8_t*)rp.pawn + OFF::AActor_RootComponent);
    if (!root) return false;
    if (!SafeCopy(root + OFF::USceneComp_Translation, out, 12)) return false;
    if (!Finite3(out)) return false;
    if (out[0] == 0.f && out[1] == 0.f && out[2] == 0.f) return false;
    return true;
}

// v58d: what does World Composition think about the tile under a location?
// Read-only walk over the tile arrays (layout: see RVA::CVar_SubPoint... notes).
static void TileDiag(void* comp, const float* loc, const char* who) {
    uint8_t* tiles = (uint8_t*)SafePtr((uint8_t*)comp + 0x48);
    int32_t  n     = SafeI32((uint8_t*)comp + 0x50);
    void**   lvls  = (void**)SafePtr((uint8_t*)comp + 0x168);
    int32_t  ln    = SafeI32((uint8_t*)comp + 0x170);
    if (!tiles || n <= 0 || n > 20000) { L("[td] %s: Tiles-Array unlesbar (%p, %d)", who, tiles, n); return; }
    int hits = 0;
    for (int i = 0; i < n && hits < 6; ++i) {
        uint8_t* t = tiles + (size_t)i * 0xE0;
        int32_t px = SafeI32(t + 0x2C), py = SafeI32(t + 0x30);
        float bmin[2] = {0,0}, bmax[2] = {0,0};
        if (!SafeCopy(t + 0x38, bmin, 8) || !SafeCopy(t + 0x44, bmax, 8)) continue;
        const float minx = px + bmin[0], miny = py + bmin[1], maxx = px + bmax[0], maxy = py + bmax[1];
        if (loc[0] < minx || loc[0] > maxx || loc[1] < miny || loc[1] > maxy) continue;
        uint8_t* lvl = (lvls && i < ln) ? (uint8_t*)SafePtr(lvls + i) : nullptr;
        int32_t lodIdx = lvl ? SafeI32(lvl + 0xC0) : -99;
        uint8_t f0 = 0, f1 = 0;
        if (lvl) { SafeCopy(lvl + 0xD0, &f0, 1); SafeCopy(lvl + 0xD1, &f1, 1); }
        // +0xD0 bits (matches the defaults 0x83 seen live): 0 bShouldBeVisible,
        // 1 bShouldBeLoaded, 7 bDrawOnLevelStatusMap. Bytes +0xD2.. hold the
        // private state (CurrentState enum: 4 LoadedNotVisible, 6 LoadedVisible).
        uint8_t st[16] = {0};
        if (lvl) SafeCopy(lvl + 0xD0, st, 16);
        // v58r: how many actors that level actually holds. This is the number
        // the whole loot question turns on -- a proxy level has 2-3, a real
        // one a few hundred -- and until now the log never printed it for the
        // tile the player is standing on.
        uint8_t* loaded = lvl ? (uint8_t*)SafePtr(lvl + 0x140) : nullptr;
        const int32_t lvlActors = loaded ? SafeI32(loaded + RVA::ULevel_Actors + 8) : -1;
        L("[td] %s {%.0f,%.0f}: Tile #%d Box[%.0f..%.0f, %.0f..%.0f] Prio=%d LODs=%d/%d Eval=%d | Level %p LODIndex=%d ShouldBeVisible=%d ShouldBeLoaded=%d | LoadedLevel %p mit %d Actors | Zustand %d",
          who, loc[0], loc[1], i, minx, maxx, miny, maxy, SafeI32(t + 0xAC), SafeI32(t + 0x18), SafeI32(t + 0xA0),
          SafeI32(t + 0xD8), lvl, lodIdx, f0 & 1, (f0 >> 1) & 1,
          loaded, lvlActors, st[8]);
        (void)f1;
        ++hits;
    }
    if (!hits) L("[td] %s {%.0f,%.0f}: kein Tile enthaelt diesen Punkt (n=%d)", who, loc[0], loc[1], n);
}

// ===========================================================================
//  v58j: THE THREE GATES BETWEEN A HOST ACTOR AND A CLIENT
// ===========================================================================
//  Counted per class, so the answer is a table and not an opinion:
//    hinzugefuegt  UNetDriver::AddNetworkActor -- the actor entered the graph
//    betrachtet    ReplicateSingleActor        -- the graph gathered it for
//                                                 this connection this frame
//    Kanal         SetChannelActor             -- it is really being sent
//  64 pickups standing next to the player with "hinzugefuegt 64, betrachtet 0"
//  means the grid never gathers them; "betrachtet 64, Kanal 0" means one of
//  the two gates inside ReplicateSingleActor drops them, and for those the
//  class policy byte and the cull distance are logged as well.
// ===========================================================================
static bool IsLootName(const wchar_t* n);        // defined with the census below
static bool ActorLocation(void* actor, float* out);

struct PipeTally { void* cls; volatile LONG added, considered, channel, woken; };
static PipeTally g_pipe[256];
static int       g_pipeN = 0;
static bool      g_pipeFull = false;
static volatile LONG g_wokenActors = 0, g_wakeSkipped = 0;   // v58k wake-up counters

static PipeTally* PipeFor(void* cls) {
    if (!cls) return nullptr;
    for (int i = 0; i < g_pipeN; ++i) if (g_pipe[i].cls == cls) return &g_pipe[i];
    if (g_pipeN >= 256) { g_pipeFull = true; return nullptr; }
    PipeTally* p = &g_pipe[g_pipeN];
    p->cls = cls; p->added = p->considered = p->channel = p->woken = 0;
    ++g_pipeN;
    return p;
}
static PipeTally* PipeForActor(void* actor) {
    return actor ? PipeFor(SafePtr((uint8_t*)actor + OFF::UObject_Class)) : nullptr;
}

// ---- stage 2: did the graph gather this actor for a connection? -----------
typedef void* (__fastcall* tRepSingle)(void* graph, void* actor, void* a3, void* a4,
                                       void* a5, void* a6, void* a7, void* a8,
                                       void* a9, void* a10);
static tRepSingle    g_origRepSingle = nullptr;
static volatile LONG g_repSingleCalls = 0;

// The per-class policy the graph consults at 0x13BECB4 before anything else.
static int ClassPolicyByte(void* graph, void* cls) {
    if (!graph || !cls) return -1;
    typedef void* (__fastcall* tLookup)(void* map, void* cls);
    void* r = nullptr;
    __try { r = ((tLookup)(g_base + RVA::RepGraph_ClassPolicyLookup))(
                    (uint8_t*)graph + RVA::RepGraph_ClassPolicyMap, cls); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
    if (!r) return -3;
    uint8_t b = 0;
    return SafeCopy(r, &b, 1) ? (int)b : -4;
}

static void* __fastcall MyReplicateSingleActor(void* graph, void* actor, void* a3, void* a4,
                                               void* a5, void* a6, void* a7, void* a8,
                                               void* a9, void* a10) {
    LONG rsn = InterlockedIncrement(&g_repSingleCalls);
    // -----------------------------------------------------------------------
    //  v64: WHICH actors does the graph actually replicate?
    //
    //  0x13BE920 is the real per-actor path -- it creates the channel
    //  (0x43C03B0), calls SetChannelActor (0x41AC3C0) and then the big
    //  replicate function 0x43F3D50. And it runs about 78 times in a whole
    //  round, against 994 channels. That is the whole of #15: an actor's state
    //  arrives when its channel opens and is essentially never re-sent, which
    //  is exactly "magazine correct at pickup, frozen afterwards".
    //
    //  So the question is no longer whether the weapon replicates -- it is why
    //  the graph yields almost nobody. Printing the class of the first sixty
    //  actors it does replicate answers that in one round: if they are all
    //  always-relevant actors (PlayerController, GameState, BlueZone) then the
    //  SPATIAL node produces nothing at all, and the grid gather is the single
    //  bug behind #15, #13, #14 and #16 together.
    // -----------------------------------------------------------------------
    if (rsn <= 60) {
        wchar_t cn[128] = L"?";
        ClassNameOf(actor, cn, 128);
        L("[rs] #%ld  der Graph repliziert %ls %p", rsn, cn, actor);
    }
    PipeTally* t = PipeForActor(actor);
    if (t) InterlockedIncrement(&t->considered);
    // For the classes this whole investigation is about, say once what the two
    // gates inside think of them.
    if (t && t->considered <= 3) {
        wchar_t nm[128] = L"?";
        if (ResolveFuncName(t->cls, nm, 128) && IsLootName(nm)) {
            float al[3] = { 0, 0, 0 }; ActorLocation(actor, al);
            float cull = 0.f;
            SafeCopy((uint8_t*)actor + OFF::AActor_NetCullDistanceSquared, &cull, 4);
            uint8_t dorm = 0, rep = 0;
            SafeCopy((uint8_t*)actor + OFF::AActor_NetDormancy, &dorm, 1);
            SafeCopy((uint8_t*)actor + OFF::AActor_bReplicates, &rep, 1);
            float d = -1.f;
            for (int i = 0; i < g_remoteCount; ++i) {
                float p[3];
                if (!RemotePawnLocation(g_remote[i], p)) continue;
                const float dx = p[0]-al[0], dy = p[1]-al[1], dz = p[2]-al[2];
                d = sqrtf(dx*dx + dy*dy + dz*dz);
                break;
            }
            L("[pl] %ls %p wird betrachtet: Klassen-Policy=%d, repliziert=%d, Dormancy=%d, Cull=%.0f m, Abstand zum Remote-Spieler %.0f",
              nm, actor, ClassPolicyByte(graph, t->cls), rep & 1, dorm,
              cull > 0.f ? sqrtf(cull) / 100.0f : 0.0f, d);
        }
    }
    return g_origRepSingle ? g_origRepSingle(graph, actor, a3, a4, a5, a6, a7, a8, a9, a10) : nullptr;
}

// ---- stage 3: the actor really gets a channel on a connection -------------
typedef void (__fastcall* tSetChannelActor)(void* channel, void* actor, int32_t flags);
static tSetChannelActor g_origSetChannelActor = nullptr;
static volatile LONG    g_channelsOpened = 0;
static volatile LONG    g_ownChannels    = 0;   // v58v: channels for the remote player's own actors

// v59d: the weapon's channel was opened THIRTEEN times in one round, always on
// the same connection -- so it is being closed again just as often, and a
// channel that keeps closing can never carry a property delta. Remember which
// channel carries which of the remote player's own actors, and say who closes
// it (SetChannelActor(channel, nullptr) with the caller's RVA).
static void* g_ownChanCh[64];
static void* g_ownChanAc[64];
static int   g_ownChanN = 0;
static volatile LONG g_ownChanClosed = 0;

static void OwnChanRemember(void* ch, void* actor) {
    for (int i = 0; i < g_ownChanN; ++i) if (g_ownChanCh[i] == ch) { g_ownChanAc[i] = actor; return; }
    if (g_ownChanN >= 64) return;
    g_ownChanCh[g_ownChanN] = ch; g_ownChanAc[g_ownChanN] = actor; ++g_ownChanN;
}
static void* OwnChanActor(void* ch) {
    for (int i = 0; i < g_ownChanN; ++i) if (g_ownChanCh[i] == ch) return g_ownChanAc[i];
    return nullptr;
}
// v73: the reverse lookup -- which channel currently carries this actor.
static void* OwnChanFor(void* actor) {
    if (!actor) return nullptr;
    for (int i = g_ownChanN - 1; i >= 0; --i)        // newest wins
        if (g_ownChanAc[i] == actor) return g_ownChanCh[i];
    return nullptr;
}

static void* g_zoneChanRing[8] = {};            // v120: every zone channel we have seen open
static int   g_zoneChanRingN = 0;
static void  ZoneChanRemember(void* ch) {
    for (int i = 0; i < 8; ++i) if (g_zoneChanRing[i] == ch) return;
    g_zoneChanRing[g_zoneChanRingN & 7] = ch; ++g_zoneChanRingN;
}
static bool  ZoneChanKnown(void* ch) {
    for (int i = 0; i < 8; ++i) if (g_zoneChanRing[i] == ch) return true;
    return false;
}
static void __fastcall MySetChannelActor(void* channel, void* actor, int32_t flags) {
    if (!actor) {
        // v120: the zone channel CLOSING, with the caller that closed it and the
        // zone's live dormancy -- this is the event that loses the phase.
        if (channel && ZoneChanKnown(channel) && g_blueZone) {
            uint8_t d = 0, wd = 0;
            SafeCopy((uint8_t*)g_blueZone + OFF::AActor_NetDormancy, &d, 1);
            void* gi = g_zoneInfoPtr;   // graph global-actor-info, if we have it
            if (gi) SafeCopy((uint8_t*)gi + 0x28, &wd, 1);   // bWantsToBeDormant area
            L("[zc] *** Zonen-Kanal %p GESCHLOSSEN -- Zone %p NetDormancy=%d bWantsToBeDormant~%d,"
              " Aufrufer base+0x%llX ***",
              channel, g_blueZone, d, wd,
              (unsigned long long)((uintptr_t)_ReturnAddress() - g_base));
        }
        if (void* was = OwnChanActor(channel)) {
            LONG c = InterlockedIncrement(&g_ownChanClosed);
            if (c <= 25) {
                wchar_t cn[128] = L"?";
                void* cls = SafePtr((uint8_t*)was + OFF::UObject_Class);
                if (cls) ResolveFuncName(cls, cn, 128);
                L("[ch] Kanal %p des eigenen Actors %ls %p wird GESCHLOSSEN (Aufrufer +0x%llX) -- #%ld",
                  channel, cn, was, (unsigned long long)((uintptr_t)_ReturnAddress() - g_base), c);
            }
        }
    }
    if (actor) {
        ChanMapPut(channel, actor);                 // v121: remember the pairing
        InterlockedIncrement(&g_channelsOpened);
        // v108: the symptom is per-JOIN, not per-round -- same host state, and
        // on one join the PC sees the zone and the match info, on the next it
        // does not. That is an ordering race at join time.
        //
        // The client learns the zone through a replicated OBJECT REFERENCE on
        // the GameState. Such a reference only resolves if the referenced actor
        // has a channel on that connection when the reference arrives. So the
        // question is simply which of these two channels opens first, and this
        // prints both with timestamps so a good join can be diffed against a
        // bad one.
        // v111: the moment the zone gets a channel is the moment the GameState's
        // zone reference becomes resolvable for that client. Do NOT nudge from
        // inside the channel code (rule 2 -- this runs inside the engine's own
        // channel setup); raise a flag and let the driver tick do it.
        if (actor == g_blueZone) {
            g_zoneChannelJustOpened = true; InterlockedIncrement(&g_zoneChannelOpens);
            ZoneChanRemember(channel);                           // v120
            uint8_t d = 0; SafeCopy((uint8_t*)actor + OFF::AActor_NetDormancy, &d, 1);
            L("[zc] Zonen-Kanal %p GEOEFFNET -- Zone %p NetDormancy=%d (1=Awake 2=DormantAll 3=DormantPartial 4=Initial)",
              channel, actor, d);
        }
        if (actor == g_blueZone || actor == SafePtr((uint8_t*)g_world + OFF::UWorld_GameState)) {
            const bool isZone = (actor == g_blueZone);
            void* gs  = SafePtr((uint8_t*)g_world + OFF::UWorld_GameState);
            void* ref = (gs && g_zoneRefOff) ? SafePtr((uint8_t*)gs + g_zoneRefOff) : nullptr;
            L("[jo] t=%lu ms  KANAL fuer %s %p auf Verbindung %p"
              " | Zonen-Referenz auf der GameState = %p %s",
              (unsigned long)GetTickCount(), isZone ? "die BlueZone" : "die GameState",
              actor, channel, ref,
              !g_zoneRefOff ? "(Offset noch unbekannt)"
                            : (ref == g_blueZone && ref ? "(gesetzt)" : "(NULL -- der Client bekaeme nichts)"));
        }
        // v58v: does the remote player's OWN weapon ever get a channel? If it
        // never does, no property of it can reach him and the magazine has to
        // travel a different way; if it does, the channel is not the problem.
        if (g_remoteCount > 0 && OwnedByRemote(actor)) {
            OwnChanRemember(channel, actor);            // v59d
            LONG c = InterlockedIncrement(&g_ownChannels);
            if (c <= 25) {
                wchar_t cn[128] = L"?";
                void* cls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
                if (cls) ResolveFuncName(cls, cn, 128);
                L("[ch] Kanal fuer einen eigenen Actor des Remote-Spielers: %ls %p auf Verbindung %p -- #%ld",
                  cn, actor, channel, c);
            }
        }
        PipeTally* t = PipeForActor(actor);
        if (t) {
            InterlockedIncrement(&t->channel);
            if (t->channel <= 2) {
                wchar_t nm[128] = L"?";
                if (ResolveFuncName(t->cls, nm, 128) && IsLootName(nm))
                    L("[pl] *** %ls %p bekommt einen Kanal auf Verbindung %p -- wird also gesendet", nm, actor, channel);
            }
        }
    }
    if (g_origSetChannelActor) g_origSetChannelActor(channel, actor, flags);
}

static void LogLootPaths() {
    if (g_lootRetN <= 0) { L("[lr] noch kein Pickup ueber AddNetworkActor gesehen"); return; }
    L("[lr] Registrierungspfade der Pickups (%d verschiedene):", g_lootRetN);
    for (int i = 0; i < g_lootRetN; ++i)
        L("[lr]   RVA 0x%08llX -- %ld Pickups", (unsigned long long)g_lootRets[i], g_lootRetHits[i]);
}

static void LogPipeline() {
    if (g_pipeN <= 0) { L("[pl] noch keine Actors beobachtet"); return; }
    L("[pl] Weg der Actors zum Client (%d Klassen beobachtet%s, %ld ReplicateSingleActor-Aufrufe, %ld Kanaele): Klasse -- hinzugefuegt / betrachtet / Kanal",
      g_pipeN, g_pipeFull ? ", TABELLE VOLL" : "", g_repSingleCalls, g_channelsOpened);
    L("[wk] geweckt: %ld Actors insgesamt, %ld bereits vorher geweckt uebersprungen", g_wokenActors, g_wakeSkipped);
    bool done[256] = { false };
    // loot classes first, then the busiest ones
    for (int pass = 0; pass < 2; ++pass) {
        for (int shown = 0; shown < (pass == 0 ? 128 : 16); ++shown) {
            int best = -1;
            for (int i = 0; i < g_pipeN; ++i) {
                if (done[i]) continue;
                if (pass == 0) {
                    wchar_t nm[128] = L"?";
                    if (!ResolveFuncName(g_pipe[i].cls, nm, 128) || !IsLootName(nm)) continue;
                    best = i; break;
                }
                const LONG scoreI = g_pipe[i].added + g_pipe[i].woken + g_pipe[i].channel;
                const LONG scoreB = best < 0 ? -1 : g_pipe[best].added + g_pipe[best].woken + g_pipe[best].channel;
                if (best < 0 || scoreI > scoreB) best = i;
            }
            if (best < 0) break;
            done[best] = true;
            wchar_t nm[128] = L"?";
            ResolveFuncName(g_pipe[best].cls, nm, 128);
            L("[pl]   %s%ls -- %ld / %ld / %ld (geweckt %ld)", pass == 0 ? "*** " : "    ",
              nm, g_pipe[best].added, g_pipe[best].considered, g_pipe[best].channel, g_pipe[best].woken);
        }
    }
}

// ---- v58i: where is a streaming level actually stuck? ---------------------
//  v58h wrote LevelLODIndex / the visibility bits straight into the
//  ULevelStreaming. That was a mistake and the log shows it: 861 of those
//  writes in two minutes, i.e. the engine put its own value back every single
//  frame. CommitTileStreamingState compares its arguments against the object
//  and, finding a difference it did not cause, runs the whole set-LOD path
//  again -- which unloads and reloads the package. The level never settles.
//  v58i drops the raw writes, reads the state machine instead, and changes the
//  engine's DECISION at its source (see MyCommitTile below).
static bool          g_commitFix  = true;    // -nocommitfix
static volatile LONG g_commitCalls = 0, g_commitForced = 0;
// v58q: block-on-load for the tile the landed remote player stands on.
static bool          g_blockLoad  = true;    // -noblockload
static volatile LONG g_blockUsed  = 0;
static const LONG    kBlockBudget = 24;
// v58r: call the commit ourselves for tiles the engine leaves unloaded.
static bool          g_pullTiles  = true;    // -nopulltiles
static bool          g_lastA8 = false, g_lastA9 = false;
static volatile LONG g_pulled = 0, g_pullSkipped = 0;

static const char* StreamStateName(uint8_t s) {
    switch (s) {
        case 0: return "Entfernt";
        case 1: return "NichtGeladen";
        case 2: return "LadenFehlgeschlagen";
        case 3: return "Laedt";
        case 4: return "GeladenUnsichtbar";
        case 5: return "WirdSichtbar";
        case 6: return "GeladenSichtbar";
        case 7: return "WirdUnsichtbar";
        default: return "?";
    }
}

// Is this ULevel one of the levels the world has actually added?
static bool LevelIsInWorld(void* lvl) {
    if (!lvl || !g_world) return false;
    void** levels = (void**)SafePtr((uint8_t*)g_world + RVA::UWorld_Levels);
    int32_t n = SafeI32((uint8_t*)g_world + RVA::UWorld_Levels + 8);
    if (!levels || n <= 0 || n > 8192) return false;
    for (int i = 0; i < n; ++i) if (SafePtr(levels + i) == lvl) return true;
    return false;
}

//  Print, for every detail tile around a point, exactly how far the engine got
//  with it. This is the measurement that separates the four possible causes:
//  never requested (NichtGeladen), stuck in I/O (Laedt), loaded but never made
//  visible (GeladenUnsichtbar / WirdSichtbar), or fully visible -- in which
//  case the world IS there and the problem is not streaming at all.
static void StreamDiag(void* comp, const float* loc, const char* who) {
    uint8_t* tiles = (uint8_t*)SafePtr((uint8_t*)comp + 0x48);
    int32_t  n     = SafeI32((uint8_t*)comp + 0x50);
    void**   lvls  = (void**)SafePtr((uint8_t*)comp + 0x168);
    int32_t  ln    = SafeI32((uint8_t*)comp + 0x170);
    if (!tiles || !lvls || n <= 0 || n > 20000 || ln <= 0) return;
    int shown = 0, states[8] = { 0 };
    for (int i = 0; i < n && i < ln; ++i) {
        uint8_t* t = tiles + (size_t)i * 0xE0;
        int32_t px = SafeI32(t + 0x2C), py = SafeI32(t + 0x30);
        float bmin[2], bmax[2];
        if (!SafeCopy(t + 0x38, bmin, 8) || !SafeCopy(t + 0x44, bmax, 8)) continue;
        const float minx = px + bmin[0], miny = py + bmin[1];
        const float maxx = px + bmax[0], maxy = py + bmax[1];
        uint8_t* lvl = (uint8_t*)SafePtr(lvls + i);
        if (!lvl) continue;
        uint8_t cs = 0;
        if (SafeCopy(lvl + OFF::LevelStreaming_CurState, &cs, 1) && cs < 8) ++states[cs];
        if (maxx - minx > 50000.0f || maxy - miny > 50000.0f) continue;    // proxy tile
        if (shown >= 6) continue;
        if (loc[0] < minx - 25000.0f || loc[0] > maxx + 25000.0f ||
            loc[1] < miny - 25000.0f || loc[1] > maxy + 25000.0f) continue;
        uint8_t ts = 0, fl = 0;
        SafeCopy(lvl + OFF::LevelStreaming_TgtState, &ts, 1);
        SafeCopy(lvl + OFF::LevelStreaming_Flags, &fl, 1);
        void* loaded = SafePtr(lvl + OFF::LevelStreaming_LoadedLvl);
        int32_t acts = loaded ? SafeI32((uint8_t*)loaded + RVA::ULevel_Actors + 8) : -1;
        L("[st] %s Kachel #%d Box[%.0f..%.0f, %.0f..%.0f] Prio=%d LODs=%d: LODIndex=%d, soll geladen=%d sichtbar=%d | Zustand %s -> Ziel %d | LoadedLevel %p mit %d Actors, in World->Levels: %s",
          who, i, minx, maxx, miny, maxy, SafeI32(t + 0xAC), SafeI32(t + 0x18),
          SafeI32(lvl + OFF::LevelStreaming_LODIndex), (fl >> 1) & 1, fl & 1,
          StreamStateName(cs), ts, loaded, acts, LevelIsInWorld(loaded) ? "ja" : "NEIN");
        ++shown;
    }
    if (!shown) L("[st] %s {%.0f,%.0f}: keine Detail-Kachel in Reichweite", who, loc[0], loc[1]);
    L("[st] Alle %d Kacheln nach Zustand: Entfernt %d, NichtGeladen %d, LadenFehlgeschlagen %d, Laedt %d, GeladenUnsichtbar %d, WirdSichtbar %d, GeladenSichtbar %d, WirdUnsichtbar %d",
      n, states[0], states[1], states[2], states[3], states[4], states[5], states[6], states[7]);
}

// ---- v58i: change the engine's decision, not its result -------------------
//  The commit is the one place where "this tile should be loaded / visible /
//  at this LOD" turns into engine action. For a tile next to a remote player
//  we answer the way the engine would answer for a LOCAL player: loaded,
//  visible, no LOD. Everything downstream -- the package load, AddToWorld, the
//  incremental component registration -- is then the engine's own, unmodified
//  path, and the commit's "nothing changed" early-out keeps working, so there
//  is no per-frame churn. Switch: -nocommitfix.
typedef bool (__fastcall* tCommitTile)(void* comp, void* world, int32_t tileIdx,
                                       bool bShouldBeLoaded, bool bShouldBeVisible,
                                       bool bBlockOnLoad, int32_t lodIndex, bool a8, bool a9);
static tCommitTile g_origCommitTile = nullptr;
// v58s: the setters the commit body calls, for our own use past its early-out.
typedef void (__fastcall* tSetLsBool)(void* levelStreaming, uint8_t value);
typedef void (__fastcall* tSetLsInt) (void* levelStreaming, int32_t value);

static bool TileIsAtRemote(void* comp, int32_t idx) {
    if (g_remoteCount == 0) return false;
    uint8_t* tiles = (uint8_t*)SafePtr((uint8_t*)comp + 0x48);
    int32_t  n     = SafeI32((uint8_t*)comp + 0x50);
    if (!tiles || idx < 0 || idx >= n) return false;
    uint8_t* t = tiles + (size_t)idx * 0xE0;
    int32_t px = SafeI32(t + 0x2C), py = SafeI32(t + 0x30);
    float bmin[2], bmax[2];
    if (!SafeCopy(t + 0x38, bmin, 8) || !SafeCopy(t + 0x44, bmax, 8)) return false;
    const float minx = px + bmin[0], miny = py + bmin[1];
    const float maxx = px + bmax[0], maxy = py + bmax[1];
    if (maxx - minx > 50000.0f || maxy - miny > 50000.0f) return false;   // proxy tile
    const float margin = 25000.0f;   // v58s: same radius the pull uses, so the
                                     // engine does not argue back over the same tiles
    for (int i = 0; i < g_remoteCount; ++i) {
        float p[3];
        if (!RemotePawnLocation(g_remote[i], p)) continue;
        if (p[0] > minx - margin && p[0] < maxx + margin &&
            p[1] > miny - margin && p[1] < maxy + margin) return true;
    }
    return false;
}

// v58q: is the remote player STANDING on this tile, i.e. close to the ground
// and inside its box? The v58p round measured what this is for: he landed at
// t=142 and the host had 37 actors around him; only at t=162 -- twenty seconds
// later, and after the round had ended -- were there 726. For twenty seconds
// there were no buildings to collide with (he fell through them) and no loot
// sources to fire. The tile he is actually standing on is worth waiting a
// frame for; everything else stays time-sliced as before.
static bool TileIsUnderRemote(void* comp, int32_t idx) {
    if (g_remoteCount == 0) return false;
    uint8_t* tiles = (uint8_t*)SafePtr((uint8_t*)comp + 0x48);
    int32_t  n     = SafeI32((uint8_t*)comp + 0x50);
    if (!tiles || idx < 0 || idx >= n) return false;
    uint8_t* t = tiles + (size_t)idx * 0xE0;
    int32_t px = SafeI32(t + 0x2C), py = SafeI32(t + 0x30);
    float bmin[2], bmax[2];
    if (!SafeCopy(t + 0x38, bmin, 8) || !SafeCopy(t + 0x44, bmax, 8)) return false;
    const float minx = px + bmin[0], miny = py + bmin[1];
    const float maxx = px + bmax[0], maxy = py + bmax[1];
    if (maxx - minx > 50000.0f || maxy - miny > 50000.0f) return false;   // proxy tile
    const float margin = 3000.0f;                                          // 30 m
    for (int i = 0; i < g_remoteCount; ++i) {
        float p[3];
        if (!RemotePawnLocation(g_remote[i], p)) continue;
        if (p[2] > 12000.0f) continue;            // still in the air: no blocking
        if (p[0] > minx - margin && p[0] < maxx + margin &&
            p[1] > miny - margin && p[1] < maxy + margin) return true;
    }
    return false;
}

static bool __fastcall MyCommitTile(void* comp, void* world, int32_t tileIdx,
                                    bool bShouldBeLoaded, bool bShouldBeVisible,
                                    bool bBlockOnLoad, int32_t lodIndex, bool a8, bool a9) {
    if (!g_origCommitTile)
        return false;
    InterlockedIncrement(&g_commitCalls);
    if (g_commitFix && g_drv && g_world && world == g_world && TileIsAtRemote(comp, tileIdx) &&
        (!bShouldBeLoaded || !bShouldBeVisible || lodIndex != -1)) {
        LONG c = InterlockedIncrement(&g_commitForced);
        if (c <= 12 || (c % 2000) == 0)
            L("[ct] Kachel #%d beim Remote-Spieler: Engine wollte geladen=%d sichtbar=%d LOD=%d -> geladen=1 sichtbar=1 LOD=-1 (#%ld)",
              tileIdx, bShouldBeLoaded, bShouldBeVisible, lodIndex, c);
        bShouldBeLoaded  = true;
        bShouldBeVisible = true;
        lodIndex         = -1;
    }
    // v58r: remember what the engine passes for the last two arguments, so our
    // own calls to this function (ForceTilesAroundRemote) look like its own.
    g_lastA8 = a8; g_lastA9 = a9;
    // The tile he is standing on, and only while it is not there yet. The
    // budget keeps this from turning into a long freeze on the host: at most
    // kBlockBudget blocking commits per round, a handful of frames in total.
    if (g_blockLoad && g_commitFix && g_world && world == g_world && !bBlockOnLoad &&
        bShouldBeLoaded && bShouldBeVisible && g_blockUsed < kBlockBudget &&
        TileIsUnderRemote(comp, tileIdx)) {
        bBlockOnLoad = true;
        LONG b = InterlockedIncrement(&g_blockUsed);
        if (b <= 20)
            L("[ct] Kachel #%d unter dem gelandeten Remote-Spieler wird blockierend geladen (#%ld von %d)",
              tileIdx, b, kBlockBudget);
    }
    return g_origCommitTile(comp, world, tileIdx, bShouldBeLoaded, bShouldBeVisible,
                            bBlockOnLoad, lodIndex, a8, a9);
}

// ---- v58r: pull the tiles the engine refuses to load ----------------------
//  The v58q round settled where the loot is: nowhere. The level walk saw
//  1098 levels holding 5409 actors between them -- five per level, the
//  signature of HLOD proxies -- and 35 building actors in the whole world.
//  Around the landed remote player the census found 4 actors for forty
//  seconds and 488 at the end, with fences and door frames but not a single
//  BPB- building, so there was nothing carrying an item spawn box to fire.
//  Meanwhile every tile pairs up: a Prio-600 tile that loads, and a Prio-500
//  tile over the same ground that reads ShouldBeLoaded=0 / NichtGeladen in
//  every single log line we have ever taken.
//  The commit hook could not help there, because the hook only sees tiles the
//  engine decides to commit and a tile it never wants is never committed.
//  v58s: and calling the commit ourselves (v58r) did not help either, because
//  it returns immediately while the tile's evaluation byte is 3 -- which it
//  always is. So we call the three setters the commit body itself calls once
//  past that byte. Engine setters, which notify
//  UWorld::UpdateStreamingLevelShouldBeConsidered, not raw field writes: the
//  v58h lesson holds.
static void ForceTilesAroundRemote() {
    if (!g_pullTiles || !g_world || g_remoteCount == 0) return;
    void* comp = SafePtr((uint8_t*)g_world + 0x608);
    if (!comp) return;
    uint8_t* tiles = (uint8_t*)SafePtr((uint8_t*)comp + 0x48);
    int32_t  n     = SafeI32((uint8_t*)comp + 0x50);
    void**   lvls  = (void**)SafePtr((uint8_t*)comp + 0x168);
    int32_t  ln    = SafeI32((uint8_t*)comp + 0x170);
    if (!tiles || !lvls || n <= 0 || n > 20000) return;
    const float margin = 25000.0f;          // 250 m around him
    int budget = 6;                         // per sweep (~2 s), so the host keeps up
    for (int i = 0; i < n && i < ln && budget > 0; ++i) {
        uint8_t* t = tiles + (size_t)i * 0xE0;
        int32_t px = SafeI32(t + 0x2C), py = SafeI32(t + 0x30);
        float bmin[2], bmax[2];
        if (!SafeCopy(t + 0x38, bmin, 8) || !SafeCopy(t + 0x44, bmax, 8)) continue;
        const float minx = px + bmin[0], miny = py + bmin[1];
        const float maxx = px + bmax[0], maxy = py + bmax[1];
        if (maxx - minx > 50000.0f || maxy - miny > 50000.0f) continue;   // world-sized proxy
        bool closeToRemote = false;    // NOT 'near' -- that is a windows.h macro
        for (int r = 0; r < g_remoteCount && !closeToRemote; ++r) {
            float p[3];
            if (!RemotePawnLocation(g_remote[r], p)) continue;
            if (p[0] > minx - margin && p[0] < maxx + margin &&
                p[1] > miny - margin && p[1] < maxy + margin) closeToRemote = true;
        }
        if (!closeToRemote) continue;
        uint8_t* lvl = (uint8_t*)SafePtr(lvls + i);
        if (!lvl) continue;
        uint8_t  state  = 0;
        SafeCopy(lvl + 0xC8, &state, 1);
        const int32_t lodIdx = SafeI32(lvl + 0xC0);
        if (state == 6 && lodIdx == -1) { InterlockedIncrement(&g_pullSkipped); continue; }  // already there
        if (state == 3 || state == 5) continue;                    // busy loading / becoming visible
        --budget;
        // v58s: NOT through CommitTileStreamingState -- it returns immediately
        // while the tile's evaluation byte is 3, which it always is (see
        // RVA::LevelStreaming_Set*). These are the three setters the commit
        // body calls itself, in its own order: loaded, visible, LOD.
        void** lvt = (void**)SafePtr(lvl);
        void*  setLoaded = lvt ? SafePtr((uint8_t*)lvt + RVA::LevelStreaming_SetShouldBeLoadedVT) : nullptr;
        if (!setLoaded || !InModule(setLoaded)) continue;
        __try {
            ((tSetLsBool)setLoaded)(lvl, 1);
            ((tSetLsBool)(g_base + RVA::LevelStreaming_SetShouldBeVisible))(lvl, 1);
            ((tSetLsInt) (g_base + RVA::LevelStreaming_SetLevelLODIndex))(lvl, -1);
        } __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        LONG c = InterlockedIncrement(&g_pulled);
        if (c <= 40) {
            uint8_t* loaded = (uint8_t*)SafePtr(lvl + 0x140);
            L("[fx] Kachel #%d (Prio %d, Zustand %d, LOD %d) beim Remote-Spieler nachgefordert -> Level %p mit %d Actors (#%ld)",
              i, SafeI32(t + 0xAC), state, lodIdx, loaded,
              loaded ? SafeI32(loaded + RVA::ULevel_Actors + 8) : -1, c);
        }
    }
}

static void __fastcall MyUpdateStreamingState(void* comp, const float* locs, const float* dirs,
                                              int32_t num, int32_t lod, float scale) {
    if (!g_origUpdStream) return;
    InterlockedIncrement(&g_streamCalls);
    if (!g_streamRemote || !g_drv || !g_world || g_remoteCount == 0 || num < 0 || num > 12 ||
        SafePtr((uint8_t*)comp + OFF::UWorldComposition_World) != g_world) {
        g_origUpdStream(comp, locs, dirs, num, lod, scale);
        return;
    }
    if (g_objArrayState == 0) {
        // Calibrate the GUObjectArray layout on an object we know is alive.
        g_objArrayState = IsObjectAlive(g_world) ? 1 : -1;
        L("[ws] GUObjectArray-Layout %s (Probe mit World %p)",
          g_objArrayState > 0 ? "bestaetigt -> Lebendigkeitspruefung aktiv"
                              : "ABWEICHEND -> nur Controller-Abgleich", g_world);
        if (g_objArrayState < 0)
            L("[!] ACHTUNG: IsObjectAlive ist in diesem Build WERTLOS (Objektzeiger liegt bei +8 und ist "
              "verschleiert). Jede Pruefung darauf muss MaybeAlive benutzen, sonst schaltet sie sich "
              "stillschweigend selbst ab -- genau das ist v58w bis v58z passiert.");
    }
    // ---- v58s: THE REMOTE PLAYER GOES FIRST ------------------------------
    //  [stated] Alen: a standalone match without the listen server runs fine.
    //  That is the whole answer, and this is where the difference lives.
    //  In standalone there is exactly ONE streaming point and it is the
    //  player, so World Composition streams full base levels around him.
    //  Here the host was point 0 and the remote player was appended after it
    //  -- and every point after index 0 is a SUB POINT, which the tile lambda
    //  (0x4752FC2) treats as second class: for a level that is not loaded yet
    //  it multiplies the distance by the sub-point scale clamped to
    //  [0.15, 0.45] for the base level. So the host, sitting in the aircraft
    //  doing nothing, got full-distance base levels, and the player who was
    //  actually on the ground got at most 45 % -- which is exactly what the
    //  logs show: he stands on a tile at LODIndex=0 whose level holds three
    //  actors, an HLOD proxy, with no buildings and therefore no loot.
    //  So the remote players go in first, as the primary point, and the host
    //  is appended after them. And while the host is up in the aircraft his
    //  point is dropped entirely -- that is the standalone situation exactly,
    //  one point, the player on the ground -- which also halves the tile load
    //  on a machine that is sitting at 97 % memory. -nohostfirst restores the
    //  old order, -hostpoint always keeps the host's point.
    enum { MAXPTS = 20 };
    float L3[MAXPTS * 3], D3[MAXPTS * 3];
    int n = 0;
    int added = 0;
    static int diag = 0;
    float hostPt[3] = {0,0,0}, hostDir[3] = {0,0,0};
    bool  haveHost = (num > 0) && locs && SafeCopy(locs, hostPt, 12);
    if (haveHost) {
        if (!dirs || !SafeCopy(dirs, hostDir, 12)) memset(hostDir, 0, 12);
        if (Finite3(hostPt)) memcpy(g_lastHostLoc, hostPt, 12);   // v58f: for the spawn statistics
    }
    // The host is only worth a streaming point when he is near the ground.
    const bool hostOnGround = haveHost && hostPt[2] < 12000.0f;
    const bool keepHost     = g_hostPoint || !g_remoteFirst || hostOnGround;
    for (int i = 0; i < g_remoteCount && n < MAXPTS; ++i) {
        float loc[3];
        if (!RemotePawnLocation(g_remote[i], loc)) continue;
        memcpy(&L3[n * 3], loc, 12);
        memset(&D3[n * 3], 0, 12);           // no direction bias for remote anchors
        ++n; ++added;
        memcpy(g_lastRemoteLoc, loc, 12);
        if (diag < 5) {
            // Diagnostics only: what would the game's own view-point helper
            // say for this controller? (Same call the local path makes.)
            float hl[3] = { 0, 0, 0 }, hd[3] = { 0, 0, 0 };
            bool ok = false;
            __try { ((tPcViewPoint)(g_base + RVA::PC_StreamingViewPoint))(g_remote[i].controller, hl, hd); ok = true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
            L("[ws] Anker #%d: Pawn %p bei {%.0f, %.0f, %.0f}; Spiel-Helfer -> %s {%.0f, %.0f, %.0f} dir {%.2f, %.2f, %.2f}; lokale Anker: %d, Lod=%d, Scale=%.2f",
              i, g_remote[i].pawn, loc[0], loc[1], loc[2], ok ? "OK" : "FAULT",
              hl[0], hl[1], hl[2], hd[0], hd[1], hd[2], num, lod, scale);
            ++diag;
        }
    }
    // Now the host's own point, behind the remote players. If no remote pawn
    // could be located we fall back to the original order so the host is never
    // left without a streaming point.
    int hostSlot = -1;
    if (added == 0 || !g_remoteFirst) {
        n = 0;
        for (int i = 0; i < num && n < MAXPTS; ++i, ++n) {
            if (!locs || !SafeCopy(locs + i * 3, &L3[n * 3], 12)) memset(&L3[n * 3], 0, 12);
            if (!dirs || !SafeCopy(dirs + i * 3, &D3[n * 3], 12)) memset(&D3[n * 3], 0, 12);
        }
        hostSlot = (num > 0) ? 0 : -1;
        for (int i = 0; i < g_remoteCount && n < MAXPTS && g_remoteFirst == false; ++i) {
            float loc[3];
            if (!RemotePawnLocation(g_remote[i], loc)) continue;
            memcpy(&L3[n * 3], loc, 12);
            memset(&D3[n * 3], 0, 12);
            ++n;
        }
    } else if (keepHost && haveHost && n < MAXPTS) {
        memcpy(&L3[n * 3], hostPt,  12);
        memcpy(&D3[n * 3], hostDir, 12);
        hostSlot = n;
        ++n;
    }
    {
        static int orderLog = 0;
        static bool lastKeep = true;
        if (g_remoteFirst && added > 0 && (orderLog < 3 || keepHost != lastKeep)) {
            L("[ws] Reihenfolge: Remote-Spieler ist Punkt 0 (Primaerpunkt), Host %s (Host-z %.0f) -- %d Punkte gesamt",
              (hostSlot > 0) ? "als Unterpunkt dahinter" : "hat gerade KEINEN eigenen Punkt (im Flieger)",
              haveHost ? hostPt[2] : 0.0f, n);
            lastKeep = keepHost; ++orderLog;
        }
    }
    for (int k = 0; k < added; ++k) InterlockedIncrement(&g_streamExtra);
    // v58d: sub points stream with half the distance by default -- give the
    // remote anchors the full distance. The float is CVar backed, so we just
    // keep writing our value (the game may reset it).
    if (g_subScale && added > 0) {
        float cur = 0.0f, one = 1.0f;
        if (SafeCopy((void*)(g_base + RVA::CVar_SubPointDistanceScale), &cur, 4) && cur != 1.0f) {
            SafeCopy(&one, (void*)(g_base + RVA::CVar_SubPointDistanceScale), 4);
            if (InterlockedIncrement(&g_subScaleWrites) <= 3)
                L("[ws] s.SubPointLevelStreamingDistanceScale %.2f -> 1.0 (Remote-Anker bekommen die volle Streaming-Distanz)", cur);
        }
    }
    g_origUpdStream(comp, L3, D3, n, lod, scale);
    // v58d diagnostics, every ~10 s: tile state under the host and under the
    // first remote anchor, plus the streaming CVars the lambda reads.
    static int tdTick = 0;
    if (added > 0 && (++tdTick % 400) == 0) {
        float sc = 0, cd = 0; int32_t en = 0, hp = 0;
        SafeCopy((void*)(g_base + RVA::CVar_SubPointDistanceScale), &sc, 4);
        SafeCopy((void*)(g_base + RVA::CVar_SubPointCullDistance), &cd, 4);
        en = SafeI32((void*)(g_base + RVA::CVar_EnableSubPoint));
        hp = SafeI32((void*)(g_base + RVA::CVar_HideWCLODLevelPriority));
        float dyn0 = 0.f, dyn1 = 0.f;    // the game's own dynamic streaming scaling
        SafeCopy((uint8_t*)comp + 0x130, &dyn0, 4);
        SafeCopy((uint8_t*)comp + 0x134, &dyn1, 4);
        L("[td] CVars: SubPointScale=%.2f SubPointCull=%.0f EnableSubPoint=%d HideWCLODLevelPriority=%d | Spiel-Skalierung comp+0x130=%.2f comp+0x134=%.2f | Punkte an die Engine: %d (lokal %d + remote %d) | [ct] Commits %ld, davon fuer den Remote-Spieler korrigiert %ld, blockierend geladen %ld von %d | [fx] Kacheln nachgefordert %ld (schon da: %ld) | [ow] eigene Actors des Remote-Spielers %ld, Aktualisierungen %ld, verworfen %ld, NetMode-Antworten %ld, eigene Kanaele %ld",
          sc, cd, en, hp, dyn0, dyn1, n, num, added, g_commitCalls, g_commitForced, g_blockUsed, kBlockBudget, g_pulled, g_pullSkipped, g_ownFound, g_ownPoked, g_ownDropped, g_ownModeSubst, g_ownChannels);
        // v58s: the remote player is point 0 now, the host sits at hostSlot
        // (or has no point at all while he is in the aircraft).
        const int remoteSlot = (g_remoteFirst && added > 0) ? 0 : num;
        if (hostSlot >= 0) {
            L("[td] Host-Punkt der Engine: Punkt #%d {%.0f, %.0f, %.0f}",
              hostSlot, L3[hostSlot*3], L3[hostSlot*3+1], L3[hostSlot*3+2]);
            TileDiag(comp, &L3[hostSlot * 3], "Host");
            StreamDiag(comp, &L3[hostSlot * 3], "HOST");
        } else {
            L("[td] Der Host hat gerade keinen eigenen Streaming-Punkt (im Flieger) -- wie im Standalone zaehlt nur der Spieler am Boden");
        }
        if (remoteSlot < n) {
            TileDiag(comp, &L3[remoteSlot * 3], "Remote");
            StreamDiag(comp, &L3[remoteSlot * 3], "REMOTE");
        }
    }
}

// ===========================================================================
//  v58: THE ZONE ACTOR AND THE REPLICATION GRAPH
// ===========================================================================
//  Engine.ini gives the host UBasicReplicationGraph. That graph decides per
//  CLASS how far an actor replicates: UBasicReplicationGraph::InitGlobalActor-
//  ClassSettings (0x13B54C0) runs once inside InitListen, walks every actor
//  class and stores
//      cull = (CDO->bAlwaysRelevant || CDO->bOnlyRelevantToOwner)
//             ? 0 : CDO->NetCullDistanceSquared;
//  BP_BlueZone_C's CDO has neither flag and the engine default cull distance
//  (15000^2 = 150 m). Every later write to the LIVE actor (v50's 1e18) is
//  invisible to the graph -- it works from its own copy, taken from the class
//  info when the actor is added. So the client only got the zone once it
//  happened to be within 150 m of the zone actor, which is what "sometimes
//  after landing, sometimes never" and "only at the first zone change" were.
//  Dormancy never had anything to do with it (v57 measured DORM_Awake).
//
//  FIX
//  Before InitListen runs, find the BP_BlueZone_C class object, flip
//  bAlwaysRelevant (+0x1CC bit 0) on its CDO and raise the CDO's cull distance.
//  The graph then snapshots "always relevant, cull 0" for the class, and the
//  actor the game spawns 0.3 s later inherits the flag and is routed to the
//  AlwaysRelevant node. If the class is not loaded yet at that moment, the
//  UNetDriver::AddNetworkActor hook still flips the flag on the instance
//  (routing is right then, the per-class cull distance may still apply).
//
//  Abschalten ohne Neubau:  -nozonerelevant
// ===========================================================================
static bool     g_haveZoneKey  = false;
static FNameRaw g_zoneKey      = { 0, 0, 0 };   // FName("BP_BlueZone_C")
static void*    g_zoneClass    = nullptr;

// FName lookup WITHOUT creating the name: FNAME_Find (0) returns NAME_None
// (index 0) for a string the name table does not know.
static bool FindNameKey(const wchar_t* s, FNameRaw* out) {
    typedef FNameRaw* (__fastcall* tCtor)(FNameRaw* out, const wchar_t* str, int findType);
    auto ctor = (tCtor)(g_base + RVA::FName_Ctor);
    FNameRaw k = { 0, 0, 0 };
    bool ok = false;
    __try { ctor(&k, s, 0); ok = true; } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok || k.Comparison == 0) return false;
    *out = k;
    return true;
}

static bool MarkAlwaysRelevant(void* actor, const char* what) {
    uint8_t f = 0;
    if (!SafeCopy((uint8_t*)actor + OFF::AActor_bAlwaysRelevant, &f, 1)) return false;
    const uint8_t was = f;
    if (!(f & 1)) {
        f |= 1;
        if (!SafeCopy(&f, (uint8_t*)actor + OFF::AActor_bAlwaysRelevant, 1)) return false;
    }
    float cull = 0.0f; const float kHuge = 1.0e18f;
    SafeCopy((uint8_t*)actor + OFF::AActor_NetCullDistanceSquared, &cull, 4);
    if (cull == 225000000.0f)
        SafeCopy(&kHuge, (uint8_t*)actor + OFF::AActor_NetCullDistanceSquared, 4);
    L("[bz] %s %p: bAlwaysRelevant %s (Flagbyte +0x1CC war 0x%02X), NetCullDistanceSquared war %.1f",
      what, actor, (was & 1) ? "war schon gesetzt" : "GESETZT", was, cull);
    return true;
}

// Called from MyListen BEFORE InitListen. The GUObjectArray scan of the first
// v58 build found nothing (FUObjectItem holds the object pointer obfuscated,
// item+0 is only sometimes the plain pointer), so this only reports whether
// the class name exists yet. The real work happens in MyAddNetworkActor.
static void PrepareZoneClassBeforeListen(void* world) {
    (void)world;
    if (!g_zoneRelevant) { L("[bz] -nozonerelevant -> Zonenklasse bleibt distanzbegrenzt"); return; }
    if (!g_haveZoneKey) g_haveZoneKey = FindNameKey(L"BP_BlueZone_C", &g_zoneKey);
    L("[bz] FName BP_BlueZone_C %s", g_haveZoneKey ? "bekannt (Klasse geladen)" : "noch nicht in der Namenstabelle");
}

// The graph's own copy of the cull distance for ONE actor. Created from the
// class info when the actor is added; we zero it afterwards, so the
// AlwaysRelevant list is never distance-culled for this actor.
static bool ZoneGraphCullToZero(void* drv, void* actor, const char* when) {
    void* graph = SafePtr((uint8_t*)drv + OFF::UNetDriver_RepDriver);
    if (!graph) { L("[bz] kein ReplicationDriver am Treiber -> keine Graph-Korrektur (%s)", when); return false; }
    void** vt = (void**)SafePtr(graph);
    if (!IsKnownRepGraph(vt)) {
        L("[bz] Graph %p ist kein UBasicReplicationGraph -> Cull-Korrektur uebersprungen (%s)", graph, when);
        return false;
    }
    typedef void* (__fastcall* tGet)(void* map, void** actorRef);
    void* ref = actor; void* info = nullptr;
    __try { info = ((tGet)(g_base + RVA::GlobalActorInfoMap_Get))((uint8_t*)graph + 0xC0, &ref); }
    __except (EXCEPTION_EXECUTE_HANDLER) { info = nullptr; }
    if (!info) { L("[bz] Graph-Info des Zonen-Actors nicht erhalten (%s)", when); return false; }
    float cull = 0.0f, cullSq = 0.0f, zero = 0.0f;
    uint8_t wantsDormant = 0, off = 0;
    int32_t refs = SafeI32((uint8_t*)info + 0x128);
    SafeCopy((uint8_t*)info + 0x90, &cull, 4);
    SafeCopy((uint8_t*)info + 0x94, &cullSq, 4);
    SafeCopy((uint8_t*)info + 0x14, &wantsDormant, 1);
    SafeCopy(&zero, (uint8_t*)info + 0x90, 4);
    SafeCopy(&zero, (uint8_t*)info + 0x94, 4);
    SafeCopy(&off,  (uint8_t*)info + 0x14, 1);        // bWantsToBeDormant = false
    L("[bz] Graph-Info %p des Zonen-Actors (%s): CullDistance %.0f / Sq %.0f -> 0 / 0, bWantsToBeDormant %d -> 0, Refs(+0x128)=%d",
      info, when, cull, cullSq, wantsDormant, refs);
    g_zoneInfoPtr = info;
    return true;
}

// v58c self-heal, from ZoneTick (game thread, MaskOff window): the first v58b
// run showed the zone's graph entry REPLACED between the add (info A) and
// ZoneTick (info B, fresh from the class defaults) -- i.e. something took the
// actor out of the graph again. If the entry is not the one we fixed up, add
// the actor once more through UNetDriver::AddNetworkActor (our hook -> flags,
// routing, cull copy) and say so.
static void ZoneGraphSelfHeal() {
    if (!g_zoneRelevant || !g_blueZone || !g_drv) return;
    void* graph = SafePtr((uint8_t*)g_drv + OFF::UNetDriver_RepDriver);
    if (!graph) return;
    typedef void* (__fastcall* tGet)(void* map, void** actorRef);
    void* ref = g_blueZone; void* info = nullptr;
    __try { info = ((tGet)(g_base + RVA::GlobalActorInfoMap_Get))((uint8_t*)graph + 0xC0, &ref); }
    __except (EXCEPTION_EXECUTE_HANDLER) { info = nullptr; }
    if (!info) return;
    if (info == g_zoneInfoPtr) {
        // Still our entry -- keep its cull copy at zero no matter who touches it.
        float sq = 0.0f, zero = 0.0f;
        if (SafeCopy((uint8_t*)info + 0x94, &sq, 4) && sq != 0.0f) {
            SafeCopy(&zero, (uint8_t*)info + 0x90, 4);
            SafeCopy(&zero, (uint8_t*)info + 0x94, 4);
            static int n = 0;
            if (n++ < 10) L("[bz] Graph-Info %p: CullDistanceSquared war wieder %.0f -> 0", info, sq);
        }
        return;
    }
    if (g_zoneReadds >= 30) return;                    // something keeps removing it -- stop, the log has it
    LONG n = InterlockedIncrement(&g_zoneReadds);
    L("[bz] Graph-Eintrag des Zonen-Actors ist NEU (%p statt %p, Refs=%d) -> Actor wird erneut hinzugefuegt (#%ld)",
      info, g_zoneInfoPtr, SafeI32((uint8_t*)info + 0x128), n);
    typedef void (__fastcall* tAdd)(void* drv, void* actor);
    __try { ((tAdd)(g_base + RVA::NetDriver_AddNetworkActor))(g_drv, g_blueZone); }
    __except (EXCEPTION_EXECUTE_HANDLER) { L("[bz] [!] erneutes AddNetworkActor ist GEFAULTET"); }
}

// v58e diagnostics: UReplicationGraph::RemoveNetworkActor (graph vtable +0x2A8)
// is reachable without UNetDriver::RemoveNetworkActor (e.g. from
// UNetDriver::NotifyActorDestroyed / NotifyActorLevelUnloaded). Patch the slot
// in the Basic graph's vtable and log removals of the zone actor.
typedef void (__fastcall* tGraphRemove)(void* graph, void* actor);
static tGraphRemove g_origGraphRemove = nullptr;
static void __fastcall MyGraphRemoveNetworkActor(void* graph, void* actor) {
    InterlockedIncrement(&g_removeGraph);             // v116
    if (actor && actor == g_blueZone) {
        static int n = 0;
        if (n++ < 10)
            L("[bz] Graph::RemoveNetworkActor fuer den Zonen-Actor %p, Rueckkehradresse base+0x%llX",
              actor, (unsigned long long)((uintptr_t)_ReturnAddress() - g_base));
    }
    if (g_origGraphRemove) g_origGraphRemove(graph, actor);
}
static void HookGraphRemove() {
    if (!g_drv || g_origGraphRemove) return;
    void* graph = SafePtr((uint8_t*)g_drv + OFF::UNetDriver_RepDriver);
    void** vt = graph ? (void**)SafePtr(graph) : nullptr;
    if (!vt || !InModule(vt)) return;
    void* cur = SafePtr((uint8_t*)vt + 0x2A8);
    if (!cur || !InModule(cur)) return;
    void* mine = (void*)&MyGraphRemoveNetworkActor;
    if (Poke((uint8_t*)vt + 0x2A8, &mine, 8)) { g_origGraphRemove = (tGraphRemove)cur; L("[bz] Graph-VTable +0x2A8 (RemoveNetworkActor, %p) wird mitgeloggt", cur); }
}

// ---------------------------------------------------------------------------
//  v69: the host crash, finally at its own call site
//
//  Five crashes now, all byte-identical: AV reading 0x40, frames
//  0x11E820A / 0x13BADC9 / 0x1394877 / 0x13AB3F7 / 0x13B9734. sp_listen is
//  nowhere in the stack and -nodirectspawn changed nothing, so neither our
//  tick nor our loot spawn is the trigger. Reading the frames outward gives
//  the whole path:
//
//      UWorld::Tick -> UIpNetDriver::TickDispatch  (an incoming packet)
//        -> UActorChannel bunch -> the Blueprint VM -> a native
//          -> UNetDriver 0x43D7960, which at 0x43D7DE8 calls
//             [driver+0x1448]->vtable[0x2A0] (actor, location)
//            -> 0x13B96A0  (rcx = node, rdx = FVector*, r8 = ACTOR)
//              -> 0x13AB3B7 -> 0x1394820 -> 0x13BABE0 -> boom
//
//  That is the spatial node's "this actor moved, re-file it" path. 0x13BABE0
//  is the function with `cmp byte [actor+0x20D], 2` (DORM_DormantAll) that
//  then dereferences [rbp+0x1D8] -- the per-connection pointer that only
//  exists inside the graph's own per-connection pass. Called from a packet
//  handler it is null, and the branch reads 0x40 off it.
//
//  So the guard belongs exactly here, and it is one comparison: if the actor
//  is DORM_DormantAll or DORM_DormantPartial, do not forward the move. A
//  dormant actor is by definition not moving, so nothing real is lost -- and
//  unlike v62 this sits ON the path the crash actually takes, verified frame
//  by frame rather than assumed. We only skip a call; no engine field is
//  written, so rule 1 holds.
//
//  Switch -nospatialguard restores the old behaviour.
// ---------------------------------------------------------------------------
// v70: OFF by default. It was meant to stop the host crash and the crashes got
// more frequent instead. Skipping the "actor moved" notification leaves the
// spatial node holding a stale cell entry for that actor, and a stale entry is
// a very good way to turn one crash into several. The reasoning in the block
// above still looks right to me, but the evidence says otherwise and the
// evidence wins. Opt in with -spatialguard.
static bool          g_spatialGuard = false;         // -spatialguard (was default-on in v69)
typedef void (__fastcall* tSpatialMove)(void* self, void* loc, void* actor);
static tSpatialMove  g_origSpatialMove = nullptr;
static volatile LONG g_spatialSkipped  = 0;

static void __fastcall MySpatialActorMoved(void* self, void* loc, void* actor) {
    if (g_spatialGuard && actor) {
        uint8_t d = 0;
        if (SafeCopy((uint8_t*)actor + OFF::AActor_NetDormancy, &d, 1) && (d == 2 || d == 3)) {
            LONG n = InterlockedIncrement(&g_spatialSkipped);
            if (n <= 20) {
                wchar_t cn[128] = L"?";
                ClassNameOf(actor, cn, 128);
                L("[sg] Positionsmeldung fuer %ls %p mit Dormancy %d VERWORFEN -- genau dieser Aufruf hat den Host fuenfmal abgestuerzt -- #%ld",
                  cn, actor, d, n);
            }
            return;                                   // do not forward
        }
    }
    if (g_origSpatialMove) g_origSpatialMove(self, loc, actor);
}

static void HookSpatialMove() {
    if (!g_spatialGuard) return;          // v70: not installed at all unless asked for
    if (!g_drv || g_origSpatialMove) return;
    void* obj = SafePtr((uint8_t*)g_drv + 0x1448);
    void** vt = obj ? (void**)SafePtr(obj) : nullptr;
    if (!vt || !InModule(vt)) return;
    void* cur = SafePtr((uint8_t*)vt + 0x2A0);
    if (!cur || !InModule(cur)) return;
    // Only touch it if it is the function the crash stack named. If this build
    // ever puts something else there, leave the slot alone.
    if ((uintptr_t)cur != g_base + 0x13B96A0) {
        L("[sg] Slot +0x2A0 bei Treiber+0x1448 zeigt auf base+0x%llX statt 0x13B96A0 -- nicht angefasst",
          (unsigned long long)((uintptr_t)cur - g_base));
        g_origSpatialMove = (tSpatialMove)cur;         // stop retrying
        return;
    }
    void* mine = (void*)&MySpatialActorMoved;
    if (Poke((uint8_t*)vt + 0x2A0, &mine, 8)) {
        g_origSpatialMove = (tSpatialMove)cur;
        L("[sg] Crash-Schutz aktiv: Positionsmeldungen an den raeumlichen Knoten (%p) laufen jetzt ueber uns", cur);
    }
}

// ---- UNetDriver::AddNetworkActor hook: belt and braces for the instance ----
typedef void (__fastcall* tAddNetActor)(void* drv, void* actor);
static tAddNetActor g_origAddNetActor = nullptr;

static bool IsZoneActor(void* actor) {
    void* cls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
    if (!cls) return false;
    if (g_zoneClass) return cls == g_zoneClass;
    if (!g_haveZoneKey) g_haveZoneKey = FindNameKey(L"BP_BlueZone_C", &g_zoneKey);   // class may load late
    if (g_haveZoneKey) {
        FNameRaw n;
        return SafeCopy((uint8_t*)cls + OFF::UObject_Name, &n, sizeof(n)) && n.Comparison == g_zoneKey.Comparison;
    }
    wchar_t nm[128];
    return ClassNameOf(actor, nm, 128) && (wcsstr(nm, L"BlueZone") || wcsstr(nm, L"PlayZone"));
}

static PipeTally* PipeForActor(void* actor);    // v58j pipeline tally

// ---------------------------------------------------------------------------
//  v67: route the remote player's weapon around the spatial node
//
//  Everything measurable about the graph is now healthy. [gn] shows the
//  GridNode registered as global node #0, present in the PrepareForReplication
//  list (graph+0xB0) so its cells are filled, and asked every frame; the
//  connection is State 3; [vw] puts the graph's viewpoint 3 m from the pawn;
//  [pl] shows hundreds of actors added. And [rs] still says the graph
//  replicates nobody but the pawn, the GameState and the blue zone.
//
//  The zone is in that list only because we force bAlwaysRelevant on it -- the
//  one thing in this build that has reliably made an actor replicate for a
//  year of versions. RouteAddNetworkActorToNodes (0x13C5B60) tests
//  bAlwaysRelevant (+0x1CC bit 0) FIRST and files the actor into the
//  AlwaysRelevant ActorList node instead of the grid. That node demonstrably
//  works: GameState and the zone travel through it every round.
//
//  So the weapon takes the same road. This is a workaround, not a cure -- the
//  spatial node is still broken and #13/#14 still ride on it -- but it is a
//  proven mechanism in this exact build, it costs a handful of actors rather
//  than thousands, and it is the difference between a magazine that updates
//  and one that does not. The flag must be set BEFORE the actor enters the
//  graph, because AddNetworkActor only routes once (it skips when the global
//  info's ref count at +0x128 is already > 0, seen at 0x13AB22F).
// ---------------------------------------------------------------------------
// v70: OFF by default. Alen's report after v69: "picking up a weapon made it
// disappear, host is crashing often". Both of the behaviour changes I shipped
// in v67/v68 and v69 went in back to back and the game got worse, so both are
// disabled here rather than argued about. This one writes bAlwaysRelevant and
// a huge cull distance onto a live weapon actor, which is a write to gameplay
// state on an actor the game attaches, hides and re-parents on every equip --
// exactly the class of change rule 1 warns about, and I shipped it anyway.
// Opt in with -weaponrelevant if we ever want to measure it again.
static bool          g_weaponRelevant = false;    // -weaponrelevant (was default-on in v67)
static volatile LONG g_wpnRelevantN   = 0;

// v68: decide ONCE PER CLASS, not once per actor. The v67 version resolved a
// class name on every AddNetworkActor call -- 9273 of them in the crashing
// round, each an FName -> FString conversion inside the hot net path. That was
// my regression; it bought nothing (the weapon branch never even fired) and it
// made the one code path the host was already dying in measurably heavier.
// A pointer-keyed cache costs one comparison per call after the first sighting.
static void* g_wpnClsYes[64];
static void* g_wpnClsNo[192];
static int   g_wpnYesN = 0, g_wpnNoN = 0;

static bool IsWeaponActor(void* actor) {
    void* cls = actor ? SafePtr((uint8_t*)actor + OFF::UObject_Class) : nullptr;
    if (!cls) return false;
    for (int i = 0; i < g_wpnYesN; ++i) if (g_wpnClsYes[i] == cls) return true;
    for (int i = 0; i < g_wpnNoN;  ++i) if (g_wpnClsNo[i]  == cls) return false;
    wchar_t nm[128] = L"";
    bool yes = ClassNameOf(actor, nm, 128) && wcsstr(nm, L"Weapon_") != nullptr;
    if (yes) { if (g_wpnYesN < 64)  g_wpnClsYes[g_wpnYesN++] = cls; }
    else     { if (g_wpnNoN  < 192) g_wpnClsNo[g_wpnNoN++]  = cls; }
    return yes;
}

static void __fastcall MyAddNetworkActor(void* drv, void* actor) {
    InterlockedIncrement(&g_addActorCalls);
    AddNetTally(actor);
    ScanClassForDoorWindow(actor);        // v99, once per class
    if (PipeTally* t = PipeForActor(actor)) InterlockedIncrement(&t->added);
    // v58n: WHERE does a pickup come from? The hook is a 14-byte jmp, so the
    // return address here is the instruction right after the caller's call.
    // Turning that into an RVA names the code path that produced the wave of
    // 2120 pickups at the host's touchdown -- which is the one thing the
    // counters cannot tell us, and the one thing worth knowing now.
    if (g_lootRet && actor) {
        void* cls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
        if (cls && cls == g_pickupClass) {
            const uintptr_t rva = (uintptr_t)_ReturnAddress() - g_base;
            bool seen = false;
            for (int i = 0; i < g_lootRetN; ++i)
                if (g_lootRets[i] == rva) { ++g_lootRetHits[i]; seen = true; break; }
            if (!seen && g_lootRetN < 12) {
                g_lootRets[g_lootRetN] = rva; g_lootRetHits[g_lootRetN] = 1; ++g_lootRetN;
                L("[lr] Pickup %p wird von RVA 0x%08llX aus registriert (neuer Pfad #%d)",
                  actor, (unsigned long long)rva, g_lootRetN);
            }
        } else if (!g_pickupClass && cls) {
            wchar_t nm[128] = L"?";
            if (ResolveFuncName(cls, nm, 128) && wcsstr(nm, L"BravoHotelPickup")) {
                g_pickupClass = cls;
                L("[lr] Klasse BravoHotelPickup = %p -- ab jetzt wird ihr Registrierungspfad mitgeschrieben", cls);
            }
        }
    }
    // v67: a weapon of the remote player, on its way into the graph.
    if (g_weaponRelevant && actor && IsWeaponActor(actor)) {
        uint8_t f = 0;
        SafeCopy((uint8_t*)actor + OFF::AActor_bAlwaysRelevant, &f, 1);
        if (!(f & 1)) {
            LONG n = InterlockedIncrement(&g_wpnRelevantN);
            MarkAlwaysRelevant(actor, "Waffe (in AddNetworkActor)");
            uint8_t dorm = 0;
            if (SafeCopy((uint8_t*)actor + 0x20D, &dorm, 1) && dorm != 1) {
                uint8_t awake = 1;
                SafeCopy(&awake, (uint8_t*)actor + 0x20D, 1);
            }
            if (n <= 30)
                L("[wr] Waffe %p wird bAlwaysRelevant + Awake, BEVOR sie in den Graphen kommt -- #%ld", actor, n);
        }
    }
    const bool zone = g_zoneRelevant && actor && IsZoneActor(actor);
    if (zone) {
        // 1) the class default object -> class-level settings for future adds
        if (!g_zoneCdoPatched) {
            void* cls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
            void* cdo = cls ? SafePtr((uint8_t*)cls + OFF::UClass_DefaultObject) : nullptr;
            if (cdo && SafePtr((uint8_t*)cdo + OFF::UObject_Class) == cls) {
                if (!g_zoneClass) g_zoneClass = cls;
                g_zoneCdoPatched = MarkAlwaysRelevant(cdo, "CDO BP_BlueZone_C");
            } else {
                L("[bz] CDO der Zonenklasse nicht gefunden (cls=%p cdo=%p)", cls, cdo);
            }
        }
        // 2) the instance -> RouteAddNetworkActorToNodes puts it in the
        //    AlwaysRelevant node (0x13C5B60 tests +0x1CC bit 0 first)
        uint8_t f = 0;
        SafeCopy((uint8_t*)actor + OFF::AActor_bAlwaysRelevant, &f, 1);
        if (!(f & 1)) InterlockedIncrement(&g_zoneAddFixes);
        MarkAlwaysRelevant(actor, (f & 1) ? "Zonen-Actor (Flag schon da)" : "Zonen-Actor (in AddNetworkActor)");
        {   // v118: which level does this zone instance live in? (there are 3)
            wchar_t lp[256]; LevelPathOf(actor, lp, 256);
            L("[zl] Zonen-Instanz %p: %ls", actor, lp);
        }
        // v58c: the constructor (0x1D1B340) leaves the zone in DORM_Initial;
        // UReplicationGraph::AddNetworkActor turns that into
        // GlobalInfo.bWantsToBeDormant and the connection then sleeps on the
        // actor after ONE replication. Awake it before the graph looks.
        uint8_t dorm = 0;
        if (SafeCopy((uint8_t*)actor + 0x20D, &dorm, 1) && dorm != 1) {
            uint8_t awake = 1;
            SafeCopy(&awake, (uint8_t*)actor + 0x20D, 1);
            L("[bz] Zonen-Actor NetDormancy %d -> 1 (Awake) vor dem Eintritt in den Graphen", dorm);
        }
        if (!g_blueZone) g_blueZone = actor;
        L("[bz] AddNetworkActor fuer Zonen-Actor %p auf Treiber %p (g_drv=%p)", actor, drv, g_drv);
    }
    // -----------------------------------------------------------------------
    //  v96: the actual cause of #13, #14 and the floor loot, and the fix.
    //
    //  UReplicationGraph::IsActorValidForReplicationGather (0x13AB0B0, second
    //  copy at 0x1392A80) ends with exactly this test:
    //
    //      if (Actor->[0x20D] == 4 /*DORM_Initial*/ && IsNetStartupActor(Actor))
    //          return false;
    //
    //  IsNetStartupActor is 0x3F93040 = (Actor[0x2D0] >> 1) & 1. So every actor
    //  PLACED IN THE LEVEL that still sits at DORM_Initial is rejected outright:
    //  no node, no cell, no channel, ever. Doors, windows, breakables and level
    //  loot are precisely that -- the [cn] log has been printing "Dormancy 4"
    //  for them since v58.
    //
    //  In a normal server that is fine, because the actor is woken on demand by
    //  AActor::FlushNetDormancy (0x3F891D0), which flips 4 -> 2 and then calls
    //  GetNetDriver()->FlushActorDormancy(). But GetNetDriver() reads
    //  World->NetDriver -- the very field the standalone mask nulls -- so under
    //  the mask that call returns before doing anything, and the v62 crash guard
    //  deliberately keeps the window shut for actors that already reached
    //  Dormancy 2. Once asleep, they can never come back.
    //
    //  The fix is NOT to touch the wake path (that is the branch that crashed
    //  the host in v59 and v69, and the guard has to stay). It is to stop the
    //  actor from being rejected in the first place: flip DORM_Initial to
    //  DORM_Awake BEFORE the driver forwards the actor to the graph. That is
    //  the same window, the same field and the same single-byte write the blue
    //  zone has used since v58c -- writing the INPUT the gate reads, not the
    //  state machine's result -- and it happens before the graph has any per-
    //  actor state to corrupt.
    //
    //  Off with -nodormwake.
    // -----------------------------------------------------------------------
    if (g_dormBlock && actor) {                       // v154: arrives already dormant (class default)
        uint8_t dorm = 0, flags = 0;
        if (SafeCopy((uint8_t*)actor + 0x20D, &dorm, 1) && (dorm == 2 || dorm == 3)
         && SafeCopy((uint8_t*)actor + 0x2D0, &flags, 1) && (!(flags & 2) || g_dormBlockLevel)) {
            const uint8_t awake = 1;
            if (SafeCopy(&awake, (uint8_t*)actor + 0x20D, 1)) {
                LONG n = InterlockedIncrement(&g_dormBlockedAdd);
                if (n <= 25) { wchar_t an[128] = L"?"; ClassNameOf(actor, an, 128);
                    L("[dz] %ls %p kam schon mit Dormancy %d in den Graphen -> DORM_Awake -- #%ld", an, actor, dorm, n); }
            }
        }
    }
    if (g_dormWake && actor) {
        uint8_t dorm = 0, flags = 0, rep = 0;
        if (SafeCopy((uint8_t*)actor + 0x20D, &dorm, 1) && dorm == 4
         && SafeCopy((uint8_t*)actor + 0x2D0, &flags, 1) && (flags & 2)
         && SafeCopy((uint8_t*)actor + 0x289, &rep, 1) && (rep & 1)) {
            void* wcls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
            // v109: optional narrowing. Doors and windows are what #13/#14 needed;
            // fences and other scenery were only ever collateral.
            if (g_wakeDoorsOnly) {
                wchar_t wn[128] = L"";
                if (!ClassNameOf(actor, wn, 128)
                 || !(wcsstr(wn, L"Door") || wcsstr(wn, L"Window") || wcsstr(wn, L"Glass")
                   || wcsstr(wn, L"Break") || wcsstr(wn, L"Destruct")))
                    { if (g_origAddNetActor) g_origAddNetActor(drv, actor); return; }
            }
            const uint8_t awake = 1;                       // DORM_Awake
            if (SafeCopy(&awake, (uint8_t*)actor + 0x20D, 1)) {
                WakeTally(wcls);
                LONG n = InterlockedIncrement(&g_dormWoken);
                // v97: resolve the class name ONCE per class -- 2192 name
                // lookups per round would be pure waste.
                if (n <= 25) {
                    wchar_t an[128] = L"?";
                    ClassNameOf(actor, an, 128);
                    L("[dw] %ls %p war DORM_Initial und im Level platziert -- auf DORM_Awake"
                      " gesetzt, damit der Graph ihn ueberhaupt annimmt -- #%ld", an, actor, n);
                }
            }
        }
    }

    if (g_origAddNetActor) g_origAddNetActor(drv, actor);
    // 3) the graph's per-actor copy of the cull distance, created just now
    //    from the (old) class info -> zero it. Runs again on every re-add.
    if (zone) ZoneGraphCullToZero(drv, actor, "nach AddNetworkActor");
}

// ===========================================================================
//  v58 (re-done v56): FLOOR LOOT AROUND THE REMOTE PLAYERS
// ===========================================================================
//  Floor loot is spawned per building by ABravoHotelBuilding::CheckSpawnBy-
//  Standalone (0x1FA5E60, called from the building Blueprint on a timer):
//      world = GetWorld(); gs = GameState; if (gs->BattleRoyaleState != 4) return;
//      pc = UWorld::GetFirstPlayerController(world);          // the HOST
//      pawn = pc->GetCharacter(false);                       // thunk 0x21F6110
//      if (|pawn.loc - building.loc|^2 < 30000^2 && ...) SpawnItems(...)
//  Vehicles: ABravoHotelVehicleSpawnActor::CheckSpawnByStandalone (0x22BEF20),
//  same shape, radius per actor. Both only ever look at the host's pawn, so
//  nothing spawns where only the remote player is (v55 loads the tiles there,
//  but the check still says "nobody near").
//
//  FIX
//  Hook both functions. After the original ran for the host, run it once more
//  per registered remote pawn within reach, with UWorld::GetFirstPlayerController
//  redirected to hand back THAT player's controller. Everything downstream
//  then refers to the remote player: the pawn (interface thunk on the PC),
//  the distance, and the gate at PC+0x1928 -- the "in-bound levels loaded"
//  flag the client sets on its own PC through ServerInBoundLevelsAreLoaded.
//  (That gate is why loot used to wait for the HOST to land: it was always
//  the host's flag.) Spawned buildings guard themselves (the game clears the
//  timer and sets [building+0x46A]), so repeated calls are harmless.
//
//  Abschalten ohne Neubau:  -nospawnremote
// ===========================================================================
typedef void  (__fastcall* tCheckSpawn)(void* actor);
typedef void* (__fastcall* tWeakGet)(void* weakPtr);
static tCheckSpawn g_origBuildingSpawn = nullptr;
static tCheckSpawn g_origVehicleSpawn  = nullptr;
static void*       g_spawnOverridePC   = nullptr;   // game thread only
static int         g_spawnDepth        = 0;
static bool        g_firstPcHooked     = false;

// Replacement for UWorld::GetFirstPlayerController (re-implemented, see RVA
// notes). During the nested spawn check it hands back the REMOTE player's
// controller, so the whole check -- pawn, distance, and the
// "in-bound levels loaded" flag at PC+0x1928 (set by the client's
// ServerInBoundLevelsAreLoaded RPC) -- runs against that player.
static void* __fastcall MyGetFirstPlayerController(void* world) {
    if (g_spawnOverridePC) return g_spawnOverridePC;
    if (*(int32_t*)((uint8_t*)world + 0x278) <= 0) return nullptr;
    void* data = *(void**)((uint8_t*)world + 0x270);
    return ((tWeakGet)(g_base + RVA::WeakObjectPtr_Get))(data);
}

// v58n: the HOST's own PlayerController, never the override -- used to read
// the gates the game's own building check runs against.
typedef void* (__fastcall* tPawnThunk)(void* pc, bool b);

static void* RealFirstPlayerController() {
    if (!g_world) return nullptr;
    if (SafeI32((uint8_t*)g_world + 0x278) <= 0) return nullptr;
    void* data = SafePtr((uint8_t*)g_world + 0x270);
    if (!data) return nullptr;
    void* pc = nullptr;
    __try { pc = ((tWeakGet)(g_base + RVA::WeakObjectPtr_Get))(data); }
    __except (EXCEPTION_EXECUTE_HANDLER) { pc = nullptr; }
    return pc;
}

static bool ActorLocation(void* actor, float* out) {
    uint8_t* root = (uint8_t*)SafePtr((uint8_t*)actor + OFF::AActor_RootComponent);
    if (!root) return false;
    return SafeCopy(root + OFF::USceneComp_Translation, out, 12) && Finite3(out);
}

// v58f: the game's own "GameState of this actor" (RVA::Actor_GameState).
static void* GameStateOf(void* actor) {
    typedef void* (__fastcall* tGS)(void*);
    void* gs = nullptr;
    __try { gs = ((tGS)(g_base + RVA::Actor_GameState))(actor); }
    __except (EXCEPTION_EXECUTE_HANDLER) { gs = nullptr; }
    return gs;
}

static void RunSpawnForRemotePawns(tCheckSpawn orig, void* actor, const char* what, float reach, bool building) {
    if (!g_spawnRemote || !g_firstPcHooked || !g_drv || g_remoteCount == 0) return;
    // v58p: Alen saw loot lying around before the round had even started --
    // that was this path, lifting the phase gate while the match was still in
    // its waiting lobby (the [ph] lines all said "Match-Phase noch 1"). The
    // lift is right once the match is running and wrong before it, so below
    // phase 3 nothing is spawned at all.
    if (!MatchStarted(actor, nullptr)) { InterlockedIncrement(&g_spawnLobbySkipped); return; }
    float aloc[3];
    if (!ActorLocation(actor, aloc)) return;
    static int loggedB = 0, loggedV = 0;
    for (int i = 0; i < g_remoteCount; ++i) {
        float p[3];
        if (!RemotePawnLocation(g_remote[i], p)) continue;
        const float dx = p[0]-aloc[0], dy = p[1]-aloc[1], dz = p[2]-aloc[2];
        if (dx*dx + dy*dy + dz*dz > reach*reach) continue;
        uint8_t* pc = (uint8_t*)g_remote[i].controller;
        // v58c: the gate at PC+0x1928 ("in-bound levels loaded") is normally
        // set by the client's ServerInBoundLevelsAreLoaded RPC. If it is still
        // 0 on the remote PC we lift it for the duration of this one check --
        // the tiles around the remote pawn are loaded by our v55 anchor, and
        // the spawn radius (300 m) keeps a player in the aircraft out anyway.
        uint8_t flag = 0; bool forced = false;
        SafeCopy(pc + 0x1928, &flag, 1);
        if (flag == 0) { uint8_t one = 1; forced = SafeCopy(&one, pc + 0x1928, 1); if (forced) InterlockedIncrement(&g_spawnFlagForced); }
        // v58f: the only reliable "it spawned" signal is the GameState's
        // spawned-building list (+0xAD0/+0xAD8) growing; the byte at +0x46A
        // v58e watched is a static flag (1 from the constructor), not a guard.
        uint8_t* gs = (uint8_t*)GameStateOf(actor);
        const int32_t listBefore = gs ? SafeI32(gs + RVA::GameState_SpawnedNum) : -1;
        // v58l: the FIRST gate of CheckSpawnByStandalone is the match phase,
        // GameState+0x9F2 == 4, and the v58k log shows what it costs. While
        // the host was still in the air the whole map held 20 pickups; in the
        // twenty seconds around its touchdown 1137 more appeared at once, 231
        // of them within 200 m of the remote player -- who was 1.5 km away
        // from the host. Not distance, not streaming: one global phase gate
        // opens and every building on the map spawns.
        // v58m then measured the phase itself and disproved the idea: it was
        // already 4 at t=84 s, with the host still at z=74800 and nobody on
        // the ground. The lift never bought anything and it is what put loot
        // in the lobby, so it is OFF now -- -stateforce brings it back for
        // experiments only.
        uint8_t* const gsPhase = (g_stateForce && gs) ? gs + RVA::GameState_Phase : nullptr;
        uint8_t phase = 0; bool phaseForced = false;
        if (gsPhase && SafeCopy(gsPhase, &phase, 1) && phase != 4) {
            const uint8_t four = 4;
            phaseForced = SafeCopy(&four, gsPhase, 1);
            if (phaseForced) InterlockedIncrement(&g_spawnPhaseForced);
        }
        uint64_t timerBefore = 0, timerAfter = 0; uint32_t flags = 0; uint8_t inZone = 0;
        if (building) {
            SafeCopy((uint8_t*)actor + RVA::Building_SpawnTimer, &timerBefore, 8);
            SafeCopy((uint8_t*)actor + RVA::Building_SpawnFlags, &flags, 4);
            SafeCopy((uint8_t*)actor + RVA::Building_InZone, &inZone, 1);
        }
        g_spawnOverridePC = pc;
        ++g_spawnDepth;
        __try { orig(actor); } __except (EXCEPTION_EXECUTE_HANDLER) {
            L("[ls] [!] Spawn-Pruefung (%s) fuer Remote-Pawn %p ist GEFAULTET", what, g_remote[i].pawn);
        }
        --g_spawnDepth;
        g_spawnOverridePC = nullptr;
        if (forced) { uint8_t z = 0; SafeCopy(&z, pc + 0x1928, 1); }
        if (phaseForced) SafeCopy(&phase, gsPhase, 1);     // v58l: restore immediately
        const int32_t listAfter = gs ? SafeI32(gs + RVA::GameState_SpawnedNum) : -1;
        const bool grew = gs && listAfter > listBefore;
        if (grew) InterlockedIncrement(&g_spawnListGrew);
        // v58m: the case this whole build is about -- loot created for the
        // remote player while the match phase had NOT yet opened the gate.
        // That is what "whoever lands first has loot" means, and it is worth
        // saying out loud in the log rather than inferring it from counters.
        if (grew && phaseForced) {
            LONG n = InterlockedIncrement(&g_spawnBeforePhase);
            if (n <= 25)
                L("[ph] Loot fuer den Remote-Spieler gespawnt, obwohl die Match-Phase noch %d war (nicht 4) -- %s %p, Abstand %.0f (#%ld)",
                  phase, what, actor, sqrtf(dx*dx + dy*dy + dz*dz), n);
        }
        if (building) SafeCopy((uint8_t*)actor + RVA::Building_SpawnTimer, &timerAfter, 8);
        const bool timerCleared = building && timerBefore != 0 && timerAfter == 0;
        if (timerCleared) InterlockedIncrement(&g_spawnGuardFlips);
        InterlockedIncrement(&g_spawnChecks);
        InterlockedIncrement(building ? &g_spawnBuilding : &g_spawnVehicle);
        int& logged = building ? loggedB : loggedV;
        if (logged < 10 || (grew && logged < 60) || (timerCleared && logged < 60)) {
            if (building)
                L("[ls] Spawn-Pruefung an %s %p fuer Remote-Pawn %p (Abstand %.0f, PC-Flag 0x1928=%d%s, Flags+0x468=%08X, InZone=%d, Timer %llX->%llX, Spawn-Liste %d->%d%s%s)",
                  what, actor, g_remote[i].pawn, sqrtf(dx*dx + dy*dy + dz*dz), flag, forced ? " erzwungen" : "",
                  flags, inZone, (unsigned long long)timerBefore, (unsigned long long)timerAfter, listBefore, listAfter,
                  grew ? ", GESPAWNT" : "", timerCleared ? ", Timer geloescht" : "");
            else
                L("[ls] Spawn-Pruefung an %s %p fuer Remote-Pawn %p (Abstand %.0f, PC-Flag 0x1928=%d%s, Spawn-Liste %d->%d%s)",
                  what, actor, g_remote[i].pawn, sqrtf(dx*dx + dy*dy + dz*dz), flag, forced ? " erzwungen" : "",
                  listBefore, listAfter, grew ? ", GESPAWNT" : "");
            ++logged;
        }
    }
}

// ---- v58e: run the spawn check OURSELVES for buildings near remote pawns ----
// The game's building Blueprint drives CheckSpawnByStandalone from a timer that
// -- per the counters of every round so far -- only ever fires for buildings
// around the HOST. Rather than guess what gates that timer, walk the loaded
// levels' actor lists (decoded, see RVA::ActorPtr_*), pick every actor that
// IsChildOf(ABravoHotelBuilding) within reach of a remote pawn and run the
// native check for it with the remote controller (same nested path as above).
static void* g_buildingNative = nullptr;   // UClass ABravoHotelBuilding
static void* g_vehicleNative  = nullptr;   // UClass ABravoHotelVehicleSpawnActor

static bool IsChildOfClass(void* obj, void* base) {
    if (!obj || !base) return false;
    void* cls = SafePtr((uint8_t*)obj + OFF::UObject_Class);
    if (!cls) return false;
    int32_t want = SafeI32((uint8_t*)base + OFF::UStruct_NumBasesM1);
    int32_t have = SafeI32((uint8_t*)cls  + OFF::UStruct_NumBasesM1);
    if (want < 0 || want > have || want > 64) return false;
    void* chain = SafePtr((uint8_t*)cls + OFF::UStruct_BaseChain);
    if (!chain) return false;
    return SafePtr((uint8_t*)chain + (size_t)want * 8) == (uint8_t*)base + OFF::UStruct_BaseChain;
}

// Walk SuperStruct (+0x48) up from an instance's class until the class is
// named `name`; returns that UClass or null.
static void* NativeAncestorNamed(void* obj, const wchar_t* name) {
    void* cls = SafePtr((uint8_t*)obj + OFF::UObject_Class);
    for (int i = 0; cls && i < 16; ++i) {
        wchar_t nm[128];
        if (ResolveFuncName(cls, nm, 128) && wcscmp(nm, name) == 0) return cls;
        cls = SafePtr((uint8_t*)cls + OFF::UStruct_SuperStruct);
    }
    return nullptr;
}

static inline uint64_t Rol64(uint64_t v, int n) { return (v << n) | (v >> (64 - n)); }
static inline uint64_t Ror64(uint64_t v, int n) { return (v >> n) | (v << (64 - n)); }
static void* DecodeActorSlot(uint64_t raw) {
    uint64_t v = ~raw;
    v = Rol64(v, 1);
    v ^= RVA::ActorPtr_XorKey;
    // "not ecx / mov [slot],ecx / mov rcx,[slot]": the game stores the full
    // 64-bit value first and then overwrites only the low dword with its
    // complement -- the upper 32 bits SURVIVE. v58e cleared them (0x432B786..
    // 0x432B78F), which is why every decoded slot failed the vtable test.
    v = (v & 0xFFFFFFFF00000000ULL) | (uint64_t)(uint32_t)(~(uint32_t)v);
    v += RVA::ActorPtr_AddKey;
    v = Ror64(v, 6);
    return (void*)v;
}

// ===========================================================================
//  v58k: WAKE THE DORMANT WORLD AROUND THE REMOTE PLAYER
// ===========================================================================
//  The v58j table is unambiguous. BravoHotelPickup never appears in it at
//  all -- UNetDriver::AddNetworkActor is never called for a single pickup,
//  while 48 of them stand within 200 m of the player. And the census says why
//  the whole family behaves the same way:
//      BravoHotelPickup             repliziert 1, Dormancy 4, Cull 100 m
//      BP-Fence_C                   repliziert 1, Dormancy 4, Cull 100 m
//      BP-Wood_In_Door05_Frame_C    repliziert 1, Dormancy 4, Cull 100 m
//      BP-BrokenWindowHISMActor_C   repliziert 1, Dormancy 4, Cull 100 m
//  Dormancy 4 is DORM_Initial: "this actor already exists on the client from
//  the level load, so do not replicate it until something wakes it". The
//  engine honours that by keeping such actors out of the network object list
//  entirely (UNetDriver::AddNetworkActor -> IsDormInitialStartupActor
//  0x3F93040), and the only thing that ever brings them in is
//  AActor::FlushNetDormancy.
//  On a normal client that assumption holds: the client loaded the same level
//  and has the same fences, doors and windows. It does NOT hold for anything
//  the host spawned during the match -- the loot -- and it does not hold for
//  a state change the client never saw. Nothing in this setup ever calls
//  FlushNetDormancy for them, so they sit there, on the host, invisible.
//  So: walk them and flush them. FlushNetDormancy is the engine's own
//  mechanism for exactly this, it is already hooked so it reaches the driver
//  through the mask, and it is idempotent -- but we still remember which
//  actors were done so each one is woken once. -nowakeup turns it off.
// ===========================================================================
typedef void (__fastcall* tActorVoidFwd)(void* actor);
extern tActorVoidFwd g_flushDormancyFwd;        // = the FlushNetDormancy trampoline
extern tActorVoidFwd g_forceNetUpdateFwd;       // = the ForceNetUpdate trampoline (v58t)
static bool          g_wakeUp = true;
static void*         g_woken[8192];          // open-addressed set of pointers
static int           g_wokenCount = 0;

static bool AlreadyWoken(void* a) {
    if (g_wokenCount >= 7000) return true;          // set is full: stop waking
    size_t h = ((size_t)a >> 4) & 8191;
    for (int i = 0; i < 8; ++i) {
        void*& slot = g_woken[(h + i) & 8191];
        if (slot == a) return true;
        if (slot == nullptr) { slot = a; ++g_wokenCount; return false; }
    }
    return true;                                    // cluster full, treat as done
}

// Bring one dormant-initial actor into the replication system.
// v58t: one actor, pushed out now. Dormant actors get flushed first --
// otherwise the property change sits on the host until something else wakes
// them, which is exactly the 5-10 second ammo delay.
// v58w: this crashed the host, and the bug was mine.
//  Alen opened a door on the PC and the host died with
//  EXCEPTION_ACCESS_VIOLATION reading 0x00000040, inside the replication
//  graph's dormancy path: at 0x11E820A the code does `mov eax,[rcx+8]` with
//  rcx = 0x38, i.e. the object whose map was being looked up (loaded one
//  instruction earlier as `lea rcx,[rdi+0x38]`) was NULL. The frames above it
//  are all graph code (0x13BABE0, 0x1394820, 0x13AB3B7, 0x13B96A0) reached
//  through the net driver (0x43D7960).
//  What fed that path was this function. v58t collected actor pointers in the
//  level sweep, kept them FOREVER without ever checking them again, and called
//  ForceNetUpdate on every one of them four times a second. A door opening
//  destroys and respawns actors; the moment one of the pointers went stale we
//  handed the engine a dead object. An exception handler does not help here --
//  the memory is still mapped, it is simply no longer that actor.
//  So now: the object must still be alive in the GUObjectArray, it must
//  actually replicate, and it must be registered with the driver before it is
//  pushed. Anything that fails a check is dropped from the list instead of
//  being retried forever.
static bool PokeOneActor(void* a) {
    if (!a || !g_drv) return false;
    if (!MaybeAlive(a)) return false;                     // dead or pending kill -> drop it
    uint8_t rep = 0;
    if (!SafeCopy((uint8_t*)a + OFF::AActor_bReplicates, &rep, 1) || !(rep & 1))
        return false;                                    // never route a non-replicated actor
    uint8_t dorm = 0;
    SafeCopy((uint8_t*)a + OFF::AActor_NetDormancy, &dorm, 1);
    // v58y: NEVER touch DORM_DormantAll (2) or DORM_DormantPartial (3).
    // That is the exact value the crashing code tests:
    //     cmp byte [r9+0x20D], 2
    //     jne  skip
    //     mov  rdi,[rbp+0x1D8]      <- the per-connection context, NULL here
    //     lea  rcx,[rdi+0x38]       -> 0x38
    //     call 0x11E8200            -> reads [rcx+8] = 0x40 -> access violation
    // The branch only exists for a dormant-all actor and it assumes it is
    // running inside the graph's per-connection pass, where +0x1D8 is set.
    // We were calling it from our own tick, outside that pass, so the context
    // was null. v58k's wake-up only ever touched DORM_Initial (4) actors --
    // which is why that one never crashed and this did, twice.
    if (dorm == 2 || dorm == 3) return false;
    if (dorm > 1) {
        if (g_flushDormancyFwd) { __try { g_flushDormancyFwd(a); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
    }
    if (g_forceNetUpdateFwd) { __try { g_forceNetUpdateFwd(a); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
    InterlockedIncrement(&g_ownPoked);
    return true;
}

static void WakeActorForClients(void* actor) {
    if (!g_drv) return;
    // v58y: same guard as PokeOneActor. The graph's DORM_DormantAll branch
    // assumes it is running inside the per-connection pass and dereferences a
    // context that is null outside it (see the comment in PokeOneActor). The
    // actors this wake-up is actually for are DORM_Initial (4); 2 and 3 were
    // only ever swept up by accident, and they are the ones that crash.
    uint8_t wdorm = 0;
    if (SafeCopy((uint8_t*)actor + OFF::AActor_NetDormancy, &wdorm, 1) && (wdorm == 2 || wdorm == 3))
        return;
    if (g_flushDormancyFwd) {
        __try { g_flushDormancyFwd(actor); }          // driver is visible here
        __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    }
    if (g_origAddNetActor) {
        __try { g_origAddNetActor(g_drv, actor); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    }
    InterlockedIncrement(&g_wokenActors);
}

// ===========================================================================
//  v58o: FIND EVERY LOOT SOURCE THE WAY THE GAME ITSELF FINDS IT
// ===========================================================================
//  Until now a "loot building" meant "an actor our walk recognises as a child
//  of ABravoHotelBuilding", and that found 38-47 actors in the entire world
//  while the host's own wave produced 2120 pickups. The game does not ask that
//  question at all: it asks the actor for a
//  UBravoHotelDetectItemSpawnBoxComponent (see RVA::ItemSpawnBoxComp_*). That
//  test needs no class hierarchy and works for every actor.
//  v58q: and the "already spawned" flag we were reading (+0x534) was never
//  one -- see RVA::SpawnComp_All. We keep our own set of sources we have
//  fired instead, and we call the inner spawn function directly.
static bool          g_directSpawn = true;      // -nodirectspawn
static bool          g_lootScan    = true;      // -nolootscan
static bool          g_gateBypass  = true;      // -nogatebypass (v58q)
static volatile LONG g_lootSources = 0, g_lootOpen = 0, g_lootFired = 0, g_lootDone = 0;
static volatile LONG g_lootGate[7] = {0,0,0,0,0,0,0};   // v58q: which gate blocked
static void*         g_spawnCompClass = nullptr;

typedef void* (__fastcall* tGetComponentByClass)(void* actor, void* cls);
typedef void* (__fastcall* tStaticClassFn)();
typedef void  (__fastcall* tCompSpawn)(void* comp, float delay, uint8_t mode);
typedef void  (__fastcall* tCompSpawnInner)(void* comp, uint8_t mode);

// v58q: our own "we have already fired this source" set. The component has no
// such flag, so keeping one is the only honest way to fire each source once.
static void*         g_firedSet[8192];
static int           g_firedCount = 0;
static bool AlreadyFired(void* c) {
    if (!c) return true;
    uintptr_t h = ((uintptr_t)c >> 4) * 2654435761u;
    for (int i = 0; i < 8; ++i) {
        const int slot = (int)((h + i) & 8191);
        if (!g_firedSet[slot]) {
            if (g_firedCount >= 7000) return false;   // set nearly full: never claim "done"
            g_firedSet[slot] = c; ++g_firedCount; return false;
        }
        if (g_firedSet[slot] == c) return true;
    }
    return false;
}

// The actor's item spawn component, or null if this actor is not a loot source.
static void* SpawnComponentOf(void* actor) {
    if (!g_spawnCompClass) {
        __try { g_spawnCompClass = ((tStaticClassFn)(g_base + RVA::ItemSpawnBoxComp_StaticClass))(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { g_spawnCompClass = nullptr; }
        if (g_spawnCompClass) {
            wchar_t nm[128] = L"?";
            ResolveFuncName(g_spawnCompClass, nm, 128);
            L("[lq] Loot-Komponentenklasse = %p (%ls)", g_spawnCompClass, nm);
        }
        if (!g_spawnCompClass) return nullptr;
    }
    void** vt = (void**)SafePtr(actor);
    if (!vt || !InModule(vt)) return nullptr;
    void* fn = SafePtr((uint8_t*)vt + RVA::AActor_GetComponentByClass_VT);
    if (!fn || !InModule(fn)) return nullptr;
    void* comp = nullptr;
    __try { comp = ((tGetComponentByClass)fn)(actor, g_spawnCompClass); }
    __except (EXCEPTION_EXECUTE_HANDLER) { comp = nullptr; }
    return comp;
}

// v58q: fire one loot source, and say out loud what stopped us if we cannot.
//
// The outer function 0x2142DA0 carries six gates before it does any work, all
// read straight out of its code (mode-2 path):
//   1 comp+0xB0   World           != null
//   2 World+0x1D0 AuthGameMode    != null
//   3 GameMode+0x370 GameState    != null
//   4 GameState+0x9F2 match phase >= 4
//   5 comp+0xA8   Owner           != null
//   6 Owner+0x20F Role            == 3 (ROLE_Authority)
// The inner function 0x2141FD0, the one that actually walks the child
// components and creates a pickup per spawn point, has NONE of them. So we
// evaluate the six ourselves, log the first one that blocks us, and then do
// exactly what 0x2142DA0 would have done next: zero the two mode bytes, set
// the timestamp, and call the inner function. -nogatebypass keeps the old
// behaviour (outer call only) for comparison.
//
// Gate 4 is the one gate we do NOT bypass: no loot before the round starts.
static const char* const kGateName[7] = {
    "-", "keine World", "kein GameMode", "kein GameState",
    "Match-Phase < 4", "kein Owner", "Owner ist nicht Authority"
};
static bool FireSpawnComponent(void* comp, void* owner) {
    if (AlreadyFired(comp)) { InterlockedIncrement(&g_lootDone); return false; }
    InterlockedIncrement(&g_lootOpen);
    if (!g_directSpawn) return false;

    // --- evaluate the outer function's gates, in its order -----------------
    int      gate  = 0;
    uint8_t* world = (uint8_t*)SafePtr((uint8_t*)comp + RVA::SpawnComp_World);
    uint8_t* gm    = world ? (uint8_t*)SafePtr(world + RVA::World_AuthGameMode) : nullptr;
    uint8_t* gs    = gm    ? (uint8_t*)SafePtr(gm    + RVA::GameMode_GameState) : nullptr;
    uint8_t  phase = 0;
    uint8_t* own   = (uint8_t*)SafePtr((uint8_t*)comp + RVA::SpawnComp_Owner);
    uint8_t  role  = 0;
    if      (!world) gate = 1;
    else if (!gm)    gate = 2;
    else if (!gs)    gate = 3;
    else if (!SafeCopy(gs + RVA::GameState_Phase, &phase, 1) || phase < 4) gate = 4;
    else if (!own)   gate = 5;
    else if (!SafeCopy(own + OFF::AActor_Role, &role, 1) || role != 3)     gate = 6;
    if (gate) InterlockedIncrement(&g_lootGate[gate]);

    // Gate 4 stands: the round has to be running. v58p, and it stays.
    if (gate == 4) { InterlockedIncrement(&g_spawnLobbySkipped); return false; }
    if (!MatchStarted(owner, nullptr)) { InterlockedIncrement(&g_spawnLobbySkipped); return false; }

    bool ok = false;
    if (gate == 0 || !g_gateBypass) {
        // Every gate is satisfied -- let the game's own entry point run.
        __try { ((tCompSpawn)(g_base + RVA::ItemSpawnComp_Spawn))(comp, 0.0f, 2); ok = true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    } else {
        // A gate we are allowed to step over. Do 0x2142DA0's own tail by hand.
        const uint8_t zero = 0;
        SafeCopy(&zero, (uint8_t*)comp + RVA::SpawnComp_ModeA, 1);
        SafeCopy(&zero, (uint8_t*)comp + RVA::SpawnComp_ModeB, 1);
        if (world) {
            uint32_t now = 0;
            if (SafeCopy(world + RVA::World_TimeSeconds, &now, 4))
                SafeCopy(&now, (uint8_t*)comp + RVA::SpawnComp_Time, 4);
        }
        __try { ((tCompSpawnInner)(g_base + RVA::ItemSpawnComp_SpawnInner))(comp, 2); ok = true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    }
    if (!ok) return false;

    LONG n = InterlockedIncrement(&g_lootFired);
    if (n <= 25) {
        wchar_t nm[128] = L"?";
        void* cls = SafePtr((uint8_t*)owner + OFF::UObject_Class);
        if (cls) ResolveFuncName(cls, nm, 128);
        L("[lq] Loot-Quelle %p an %ls %p ausgeloest (Match-Phase %d, %s) -- #%ld",
          comp, nm, owner, phase,
          gate ? kGateName[gate] : "alle Tore offen", n);
    }
    return true;
}

static void ForceTilesAroundRemote();        // v58r, defined with the streaming block

// ===========================================================================
//  v58v: FIND THE MAGAZINE FIELD BY WATCHING IT CHANGE
// ===========================================================================
//  Every attempt so far has needed a guess about WHERE the magazine count
//  lives. It does not have to be a guess. The weapon object is a flat block of
//  memory; when Alen fires a shot exactly one 32-bit slot in it goes 20 -> 19.
//  So snapshot the weapon four times a second, diff it, and print the slots
//  that changed together with their old and new values. One magazine emptied
//  on the range names the offset, and after that the fix can be aimed.
//  Ints only, and only plausible ammo values (0..400), so the log stays short
//  and does not fill up with timers and float noise.
static void*   g_watchWeapon = nullptr;
static bool    g_weaponWatch = true;         // -noweaponwatch
// v58w: and the second, much better candidate, straight out of the other
// team's reflection database (SP-hand-off-main, native-properties.json):
//     BravoHotelCharacter::CharacterReplication   object_reference  +0x40C8
//     BHCharacterReplication::ReplicatedStateID   uint8  +0xD4
//         net = true, OnRep_ReplicatedStateID
// A separate replication proxy object hanging off the character is exactly
// where a game puts the state it wants pushed to the owning client -- and the
// magazine count is that kind of state. It is a UObject, not an Actor, so it
// never gets a channel of its own: it rides on the character's, through
// ReplicateSubobjects. Watch it the same way as the weapon.
static void*   g_watchCharRep = nullptr;

// v58z: the v58y round produced NOT ONE field diff, and that was my
// instrument, not the game. SafeCopy is a single memcpy inside one __try, so
// reading 0x600 bytes off an object that ends before the next page boundary
// fails ENTIRELY -- and the old code then set g_watchWeapon = nullptr and went
// quiet. Silently. So: read in 0x40-byte chunks, keep a validity mask, never
// drop the watcher over a failed read, and diff BYTES as well as int32s,
// because a magazine counter is very often a uint8.
struct FieldSnap {
    uint8_t data[0x2800];            // v156: ABravoHotelRangedWeapon is 0x2610 B (SDK) -- bReady +0x2248,
    bool    ok[0x2800 / 0x40];       //       CurrentState +0x2249, bPendingReload +0x24EC, FireMode +0x25F8
    bool    valid;                   //       were outside the old 0x2000 window
};
static FieldSnap g_weaponSnapS;
static FieldSnap g_charRepSnapS;

static bool ReadChunked(void* obj, FieldSnap* out, size_t bytes) {
    bool any = false;
    for (size_t c = 0; c * 0x40 < bytes; ++c) {
        out->ok[c] = SafeCopy((uint8_t*)obj + c * 0x40, out->data + c * 0x40, 0x40);
        if (out->ok[c]) any = true;
    }
    return any;
}

// Report every slot that changed into a plausible small counter. Both widths,
// because we do not know whether the magazine is a byte or an int.
static void DiffSnap(const char* tag, void* obj, FieldSnap* prev, size_t bytes, int* reported) {
    FieldSnap now;
    if (!ReadChunked(obj, &now, bytes)) return;      // object gone this tick: just skip
    if (!prev->valid) { memcpy(prev, &now, sizeof(now)); prev->valid = true; return; }
    int shown = 0;
    for (size_t off = 0; off + 4 <= bytes && shown < 8 && *reported < 400; off += 4) {
        const size_t c = off / 0x40;
        if (!now.ok[c] || !prev->ok[c]) continue;
        int32_t a = 0, b = 0;
        memcpy(&a, prev->data + off, 4);
        memcpy(&b, now.data  + off, 4);
        if (a != b && a >= 0 && b >= 0 && a <= 400 && b <= 400) {
            L("[%s] %p: +0x%03X  int %d -> %d", tag, obj, (unsigned)off, a, b);
            ++shown; ++(*reported);
            continue;                                 // do not also report its bytes
        }
        for (size_t k = 0; k < 4 && shown < 8 && *reported < 400; ++k) {
            const uint8_t x = prev->data[off + k], y = now.data[off + k];
            if (x == y || x > 250 || y > 250) continue;
            const int d = (int)y - (int)x;
            if (d > 60 || d < -60) continue;           // a counter moves in small steps
            L("[%s] %p: +0x%03X  byte %d -> %d", tag, obj, (unsigned)(off + k), x, y);
            ++shown; ++(*reported);
        }
    }
    memcpy(prev, &now, sizeof(now));
}

static void WatchWeaponFields() {
    if (!g_weaponWatch || !g_watchWeapon) return;
    if (!MaybeAlive(g_watchWeapon)) { g_watchWeapon = nullptr; g_weaponSnapS.valid = false; return; }
    static int reported = 0;
    DiffSnap("wp", g_watchWeapon, &g_weaponSnapS, sizeof(g_weaponSnapS.data), &reported);
}

// v59b: the first 0x800 bytes of the weapon held nothing that moved while Alen
// emptied a magazine -- the only [wp] lines in that round were our own
// NetDormancy write at +0x20D and a flag at +0x9B. So the count is not there.
// GetCurrentWeapon reads Character+0x648 and hands that object to a resolver,
// which makes +0x648 the inventory/weapon holder -- a very plausible home for
// the magazine of the CURRENT weapon. Watch it too. Read-only, like the rest.
static void*     g_watchHolder = nullptr;
static FieldSnap g_holderSnap;
static void WatchWeaponHolder() {
    if (!g_weaponWatch || g_remoteCount == 0) return;
    void* pawn = g_remote[0].pawn;
    if (!pawn) return;
    void* h = SafePtr((uint8_t*)pawn + RVA::Character_WeaponHolder);
    if (!h) return;
    if (h != g_watchHolder) {
        g_watchHolder = h; g_holderSnap.valid = false;
        wchar_t hn[128] = L"?";
        void* cls = SafePtr((uint8_t*)h + OFF::UObject_Class);
        if (cls) ResolveFuncName(cls, hn, 128);
        L("[wh] Beobachte ab jetzt %ls %p (Pawn+0x648, der Waffenhalter aus GetCurrentWeapon)", hn, h);
    }
    static int reported = 0;
    DiffSnap("wh", g_watchHolder, &g_holderSnap, sizeof(g_holderSnap.data), &reported);
}

// v58x: ask the game which weapon the remote player is holding, instead of
// waiting for the level sweep to stumble over it. GetCurrentWeapon is a
// nineteen-byte leaf (Character+0x648 -> resolver 0x1C56650), so this is cheap
// enough to do on every poke and it follows every weapon switch instantly.
typedef void* (__fastcall* tGetCurrentWeapon)(void* character);
// v58z: THE WEAPON IS DORM_DormantAll. That is the finding of the v58y round:
//     [ow] BP-Weapon_SKS_LV3_C (repliziert 1, Dormancy 2, Role 3,
//                               RemoteRole 1, NetUpdateFrequency 100.0)
// Dormancy 2 = DORM_DormantAll: the actor replicates once and then sleeps, and
// a sleeping actor sends no property changes at all. Its channel opens over and
// over ([ch] six times for the same weapon on the same connection), which is
// the sleep/wake cycle -- so the client gets the state it had at each wake and
// nothing in between. That is exactly what Alen described: his copy of the
// magazine never moves, and only a weapon switch (a fresh equip) refreshes it.
//
// We cannot call FlushNetDormancy ourselves -- that is the branch that crashed
// the host twice (see v58y). v58z argued NetDormancy was an INPUT field the
// replication path merely reads, so writing DORM_Awake into it could not reach
// that branch. THAT WAS WRONG, and v59 -- the first build where the write
// actually ran -- crashed the host a third time, same access violation at
// address 0x40. The branch selects on exactly this byte and then dereferences a
// per-connection context that only exists inside the graph's own pass, and the
// v59 log shows the engine writing the byte straight back to 0 in the same tick
// we set it. This is the v58h rule again: never write the result field of an
// engine state machine, only the decision in front of it.
// Default OFF. -dormawake turns it back on for a deliberate experiment.
static bool          g_dormAwake = false;   // v59b: OFF -- see the header. -dormawake re-enables
static volatile LONG g_dormAwakeSet = 0;

static void KeepAwakeForClient(void* a, const wchar_t* what) {
    if (!g_dormAwake || !a) return;
    uint8_t dorm = 0;
    if (!SafeCopy((uint8_t*)a + OFF::AActor_NetDormancy, &dorm, 1)) return;
    if (dorm != 2 && dorm != 3) return;              // only the sleeping ones
    const uint8_t awake = 1;                          // DORM_Awake
    if (!SafeCopy(&awake, (uint8_t*)a + OFF::AActor_NetDormancy, 1)) return;
    LONG n = InterlockedIncrement(&g_dormAwakeSet);
    if (n <= 20)
        L("[da] %ls %p stand auf Dormancy %d (schlafend) -> DORM_Awake gesetzt, damit Aenderungen beim Client ankommen -- #%ld",
          what, a, dorm, n);
}

// ===========================================================================
//  v127 -- WEAPON ATTACHMENT, logged end to end
//
//  The symptom: switch to a weapon and it is invisible; switch away and back
//  and it appears. What is known so far:
//    * the same weapon actor takes five or six DIFFERENT channel objects in one
//      round, so its channel is torn down and rebuilt repeatedly ([ch]);
//    * the mesh is attached inside an OnRep, and an OnRep only fires when a
//      property CHANGES on the client -- the same mechanism that kept the blue
//      zone invisible until v122 forced a real change;
//    * from the reflection table: bIsEquipped and bPendingEquip are both
//      CPF_Net with RepNotify (OnRep_IsEquipped / OnRep_PendingEquip),
//      CurrentWeapon sits at character+0x350, OverrideAttachSocketName at
//      weapon+0x8D0.
//
//  Rather than guess where AttachParent lives, this calibrates itself: when a
//  weapon becomes current, scan its first 0x600 bytes for any qword that equals
//  the owning pawn or one of the pawn's components. Those slots ARE the
//  attachment. From then on, watch exactly those slots plus a full byte diff of
//  the weapon for the first seconds after the switch -- that catches bIsEquipped
//  flipping, the socket name being written, and the attach parent being cleared,
//  without needing a single hardcoded offset.
//
//  Tags: [wa] calibration and attach-slot changes, [wd] the byte diff.
//  Off with -nowatchattach.
// ===========================================================================
static bool          g_attachWatch = true;     // -nowatchattach
static volatile LONG g_waLogged = 0, g_wdLogged = 0;
static volatile LONG g_rrLogged = 0;    // v128: reload events with the ammo triple
static volatile LONG g_smIn = 0, g_smAck = 0, g_smAdj = 0;   // v129: the move ledger

struct AttachWatch {
    void*    weapon;
    void*    pawn;
    DWORD    since;              // when the switch happened
    int      slotN;              // offsets whose qword pointed at the pawn
    uint32_t slot[12];
    void*    slotVal[12];
    uint8_t  snap[0x600];
    bool     snapValid;
};
static AttachWatch g_aw[4];

static void AttachCalibrate(int i, void* weapon, void* pawn) {
    AttachWatch& a = g_aw[i];
    a.weapon = weapon; a.pawn = pawn; a.since = GetTickCount();
    a.slotN = 0; a.snapValid = false;
    // 1) which slots of the weapon point at the pawn?
    for (uint32_t off = 0; off + 8 <= 0x600 && a.slotN < 12; off += 8) {
        void* v = SafePtr((uint8_t*)weapon + off);
        if (v == pawn) { a.slot[a.slotN] = off; a.slotVal[a.slotN] = v; ++a.slotN; }
    }
    // 2) full byte snapshot, read in chunks so one bad page cannot kill it all
    bool ok = true;
    for (uint32_t off = 0; off < 0x600; off += 0x40)
        if (!SafeCopy((uint8_t*)weapon + off, a.snap + off, 0x40)) { ok = false; break; }
    a.snapValid = ok;
    if (g_waLogged < 200) {
        InterlockedIncrement(&g_waLogged);
        wchar_t cn[128] = L"?";
        ClassNameOf(weapon, cn, 128);
        wchar_t list[256] = L""; int used = 0;
        for (int k = 0; k < a.slotN && used < 200; ++k)
            used += swprintf_s(list + used, 256 - used, L"+0x%03X ", a.slot[k]);
        L("[wa] Spieler %d: neue Waffe %ls %p (Pawn %p) | Slots, die auf den Pawn zeigen: %ls%s"
          " | Byte-Abbild %s",
          i, cn, weapon, pawn, a.slotN ? list : L"KEINE",
          a.slotN ? "" : "  <<< die Waffe ist auf dem Host an NICHTS gebunden >>>",
          ok ? "gesetzt" : "FEHLGESCHLAGEN");
    }
}

// v130: ForceNetUpdate on any actor, through its vtable.
static void ForceNetUpdateOn(void* a) {
    if (!a) return;
    void** vt = (void**)SafePtr(a);
    if (!vt || !InModule(vt)) return;
    void* fn = SafePtr((uint8_t*)vt + OFF::AActor_ForceNetUpdate_VT);
    if (!fn || !InModule(fn)) return;
    __try { ((void (__fastcall*)(void*))fn)(a); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ===========================================================================
//  v130 -- THE INVISIBLE WEAPON, and why it is the same bug as the blue zone
//
//  The PC's log names it exactly:
//      LogSkinnedMeshComp: Warning: GetSocketByName(Sight_Basic): No SkeletalMesh
//          for Component(SkelMesh) Actor(BP-Weapon_SCAR_LV3_C_...)
//      LogSkinnedMeshComp: Warning: GetSocketByName(Handgrip_joint): No SkeletalMesh
//          for Component(FPSK_WeaponShadow) Actor(BP-BattleRoyalePawn_C_...)
//  The weapon actor exists on the client; its SkelMesh component simply has no
//  skeletal mesh assigned. There is nothing to draw, attached or not -- so the
//  attach slots were the wrong half of the problem.
//
//  Two replicated properties drive that mesh, and both are RepNotify:
//      AttachmentIndices     weapon   +0x190  TArray (Num +0x198), OnRep_ChangeAttachments
//      CurrentEquipWeaponID  character+0x3A4  int32,               OnRep_ChangeEquipWeapon
//  A RepNotify only fires on the client when the value CHANGES. A weapon whose
//  channel opens after it is already equipped receives both as part of its
//  opening state, not as a change, so neither OnRep runs and the mesh is never
//  applied. Switching away and back changes them for real, which is exactly the
//  workaround that works.
//
//  This is the third time the same defect has appeared -- the zone reference,
//  the zone index and the phase list all behaved identically -- and the same
//  repair applies: give each one a genuine one-frame change. Two int/Num writes
//  that are put back on the next tick, nothing else touched.
//
//  Off with -noweaponpush.
// ===========================================================================
// v132: THE v130 WEAPON PUSH IS WITHDRAWN. It crashed the host on the first
// weapon pickup, and the log shows it happening in the same breath:
//     [wa]  neue Waffe BP-Weapon_KAR98_LV3_C ... (Pawn 0000016FC462AAB0)
//     [wp2] CurrentEquipWeaponID 367 ... fuer EINEN Tick veraendert
//     [ax]  *** Ausnahme 0xC0000005 an base+0x20CF35A
// and the faulting code is
//     0x20CF34A  mov rcx, [rdi + 0x3A0]      ; rdi == that same pawn
//     0x20CF35A  subss xmm8, [rcx + 0x2E0]   ; <== fault
// CurrentEquipWeaponID at +0x3A4 is not a display value like the zone index --
// it is a live key the host's own code looks weapon data up by, and the result
// is cached in the neighbouring field at +0x3A0. Writing 367 -> 368 for one
// frame made the host resolve a weapon that does not exist, and the cached
// pointer it kept was then dereferenced.
// This is precisely the rule this project learned the hard way and wrote down:
// never write the RESULT field of an engine state machine, only the decision in
// front of it. The zone reference was safe because nothing caches off it; this
// is not the same thing and I should not have treated it as such.
// The push is therefore OFF unless -weaponpush is passed, and the ID write is
// gone for good. Only the array-length nudge remains behind that switch.
static bool          g_weaponPush = false;     // v132: OFF by default, -weaponpush
static volatile LONG g_wpPushes   = 0;
struct WeaponNudge { void* weapon; void* pawn; int phase; int32_t savedId; int32_t savedNum; };
static WeaponNudge g_wn[4];

static void WeaponPushTick() {
    if (!g_weaponPush) return;
    for (int i = 0; i < g_remoteCount && i < 4; ++i) {
        WeaponNudge& w = g_wn[i];
        void* wpn  = g_remote[i].weapon;
        void* pawn = g_remote[i].pawn;
        if (!wpn || !pawn || !MaybeAlive(wpn) || !MaybeAlive(pawn)) continue;

        if (w.phase == 1) {                       // second frame: put it back
            if (w.savedNum > 0) SafeCopy(&w.savedNum, (uint8_t*)wpn + 0x198, 4);
            L("[wp2] Waffe %p: AttachmentIndices zurueckgeschrieben (#%ld)", wpn, g_wpPushes);
            w.phase = 0; w.savedNum = 0;
            continue;
        }
        if (w.weapon == wpn) continue;            // already nudged for this weapon
        w.weapon = wpn; w.pawn = pawn;
        w.savedNum = 0;
        int32_t num = 0;
        if (SafeCopy((uint8_t*)wpn + 0x198, &num, 4) && num >= 1 && num < 64) {
            w.savedNum = num;
            int32_t shorter = num - 1;
            SafeCopy(&shorter, (uint8_t*)wpn + 0x198, 4);
            w.phase = 1;
            InterlockedIncrement(&g_wpPushes);
            ForceNetUpdateOn(wpn);
            L("[wp2] Waffe %p: AttachmentIndices Num %d fuer EINEN Tick gekuerzt (#%ld)."
              " CurrentEquipWeaponID wird NICHT mehr angefasst -- das hat v130 zum Absturz gebracht.",
              wpn, num, g_wpPushes);
        }
    }
}

static void AttachTick() {
    if (!g_attachWatch) return;
    for (int i = 0; i < g_remoteCount && i < 4; ++i) {
        void* w = g_remote[i].weapon;
        void* p = g_remote[i].pawn;
        if (!w || !p || !MaybeAlive(w)) continue;
        AttachWatch& a = g_aw[i];
        if (a.weapon != w) { AttachCalibrate(i, w, p); continue; }

        // (a) did any attach slot change?
        for (int k = 0; k < a.slotN; ++k) {
            void* v = SafePtr((uint8_t*)w + a.slot[k]);
            if (v != a.slotVal[k]) {
                if (g_waLogged < 200) {
                    InterlockedIncrement(&g_waLogged);
                    L("[wa] Spieler %d, Waffe %p: Bindung +0x%03X  %p -> %p%s",
                      i, w, a.slot[k], a.slotVal[k], v,
                      (v == nullptr) ? "   <<< GELOEST >>>"
                                     : ((v == p) ? "   (wieder am Pawn)" : ""));
                }
                a.slotVal[k] = v;
            }
        }

        // (b) byte diff for the first 8 s after a switch -- that is the window
        //     in which the equip actually happens.
        if (a.snapValid && (GetTickCount() - a.since) < 8000 && g_wdLogged < 400) {
            uint8_t now[0x600];
            bool ok = true;
            for (uint32_t off = 0; off < 0x600; off += 0x40)
                if (!SafeCopy((uint8_t*)w + off, now + off, 0x40)) { ok = false; break; }
            if (ok) {
                int shown = 0;
                for (uint32_t off = 0; off + 4 <= 0x600; off += 4) {
                    if (memcmp(a.snap + off, now + off, 4) == 0) continue;
                    if (shown == 0) {
                        InterlockedIncrement(&g_wdLogged);
                        L("[wd] Waffe %p, %lu ms nach dem Wechsel:", w,
                          (unsigned long)(GetTickCount() - a.since));
                    }
                    if (shown < 24) {
                        uint32_t o = 0, n2 = 0;
                        memcpy(&o, a.snap + off, 4); memcpy(&n2, now + off, 4);
                        L("[wd]    +0x%03X  %08X -> %08X   (int %d -> %d)",
                          off, o, n2, (int)o, (int)n2);
                    }
                    ++shown;
                }
                if (shown > 24) L("[wd]    ... %d weitere Aenderungen", shown - 24);
                memcpy(a.snap, now, 0x600);
            }
        }
    }
}

static void RefreshWatchedWeapon() {
    if (g_remoteCount == 0) return;
    // v112: walk EVERY remote. g_watchWeapon stays as remote 0's weapon so the
    // older single-player diagnostics keep working unchanged.
    for (int i = 0; i < g_remoteCount; ++i) {
        void* pawn = g_remote[i].pawn;
        if (!pawn || !MaybeAlive(pawn)) continue;
        void* w = nullptr;
        __try { w = ((tGetCurrentWeapon)(g_base + RVA::Character_GetCurrentWeapon))(pawn); }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (!w || !MaybeAlive(w)) continue;
        KeepAwakeForClient(w, L"Die Waffe eines Remote-Spielers");
        if (g_remote[i].weapon != w) {
            g_remote[i].weapon = w;
            g_remote[i].lastMag = g_remote[i].lastBp = g_remote[i].lastA = g_remote[i].lastB = -1;
            OwnedSetAdd(w);
            static int shownAny = 0;
            if (++shownAny <= 24) {
                wchar_t cn[128] = L"?";
                ClassNameOf(w, cn, 128);
                L("[ww] Spieler %d hat jetzt %ls %p", i, cn, w);
            }
        }
        if (i == 0 && w != g_watchWeapon) { g_watchWeapon = w; g_weaponSnapS.valid = false; }
    }
}

// v112: which remote is holding this weapon? -1 if none.
static int RemoteOfWeapon(void* w) {
    if (!w) return -1;
    for (int i = 0; i < g_remoteCount; ++i) if (g_remote[i].weapon == w) return i;
    return -1;
}

static void WatchCharRepFields() {
    if (!g_weaponWatch || g_remoteCount == 0) return;
    if (!g_watchCharRep) {
        void* pawn = g_remote[0].pawn;
        if (!pawn || !MaybeAlive(pawn)) return;
        g_watchCharRep = SafePtr((uint8_t*)pawn + RVA::Character_CharacterReplication);
        static int complained = 0;
        if (!g_watchCharRep) {
            if (++complained <= 3)
                L("[cr] Pawn %p hat bei +0x40C8 KEIN Replikations-Objekt -- der Offset aus dem Handoff passt fuer diese Klasse nicht", pawn);
            return;
        }
        g_charRepSnapS.valid = false;
        wchar_t cn[128] = L"?";
        void* cls = SafePtr((uint8_t*)g_watchCharRep + OFF::UObject_Class);
        if (cls) ResolveFuncName(cls, cn, 128);
        L("[cr] Beobachte ab jetzt %ls %p (Pawn+0x40C8, das Replikations-Objekt des Characters)", cn, g_watchCharRep);
    }
    if (!MaybeAlive(g_watchCharRep)) { g_watchCharRep = nullptr; g_charRepSnapS.valid = false; return; }
    static int reported = 0;
    DiffSnap("cr", g_watchCharRep, &g_charRepSnapS, 0x400, &reported);
}

// v58t: keep everything the remote player owns replicating. Runs on the game
// thread inside the MaskOff window, so FlushNetDormancy/ForceNetUpdate are not
// the no-ops they are with the mask on (the v58c finding). Four times a second
// over at most 256 actors -- cheap, and it is the only place a changed
// magazine count or perk field can be pushed out from.
// ===========================================================================
//  v59c: the magazine has an address, and the next question is relevance
//
//  The v59b round found it. Pawn+0x648 is a PlayerInventoryComponent (the game
//  names it itself), and on the weapon actor two adjacent ints move exactly
//  like an ammo counter:
//      +0x1B0  1 -> 9 -> 16 -> 20   (the reload, seen across four polls)
//              20 -> 12 -> 5        (firing)
//      +0x1B4  4 -> 24              (once, at the same moment)
//  Nothing else in 0x2000 bytes behaves like that. So [mag] prints the pair
//  with a millisecond stamp on every change -- one round with Alen telling me
//  what the HUD showed turns "looks like the magazine" into "is".
//
//  The delivery question is now the whole of #15, and the census line names a
//  candidate that we have already beaten once:
//      [cn] *** 1 x BP-Weapon_VECTOR_LV4_C (repliziert 1, immer-relevant 0,
//                                           Dormancy 4, Cull 150 m)
//  150 m class cull, not always-relevant -- the exact shape of the blue-zone
//  bug (#10). UBasicReplicationGraph keeps a PER-ACTOR copy of that cull
//  distance in FGlobalActorReplicationInfo (+0x90 / +0x94) and checks it in the
//  replicate loop. For the zone we zero that copy and the zone has worked ever
//  since.
//  Whether it bites here depends on something we have never measured: the
//  distance the graph computes is viewer -> ACTOR LOCATION, and an attached
//  weapon can easily keep a stale root location on the server. If it does, the
//  weapon is out of range almost always, its channel closes and reopens (the
//  v59b log has eleven opens for one weapon), and the client only ever gets the
//  full state of a fresh channel -- which is exactly Alen's "after 5-10 seconds
//  it applies by itself" and "switch away and back and it is right".
//  [wl] prints the distance, the flags and the graph's cull copy, so the log
//  says whether that is what happens. The fix is applied in the same round
//  because it costs nothing if the distance turns out to be zero.
// ===========================================================================
static bool          g_ownRelevant = true;         // -noownrelevant disables
static bool          g_magTest     = false;        // -magtest: one write, see WeaponRelevance
static bool          g_magTestDone = false;
static volatile LONG g_cullZeroed  = 0;
static int           g_relLogged   = 0;
static int           g_relTick     = 0;
static int32_t       g_magA = -1, g_magB = -1;
static int32_t       g_magMag = -1, g_magBp = -1;   // v60: the replicated pair
static volatile LONG g_magDirty = 0;                // v73: magazine changed this tick
// v74: OFF by default. The v73 round: ReplicateActor returned 0 twice and
// threw on the third call, and the SEH guard switched it off (the host did NOT
// crash -- the guard did its job). The reason is the one I should have weighed
// first: ReplicateActor is meant to run inside the graph's PER-CONNECTION pass,
// where the connection's replication state is set up. Calling it from our own
// tick is the same category of mistake as v58y's dormancy poke, and the return
// value of 0 said so before the exception did.
// Opt in with -magrep if we ever want it under a different context.
static bool          g_magRep   = false;            // -magrep
static volatile LONG g_magRepN  = 0;
// v156: OFF by default. The push was a workaround for the dormancy symptom
// (v154 fixed that at the source), and the 18:38 round shows its downside: the
// host's copy of a freshly picked-up weapon has Magazine 0 until the client's
// first reload, and the push sent ClientSetMagazine(0) sixteen times -- the
// client's magazine zeroed at every weapon switch. -magpush re-enables it;
// even then a 0 is never pushed.
static bool          g_magPush = true;              // v158: ON again (never 0) -- the host now reloads for real (v157) and the client's HUD needs the number pushed; -nomagpush disables
static int           g_magPushed = 0;

static void* GraphInfoFor(void* actor) {
    if (!g_drv || !actor) return nullptr;
    void* graph = SafePtr((uint8_t*)g_drv + OFF::UNetDriver_RepDriver);
    if (!graph) return nullptr;
    void** vt = (void**)SafePtr(graph);
    if (!IsKnownRepGraph(vt)) return nullptr;         // v71: Basic or Real, both fine
    typedef void* (__fastcall* tGet)(void* map, void** actorRef);
    void* ref = actor; void* info = nullptr;
    __try { info = ((tGet)(g_base + RVA::GlobalActorInfoMap_Get))((uint8_t*)graph + 0xC0, &ref); }
    __except (EXCEPTION_EXECUTE_HANDLER) { info = nullptr; }
    return info;
}

// Same one-value repair as the blue zone: the graph's own copy of the cull
// distance for this actor goes to zero, which switches the distance check in
// the replicate loop off for it (cull <= 0 -> no check, 0x13BECC6).
static void OwnedCullToZero(void* actor, const wchar_t* what) {
    if (!g_ownRelevant || !actor) return;
    void* info = GraphInfoFor(actor);
    if (!info) return;
    float sq = 0.0f;
    if (!SafeCopy((uint8_t*)info + 0x94, &sq, 4) || sq == 0.0f) return;
    float cull = 0.0f, zero = 0.0f;
    SafeCopy((uint8_t*)info + 0x90, &cull, 4);
    SafeCopy(&zero, (uint8_t*)info + 0x90, 4);
    SafeCopy(&zero, (uint8_t*)info + 0x94, 4);
    LONG n = InterlockedIncrement(&g_cullZeroed);
    if (n <= 20)
        L("[wl] %ls %p: Graph-Cull %.0f m (Sq %.0f) -> 0 -- der Actor wird fuer die Verbindung nicht mehr distanzgecullt -- #%ld",
          what, actor, cull / 100.0f, sq, n);
}

// ---------------------------------------------------------------------------
//  v60: push the magazine to the owning client with the game's own RPC
//
//  Static analysis settled what four rounds of measuring could not. The
//  replicated property is Magazine at weapon+0xDD0 (CPF_Net | CPF_RepNotify,
//  OnRep_Magazine), and the HUD is repainted by the OnRep delegate chain that
//  v58t already found (UBravoHotelAmmoWidget::UpdateAmmo, bound at
//  object+0x1DE8). The field this code watched until now has no reflection
//  entry at all, so nothing about it ever leaves the host.
//
//  This is not rule 1 being broken again. ClientSetMagazine is a Client RPC
//  the game declares on the weapon, with a single int32 parameter -- we call
//  it the way the game would: on the game thread, inside the MaskOff window,
//  only for the weapon of a remote player, and only when the host's own value
//  actually changed. No engine state machine, no result field. Since v58u the
//  weapon answers NM_ListenServer, so GetFunctionCallspace routes this Remote,
//  to the owning connection -- the same connection that already delivers
//  ClientAddInventoryItem to the millisecond (v58v).
// ---------------------------------------------------------------------------
typedef void* (__fastcall* tGetMagFn)();
typedef void  (__fastcall* tProcessEvent)(void* obj, void* fn, void* params);
typedef void* (__fastcall* tGetConn)(void* actor);   // tCallspace already exists above

static void* ClientSetMagazineFn() {
    static void* fn = nullptr;
    static bool  tried = false;
    if (tried) return fn;
    tried = true;
    __try { fn = ((tGetMagFn)(g_base + RVA::Weapon_GetClientSetMagazineFn))(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { fn = nullptr; }
    if (fn && !InModule((void*)SafePtr(fn))) fn = nullptr;   // vtable sanity
    L("[mp] ClientSetMagazine-UFunction %p", fn);
    return fn;
}

// ---------------------------------------------------------------------------
//  v65: which nodes does the graph actually ask?
//
//  The v64 round settled what replicates. In a whole round the graph touched
//  exactly three actors: the remote player's own pawn, the GameState and the
//  blue zone -- and the zone only because we force bAlwaysRelevant on it. Every
//  one of those comes from a NON-spatial node. Not one spatially gathered actor
//  in the entire round: no weapon, no pickup, no door, no window.
//
//  UReplicationGraph::ServerReplicateActors gathers from two arrays, read
//  straight off 0x13C6C60:
//      graph + 0xA0 / + 0xA8          global nodes
//      connMgr + 0x1C0 / + 0x1C8      per-connection nodes
//  and calls node->vtable[0x288] (GatherActorListsForConnection) on each.
//  UBasicReplicationGraph keeps its GridNode at graph+0x5A0, the always-
//  relevant node at +0x5A8 and the owner node at +0x5C0.
//
//  So the question is one pointer comparison: is the GridNode in that global
//  list at all? If it is not, the spatial gather never runs and that single
//  fact is #15, #13, #14 and #16 at once. Pure reads, printed once.
// ---------------------------------------------------------------------------

// v94/v95: what does the grid really contain?
//
// Proven layout, all of it from the disassembly:
//   GridCell::Gather (0x13AF990)   own list ptr +0x58, count +0x68
//                                  children TArray +0x30 / count +0x38
//   FrequencyBuckets::Gather (0x13AFA50)
//                                  bucket array +0xA0, count +0xA8,
//                                  entry 0x18 bytes: list +0x00, count +0x10
//   FActorRepList                  {int32 RefCount, Max, Num} then the actors
//
// v94 answered the routing question and killed it: the grid holds 1734
// entries while the driver registered 1599 actors, so actors DO reach the
// spatial node. But entries are not actors -- UE4 files an actor into every
// cell its cull radius covers, so one actor with a 150 m cull lands in nine
// 100 m cells. 1734 entries could be 1734 actors or 190. That difference is
// the whole question, so count DISTINCT actors and name their classes.
static void* g_census[4096];
static int   g_censusN = 0;
static wchar_t g_censusClsName[64][96];
static int   g_censusClsN[64];
static int   g_censusClsCount = 0;
static void* g_censusSample[10];
static int   g_censusSampleN = 0;

static void CensusReset() {
    g_censusN = 0; g_censusClsCount = 0; g_censusSampleN = 0;
}
static bool CensusAdd(void* a) {
    for (int i = 0; i < g_censusN; ++i) if (g_census[i] == a) return false;
    if (g_censusN < 4096) g_census[g_censusN++] = a;
    if (g_censusSampleN < 10) g_censusSample[g_censusSampleN++] = a;
    // v96: v95 bucketed by class POINTER and came back with zero classes for
    // all 56 entries, so those pointers are not what we assumed. Resolve the
    // name off the object itself and bucket by that -- and keep raw samples
    // so a failure is visible instead of silent.
    wchar_t nm[96] = L"";
    if (!ClassNameOf(a, nm, 96) || !nm[0]) wcscpy_s(nm, 96, L"<kein Klassenname>");
    for (int i = 0; i < g_censusClsCount; ++i)
        if (wcscmp(g_censusClsName[i], nm) == 0) { ++g_censusClsN[i]; return true; }
    if (g_censusClsCount < 64) {
        wcscpy_s(g_censusClsName[g_censusClsCount], 96, nm);
        g_censusClsN[g_censusClsCount] = 1;
        ++g_censusClsCount;
    }
    return true;
}

// Walk one FActorRepList and feed the census. The header is three int32 and
// the actor pointers follow, but the exact start is worth confirming rather
// than assuming, so try +0x0C first and fall back to +0x10.
static int ListActors(void* lp, int n, bool census) {
    if (!lp || n <= 0 || n > 100000) return 0;
    for (uintptr_t d = 0x0C; d <= 0x10; d += 4) {
        int hits = 0;
        for (int i = 0; i < n && i < 4096; ++i) {
            void* a = SafePtr((uint8_t*)lp + d + (size_t)i * 8);
            if (a && MaybeAlive(a)) ++hits;
        }
        if (hits * 2 >= n) {                    // that offset is the right one
            if (census)
                for (int i = 0; i < n && i < 4096; ++i) {
                    void* a = SafePtr((uint8_t*)lp + d + (size_t)i * 8);
                    if (a && MaybeAlive(a)) CensusAdd(a);
                }
            return hits;
        }
    }
    return 0;
}

static int CellActorCount(void* cellNode, bool census) {
    if (!cellNode) return 0;
    int total = 0;
    int32_t own = SafeI32((uint8_t*)cellNode + 0x68);
    if (own > 0 && own < 100000) {
        total += own;
        ListActors(SafePtr((uint8_t*)cellNode + 0x58), own, census);
    }

    void*   kids = SafePtr((uint8_t*)cellNode + 0x30);
    int32_t kidN = SafeI32((uint8_t*)cellNode + 0x38);
    if (!kids || kidN <= 0 || kidN > 16) return total;

    for (int k = 0; k < kidN; ++k) {
        void* kid = SafePtr((uint8_t*)kids + (size_t)k * 8);
        if (!kid) continue;
        int32_t kown = SafeI32((uint8_t*)kid + 0x68);
        if (kown > 0 && kown < 100000) {
            total += kown;
            ListActors(SafePtr((uint8_t*)kid + 0x58), kown, census);
        }
        void*   bArr = SafePtr((uint8_t*)kid + 0xA0);
        int32_t bNum = SafeI32((uint8_t*)kid + 0xA8);
        if (!bArr || bNum <= 0 || bNum > 64) continue;
        for (int b = 0; b < bNum; ++b) {
            uint8_t* e = (uint8_t*)bArr + (size_t)b * 0x18;
            int32_t n = SafeI32(e + 0x10);
            if (n > 0 && n < 100000) {
                total += n;
                ListActors(SafePtr(e), n, census);
            }
        }
    }
    return total;
}

static void DumpGraphNodesOnce() {
    // v75: three times, ~20 s apart, so the counts can be compared over time.
    static int  runs = 0;
    static DWORD nextAt = 0;
    if (runs >= 3 || !g_drv) return;
    const DWORD now = GetTickCount();
    if (nextAt && now < nextAt) return;
    void* graph = SafePtr((uint8_t*)g_drv + OFF::UNetDriver_RepDriver);
    if (!graph) return;
    void** gvt = (void**)SafePtr(graph);
    if (!IsKnownRepGraph(gvt)) return;            // v71: Basic or Real, both fine
    ++runs; nextAt = now + 20000;

    void* grid  = SafePtr((uint8_t*)graph + 0x5A0);
    void* arel  = SafePtr((uint8_t*)graph + 0x5A8);
    void* owner = SafePtr((uint8_t*)graph + 0x5C0);
    void* arr   = SafePtr((uint8_t*)graph + 0xA0);
    int   n     = SafeI32((uint8_t*)graph + 0xA8);
    L("[gn] Graph %p -- globale Knoten: Liste %p, %d Eintraege | GridNode %p | AlwaysRelevant %p | NurBesitzer %p",
      graph, arr, n, grid, arel, owner);

    bool gridListed = false;
    for (int i = 0; i < n && i < 32; ++i) {
        void* node = SafePtr((uint8_t*)arr + (size_t)i * 8);
        if (node && node == grid) gridListed = true;
        wchar_t cn[128] = L"?";
        if (node) ClassNameOf(node, cn, 128);
        L("[gn]   global #%d  %p  %ls%ls", i, node, cn,
          (node && node == grid) ? L"   <== GridNode" : L"");
    }
    // v133: is the blue zone actually IN the AlwaysRelevant node?
    // This round the host had everything -- name LV-OrbIsland_ALPHA_16_10_1,
    // InfoIndex 1, AreaDescKey 'Hidden Valley', a six-entry phase list -- the
    // zone was bAlwaysRelevant, it was added to the driver three times and got
    // 18 ForceNetUpdates, and still: "Zonen-Kanal geoeffnet: 0 x" for a whole
    // minute, with no [jo] line for the zone at all. So the toggle from v122
    // could never fire: its trigger IS the channel opening, and the channel
    // never opens. Before touching anything else, settle the one question that
    // separates "the graph does not have it" from "the graph has it and the
    // connection refuses it": walk the AlwaysRelevant node's own actor list and
    // look for the zone. ReplicationGraphNode_ActorList keeps it at +0x58 with
    // the count at +0x68, the same layout the grid cells use.
    if (arel && g_blueZone) {
        void* lst = SafePtr((uint8_t*)arel + 0x58);
        int   ln  = SafeI32((uint8_t*)arel + 0x68);
        bool  found = false;
        int   shown = 0;
        if (lst && ln > 0 && ln < 4096) {
            for (int i = 0; i < ln; ++i) {
                void* a = SafePtr((uint8_t*)lst + (size_t)i * 8);
                if (a == g_blueZone) { found = true; }
                if (shown < 8 && a) {
                    wchar_t an[128] = L"?";
                    ClassNameOf(a, an, 128);
                    L("[ar]   AlwaysRelevant #%d  %p  %ls%ls", i, a, an,
                      (a == g_blueZone) ? L"   <== die BlueZone" : L"");
                    ++shown;
                }
            }
        }
        L("[ar] AlwaysRelevant-Knoten %p: Liste %p, %d Eintraege -- die BlueZone %p ist %ls."
          " %ls",
          arel, lst, ln, g_blueZone, found ? L"DRIN" : L"NICHT DRIN",
          found ? L"Der Graph hat sie also, und die Verbindung macht trotzdem keinen Kanal auf"
                  L" -- dann liegt es an der Relevanz je Verbindung, nicht am Knoten."
                : L"Damit ist es der Knoten: bAlwaysRelevant wird gesetzt, aber der Actor"
                  L" landet nie in der Liste, die immer gesendet wird.");
    }

    L("[gn] *** Der GridNode steht %ls in der globalen Knotenliste -- %ls ***",
      gridListed ? L"" : L"NICHT",
      gridListed ? L"die Sammelphase fragt ihn, das Problem liegt IM Knoten"
                 : L"die raeumliche Sammlung laeuft NIE, das ist die Ursache");

    // v66: the second list, and the one that decides whether the grid has any
    // cells at all. ServerReplicateActors walks graph+0xB0 / +0xB8 BEFORE the
    // gather and calls node->vtable[0x290] (PrepareForReplication) on each
    // (loop at 0x13C6EE2..0x13C6F13). A GridSpatialization2D node keeps newly
    // added actors in pending lists and only files them into cells inside that
    // call. If the GridNode is missing from this list its cells stay empty
    // forever -- it would be asked to gather every frame and always answer
    // "nobody", which is exactly what [rs] shows.
    void* parr = SafePtr((uint8_t*)graph + 0xB0);
    int   pn   = SafeI32((uint8_t*)graph + 0xB8);
    bool  gridPrepared = false;
    L("[gn] PrepareForReplication-Knoten: Liste %p, %d Eintraege", parr, pn);
    for (int i = 0; i < pn && i < 32; ++i) {
        void* node = SafePtr((uint8_t*)parr + (size_t)i * 8);
        if (node && node == grid) gridPrepared = true;
        wchar_t cn[128] = L"?";
        if (node) ClassNameOf(node, cn, 128);
        L("[gn]   prepare #%d  %p  %ls%ls", i, node, cn,
          (node && node == grid) ? L"   <== GridNode" : L"");
    }
    L("[gn] *** Der GridNode wird %ls vorbereitet -- %ls ***",
      gridPrepared ? L"" : L"NICHT",
      gridPrepared ? L"seine Zellen werden gefuellt, die Ursache liegt woanders"
                   : L"seine Zellen bleiben LEER, deshalb sammelt er nie jemanden");

    // -----------------------------------------------------------------------
    //  v75: look INSIDE the grid node.
    //
    //  Everything around it is healthy and it still gathers nobody, so the
    //  remaining question is whether actors ever land in its cells. Static
    //  analysis got as far as its construction, which is exact:
    //      UBasicReplicationGraph::InitGlobalGraphNodes (0x13B5A30) creates it,
    //      stores it at graph+0x5A0 and writes
    //          [node+0x58] = 10000.0f   CellSize
    //          [node+0x5C] = -2097152.0f SpatialBias.X
    //          [node+0x60] = -2097152.0f SpatialBias.Y
    //      then appends it to the global node array at graph+0xA0.
    //  A UReplicationGraphNode_GridSpatialization2D is 0x240 bytes
    //  (GetPrivateStaticClass at 0x13CA330 passes InSize = 0x240), so every
    //  list it owns lies inside that window.
    //
    //  A TArray is {ptr, int32 Num, int32 Max}. Printing every such triple in
    //  the node says in one round whether the grid has cells and whether the
    //  pending lists are holding actors that never get filed. Pure reads.
    // -----------------------------------------------------------------------
    if (grid) {
        void** nvt = (void**)SafePtr(grid);
        float cell = 0.f, bx = 0.f, by = 0.f;
        SafeCopy((uint8_t*)grid + 0x58, &cell, 4);
        SafeCopy((uint8_t*)grid + 0x5C, &bx, 4);
        SafeCopy((uint8_t*)grid + 0x60, &by, 4);
        L("[gc] GridNode %p  VTable base+0x%llX | CellSize %.0f  SpatialBias {%.0f, %.0f}",
          grid, (unsigned long long)((uintptr_t)nvt - g_base), cell, bx, by);
        int shown = 0;
        for (uintptr_t o = 0x08; o + 16 <= 0x240 && shown < 24; o += 8) {
            void*   ptr = SafePtr((uint8_t*)grid + o);
            int32_t num = SafeI32((uint8_t*)grid + o + 8);
            int32_t max = SafeI32((uint8_t*)grid + o + 12);
            if (!ptr || (uintptr_t)ptr < 0x10000) continue;
            if (num <= 0 || num > 200000 || max < num || max > 400000) continue;
            ++shown;
            L("[gc]   +0x%03llX  TArray{%p, Num=%d, Max=%d}",
              (unsigned long long)o, ptr, num, max);
        }
        if (!shown) L("[gc]   keine belegte TArray im Knoten gefunden -- der Knoten haelt NICHTS");

        // -------------------------------------------------------------------
        //  v89: [gx] -- X-ray of the grid itself.
        //
        //  Static analysis of the gather (0x13B12F0) settled the layout, so we
        //  can now read exactly what the engine reads:
        //
        //      node+0x58  float CellSize          (10000)
        //      node+0x5C  float SpatialBias.X     (-2097152)
        //      node+0x60  float SpatialBias.Y     (-2097152)
        //      node+0x64  float ConnectionMaxZ    -- viewer is SKIPPED if this
        //                                            is below the viewer's Z
        //      node+0x210 TArray<TArray<Cell*>>   Grid (Num at +0x218)
        //
        //  and the gather's inner loop is literally:
        //
        //      Column = Grid[CellX];                     // grown on demand
        //      Cell   = Column.Num() > CellY ? Column[CellY] : nullptr;
        //      if (Cell) Cell->vtable[0x288](Cell, Params);   // <- the gather
        //
        //  So "gathered nothing" has exactly three possible causes, and this
        //  instrument tells them apart in one round:
        //    1. ConnectionMaxZ rejects the viewer before the grid is touched,
        //    2. the grid holds no non-null cell anywhere -> actors never reach
        //       the node (a ROUTING problem, not a grid problem),
        //    3. cells exist but not at the player's cell -> a coordinate or
        //       bias mismatch, and the log says which cells are occupied.
        //  Pure reads, nothing is written.
        // -------------------------------------------------------------------
        float maxZ = 0.f;
        SafeCopy((uint8_t*)grid + 0x64, &maxZ, 4);

        void*   gridArr = SafePtr((uint8_t*)grid + 0x210);
        int32_t gridNum = SafeI32((uint8_t*)grid + 0x218);

        float vp[3] = { 0, 0, 0 };
        bool haveVp = (g_remoteCount > 0 && g_remote[0].pawn)
                    ? ActorLocation(g_remote[0].pawn, vp) : false;

        L("[gx] Raster %p, Spalten %d | CellSize %.0f  Bias {%.0f, %.0f}  ConnectionMaxZ %.0f",
          gridArr, gridNum, cell, bx, by, maxZ);

        if (haveVp && cell > 0.f) {
            const int cx = (int)((vp[0] - bx) / cell);
            const int cy = (int)((vp[1] - by) / cell);
            L("[gx] Blickpunkt {%.0f, %.0f, %.0f} -> Zelle [%d][%d]", vp[0], vp[1], vp[2], cx, cy);
            if (maxZ < vp[2]) {
                L("[gx] *** ConnectionMaxZ (%.0f) liegt UNTER der Zuschauer-Hoehe (%.0f) -- der Gather"
                  " ueberspringt den Zuschauer, bevor er das Raster ueberhaupt anfasst ***", maxZ, vp[2]);
            }
            if (gridArr && cx >= 0 && cx < gridNum) {
                void*   colArr = SafePtr((uint8_t*)gridArr + (size_t)cx * 16);
                int32_t colNum = SafeI32((uint8_t*)gridArr + (size_t)cx * 16 + 8);
                L("[gx] Spalte [%d]: TArray{%p, Num=%d}", cx, colArr, colNum);
                if (colArr && cy >= 0 && cy < colNum) {
                    void* cellNode = SafePtr((uint8_t*)colArr + (size_t)cy * 8);
                    if (cellNode) {
                        wchar_t cnm[128] = L"?";
                        ClassNameOf(cellNode, cnm, 128);
                        L("[gx] *** Zelle [%d][%d] EXISTIERT: %p (%ls) -- der Gather ruft sie auf,"
                          " das Problem sitzt IN der Zelle ***", cx, cy, cellNode, cnm);

                        // ---------------------------------------------------
                        //  v90: [cz] -- look inside the cell.
                        //
                        //  The grid is healthy: 634-1199 occupied cells,
                        //  ConnectionMaxZ 2097152 (read as a FLOAT this time,
                        //  so it really does pass), and the player's own cell
                        //  is a live ReplicationGraphNode_GridCell. The gather
                        //  calls it and still collects nothing, so the cell
                        //  either holds no actors or filters them all away.
                        //
                        //  A UReplicationGraphNode_GridCell is an ActorList
                        //  node: one plain list plus a per-streaming-level
                        //  collection. The per-level lists are only handed out
                        //  for levels the CLIENT has reported as visible --
                        //  which is the obvious suspect, because floor loot,
                        //  doors and windows all live in streaming sublevels.
                        //
                        //  Dump every list the cell owns and name a few of the
                        //  actors in the biggest one. If the cell is full, the
                        //  fault is the filter; if it is empty, the fault is
                        //  upstream. Also print the cell's vtable RVA so its
                        //  gather can be disassembled directly.
                        // ---------------------------------------------------
                        void** cvt = (void**)SafePtr(cellNode);
                        L("[cz] Zelle %p  VTable base+0x%llX", cellNode,
                          (unsigned long long)((uintptr_t)cvt - g_base));

                        uintptr_t bestOff = 0; void* bestPtr = nullptr; int32_t bestNum = 0;
                        int lists = 0;
                        for (uintptr_t o = 0x08; o + 16 <= 0x200; o += 8) {
                            void*   ptr = SafePtr((uint8_t*)cellNode + o);
                            int32_t num = SafeI32((uint8_t*)cellNode + o + 8);
                            int32_t max = SafeI32((uint8_t*)cellNode + o + 12);
                            if (!ptr || (uintptr_t)ptr < 0x10000) continue;
                            if (num <= 0 || num > 100000 || max < num || max > 200000) continue;
                            ++lists;
                            L("[cz]   +0x%03llX  TArray{%p, Num=%d, Max=%d}",
                              (unsigned long long)o, ptr, num, max);
                            if (num > bestNum) { bestNum = num; bestPtr = ptr; bestOff = o; }
                        }
                        if (!lists) {
                            L("[cz] *** Die Zelle haelt KEINE einzige belegte Liste --"
                              " die Actors kommen nie in der Zelle an ***");
                        } else if (bestPtr) {
                            L("[cz]   groesste Liste +0x%03llX (%d Eintraege) -- die ersten Actors:",
                              (unsigned long long)bestOff, bestNum);
                            int named = 0;
                            for (int i = 0; i < bestNum && named < 8; ++i) {
                                void* a = SafePtr((uint8_t*)bestPtr + (size_t)i * 8);
                                if (!a || !MaybeAlive(a)) continue;
                                wchar_t an[128] = L"?";
                                ClassNameOf(a, an, 128);
                                float al[3] = { 0, 0, 0 }; ActorLocation(a, al);
                                ++named;
                                L("[cz]     #%d  %p  %ls  @ {%.0f, %.0f, %.0f}",
                                  i, a, an, al[0], al[1], al[2]);
                            }
                            if (!named)
                                L("[cz]     keiner der Eintraege sieht wie ein lebender Actor aus --"
                                  " die Liste haelt vermutlich etwas anderes");
                        }

                        // ---------------------------------------------------
                        //  v91: [sl] -- the streaming-level filter.
                        //
                        //  UReplicationGraphNode_GridCell::GatherActorListsFor-
                        //  Connection is 0x13AF990 and is only 34 bytes of
                        //  setup plus this:
                        //
                        //      if (this[0x68] > 0)                  // own list
                        //          Out.Add(this[0x58]);
                        //      StreamingLevelCollection_Gather(this+0x70, P);
                        //      for (child in this[0x30]) child->Gather(P);
                        //
                        //  and the collection gather (0x13AF790) walks entries
                        //  of 0x28 bytes {FName LevelName @+0x00, list @+0x10,
                        //  Num @+0x20} and, for every entry whose name is NOT
                        //  in Params->ClientVisibleLevelNames (Params+0xF0),
                        //  SKIPS THE WHOLE LEVEL:
                        //
                        //      idx = SetFind(P[0xF0], entry.Name);
                        //      if (*idx == -1) continue;           // dropped
                        //
                        //  Loot, doors and windows all live in streaming
                        //  sublevels, so if the server thinks the client has
                        //  none of them loaded, every one of those lists is
                        //  discarded here -- and that single check would
                        //  explain #13, #14 and the loot under the floor at
                        //  once. Read the cell's own list and every level
                        //  entry it holds. Pure reads.
                        // ---------------------------------------------------
                        void*   ownList = SafePtr((uint8_t*)cellNode + 0x58);
                        int32_t ownNum  = SafeI32((uint8_t*)cellNode + 0x68);
                        L("[sl] Zelle: eigene Liste %p Num=%d  (die geht IMMER raus)", ownList, ownNum);

                        // v92: the v91 dump settled it -- the cell itself is EMPTY.
                        // Its own list is Num=0 and everything from +0x70 to +0x138
                        // is zero, so its streaming-level collection holds no levels
                        // at all. There is nothing there for the visibility filter to
                        // throw away, so that filter is NOT the cause for this cell.
                        //
                        // GridCell::Gather ends by forwarding to its children, and the
                        // children are where the actors must be:
                        //     ReplicationGraphNode_ActorListFrequencyBuckets
                        //     ReplicationGraphNode_DormancyNode
                        // Follow them. For each: its vtable RVA (so its own gather can
                        // be disassembled), its own list (+0x58 ptr / +0x68 Num, the
                        // layout the cell's gather proves), its streaming-level
                        // collection, and a raw window so nothing is guessed. Reads only.
                        void*   kids  = SafePtr((uint8_t*)cellNode + 0x30);
                        int32_t kidN  = SafeI32((uint8_t*)cellNode + 0x38);
                        L("[ch] Zelle hat %d Kindknoten", kidN);
                        for (int k = 0; k < kidN && k < 6; ++k) {
                            void* kid = SafePtr((uint8_t*)kids + (size_t)k * 8);
                            if (!kid) continue;
                            wchar_t kn[128] = L"?";
                            ClassNameOf(kid, kn, 128);
                            void** kvt = (void**)SafePtr(kid);
                            void*   kList = SafePtr((uint8_t*)kid + 0x58);
                            int32_t kNum  = SafeI32((uint8_t*)kid + 0x68);
                            L("[ch] Kind #%d %p  %ls  VTable base+0x%llX", k, kid, kn,
                              (unsigned long long)((uintptr_t)kvt - g_base));
                            L("[ch]   eigene Liste %p Num=%d", kList, kNum);

                            // v93: the bucket layout is settled by disassembly.
                            // ActorListFrequencyBuckets::Gather (0x13AFA50):
                            //   Settings = this[0x58] ? this[0x58] : &Default(0x6BA11A0)
                            //   if (Settings[8] == 0)                 // it IS 0 in the
                            //       goto slow;                        // shipped default
                            //   ...
                            // slow:
                            //   idx    = FrameNum % this[0xA8];       // NumBuckets
                            //   base   = this[0xA0];                  // bucket array
                            //   Out.Add(&base[idx * 0x18]);           // ONE bucket/frame
                            //   tail-call StreamingLevelCollection(this+0xB0)
                            //
                            // A bucket is an FActorRepListRefView of 0x18 bytes:
                            // list pointer at +0x00, cached count at +0x10 -- the same
                            // 0x10 spacing the cell's own list uses (+0x58 / +0x68).
                            // So: are the buckets actually holding actors?
                            if (wcsstr(kn, L"FrequencyBuckets")) {
                                void*   bArr = SafePtr((uint8_t*)kid + 0xA0);
                                int32_t bNum = SafeI32((uint8_t*)kid + 0xA8);
                                L("[bk] %d Eimer (je Frame wird GENAU EINER herausgegeben)", bNum);
                                for (int b = 0; b < bNum && b < 8; ++b) {
                                    uint8_t* e = (uint8_t*)bArr + (size_t)b * 0x18;
                                    void*   lp = SafePtr(e);
                                    int32_t ln = SafeI32(e + 0x10);
                                    L("[bk]   Eimer #%d: Liste %p  Num=%d", b, lp, ln);
                                    if (!lp || ln <= 0) continue;
                                    // FActorRepList header: {int32 RefCount, Max, Num} then the
                                    // actor pointers. Probe both plausible data offsets.
                                    L("[bk]     Kopf: %d / %d / %d", SafeI32(lp),
                                      SafeI32((uint8_t*)lp + 4), SafeI32((uint8_t*)lp + 8));
                                    for (uintptr_t d = 0x0C; d <= 0x10; d += 4) {
                                        int named = 0;
                                        for (int i = 0; i < ln && named < 4; ++i) {
                                            void* a = SafePtr((uint8_t*)lp + d + (size_t)i * 8);
                                            if (!a || !MaybeAlive(a)) continue;
                                            wchar_t an[128] = L"?";
                                            if (!ClassNameOf(a, an, 128)) continue;
                                            float al[3] = { 0, 0, 0 }; ActorLocation(a, al);
                                            ++named;
                                            L("[bk]     (Daten ab +0x%02llX) #%d %p %ls @ {%.0f, %.0f, %.0f}",
                                              (unsigned long long)d, i, a, an, al[0], al[1], al[2]);
                                        }
                                        if (named) break;
                                    }
                                }
                                void*   sArr = SafePtr((uint8_t*)kid + 0xB0 + 0xA0);
                                int32_t sNum = SafeI32((uint8_t*)kid + 0xB0 + 0xA8);
                                L("[bk]   Streaming-Level dieses Knotens: %p Num=%d", sArr, sNum);
                                for (int i = 0; i < sNum && i < 12; ++i) {
                                    uint8_t* e = (uint8_t*)sArr + (size_t)i * 0x28;
                                    wchar_t ln2[128] = L"?";
                                    FNameToStr(e, ln2, 128);
                                    L("[bk]     %ls -- %d Actors", ln2, SafeI32(e + 0x20));
                                }
                            }

                            // name a few actors out of the child's own list
                            int named = 0;
                            for (int i = 0; i < kNum && named < 6; ++i) {
                                void* a = SafePtr((uint8_t*)kList + (size_t)i * 8);
                                if (!a || !MaybeAlive(a)) continue;
                                wchar_t an[128] = L"?";
                                ClassNameOf(a, an, 128);
                                float al[3] = { 0, 0, 0 }; ActorLocation(a, al);
                                ++named;
                                L("[ch]     #%d  %p  %ls  @ {%.0f, %.0f, %.0f}",
                                  i, a, an, al[0], al[1], al[2]);
                            }

                            // every plausible TArray inside the child, plus a raw window
                            int found = 0;
                            for (uintptr_t o = 0x30; o + 16 <= 0x180; o += 8) {
                                void*   ptr = SafePtr((uint8_t*)kid + o);
                                int32_t num = SafeI32((uint8_t*)kid + o + 8);
                                int32_t max = SafeI32((uint8_t*)kid + o + 12);
                                if (!ptr || (uintptr_t)ptr < 0x10000) continue;
                                if (num <= 0 || num > 100000 || max < num || max > 200000) continue;
                                ++found;
                                L("[ch]   +0x%03llX  TArray{%p, Num=%d, Max=%d}",
                                  (unsigned long long)o, ptr, num, max);
                            }
                            if (!found) {
                                L("[ch]   keine belegte TArray gefunden -- Rohbild:");
                                for (uintptr_t o = 0x40; o < 0x120; o += 8)
                                    L("[ch]     +0x%03llX  %p  (als int32: %d / %d)",
                                      (unsigned long long)o, SafePtr((uint8_t*)kid + o),
                                      SafeI32((uint8_t*)kid + o), SafeI32((uint8_t*)kid + o + 4));
                            }
                        }
                    } else {
                        L("[gx] *** Zelle [%d][%d] ist NULL -- genau hier steigt der Gather aus ***", cx, cy);
                    }
                } else {
                    L("[gx] *** Spalte [%d] ist nur %d Eintraege lang, gebraucht wird [%d] --"
                      " die Zeile existiert gar nicht ***", cx, colNum, cy);
                }
            } else {
                L("[gx] *** Spalte [%d] liegt ausserhalb des Rasters (%d Spalten) ***", cx, gridNum);
            }
        }

        // Sweep the whole grid: how many cells are occupied, and where?
        if (gridArr && gridNum > 0 && gridNum < 4096) {
            int occCols = 0, occCells = 0;
            int minX = 1 << 30, maxX = -1, minY = 1 << 30, maxY = -1;
            int gridActors = 0, bestCellN = 0, bestCellX = -1, bestCellY = -1;
            CensusReset();
            for (int x = 0; x < gridNum; ++x) {
                void*   colArr = SafePtr((uint8_t*)gridArr + (size_t)x * 16);
                int32_t colNum = SafeI32((uint8_t*)gridArr + (size_t)x * 16 + 8);
                if (!colArr || colNum <= 0 || colNum > 8192) continue;
                bool colHit = false;
                for (int y = 0; y < colNum; ++y) {
                    void* cc = SafePtr((uint8_t*)colArr + (size_t)y * 8);
                    if (!cc) continue;
                    ++occCells; colHit = true;
                    if (x < minX) minX = x;  if (x > maxX) maxX = x;
                    if (y < minY) minY = y;  if (y > maxY) maxY = y;
                    // v94: count the actors this cell really holds -- its own
                    // list plus every child's buckets and own list.
                    int here = CellActorCount(cc, true);
                    gridActors += here;
                    if (here > bestCellN) { bestCellN = here; bestCellX = x; bestCellY = y; }
                }
                if (colHit) ++occCols;
            }
            if (occCells > 0) {
                L("[gx] Belegt: %d Zellen in %d Spalten, Bereich X %d..%d, Y %d..%d"
                  "  (entspricht Welt X %.0f..%.0f, Y %.0f..%.0f)",
                  occCells, occCols, minX, maxX, minY, maxY,
                  bx + minX * cell, bx + (maxX + 1) * cell,
                  by + minY * cell, by + (maxY + 1) * cell);
                // v94: the number that actually matters.
                L("[gt] *** Im GANZEN Raster stecken %d Actors, verteilt auf %d Zellen"
                  " (Durchschnitt %.2f pro Zelle). Vollste Zelle: [%d][%d] mit %d. ***",
                  gridActors, occCells,
                  occCells ? (double)gridActors / occCells : 0.0,
                  bestCellX, bestCellY, bestCellN);
                L("[gt] Zum Vergleich: der Treiber hat %ld Actors angemeldet.",
                  (long)g_addActorCalls);
                // v95: entries are not actors. UE4 files an actor into every cell
                // its cull radius covers, so one actor with a 150 m cull occupies
                // nine 100 m cells. Only the DISTINCT count says how much of the
                // world the client could ever see.
                L("[gt] *** UNTERSCHIEDLICHE Actors im Raster: %d (bei %d Eintraegen"
                  " -- im Schnitt %.1f Zellen je Actor). ***",
                  g_censusN, gridActors,
                  g_censusN ? (double)gridActors / g_censusN : 0.0);
                if (g_censusN >= 4096)
                    L("[gt]   (Zaehlgrenze 4096 erreicht -- es sind mindestens so viele)");
                // Which classes are actually in there? That names what the client
                // could see and, by omission, what is missing.
                L("[gt] Klassen im Raster (%d verschiedene):", g_censusClsCount);
                for (int pass = 0; pass < 20; ++pass) {
                    int best = -1;
                    for (int i = 0; i < g_censusClsCount; ++i)
                        if (g_censusClsN[i] > 0 && (best < 0 || g_censusClsN[i] > g_censusClsN[best]))
                            best = i;
                    if (best < 0) break;
                    L("[gt]   %5d x  %ls", g_censusClsN[best], g_censusClsName[best]);
                    g_censusClsN[best] = 0;
                }
                // The registration side, for comparison: which classes does the
                // driver hand to the graph, and how many of each?
                if (g_wakeCsReady) {
                    EnterCriticalSection(&g_wakeCs);
                    L("[dw] geweckt nach Klasse (%d verschiedene) -- jede davon kostet"
                      " Replikation gegen JEDE Verbindung:", g_wakeClsCount);
                    LONG wc[64]; int wn2 = g_wakeClsCount;
                    for (int i = 0; i < wn2; ++i) wc[i] = g_wakeClsN[i];
                    for (int pass = 0; pass < 12; ++pass) {
                        int best = -1;
                        for (int i = 0; i < wn2; ++i)
                            if (wc[i] > 0 && (best < 0 || wc[i] > wc[best])) best = i;
                        if (best < 0) break;
                        wchar_t cn4[128] = L"?";
                        void* cdo2 = SafePtr((uint8_t*)g_wakeCls[best] + OFF::UClass_DefaultObject);
                        if (!cdo2 || !ClassNameOf(cdo2, cn4, 128))
                            FNameToStr((uint8_t*)g_wakeCls[best] + OFF::UObject_Name, cn4, 128);
                        L("[dw]   %5ld x  %ls", (long)wc[best], cn4);
                        wc[best] = 0;
                    }
                    LeaveCriticalSection(&g_wakeCs);
                }
                if (g_anCsReady) {
                    EnterCriticalSection(&g_anCs);
                    L("[an] Beim Treiber angemeldet, nach Klasse (%d verschiedene):", g_anClsCount);
                    LONG copy[96]; int cnt = g_anClsCount;
                    for (int i = 0; i < cnt; ++i) copy[i] = g_anClsN[i];
                    for (int pass = 0; pass < 20; ++pass) {
                        int best = -1;
                        for (int i = 0; i < cnt; ++i)
                            if (copy[i] > 0 && (best < 0 || copy[i] > copy[best])) best = i;
                        if (best < 0) break;
                        wchar_t cn3[128] = L"?";
                        void* cdo = SafePtr((uint8_t*)g_anCls[best] + OFF::UClass_DefaultObject);
                        if (!cdo || !ClassNameOf(cdo, cn3, 128))
                            FNameToStr((uint8_t*)g_anCls[best] + OFF::UObject_Name, cn3, 128);
                        L("[an]   %5ld x  %ls", (long)copy[best], cn3);
                        copy[best] = 0;
                    }
                    LeaveCriticalSection(&g_anCs);
                }
                for (int i = 0; i < g_censusSampleN; ++i) {
                    float sl2[3] = { 0, 0, 0 };
                    ActorLocation(g_censusSample[i], sl2);
                    L("[gt]   Probe #%d %p  @ {%.0f, %.0f, %.0f}",
                      i, g_censusSample[i], sl2[0], sl2[1], sl2[2]);
                }
            } else {
                L("[gx] *** KEINE EINZIGE belegte Zelle im ganzen Raster --"
                  " die Actors erreichen den Knoten nie. Das ist ein ROUTING-Problem,"
                  " kein Raster-Problem. ***");
            }
        }
    }

    void* connArr = SafePtr((uint8_t*)graph + 0x40);
    int   connN   = SafeI32((uint8_t*)graph + 0x48);
    L("[gn] Verbindungen im Graphen: %d", connN);
    for (int c = 0; c < connN && c < 4; ++c) {
        void* cm = SafePtr((uint8_t*)connArr + (size_t)c * 8);
        if (!cm) continue;
        void* carr = SafePtr((uint8_t*)cm + 0x1C0);
        int   cn2  = SafeI32((uint8_t*)cm + 0x1C8);
        void* uconn = SafePtr((uint8_t*)cm + 0x30);
        int   st    = uconn ? SafeI32((uint8_t*)uconn + 0x1BC) : -1;
        L("[gn]   Verbindung #%d: Manager %p, UNetConnection %p (State %d, 3=offen), %d eigene Knoten",
          c, cm, uconn, st, cn2);
        for (int i = 0; i < cn2 && i < 16; ++i) {
            void* node = SafePtr((uint8_t*)carr + (size_t)i * 8);
            wchar_t cnm[128] = L"?";
            if (node) ClassNameOf(node, cnm, 128);
            L("[gn]     Knoten #%d  %p  %ls", i, node, cnm);
        }
    }
}

// ---------------------------------------------------------------------------
//  v80: ClientSetMagazine IS a Client RPC -- I read the wrong descriptor
//
//  Alen's SDK dump settles it. From BravoHotelGame_classes.h:
//
//    void ClientSetMagazine(int32_t NewMagazine);
//      // Function BravoHotelGame.BravoHotelWeaponBase.ClientSetMagazine
//      // (Net | NetReliableNative | Event | Public | NetClient)
//
//  That is a reliable client RPC, exactly the push path v60 wanted. My v62
//  conclusion that it "is not an RPC" came from reading FunctionFlags off the
//  wrong structure -- the pointer the native-table getter (0x2526BF0) handed
//  back is not the UFunction, which is also why v61 measured Callspace = Local:
//  our own router asks for FUNC_Net at UFunction+0xC8 and that object has
//  0x20400 (Native|Public) there. Wrong object, not a wrong design.
//
//  The SDK is trustworthy for this build: its UBasicReplicationGraph layout
//  (GridNode +0x5A0, AlwaysRelevantNode +0x5A8, ActorsWithoutNetConnection
//  +0x5C0) matches the pointers [gn] read out of the live graph exactly.
//
//  So find the UFunction the honest way -- walk the weapon class's own Children
//  chain (UField::Next at +0x28) up the SuperStruct chain (+0x48) and match the
//  name. Then VERIFY: only use it when its flags really carry FUNC_Net and
//  FUNC_NetClient. If they do not, we log and do nothing, so a wrong pointer
//  can never turn into a bogus call again.
// ---------------------------------------------------------------------------
// v82: the probe latched onto SuperStruct, so make it stricter
//
// v81 accepted the first slot whose pointer resolved to a name and whose
// ->Next also did. UStruct+0x48 satisfies that -- it is SuperStruct, and the
// "chain" it produced was the class hierarchy with each class's package as the
// second link:
//     BP-Weapon_SCAR_LV1_C -> /Game/.../BP-Weapon_SCAR_LV1
//     BravoHotelWeaponBase -> /Script/BravoHotelGame
// Useful in one way: it proves the class chain reaches ABravoHotelWeaponBase,
// which is where the SDK says ClientSetMagazine lives. But it is not the
// function list.
//
// So: skip 0x48, try both plausible UField::Next offsets (0x28 is Outer here,
// 0x30 is the likelier Next), and require a chain of at least 8 resolvable
// names -- a class has dozens of functions, a superclass pointer has two.
// Pick whichever candidate yields the longest chain.
static void* g_setMagFn = nullptr;
static bool  g_setMagTried = false;
static int   g_childOff = -1, g_nextOff = -1;

static int ChainLength(void* head, int nextOff) {
    int n = 0;
    void* f = head;
    for (int i = 0; f && i < 512; ++i) {
        wchar_t nm[128] = L"";
        if (!ResolveFuncName(f, nm, 128)) break;
        ++n;
        f = SafePtr((uint8_t*)f + nextOff);
    }
    return n;
}

static void* FindClientSetMagazine(void* weapon) {
    if (g_setMagTried) return g_setMagFn;
    g_setMagTried = true;
    void* cls = weapon ? SafePtr((uint8_t*)weapon + OFF::UObject_Class) : nullptr;
    if (!cls) return nullptr;

    // Walk up to the native base first -- that is where the functions live.
    void* base = cls;
    for (int d = 0; d < 10; ++d) {
        void* sup = SafePtr((uint8_t*)base + OFF::UStruct_SuperStruct);
        if (!sup || !MaybeAlive(sup)) break;
        base = sup;
        wchar_t bn[128] = L"";
        if (ResolveFuncName(base, bn, 128) && wcscmp(bn, L"BravoHotelWeaponBase") == 0) break;
    }

    if (g_childOff < 0) {
        int bestLen = 0;
        static const int kNext[2] = { 0x30, 0x28 };
        for (int off = 0x30; off <= 0xA0; off += 8) {
            if (off == (int)OFF::UStruct_SuperStruct) continue;    // that is the parent pointer
            void* head = SafePtr((uint8_t*)base + off);
            if (!head || !MaybeAlive(head)) continue;
            for (int k = 0; k < 2; ++k) {
                const int len = ChainLength(head, kNext[k]);
                if (len > bestLen) { bestLen = len; g_childOff = off; g_nextOff = kNext[k]; }
            }
        }
        if (g_childOff < 0 || bestLen < 8) {
            L("[mp] Keine Funktionsliste gefunden (laengste Kette %d) -- Push nicht moeglich", bestLen);
            g_childOff = -1;
            return nullptr;
        }
        L("[mp] Funktionsliste: UStruct+0x%02X, Next +0x%02X, %d Eintraege", g_childOff, g_nextOff, bestLen);
    }

    int listed = 0;
    for (int depth = 0; base && depth < 10; ++depth) {
        void* f = SafePtr((uint8_t*)base + g_childOff);
        for (int i = 0; f && i < 2048; ++i) {
            wchar_t nm[128] = L"";
            if (ResolveFuncName(f, nm, 128)) {
                if (listed < 10) { L("[mp]   %ls", nm); ++listed; }
                if (wcscmp(nm, L"ClientSetMagazine") == 0) {
                    const uint32_t fl = (uint32_t)SafeI32((uint8_t*)f + OFF::UFunction_FunctionFlags);
                    L("[mp] ClientSetMagazine gefunden: UFunction %p, FunctionFlags %#x -- Net %ls, NetClient %ls",
                      f, fl, (fl & FUNC_Net) ? L"JA" : L"nein", (fl & 0x01000000u) ? L"JA" : L"nein");
                    if ((fl & FUNC_Net) && (fl & 0x01000000u)) { g_setMagFn = f; return f; }
                    L("[mp] -> Flags passen nicht, wird NICHT benutzt");
                    return nullptr;
                }
            }
            f = SafePtr((uint8_t*)f + g_nextOff);
        }
        base = SafePtr((uint8_t*)base + OFF::UStruct_SuperStruct);
    }
    L("[mp] ClientSetMagazine NICHT in der Funktionsliste (Children +0x%02X, Next +0x%02X)", g_childOff, g_nextOff);
    return nullptr;
}

static void PushMagazineToClient(void* weapon, int32_t mag) {
    if (!g_magPush || !weapon) return;
    if (mag <= 0 || mag > 500) return;             // never act on a garbage read, and never push 0 (v156)

    void* fn = FindClientSetMagazine(weapon);
    if (!fn) return;

    void** vt = (void**)SafePtr(weapon);
    if (!vt) return;
    void* pe = SafePtr((uint8_t*)vt + RVA::VT_ProcessEvent);
    if (!pe || !InModule(pe)) return;

    // One last check at the call site: ask the router what this will do. A
    // client RPC on an actor the remote player owns must come back Remote (1);
    // anything else and we do not call, because Local would run the function
    // on the host instead of sending it -- v61's failure, understood at last.
    int cs = -1;
    __try { cs = ((tCallspace)(g_base + RVA::GetFunctionCallspace))(weapon, fn, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { cs = -1; }
    if (!(cs & 1)) {
        static int warned = 0;
        if (!warned++)
            L("[mp] Callspace %d fuer ClientSetMagazine -- nicht Remote, es wird NICHT gesendet", cs);
        return;
    }

    struct { int32_t NewMagazine; } params;
    params.NewMagazine = mag;
    const bool win = g_maskActive && g_drv && g_world;
    if (win) MaskOff();
    __try { ((tProcessEvent)pe)(weapon, fn, &params); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (win) MaskOn();
        L("[mp] ProcessEvent(ClientSetMagazine) hat geworfen -- Push abgeschaltet");
        g_magPush = false;
        return;
    }
    if (win) MaskOn();

    if (++g_magPushed <= 60)
        L("[mp] ClientSetMagazine(%d) an den besitzenden Client gesendet (Waffe %p, Callspace %d) -- #%d",
          mag, weapon, cs, g_magPushed);
}

// ===========================================================================
//  BEFEHLSDATEI  (v167)  --  das Admin-Panel greift in die laufende Runde
//
//  tools/sp-listen-alive.js schreibt je Befehl eine Zeile in die Datei aus
//  -cmdfile=<Pfad>:
//        <id><TAB><Text>
//  und liest die Antwort aus DIESEM Log:
//        [cmd] <id> ok <Text>        bzw.      [cmd] <id> fail <Grund>
//  Die Datei ist die Bruecke, weil die DLL kein eigenes Netz hat -- genau wie
//  bei loadouts.txt. Die Antwortzeilen sind ENGLISCH, weil sie unveraendert im
//  Panel stehen; alles andere bleibt Deutsch wie im Rest der Datei.
//
//  WAS SIE AUSFUEHREN KANN
//    cheatable            -> APlayerController::EnableCheats. Das ist die
//                            Funktion, die den CheatManager ueberhaupt erst
//                            anlegt; ohne ihn geht StartGame nicht.
//    StartGame <s> <bool> -> BravoHotelCheatManager::StartGame(InDelay,
//                            bUseAircraft): die Runde startet nach <s>
//                            Sekunden, mit oder ohne Flugzeug.
//    <Name>               -> jede ANDERE Funktion des CheatManagers oder des
//                            Controllers, die KEINE Parameter nimmt, per Name.
//                            Eine mit Parametern wird abgelehnt, nicht geraten.
//
//  Alles laeuft ueber die Reflexionsliste, die der Magazin-Push (v58) schon
//  abgeht -- UStruct-Kinderliste und UField::Next, beide zur Laufzeit gesucht.
//  Kein neuer RVA, kein Namentabellen-Trick. Feste Offsets: nur
//  APlayerController.CheatManager +0x620 (aus dem SDK-Dump, Class size 1776,
//  CheatManager @1568) und NumParms/ParmsSize direkt hinter FunctionFlags.
//
//  AUS, solange -cmdfile fehlt. Beim Start ist das aktuelle Dateiende der
//  Anfangspunkt, damit Befehle einer frueheren Sitzung nie nachtraeglich
//  losgehen -- ein "StartGame" von gestern in einer laufenden Runde waere
//  schlimmer als ein verlorener Befehl.
// ===========================================================================
static bool      g_cmdEnabled = false;           // -cmdfile
static wchar_t   g_cmdPath[512] = L"";
static long long g_cmdOff      = -1;             // -1 = noch nicht positioniert
static int       g_cmdCount    = 0;

// SDK-Dump: APlayerController.CheatManager ist ObjectProperty @1568 (0x620),
// CheatClass @1552. Beide in /Script/Engine.PlayerController (Groesse 1776).
constexpr uintptr_t APC_CheatManager    = 0x620;
// v167 nahm an, NumParms/ParmsSize laegen direkt hinter FunctionFlags (0xC8).
// Die erste Runde widerlegte das (17 Parameter, 58688 Byte), also steht die
// Annahme nicht mehr im Code -- geeicht wird an StartGame, siehe unten.

// v168: NumParms/ParmsSize liegen in DIESEM Build NICHT direkt hinter
// FunctionFlags -- v167 las dort 17 Parameter und 58688 Byte. Statt weiter zu
// raten wird das Layout an einer Funktion geeicht, deren Parameterblock aus
// dem SDK-Dump feststeht: BravoHotelCheatManager::StartGame = 2 Parameter,
// 8 Byte. Was danach gelesen wird, ist gemessen und nicht abgeleitet.
static int g_fnNumParmsOff  = -1;
static int g_fnParmsSizeOff = -1;

// v169: ZWEI Proben statt einer. v168 suchte 8 und direkt davor 2 und fand
// nichts -- die beiden Felder liegen in diesem Build also nicht nebeneinander,
// oder gar nicht dort, wo UE4 sie normalerweise hat. Mit einer zweiten
// Funktion, deren Block sicher LEER ist (Cheatable, 0 Parameter), wird daraus
// eine Differenzmessung: gesucht ist die Stelle, die bei StartGame 8 und bei
// Cheatable 0 liest. Das ist ungleich schaerfer als ein einzelner Wert, und
// mehrdeutige Treffer stehen im Log, damit die naechste Runde entscheidet.
static void CalibrateUFunctionLayout(void* withParams, void* withoutParams) {
    if (g_fnParmsSizeOff >= 0 || !withParams || !withoutParams) return;

    int psCand[4], psN = 0, npCand[4], npN = 0;
    for (int off = 0x60; off <= 0x200; off += 2) {
        if ((SafeI32((uint8_t*)withParams    + off) & 0xFFFF) == 8 &&
            (SafeI32((uint8_t*)withoutParams + off) & 0xFFFF) == 0 && psN < 4) psCand[psN++] = off;
    }
    for (int off = 0x60; off <= 0x200; ++off) {
        if ((SafeI32((uint8_t*)withParams    + off) & 0xFF) == 2 &&
            (SafeI32((uint8_t*)withoutParams + off) & 0xFF) == 0 && npN < 4) npCand[npN++] = off;
    }

    char ps[64] = "", np[64] = "";
    for (int i = 0; i < psN; ++i) { char t[12]; sprintf_s(t, "%s+0x%02X", i ? " " : "", psCand[i]); strcat_s(ps, t); }
    for (int i = 0; i < npN; ++i) { char t[12]; sprintf_s(t, "%s+0x%02X", i ? " " : "", npCand[i]); strcat_s(np, t); }
    L("[cmd] Eichung: ParmsSize-Kandidaten [%s], NumParms-Kandidaten [%s]", psN ? ps : "keine", npN ? np : "keine");

    if (psN) {
        g_fnParmsSizeOff = psCand[0];
        g_fnNumParmsOff  = npN ? npCand[0] : -1;
        if (npN) L("[cmd] UFunction-Layout geeicht: ParmsSize +0x%02X, NumParms +0x%02X",
                   g_fnParmsSizeOff, g_fnNumParmsOff);
        else     L("[cmd] UFunction-Layout geeicht: ParmsSize +0x%02X (NumParms nicht gefunden -- "
                   "die Groesse allein entscheidet, ob eine Funktion Parameter nimmt)", g_fnParmsSizeOff);
        if (psN > 1) L("[cmd] Achtung: %d ParmsSize-Kandidaten -- der erste wird benutzt", psN);
        return;
    }
    L("[cmd] UFunction-Layout NICHT eichbar -- Funktionen mit Parametern werden abgelehnt, "
      "parameterlose nur noch nach Namen (Cheatable, EnableCheats)");
}

// ASCII, ohne CRT-Gebimmel: Vergleich ohne Ruecksicht auf Gross/Klein.
static bool EqNoCase(const char* a, const char* b) {
    if (!a || !b) return false;
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
        if (!x) return true;
    }
}

// Die Funktionsliste EINER Klasse, fuer ein beliebiges Objekt. g_childOff /
// g_nextOff teilen wir uns mit dem Magazin-Push: wer zuerst probt, gewinnt.
static void* FindFunctionNamed(void* obj, const wchar_t* want, int* numParms, int* parmsSize) {
    if (numParms)  *numParms  = -1;
    if (parmsSize) *parmsSize = -1;
    if (!obj || !want) return nullptr;
    void* cls = SafePtr((uint8_t*)obj + OFF::UObject_Class);
    if (!cls || !MaybeAlive(cls)) return nullptr;

    for (int depth = 0; cls && depth < 14; ++depth) {
        // Offsets noch unbekannt? Auf DIESER Klasse proben. Eine Klasse mit
        // Funktionen liefert eine lange Kette lesbarer Namen, ein Elternzeiger
        // zwei -- also die laengste Kette gewinnt.
        if (g_childOff < 0) {
            int bestLen = 0;
            static const int kNext[2] = { 0x30, 0x28 };
            for (int off = 0x30; off <= 0xA0; off += 8) {
                if (off == (int)OFF::UStruct_SuperStruct) continue;
                void* head = SafePtr((uint8_t*)cls + off);
                if (!head || !MaybeAlive(head)) continue;
                for (int k = 0; k < 2; ++k) {
                    const int len = ChainLength(head, kNext[k]);
                    if (len > bestLen) { bestLen = len; g_childOff = off; g_nextOff = kNext[k]; }
                }
            }
            if (bestLen < 8) { g_childOff = -1; g_nextOff = -1; }
            else L("[cmd] Funktionsliste gefunden: UStruct+0x%02X, Next +0x%02X, %d Eintraege",
                   g_childOff, g_nextOff, bestLen);
        }

        if (g_childOff >= 0) {
            void* f = SafePtr((uint8_t*)cls + g_childOff);
            for (int i = 0; f && i < 4096; ++i) {
                wchar_t nm[128] = L"";
                if (ResolveFuncName(f, nm, 128) && wcscmp(nm, want) == 0) {
                    // -1 heisst "unbekannt", nicht "keine Parameter".
                    if (numParms && g_fnNumParmsOff >= 0)
                        *numParms = SafeI32((uint8_t*)f + g_fnNumParmsOff) & 0xFF;
                    if (parmsSize && g_fnParmsSizeOff >= 0)
                        *parmsSize = SafeI32((uint8_t*)f + g_fnParmsSizeOff) & 0xFFFF;
                    return f;
                }
                f = SafePtr((uint8_t*)f + g_nextOff);
            }
        }

        void* sup = SafePtr((uint8_t*)cls + OFF::UStruct_SuperStruct);
        if (!sup || !MaybeAlive(sup)) break;
        cls = sup;
    }
    return nullptr;
}

// ProcessEvent ueber die VTable des Objekts. KEIN MaskOff/MaskOn hier: der
// Aufrufer sitzt schon im MaskOff-Fenster von MyTickFlush, und ein eigenes
// MaskOn wuerde dieses Fenster vorzeitig schliessen.
static bool CallFunctionOn(void* obj, void* fn, void* params) {
    if (!obj || !fn) return false;
    void** vt = (void**)SafePtr(obj);
    if (!vt) return false;
    void* pe = SafePtr((uint8_t*)vt + RVA::VT_ProcessEvent);
    if (!pe || !InModule(pe)) return false;
    bool ok = true;
    __try { ((tProcessEvent)pe)(obj, fn, params); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

static void* CheatManagerOf(void* pc) {
    if (!pc) return nullptr;
    void* cm = SafePtr((uint8_t*)pc + APC_CheatManager);
    return (cm && MaybeAlive(cm)) ? cm : nullptr;
}

// "cheatable" -- ZWEI Tore, nicht eins. Das hat die erste v167-Runde gezeigt:
// der CheatManager war laengst da (UE4 legt ihn auf dem Host selbst an), und
// StartGame kam trotzdem nur bis zum Spiel: "BravoHotelCheat: Failed to apply
// [Player018: StartGame]". Das zweite Tor ist das des SPIELS --
// BravoHotelGameInstance::bCheatable (+0x2D8 laut SDK-Dump), und umgelegt wird
// es von BravoHotelCheatManager::Cheatable(), der parameterlosen Funktion
// hinter dem Konsolenbefehl gleichen Namens. Genau die rufen wir hier.
static bool CmdEnableCheats(char* out, int cap) {
    void* pc = RealFirstPlayerController();
    if (!pc) { sprintf_s(out, cap, "no host PlayerController yet -- is the round loaded?"); return false; }

    // Tor 1: der UE4-CheatManager. Auf einem Listen-Host steht er meistens
    // schon; fehlt er, legt ihn EnableCheats an.
    void* cm = CheatManagerOf(pc);
    if (!cm) {
        int np = -1, ps = -1;
        void* fn = FindFunctionNamed(pc, L"EnableCheats", &np, &ps);
        if (!fn) { sprintf_s(out, cap, "no cheat manager, and EnableCheats is not in the controller's function list"); return false; }
        L("[cmd] EnableCheats: UFunction %p, NumParms %d, ParmsSize %d", fn, np, ps);
        if (!CallFunctionOn(pc, fn, nullptr)) { sprintf_s(out, cap, "EnableCheats threw"); return false; }
        cm = CheatManagerOf(pc);
        if (!cm) { sprintf_s(out, cap, "EnableCheats ran but PlayerController+0x620 is still null"); return false; }
    }
    wchar_t cn[128] = L"?";
    ClassNameOf(cm, cn, 128);
    L("[cmd] CheatManager %p, Klasse %ls", cm, cn);

    // Bei der Gelegenheit das Parameter-Layout eichen, solange wir eine
    // Funktion in der Hand haben, deren Block bekannt ist.
    // Tor 2: das Spiel selbst.
    int np2 = -1, ps2 = -1;
    void* ch = FindFunctionNamed(cm, L"Cheatable", &np2, &ps2);
    { int a = -1, b = -1; void* sg = FindFunctionNamed(cm, L"StartGame", &a, &b);
      if (sg && ch) CalibrateUFunctionLayout(sg, ch);
      if (ch && g_fnParmsSizeOff >= 0) {
          np2 = g_fnNumParmsOff >= 0 ? (SafeI32((uint8_t*)ch + g_fnNumParmsOff) & 0xFF) : -1;
          ps2 = SafeI32((uint8_t*)ch + g_fnParmsSizeOff) & 0xFFFF;
      } }
    if (!ch) { sprintf_s(out, cap, "cheat manager %p is there, but it has no Cheatable() -- the game's own gate stays shut", cm); return false; }
    L("[cmd] Cheatable: UFunction %p, NumParms %d, ParmsSize %d", ch, np2, ps2);
    if (!CallFunctionOn(cm, ch, nullptr)) { sprintf_s(out, cap, "Cheatable() threw"); return false; }
    sprintf_s(out, cap, "cheat manager %p, Cheatable() called -- the game should accept cheats now", cm);
    return true;
}

static bool CmdStartGame(float delay, bool aircraft, char* out, int cap) {
    void* pc = RealFirstPlayerController();
    if (!pc) { sprintf_s(out, cap, "no host PlayerController yet -- is the round loaded?"); return false; }
    void* cm = CheatManagerOf(pc);
    if (!cm) { sprintf_s(out, cap, "no cheat manager (run cheatable first)"); return false; }

    int np = -1, ps = -1;
    void* fn = FindFunctionNamed(cm, L"StartGame", &np, &ps);
    if (!fn) { sprintf_s(out, cap, "StartGame not found in the cheat manager's function list"); return false; }
    { void* ch = FindFunctionNamed(cm, L"Cheatable", nullptr, nullptr);
      if (ch) CalibrateUFunctionLayout(fn, ch); }
    if (g_fnParmsSizeOff >= 0 && (np < 0 || ps < 0)) {       // erst jetzt lesbar
        if (g_fnNumParmsOff >= 0) np = SafeI32((uint8_t*)fn + g_fnNumParmsOff) & 0xFF;
        ps = SafeI32((uint8_t*)fn + g_fnParmsSizeOff) & 0xFFFF;
    }
    L("[cmd] StartGame: UFunction %p, NumParms %d, ParmsSize %d (erwartet 2 / 8)", fn, np, ps);
    // Der SDK-Dump sagt: InDelay float @0, bUseAircraft bool @4, 8 Byte gesamt.
    // Weicht die Laufzeit davon ab, wird NICHT aufgerufen -- ein zu kurzer
    // Parameterblock schreibt sonst in fremden Stack.
    if (ps > 0 && ps <= 256 && ps != 8) {
        sprintf_s(out, cap, "StartGame wants %d parameter bytes, not the 8 the SDK dump states -- not calling it", ps);
        return false;
    }
    if (ps < 0)
        L("[cmd] ParmsSize unbekannt (Layout nicht geeicht) -- Aufruf mit der SDK-Form (float, bool).");

    struct { float InDelay; uint8_t bUseAircraft; uint8_t pad[3]; } params{};
    params.InDelay = delay;
    params.bUseAircraft = aircraft ? 1 : 0;
    if (!CallFunctionOn(cm, fn, &params)) { sprintf_s(out, cap, "StartGame threw"); return false; }
    sprintf_s(out, cap, "StartGame %.0f %s sent to the cheat manager", (double)delay, aircraft ? "true" : "false");
    return true;
}

// Alles andere: eine Funktion OHNE Parameter, erst am CheatManager, dann am
// Controller. Mit Parametern wird sie nicht aufgerufen -- welche Werte sie
// erwartet, weiss hier niemand.
static bool CmdNamedFunction(const char* name, char* out, int cap) {
    wchar_t want[128] = L"";
    int k = 0;
    for (; name[k] && k < 127; ++k) want[k] = (wchar_t)(unsigned char)name[k];
    want[k] = 0;
    if (!k) { sprintf_s(out, cap, "empty command"); return false; }

    void* pc = RealFirstPlayerController();
    if (!pc) { sprintf_s(out, cap, "no host PlayerController yet -- is the round loaded?"); return false; }

    void* targets[2] = { CheatManagerOf(pc), pc };
    const char* where[2] = { "cheat manager", "controller" };
    for (int i = 0; i < 2; ++i) {
        if (!targets[i]) continue;
        int np = -1, ps = -1;
        void* fn = FindFunctionNamed(targets[i], want, &np, &ps);
        if (!fn) continue;
        if (ps < 0) {
            // Ohne Eichung wissen wir NICHT, ob die Funktion Parameter nimmt.
            // ProcessEvent wuerde dann aus einem Nullzeiger kopieren, also
            // nur das, was sicher parameterlos ist.
            // Diese vier stehen im SDK-Dump mit Parametergroesse 0 -- sie
            // duerfen auch ohne Eichung laufen.
            const bool knownEmpty = (wcscmp(want, L"Cheatable") == 0)
                                 || (wcscmp(want, L"EnableCheats") == 0)
                                 || (wcscmp(want, L"IWantToDie") == 0)
                                 || (wcscmp(want, L"ForceEndMatch") == 0);
            if (!knownEmpty) {
                sprintf_s(out, cap, "%s found, but this build's parameter fields are not calibrated yet -- run Enable cheats once, then try again", name);
                return false;
            }
        } else if (ps > 0 || np > 0) {
            sprintf_s(out, cap, "%s takes %d parameter(s), %d bytes -- only parameterless commands are allowed here", name, np, ps);
            return false;
        }
        L("[cmd] %s auf dem %s: UFunction %p, NumParms %d, ParmsSize %d", name, where[i], fn, np, ps);
        if (!CallFunctionOn(targets[i], fn, nullptr)) { sprintf_s(out, cap, "%s threw", name); return false; }
        sprintf_s(out, cap, "%s ran on the %s", name, where[i]);
        return true;
    }
    sprintf_s(out, cap, "no parameterless function called \"%s\" on the cheat manager or the controller", name);
    return false;
}

static void RunCommandLine(const char* id, char* text) {
    char out[240] = "";
    bool ok = false;

    // StartGame <Sekunden> <true|false>
    if (_strnicmp(text, "StartGame", 9) == 0 && (text[9] == ' ' || text[9] == 0)) {
        float delay = 10.0f;
        bool  air   = true;
        const char* p = text + 9;
        while (*p == ' ') ++p;
        if (*p) {
            delay = (float)atof(p);
            while (*p && *p != ' ') ++p;
            while (*p == ' ') ++p;
            if (*p) air = EqNoCase(p, "true") || EqNoCase(p, "1") || EqNoCase(p, "yes");
        }
        if (delay < 0.0f || delay > 600.0f) { sprintf_s(out, (int)sizeof(out), "delay %.0f is outside 0-600 seconds", (double)delay); ok = false; }
        else ok = CmdStartGame(delay, air, out, (int)sizeof(out));
    } else if (EqNoCase(text, "cheatable") || EqNoCase(text, "EnableCheats")) {
        ok = CmdEnableCheats(out, (int)sizeof(out));
    } else {
        ok = CmdNamedFunction(text, out, (int)sizeof(out));
    }

    // DIE Antwortzeile. tools/sp-listen-alive.js sucht genau dieses Muster.
    L("[cmd] %s %s %s", id, ok ? "ok" : "fail", out[0] ? out : (ok ? "done" : "failed"));
}

// Laeuft im Spiel-Thread aus MyTickFlush, rund zweimal je Sekunde.
static void CmdFileTick() {
    if (!g_cmdEnabled) return;
    if (!g_cmdPath[0]) {
        // Kein -cmdfile=<Pfad> angegeben: die Datei liegt neben DIESER DLL,
        // genau wie loadouts.txt. Das Modul wird ueber eine eigene Adresse
        // gesucht, damit hier kein Zeiger von weiter unten gebraucht wird.
        HMODULE self = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)&CmdFileTick, &self) && self) {
            GetModuleFileNameW(self, g_cmdPath, 500);
            wchar_t* bs = wcsrchr(g_cmdPath, L'\\');
            const size_t rem = bs ? (size_t)(512 - (bs + 1 - g_cmdPath)) : 0;
            if (bs && rem >= 24) wcscpy_s(bs + 1, rem, L"sp_listen_cmd.txt"); else g_cmdPath[0] = 0;
        }
        if (!g_cmdPath[0]) { g_cmdEnabled = false; L("[cmd] Pfad der eigenen DLL nicht lesbar -- Befehlsdatei AUS"); return; }
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, g_cmdPath, L"rb") != 0 || !f) return;     // noch nicht da: nichts zu tun
    _fseeki64(f, 0, SEEK_END);
    const long long size = _ftelli64(f);

    if (g_cmdOff < 0) {
        // Erster Blick: ans Ende stellen. Was vor dem Start drinstand, gehoert
        // zu einer anderen Sitzung und wird NICHT ausgefuehrt.
        g_cmdOff = size;
        L("[cmd] Befehlsdatei %ls, Start bei Byte %lld", g_cmdPath, size);
        fclose(f);
        return;
    }
    if (size < g_cmdOff) { g_cmdOff = 0; L("[cmd] Befehlsdatei wurde gekuerzt -- lese wieder von vorn"); }
    if (size == g_cmdOff) { fclose(f); return; }

    long long want = size - g_cmdOff;
    if (want > 16384) want = 16384;                 // pro Tick genug fuer hunderte Zeilen
    static char buf[16384 + 1];        // Spiel-Thread, ein Aufrufer -- nicht auf den Stack
    _fseeki64(f, g_cmdOff, SEEK_SET);
    const size_t got = fread(buf, 1, (size_t)want, f);
    fclose(f);
    if (!got) return;
    buf[got] = 0;

    // Nur VOLLSTAENDIGE Zeilen verarbeiten; ein halb geschriebener Rest wartet
    // auf den naechsten Tick.
    size_t lineStart = 0, consumed = 0;
    for (size_t i = 0; i < got; ++i) {
        if (buf[i] != '\n') continue;
        char line[1024];
        size_t len = i - lineStart;
        if (len > sizeof(line) - 1) len = sizeof(line) - 1;
        memcpy(line, buf + lineStart, len);
        line[len] = 0;
        while (len && (line[len - 1] == '\r' || line[len - 1] == ' ')) line[--len] = 0;
        lineStart = i + 1;
        consumed  = lineStart;

        char* tab = strchr(line, '\t');
        if (!tab || tab == line) continue;          // keine id, keine Antwort
        *tab = 0;
        char* text = tab + 1;
        while (*text == ' ') ++text;
        if (!*text) { L("[cmd] %s fail empty command", line); continue; }

        ++g_cmdCount;
        L("[cmd] #%d %s: %s", g_cmdCount, line, text);
        __try { RunCommandLine(line, text); }
        __except (EXCEPTION_EXECUTE_HANDLER) { L("[cmd] %s fail the DLL threw while running it", line); }
    }
    g_cmdOff += (long long)consumed;
}

// v79: OFF by default -- v78's graph re-route broke the blue zone and pushed
// the player downwards. Kept behind -weaponalways, never on by default again
// without evidence that removing and re-adding an actor from our own tick is
// safe. v80 does not need any of this; the RPC is the road now.
static bool          g_weaponAlways = false;       // -weaponalways
static void*         g_weaponMarked = nullptr;
static volatile LONG g_weaponAlwaysN = 0;

static void MakeHeldWeaponAlwaysRelevant(void* w) {
    if (!g_weaponAlways || !w || w == g_weaponMarked) return;
    if (!g_drv || !MaybeAlive(w)) return;
    uint8_t f = 0;
    if (!SafeCopy((uint8_t*)w + OFF::AActor_bAlwaysRelevant, &f, 1)) return;
    const bool had = (f & 1) != 0;
    if (!had) {
        uint8_t nf = (uint8_t)(f | 1);
        if (!SafeCopy(&nf, (uint8_t*)w + OFF::AActor_bAlwaysRelevant, 1)) return;
    }
    g_weaponMarked = w;
    void* graph = SafePtr((uint8_t*)g_drv + OFF::UNetDriver_RepDriver);
    const bool win = g_maskActive && g_drv && g_world;
    if (win) MaskOff();
    __try {
        if (graph && g_origGraphRemove) g_origGraphRemove(graph, w);
        if (g_origAddNetActor)          g_origAddNetActor(g_drv, w);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (win) MaskOn();
        L("[wa] Neu-Einsortieren der Waffe hat geworfen -- abgeschaltet");
        g_weaponAlways = false;
        return;
    }
    if (win) MaskOn();
    LONG n = InterlockedIncrement(&g_weaponAlwaysN);
    if (n <= 20) {
        wchar_t cn[128] = L"?";
        ClassNameOf(w, cn, 128);
        L("[wa] Gehaltene Waffe %ls %p: bAlwaysRelevant %ls, neu einsortiert -- #%ld",
          cn, w, had ? L"war schon gesetzt" : L"gesetzt", n);
    }
}

// ===========================================================================
//  v157 -- THE RELOAD GATE, read out of the client code at last
//
//  ABravoHotelRangedWeapon::CanReload (0x1EF2F80, the check DoReload 0x1FF12C0
//  runs before it ever sends ServerReloadWeapon) ends, on a CLIENT, with
//      ... && backpackAmmo > 0 && weapon->+0x21E0 != 0
//  +0x21E0 is a native bool that only two RepNotify handlers set:
//      0x1F073E0  OnRep_IsEquipped:      +0x21E0 = bIsEquipped || State==Equipping
//      0x1F08E80  OnRep_IsServerChanged: if (bIsServerChanged) +0x21E0 = 1
//  Both are bits of +0x94C on the weapon (SDK: bIsEquipped bit0,
//  bIsServerChanged bit1, both CPF_Net + RepNotify). The owning client equips
//  the weapon itself, so when the host's bIsEquipped=1 arrives it is no change
//  and OnRep_IsEquipped does not run; the flag stays at its constructor value 0
//  and every R press dies in CanReload -- no packet, nothing in any log. On the
//  retail server the Blueprint sets bIsServerChanged (reflection setter
//  0x23CCF80 has no native caller), which is the second way to get +0x21E0 = 1.
//  Our host never sets it. GetMagazineInBackPack (0x1F30E10) confirms the
//  role of the bit from the other side: the client only sends
//  ServerBackPackInTotalAmmoCount when bIsServerChanged is set -- which is why
//  the host's +0xE50 has been 0 in every round.
//  v157 does what the retail BP does: for every remote player's held weapon
//  with bIsEquipped set, set bIsServerChanged; when the weapon is put away,
//  clear it again so the next equip is a fresh 0->1 for the RepNotify.
//  Switch off: -noservchanged.
// ===========================================================================
static bool g_servChanged = true;
static volatile LONG g_scSet = 0, g_scCleared = 0, g_scPulsed = 0;
static void* g_scLast[24];
// v163: the one lever that is proven to work here. v157 showed that the
// client only lets a reload through after OnRep_IsServerChanged has run on
// it (that is what put +0x21E0 to 1 and made reloading possible at all).
// A RepNotify only fires on a CHANGE, so it ran once, when the weapon was
// taken into the hand. When the magazine runs dry the client needs the same
// permission again -- so the bit is pulsed 1 -> 0 -> 1 the moment the
// magazine reaches zero, which makes the notify run a second time. Nothing
// else is touched, and it is the same single bit that already works.
static int  g_epPhase[24];       // v165: 1 = the empty-magazine pulse has the bit down right now
static bool g_scPulse = false;   // v164: v163's pulse is provably a no-op, see below -- on with -scpulse
static int  g_scLastMag[24];
static int  g_scPhase[24];
static void ServerChangedTick() {
    if (!g_servChanged) return;
    for (int i = 0; i < g_remoteCount && i < 24; ++i) {
        void* w = g_remote[i].weapon;
        void* prev = g_scLast[i];
        if (prev && prev != w && MaybeAlive(prev)) {           // put away: clear bit1
            uint8_t f = 0;
            if (SafeCopy((uint8_t*)prev + 0x94C, &f, 1) && (f & 2)) {
                f &= ~2; SafeWriteU8((uint8_t*)prev + 0x94C, f);
                LONG n = InterlockedIncrement(&g_scCleared);
                if (n <= 40) L("[sc] bIsServerChanged geloescht auf abgelegter Waffe %p (Spieler %d) -- #%ld", prev, i, n);
            }
        }
        if (g_scLast[i] != w) { g_scLastMag[i] = -1; g_scPhase[i] = 0; }
        g_scLast[i] = w;
        if (!w || !MaybeAlive(w)) continue;
        uint8_t f = 0;
        if (!SafeCopy((uint8_t*)w + 0x94C, &f, 1)) continue;

        // --- v163: pulse the permission again when the magazine runs dry ---
        const int32_t cap0 = SafeI32((uint8_t*)w + RVA::Weapon_MagazineCapacity);
        const int32_t mag0 = SafeI32((uint8_t*)w + RVA::Weapon_Magazine);
        if (g_scPulse && cap0 > 0 && cap0 <= 500) {
            if (g_scPhase[i] == 1) {                    // second tick: set it again
                if (SafeWriteU8((uint8_t*)w + 0x94C, (uint8_t)(f | 2))) {
                    LONG n = InterlockedIncrement(&g_scPulsed);
                    if (n <= 40)
                        L("[sc] Magazin leer -> bIsServerChanged neu gesetzt (Waffe %p, Spieler %d,"
                          " +0x94C=%02X): der Client bekommt OnRep_IsServerChanged noch einmal und"
                          " damit die Nachlade-Freigabe zurueck -- #%ld", w, i, f | 2, n);
                }
                g_scPhase[i] = 0;
                g_scLastMag[i] = mag0;
                continue;
            }
            if (mag0 == 0 && g_scLastMag[i] > 0 && (f & 2)) {   // just ran dry
                if (SafeWriteU8((uint8_t*)w + 0x94C, (uint8_t)(f & ~2))) {
                    g_scPhase[i] = 1;                   // put it back on the next tick
                    g_scLastMag[i] = mag0;
                    continue;                           // do not let the setter below undo it
                }
            }
            g_scLastMag[i] = mag0;
        }
        if (g_epPhase[i] == 1) continue;                       // v165: a pulse has the bit down on purpose
        if ((f & 1) && !(f & 2)) {                             // equipped on the host, not yet confirmed
            f |= 2;
            if (SafeWriteU8((uint8_t*)w + 0x94C, f)) {
                LONG n = InterlockedIncrement(&g_scSet);
                if (n <= 40) {
                    wchar_t wn[128] = L"?"; ClassNameOf(w, wn, 128);
                    L("[sc] bIsServerChanged GESETZT auf %ls %p (Spieler %d, +0x94C=%02X) -> Client: OnRep_IsServerChanged, Nachladen frei, Rucksackmunition wird gemeldet -- #%ld", wn, w, i, f, n);
                }
            }
        }
    }
}

// ===========================================================================
//  v160 -- THE EMPTY MAGAZINE: the chambering action nobody finishes
//
//  Alen, 18.09.: with rounds left R reloads; shot down to zero, R does
//  nothing at all until the weapon is switched. The v159 host log settles
//  what changes:
//      t=512796  ServerStartReload | Magazin 0  (weapon just picked up)  -> works
//      t=516625  ServerStartReload | Magazin 23 (partly empty)           -> works
//      t=524765  Magazin 0 by FIRING  -> no reload request ever again,
//                only a lone ServerFire every ~2 s (the client still tries
//                to shoot, so its weapon is not frozen -- it is refusing)
//  So it is not the magazine VALUE (zero from a pickup reloads fine); it is
//  what the last shot leaves behind. The client's CanReload (0x1EF2F80) ends
//  with, among other terms,
//      CurrentState(+0x2249) < EWS_Reloading(2)
//      && !(WeaponInfo.bIsBoltAction && pawn->bWantsToFire)
//  and the last round out of the magazine leaves the weapon needing a
//  chambering action: EWS_BoltAction(3) is NOT < 2, so CanReload is false
//  for good. Two replicated fields drive that on the host side:
//      +0x1FDC bit0  bPendingBoltAction   (CPF_Net + RepNotify OnRep_BoltAction)
//      +0x217A bit1  bIsNeedBoltAction    (CPF_Net)
//  The host's fire path (0x1F16D10) sets them after a shot; they are cleared
//  again by the bolt-action completion (0x1EF6500 / 0x1F186AC), which on this
//  listen server does not run for a remote player's weapon -- with rounds
//  left the NEXT shot papers over it, the last shot does not, and the flags
//  stay set until the weapon is switched. That is exactly Alen's workaround.
//
//  v160 does two things for the held weapon of every remote player:
//   1. [wz] logs every change of the replicated state fields, so the next
//      round shows the flags in black and white instead of by inference.
//   2. [bx] once the magazine is empty, the chambering flags are released:
//      bPendingBoltAction 1 -> 0 (a real change, so the client's
//      OnRep_BoltAction runs and ends its bolt action), bIsNeedBoltAction
//      cleared, and MulticastStopSimulatingBoltAction(false) sent once, which
//      is the game's own way of telling everyone the action is over.
//      Only while Magazine == 0 -- with rounds left the game's own bolt
//      action must keep running, and it does.
//  Switches: -noboltfix (fix off), -nowzlog (instrument off).
// ===========================================================================
// v162: OFF again. v161 wrote this every tick and reloading stopped working
// even with rounds left -- so the client's reload path needs that bit, the
// opposite of what v161 assumed. No more writes to it until a measurement on
// the CLIENT says what the reload actually waits for (-boltfix re-enables).
// ---- v162: client-side measurement (see the -spclient block in Main) -----
static bool g_clientMode = false;
typedef bool (__fastcall* tCanReload)(void* weapon);
typedef void (__fastcall* tDoReloadC)(void* character);
static tCanReload  g_origCanReload = nullptr;
static tDoReloadC  g_origDoReloadC = nullptr;
static volatile LONG g_cxLines = 0;

static void __fastcall MyDoReloadC(void* ch) {
    if (InterlockedIncrement(&g_cxLines) <= 400)
        L("[cx] --- DoReload (R gedrueckt) fuer Character %p ---", ch);
    if (g_origDoReloadC) g_origDoReloadC(ch);
}

static bool __fastcall MyCanReload(void* w) {
    const bool r = g_origCanReload ? g_origCanReload(w) : false;
    // one line per change, so a whole round stays readable
    static void* lastW = nullptr; static int lastSig = -1; static bool lastR = false;
    int32_t mag = SafeI32((uint8_t*)w + 0x0DD0);
    int32_t cap = SafeI32((uint8_t*)w + 0x1A28);
    int32_t bp  = SafeI32((uint8_t*)w + 0x0E50);
    uint8_t st=0, bolt=0, ff=0, rdy=0, rel=0, ok=0, eq=0, isBolt=0, warm=0;
    SafeCopy((uint8_t*)w + 0x2249, &st,   1);   // CurrentState
    SafeCopy((uint8_t*)w + 0x1FDC, &bolt, 1);   // bPendingBoltAction  bit0
    SafeCopy((uint8_t*)w + 0x217A, &ff,   1);   // bit0 bIsNeedBoltAction, bit1 bIsFiring
    SafeCopy((uint8_t*)w + 0x2248, &rdy,  1);   // bReady
    SafeCopy((uint8_t*)w + 0x24EC, &rel,  1);   // bPendingReload
    SafeCopy((uint8_t*)w + 0x2350, &warm, 1);   // bWarmingUp
    SafeCopy((uint8_t*)w + 0x21E0, &ok,   1);   // the server-confirmation byte
    SafeCopy((uint8_t*)w + 0x094C, &eq,   1);   // bIsEquipped | bIsServerChanged
    SafeCopy((uint8_t*)w + 0x17C8, &isBolt, 1); // WeaponInfo.bIsBoltAction
    void*   pawn  = SafePtr((uint8_t*)w + 0x2B0);
    uint8_t wants = 0;
    if (pawn) SafeCopy((uint8_t*)pawn + 0x31C8, &wants, 1);   // bWantsToFire
    const int sig = (int)((mag & 0x3FF) | (st << 10) | ((bolt & 1) << 14) | ((ff & 3) << 15)
                        | ((rel & 1) << 17) | ((ok & 1) << 18) | ((wants & 1) << 19)
                        | ((rdy & 1) << 20) | ((eq & 3) << 21) | ((bp > 0 ? 1 : 0) << 23));
    if (w == lastW && sig == lastSig && r == lastR) return r;
    lastW = w; lastSig = sig; lastR = r;
    if (InterlockedIncrement(&g_cxLines) <= 400) {
        wchar_t wn[128] = L"?"; ClassNameOf(w, wn, 128);
        L("[cx] CanReload=%s | %ls %p | Magazin %d/%d Rucksack(Host) %d | Zustand=%d"
          " (0 Idle,1 Feuern,2 Nachladen,3 Repetieren,4 Ziehen) | Repetieren(+0x1FDC)=%d"
          " Repetierbedarf(+0x217A.0)=%d Feuern(+0x217A.1)=%d | bereit=%d Warmlauf=%d"
          " Nachladen-Flag=%d | Freigabe(+0x21E0)=%d | Equip(+0x94C)=%02X |"
          " Waffe-ist-Repetierer=%d Abzug-gehalten(bWantsToFire)=%d",
          r ? "JA" : "NEIN", wn, w, mag, cap, bp, st, bolt & 1, ff & 1, (ff >> 1) & 1,
          rdy & 1, warm & 1, rel & 1, ok & 1, eq, isBolt, wants);
    }
    return r;
}

static bool g_boltFix = false;
static bool g_wzLog   = true;
static volatile LONG g_bxDone = 0, g_wzLines = 0;
namespace WPO {                       // ABravoHotelRangedWeapon, SDK-verified
    constexpr uintptr_t PendingBolt   = 0x1FDC;   // bit0, OnRep_BoltAction
    constexpr uintptr_t FireFlags     = 0x217A;   // SDK bool_layout: bit0 (0x01) bIsNeedBoltAction, bit1 (0x02) bIsFiring
    constexpr uintptr_t Ready         = 0x2248;   // bit0 bReady, OnRep_Ready
    constexpr uintptr_t State         = 0x2249;   // EWeaponState
    constexpr uintptr_t WarmingUp     = 0x2350;   // bit0, OnRep_WarmingUp
    constexpr uintptr_t PendingReload = 0x24EC;   // bit0, OnRep_Reload
    constexpr uintptr_t FireMode      = 0x25F8;   // OnRep_FireMode
}
struct WzState { void* w; int mag, bp; uint8_t bolt, ff, rdy, st, warm, rel, fm, f94c, ok; bool valid; };
static WzState g_wz[24];

// The same lookup PushMagazineToClient uses, for one more function by name.
static void* FindWeaponFuncNamed(void* weapon, const wchar_t* want) {
    if (g_childOff < 0) return nullptr;            // function list not discovered yet
    void* base = weapon ? SafePtr((uint8_t*)weapon + OFF::UObject_Class) : nullptr;
    for (int depth = 0; base && depth < 12; ++depth) {
        void* f = SafePtr((uint8_t*)base + g_childOff);
        for (int i = 0; f && i < 2048; ++i) {
            wchar_t nm[128] = L"";
            if (ResolveFuncName(f, nm, 128) && wcscmp(nm, want) == 0) return f;
            f = SafePtr((uint8_t*)f + g_nextOff);
        }
        base = SafePtr((uint8_t*)base + OFF::UStruct_SuperStruct);
    }
    return nullptr;
}
// Multicast, so every client hears it -- same call shape as the magazine push.
static void StopSimulatingBoltAction(void* weapon) {
    // The function list is discovered by the magazine push; until that has
    // happened there is nothing to walk, so keep trying instead of giving up
    // once (the empty magazine can come before the first reload).
    static void* fn = nullptr; static bool tried = false;
    if (!fn && !tried && g_childOff >= 0) {
        tried = true;
        fn = FindWeaponFuncNamed(weapon, L"MulticastStopSimulatingBoltAction");
        L("[bx] MulticastStopSimulatingBoltAction %s", fn ? "gefunden" : "NICHT in der Funktionsliste -- es bleibt beim Loesen der Flags");
    }
    if (!fn || !weapon) return;
    static DWORD lastCall = 0;                 // never more than twice a second
    const DWORD now = GetTickCount();
    if (now - lastCall < 500) return;
    lastCall = now;
    void** vt = (void**)SafePtr(weapon);
    void* pe = vt ? SafePtr((uint8_t*)vt + RVA::VT_ProcessEvent) : nullptr;
    if (!pe || !InModule(pe)) return;
    struct { uint8_t bSkipOwner; } params; params.bSkipOwner = 0;
    const bool win = g_maskActive && g_drv && g_world;
    if (win) MaskOff();
    __try { ((tProcessEvent)pe)(weapon, fn, &params); }
    __except (EXCEPTION_EXECUTE_HANDLER) { L("[bx] ProcessEvent(MulticastStopSimulatingBoltAction) hat geworfen"); }
    if (win) MaskOn();
}

static void BoltFixTick() {
    for (int i = 0; i < g_remoteCount && i < 24; ++i) {
        void* w = g_remote[i].weapon;
        if (!w || !MaybeAlive(w)) { g_wz[i].valid = false; continue; }
        const int32_t cap = SafeI32((uint8_t*)w + RVA::Weapon_MagazineCapacity);
        if (cap <= 0 || cap > 500) { g_wz[i].valid = false; continue; }   // grenade / melee / sensor
        WzState c{}; c.w = w; c.valid = true;
        c.mag = SafeI32((uint8_t*)w + RVA::Weapon_Magazine);
        c.bp  = SafeI32((uint8_t*)w + RVA::Weapon_BackPackAmmo);
        SafeCopy((uint8_t*)w + WPO::PendingBolt,   &c.bolt, 1);
        SafeCopy((uint8_t*)w + WPO::FireFlags,     &c.ff,   1);
        SafeCopy((uint8_t*)w + WPO::Ready,         &c.rdy,  1);
        SafeCopy((uint8_t*)w + WPO::State,         &c.st,   1);
        SafeCopy((uint8_t*)w + WPO::WarmingUp,     &c.warm, 1);
        SafeCopy((uint8_t*)w + WPO::PendingReload, &c.rel,  1);
        SafeCopy((uint8_t*)w + WPO::FireMode,      &c.fm,   1);
        SafeCopy((uint8_t*)w + 0x94C,              &c.f94c, 1);
        SafeCopy((uint8_t*)w + 0x21E0,             &c.ok,   1);

        WzState& p = g_wz[i];
        const bool changed = !p.valid || p.w != w || p.mag != c.mag || p.bp != c.bp
                          || p.bolt != c.bolt || p.ff != c.ff || p.rdy != c.rdy
                          || p.st != c.st || p.warm != c.warm || p.rel != c.rel
                          || p.fm != c.fm || p.f94c != c.f94c || p.ok != c.ok;
        if (g_wzLog && changed && InterlockedIncrement(&g_wzLines) <= 400)
            L("[wz] Spieler %d Waffe %p | Magazin %d/%d Rucksack %d | Zustand(+0x2249)=%d"
              " Nachladen(+0x24EC)=%d Repetieren(+0x1FDC)=%d Repetierbedarf/Feuern(+0x217A)=%02X"
              " bereit(+0x2248)=%d Warmlauf(+0x2350)=%d Feuermodus(+0x25F8)=%d"
              " Equip-Flags(+0x94C)=%02X Freigabe(+0x21E0)=%d",
              i, w, c.mag, cap, c.bp, c.st, c.rel & 1, c.bolt & 1, c.ff,
              c.rdy & 1, c.warm & 1, c.fm, c.f94c, c.ok);
        g_wz[i] = c;

        // --- the fix, corrected in v161 -----------------------------------
        //  The v160 round settled it. On the HOST nothing is ever pending:
        //      [wz] ... Magazin 0/10 ... Zustand=0 Nachladen=0 Repetieren=0
        //               Repetierbedarf/Feuern(+0x217A)=01
        //  -- state Idle, no reload, no pending bolt action, and [bx] never
        //  fired because there was nothing to clear. But +0x217A stands at
        //  0x01 from the first line of the round to the last, with a full
        //  magazine as well as an empty one. The bit masks (SDK) are
        //      +0x217A bit0 = bIsNeedBoltAction (CPF_Net)
        //      +0x217A bit1 = bIsFiring         (CPF_Net + RepNotify)
        //  so bit0 -- "this weapon still has to be chambered" -- is stuck on.
        //  The weapon's constructor (0x1EEF6B0 @ 0x1EEFA98) sets it, and the
        //  three places that clear it (0x1EF6526, 0x1F090BB, 0x1F104C2) never
        //  run for a remote player's weapon here, so the host keeps
        //  replicating "needs chambering" forever. The client chambers
        //  locally, the host's 1 overwrites it again, and the moment the
        //  magazine is empty the client has nothing left to chamber with:
        //  it sits in EWS_BoltAction, and CanReload demands
        //  CurrentState < EWS_Reloading -- R does nothing until the weapon is
        //  switched. v160 cleared the wrong bit (0x02 = bIsFiring), which was
        //  never set; v161 clears bit 0x01, always, because the host does not
        //  manage that flag at all -- the client's own chambering logic does.
        if (!g_boltFix) continue;
        bool released = false;
        if (c.ff & 1) {                                   // bIsNeedBoltAction
            if (SafeWriteU8((uint8_t*)w + WPO::FireFlags, (uint8_t)(c.ff & ~1))) {
                released = true; g_wz[i].ff = (uint8_t)(c.ff & ~1);
            }
        }
        if (c.bolt & 1) {                                 // bPendingBoltAction
            if (SafeWriteU8((uint8_t*)w + WPO::PendingBolt, (uint8_t)(c.bolt & ~1))) {
                released = true; g_wz[i].bolt = (uint8_t)(c.bolt & ~1);
            }
        }
        if (!released) continue;
        // With an empty magazine the client may already be sitting in the
        // chambering state; this is the game's own "the action is over".
        if (c.mag == 0) StopSimulatingBoltAction(w);
        LONG n = InterlockedIncrement(&g_bxDone);
        if (n <= 40) {
            wchar_t wn[128] = L"?"; ClassNameOf(w, wn, 128);
            L("[bx] %ls %p (Spieler %d, Magazin %d): Repetierbedarf geloest"
              " (+0x217A %02X -> %02X, +0x1FDC %02X -> %02X)%s -- der Client"
              " bekommt jetzt 'nichts mehr zu repetieren' und kommt aus"
              " EWS_BoltAction heraus -- #%ld",
              wn, w, i, c.mag, c.ff, c.ff & ~1, c.bolt, c.bolt & ~1,
              c.mag == 0 ? " + MulticastStopSimulatingBoltAction" : "", n);
        }
    }
}

// ===========================================================================
//  v164 -- THE UNFINISHED CHAMBERING ACTION, FINISHED THE WAY A SERVER DOES
//
//  Measured in the client binary, no guesswork left:
//
//  ABravoHotelRangedWeapon::CanReload (0x1EF2F80) returns true only if ALL of
//  these hold -- this is the complete list, read off the function:
//      * the weapon is the pawn's current weapon
//      * the pawn passes its own check (0x1FCF230)
//      * Magazine < MagazineCapacity
//      * !(WeaponInfo.bIsBoltAction (+0x17C8) && pawn->bWantsToFire (+0x31C8))
//      * CurrentState (+0x2249) < EWS_Reloading (2)            <-- the blocker
//      * on a client only, additionally: ammo in the backpack > 0
//        and weapon+0x21E0 != 0
//
//  UpdateWeaponState (0x1EF5FE0) is what computes CurrentState, and it is a
//  pure function of the flags:
//      bPendingEquip  -> EWS_Equipping (4)
//      bPendingReload -> EWS_Reloading (2)
//      bPendingBoltAction (+0x1FDC) -> EWS_BoltAction (3)
//  So CurrentState is 3 -- and CanReload is false -- for exactly as long as
//  bPendingBoltAction is set. That field is REPLICATED with a RepNotify
//  (OnRep_BoltAction), which means the host owns it: whatever the host has,
//  the client gets, and the client recomputes its state from it.
//
//  On a retail dedicated server the client sends ServerStartBoltAction, the
//  server sets the flag, runs its bolt timer and clears it again, and the
//  clearing is what lets the client leave EWS_BoltAction. Our host has no such
//  timer -- the flag goes up on the last shot and stays up, so the client sits
//  in EWS_BoltAction and every R press dies inside CanReload without ever
//  sending a packet. Exactly the reported symptom: rounds left -> reload fine,
//  fired to empty -> nothing happens until the weapon is switched (an equip
//  recomputes the state).
//
//  v164 supplies the missing timer and nothing else: when the flag has been up
//  for longer than the bolt time it is cleared ONCE. This is the opposite of
//  v161, which wiped the flag continuously and therefore also killed the
//  chambering action that was legitimately running -- that is why reloading
//  broke completely there. bIsNeedBoltAction (+0x217A bit0) is not touched at
//  all; CanReload never reads it.
//
//  bPendingReload (+0x24EC) gets the same treatment with a much longer timeout,
//  purely as a safety net: it forces CurrentState to 2, which is also >= 2.
//
//  Off with -noboltfinish / -noreloadfinish, bolt time with -boltholdms=N.
// ===========================================================================
static bool g_boltFinish   = true;     // -noboltfinish
static bool g_reloadFinish = true;     // -noreloadfinish
static int  g_boltHoldMs   = 600;      // -boltholdms=N
static int  g_reloadHoldMs = 6000;
static unsigned long long g_bfSince[24];   // when bPendingBoltAction went up
static unsigned long long g_rfSince[24];   // when bPendingReload went up
static void*              g_bfLast[24];
static volatile LONG g_bfDone = 0, g_rfDone = 0;

static void BoltFinishTick() {
    if (!g_boltFinish && !g_reloadFinish) return;
    const unsigned long long now = GetTickCount64();
    for (int i = 0; i < g_remoteCount && i < 24; ++i) {
        void* w = g_remote[i].weapon;
        if (g_bfLast[i] != w) { g_bfSince[i] = 0; g_rfSince[i] = 0; g_bfLast[i] = w; }
        if (!w || !MaybeAlive(w)) continue;

        // ---- bPendingBoltAction: the chambering action nobody ends ----
        if (g_boltFinish) {
            uint8_t b = 0;
            if (SafeCopy((uint8_t*)w + WPO::PendingBolt, &b, 1)) {
                if (b & 1) {
                    if (g_bfSince[i] == 0) g_bfSince[i] = now;
                    else if (now - g_bfSince[i] >= (unsigned long long)g_boltHoldMs) {
                        if (SafeWriteU8((uint8_t*)w + WPO::PendingBolt, (uint8_t)(b & ~1))) {
                            g_bfSince[i] = 0;
                            uint8_t st = 0; SafeCopy((uint8_t*)w + WPO::State, &st, 1);
                            const int32_t mg = SafeI32((uint8_t*)w + RVA::Weapon_Magazine);
                            LONG n = InterlockedIncrement(&g_bfDone);
                            if (n <= 60)
                                L("[bf] Repetieren beendet: bPendingBoltAction nach %d ms geloescht"
                                  " (Waffe %p, Spieler %d, Magazin %d, Host-Zustand %u) -- der Client bekommt"
                                  " OnRep_BoltAction, rechnet seinen Waffenzustand neu und faellt aus"
                                  " EWS_BoltAction heraus; damit laesst CanReload das Nachladen wieder zu -- #%ld",
                                  g_boltHoldMs, w, i, (int)mg, (unsigned)st, n);
                        }
                    }
                } else {
                    g_bfSince[i] = 0;
                }
            }
        }

        // ---- bPendingReload: safety net, same idea, long timeout ----
        if (g_reloadFinish) {
            uint8_t r = 0;
            if (SafeCopy((uint8_t*)w + WPO::PendingReload, &r, 1)) {
                if (r & 1) {
                    if (g_rfSince[i] == 0) g_rfSince[i] = now;
                    else if (now - g_rfSince[i] >= (unsigned long long)g_reloadHoldMs) {
                        if (SafeWriteU8((uint8_t*)w + WPO::PendingReload, (uint8_t)(r & ~1))) {
                            g_rfSince[i] = 0;
                            LONG n = InterlockedIncrement(&g_rfDone);
                            if (n <= 40)
                                L("[bf] bPendingReload hing seit %d ms und wurde geloescht"
                                  " (Waffe %p, Spieler %d) -- sonst bleibt der Client in EWS_Reloading"
                                  " stehen und CanReload sperrt ebenfalls -- #%ld",
                                  g_reloadHoldMs, w, i, n);
                        }
                    }
                } else {
                    g_rfSince[i] = 0;
                }
            }
        }
    }
}

// ===========================================================================
//  v165 -- THE RELOAD PERMISSION IS CLEARED BY THE CLIENT, AND ONLY THE HOST
//          CAN HAND IT BACK
//
//  The v164 round settled two things at once.
//
//  1. v164 could never have worked. The host log shows bPendingBoltAction
//     (+0x1FDC) at 0 for the whole round -- the host never sets it, so there
//     was nothing to clear, and [bf] never fired once. Worse, writing 0 over a
//     0 is not a change, so nothing would have gone out on the wire anyway.
//
//  2. The log contains the decisive comparison. The SAME weapon, picked up
//     with an empty magazine, reloads: [sc] sets bIsServerChanged, the client
//     immediately reports its backpack ammo and ServerStartReload arrives.
//     Later, after the player fires that same weapon down to 0, every
//     host-visible field is identical -- Magazin 0/30, Zustand 0, Nachladen 0,
//     Repetieren 0, +0x217A 01 -- and no reload request arrives at all. So the
//     difference is not in any replicated field. It is something firing leaves
//     behind on the CLIENT.
//
//  Which is exactly what weapon+0x21E0 is. It is a native member, not a
//  UProperty, so it never replicates, and CanReload demands it on clients.
//  Three places write it:
//      0x1F23691  writes +0x1EE4 = b and +0x21E0 = !b   (a paired setter)
//      0x1F29E10  the weapon's own tick: when its blend timer runs out it
//                 clears BOTH +0x1EE4 and +0x21E0 to 0
//      0x1F073E0  OnRep_IsEquipped        -> +0x21E0 = 1
//      0x1F08E80  OnRep_IsServerChanged   -> +0x21E0 = 1
//  My v164 note claimed nothing ever clears +0x21E0. That was wrong: the
//  weapon clears it itself. Which means the permission is not granted once and
//  kept -- it is granted by an OnRep and taken away again by the weapon, and
//  after the magazine runs dry nothing on the client hands it back. Equipping
//  another weapon does, through OnRep_IsEquipped -- the one workaround that
//  always worked.
//
//  Of the two setters, exactly one belongs to the host: bIsServerChanged
//  (+0x94C bit1, Net + RepNotify). A RepNotify only runs on a CHANGE, so the
//  bit has to go 1 -> 0 -> 1 with a replication pass in between; holding it at
//  1 does nothing. v165 does that for as long as a remote player's magazine
//  is empty, roughly once per second, so the client gets the permission back
//  within a second of running dry no matter what its own tick did to it.
//
//  Cheap enough to leave running: one replicated bool per second per empty
//  weapon. Off with -noemptypulse, interval with -emptypulsems=N.
// ===========================================================================
static bool g_emptyPulse    = true;    // -noemptypulse
static bool g_pulseOnlyEmpty = false;  // -pulseonlyempty (v165 behaviour)
static int  g_emptyPulseMs = 900;     // -emptypulsems=N
static unsigned long long g_epNext[24];    // next pulse due
static void* g_epLast[24];
static volatile LONG g_epDone = 0;

static void EmptyPulseTick() {
    if (!g_emptyPulse) return;
    const unsigned long long now = GetTickCount64();
    for (int i = 0; i < g_remoteCount && i < 24; ++i) {
        void* w = g_remote[i].weapon;
        if (g_epLast[i] != w) { g_epLast[i] = w; g_epPhase[i] = 0; g_epNext[i] = 0; }
        if (!w || !MaybeAlive(w)) continue;

        uint8_t f = 0;
        if (!SafeCopy((uint8_t*)w + 0x94C, &f, 1)) continue;
        if (!(f & 1)) { g_epPhase[i] = 0; continue; }     // not equipped on the host

        // second half of the pulse: put the bit back, which is the 0 -> 1 the
        // RepNotify needs. Always finish this, even if the magazine refilled
        // in the meantime -- leaving the bit down would cost the next reload.
        if (g_epPhase[i] == 1) {
            if (SafeWriteU8((uint8_t*)w + 0x94C, (uint8_t)(f | 2))) {
                g_epPhase[i] = 0;
                g_epNext[i]  = now + (unsigned long long)g_emptyPulseMs;
                LONG n = InterlockedIncrement(&g_epDone);
                if (n <= 60) {
                    const int32_t mg = SafeI32((uint8_t*)w + RVA::Weapon_Magazine);
                    L("[ep] bIsServerChanged 0 -> 1 gepulst (Waffe %p, Spieler %d, Magazin %d):"
                      " der Client laeuft OnRep_IsServerChanged und setzt seine Nachlade-Freigabe"
                      " +0x21E0 wieder auf 1, die sein eigener Waffen-Tick geloescht hat -- #%ld",
                      w, i, (int)mg, n);
                }
            }
            continue;
        }

        const int32_t cap = SafeI32((uint8_t*)w + RVA::Weapon_MagazineCapacity);
        const int32_t mag = SafeI32((uint8_t*)w + RVA::Weapon_Magazine);
        if (cap <= 0 || cap > 500) continue;
        // v166: the permission goes stale for other reasons than an empty
        // magazine -- putting the weapon away and drawing it again does it too,
        // and the host does not see that switch at all when the weapon pointer
        // it tracks never changes (fists are not a weapon). So the pulse no
        // longer waits for mag == 0; it simply keeps the permission fresh for
        // as long as the weapon is in the hand. -pulseonlyempty restores the
        // v165 behaviour.
        if (g_pulseOnlyEmpty && mag > 0) { g_epNext[i] = 0; continue; }
        if (g_epNext[i] && now < g_epNext[i]) continue;

        // first half: take the bit down so the next tick is a real change
        if (f & 2) {
            if (SafeWriteU8((uint8_t*)w + 0x94C, (uint8_t)(f & ~2)))
                g_epPhase[i] = 1;
        } else {
            g_epPhase[i] = 1;                              // already down, just set it
        }
    }
}



static void WeaponRelevance() {
    void* w = g_watchWeapon;
    if (!w || g_remoteCount == 0) return;
    void* pawn = g_remote[0].pawn;

    MakeHeldWeaponAlwaysRelevant(w);   // v78

    // [mag] -- v60: the fields the reflection table names, not the ones that
    // looked right. The old pair rides along for one more round so the log
    // shows how the two diverge, and so the object pointer printed here can be
    // compared with the one [wp] prints: v59b's 0x800 window saw nothing move
    // while firing and [mag] saw +0x1B0 move on the same action, which means
    // the two watchers were not looking at the same object.
    // v112: every player's weapon, each with its own last-seen values. Before
    // this, only g_remote[0] was watched and pushed, which is exactly why the
    // magazine bug came back the moment more than two people played.
    for (int ri = 0; ri < g_remoteCount; ++ri) {
        void* rw = g_remote[ri].weapon;
        if (!rw || !MaybeAlive(rw)) continue;
        int32_t mag = SafeI32((uint8_t*)rw + RVA::Weapon_Magazine);
        int32_t bp  = SafeI32((uint8_t*)rw + RVA::Weapon_BackPackAmmo);
        int32_t cap = SafeI32((uint8_t*)rw + RVA::Weapon_MagazineCapacity);
        int32_t a = SafeI32((uint8_t*)rw + 0x1B0), b = SafeI32((uint8_t*)rw + 0x1B4);
        if (mag == g_remote[ri].lastMag && bp == g_remote[ri].lastBp
         && a == g_remote[ri].lastA && b == g_remote[ri].lastB) continue;
        static int mags = 0;
        if (++mags <= 300)
            L("[mag] t=%lu ms  Spieler %d, Waffe %p: Magazin(+0xDD0) = %d  Rucksack(+0xE50) = %d  Kapazitaet(+0x1A28) = %d   |   alt: +0x1B0 = %d  +0x1B4 = %d",
              (unsigned long)GetTickCount(), ri, rw, mag, bp, cap, a, b);
        // v159: push only INCREASES (a reload the host executed). A decrease is
        // the client's own shot, already applied locally; pushing it back with
        // network latency re-inflated the shotgun's magazine (host 1 -> client
        // had 0 -> push "1" -> client fires into an empty host magazine and
        // waits for a bolt action that never comes -> R dead until a switch).
        if (mag > g_remote[ri].lastMag && g_remote[ri].lastMag >= 0) {
            PushMagazineToClient(rw, mag);
            if (ri == 0) InterlockedExchange(&g_magDirty, 1);   // v73 path stays on remote 0
        }
        g_remote[ri].lastMag = mag; g_remote[ri].lastBp = bp;
        g_remote[ri].lastA   = a;   g_remote[ri].lastB  = b;
    }

    // v59d, -magtest: one write, once per round, and it answers the only
    // question left. If the client's HUD jumps to 99 within a second, then
    // +0x1B0 IS replicated, the channel works, and what is late is the host's
    // own value. If the HUD does not move, the client's display never comes
    // from this field and the ammo travels some other way -- and then the
    // inventory item (ClientModifyInventoryItem, four times a round) is the
    // only remaining candidate. This is gameplay state, not an engine state
    // machine, and it is off unless the switch is given.
    // v112: "mag" used to be a single local from the old one-weapon watcher.
    // The watcher is a per-player loop now, so read it here for remote 0.
    if (g_magTest && !g_magTestDone && w) {
        const int32_t magNow = SafeI32((uint8_t*)w + RVA::Weapon_Magazine);
        if (magNow > 0 && magNow < 90) {
            const int32_t marker = 99;
            if (SafeCopy(&marker, (uint8_t*)w + RVA::Weapon_Magazine, 4)) {
                g_magTestDone = true;
                L("[mag] *** TEST: Waffe %p Magazin(+0xDD0) = %d -> 99 geschrieben. Das ist diesmal die WIRKLICH replizierte Property. ***", w, magNow);
            }
        }
    }

    if (++g_relTick % 8 != 0) return;                 // ~ every two seconds
    float wl[3] = { 0, 0, 0 }, pl[3] = { 0, 0, 0 };
    bool haveW = ActorLocation(w, wl), haveP = pawn && ActorLocation(pawn, pl);
    float d = -1.0f;
    if (haveW && haveP) {
        float dx = wl[0] - pl[0], dy = wl[1] - pl[1], dz = wl[2] - pl[2];
        d = sqrtf(dx * dx + dy * dy + dz * dz);
    }
    uint8_t ar = 0, oo = 0, dorm = 0;
    SafeCopy((uint8_t*)w + OFF::AActor_bAlwaysRelevant, &ar, 1);
    SafeCopy((uint8_t*)w + OFF::AActor_bOnlyRelevantToOwner, &oo, 1);
    SafeCopy((uint8_t*)w + OFF::AActor_NetDormancy, &dorm, 1);
    void* info = GraphInfoFor(w);
    float cull = -1.0f, sq = -1.0f;
    if (info) { SafeCopy((uint8_t*)info + 0x90, &cull, 4); SafeCopy((uint8_t*)info + 0x94, &sq, 4); }
    if (g_relLogged < 60) {
        ++g_relLogged;
        L("[wl] Waffe %p bei {%.0f, %.0f, %.0f}, Spieler bei {%.0f, %.0f, %.0f} -- Abstand %.0f (%.0f m) | immer-relevant %d, nur-fuer-Besitzer %d, Dormancy %d | Graph-Info %p Cull %.0f / Sq %.0f",
          w, wl[0], wl[1], wl[2], pl[0], pl[1], pl[2], d, d / 100.0f,
          ar & 1, oo & 1, dorm, info, cull, sq);
    }
    OwnedCullToZero(w, L"Waffe des Remote-Spielers");
    for (int k = 0; k < g_ownedCount; ++k)
        if (g_ownedActors[k]) OwnedCullToZero(g_ownedActors[k], L"Eigener Actor des Remote-Spielers");
    for (int i = 0; i < g_remoteCount; ++i) {
        OwnedCullToZero(g_remote[i].pawn, L"Remote-Pawn");
        if (void* ps = SafePtr((uint8_t*)g_remote[i].controller + OFF::AController_PlayerState))
            OwnedCullToZero(ps, L"PlayerState des Remote-Spielers");
    }
}

// ---------------------------------------------------------------------------
//  v73: replicate the weapon the way the engine would, for one actor
//
//  What is established: the spatial gather returns nobody, so the weapon's
//  Magazine (weapon+0xDD0, CPF_Net|RepNotify) is never re-sent after the
//  channel opens. What is ALSO established, from the other team's notes and
//  from our own logs, is that the channel exists and carries state fine the
//  moment something uses it.
//
//  UActorChannel::ReplicateActor (0x41A79B0, one argument: the channel) is
//  exactly what the graph calls once it has gathered an actor. We already
//  remember which channel carries which of the remote player's actors, from
//  the SetChannelActor hook. So when the host's magazine value changes, ask
//  that one channel to replicate -- the engine's own function, one actor, on
//  the game thread, immediately after ServerReplicateActors has set up the
//  frame for this tick.
//
//  This does not fix the gather and it does not touch a single field. If the
//  property really is replicated and only the gather is starving it, this is
//  the whole fix. Switch: -nomagrep.
// ---------------------------------------------------------------------------
typedef bool (__fastcall* tReplicateActor)(void* channel);

static void ReplicateWeaponIfDirty() {
    if (!g_magRep) return;
    if (InterlockedExchange(&g_magDirty, 0) == 0) return;
    void* w = g_watchWeapon;
    if (!w || !MaybeAlive(w)) return;
    void* ch = OwnChanFor(w);
    if (!ch || !MaybeAlive(ch)) return;
    // The channel must still be carrying this very actor.
    if (OwnChanActor(ch) != w) return;
    bool ok = false;
    __try { ok = ((tReplicateActor)(g_base + RVA::UActorChannel_ReplicateActor))(ch); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        L("[mr] ReplicateActor auf dem Waffenkanal hat geworfen -- abgeschaltet");
        g_magRep = false;
        return;
    }
    LONG n = InterlockedIncrement(&g_magRepN);
    if (n <= 40)
        L("[mr] Magazin geaendert -> ReplicateActor(Kanal %p, Waffe %p) = %d -- #%ld",
          ch, w, ok ? 1 : 0, n);
}

// ---------------------------------------------------------------------------
//  v83: what does the host actually execute on the remote player's weapon?
//
//  The reload now works -- ClientSetMagazine reaches the client and the counter
//  fills. What is left is that the host's own Magazine never goes DOWN: it is
//  set full on a reload and stays there, so after firing, an equip refills the
//  client from a value that was never decremented.
//
//  The SDK names the client->server calls that should tell the host about it:
//      ServerFire(bool), ServerHandleFiring(), ServerFireProjectile(...)
//      ServerReloadWeapon(int32), ServerStartReload(float)
//  all (Net|NetReliableNative|Event|NetServer|NetValidate) on
//  ABravoHotelRangedWeapon. Incoming RPCs are dispatched through ProcessEvent,
//  and our [rpcname] hook only ever saw OUTGOING ones -- so we have never
//  actually looked at this side.
//
//  So: hook AActor::ProcessEvent (0x3F98AF0, 15-byte prologue) and log the
//  function name ONLY when the object is the watched weapon or a remote pawn.
//  One pointer comparison in the hot path, capped at 80 lines. Read-only.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  v86: the host hears every shot -- it just never spends the round
//
//  The v85 round finally measured the incoming side, and it is emphatic:
//      54 x ServerFire
//      19 x ServerFireProjectile      (1:1 with MulticastFireProjectile)
//      18 x ServerHandleFiring
//       2 x ServerStartReload
//       2 x ClientSetMagazine          <- our own push going out
//  Every shot reaches the host and is dispatched. And across the same round
//  the host's Magazine at weapon+0xDD0 never moved off 30.
//
//  So the host is told, and does not decrement. The decrement lives in
//  Blueprint (there is no native write to +0xDD0 anywhere in .text -- checked),
//  and that Blueprint path is evidently not running server-side here.
//
//  We can close it: count the shots ourselves. ServerFireProjectile pairs 1:1
//  with MulticastFireProjectile, so it is exactly one per bullet. On each one,
//  take the host's Magazine down by one, clamped at zero. That makes the
//  host's value CORRECT rather than forged -- it is a plain replicated int the
//  game itself writes from Blueprint, not an engine state machine -- and the
//  existing [mag] watcher then pushes the new value with ClientSetMagazine,
//  which we already know arrives.
//
//  That should close the last symptom: equip no longer refills from a value
//  that was never spent. Switch: -nomagtrack.
// ---------------------------------------------------------------------------
static bool          g_magTrack = true;             // -nomagtrack
static int           g_fireSrcKind = 0;          // v87: 1 = weapon emits ServerFireProjectile, 2 = pawn
static volatile LONG g_magSpent = 0;

static void SpendOneRound(void* weapon) {
    if (!g_magTrack || !weapon) return;
    int32_t mag = SafeI32((uint8_t*)weapon + RVA::Weapon_Magazine);
    if (mag <= 0 || mag > 500) return;
    const int32_t next = mag - 1;
    if (!SafeCopy(&next, (uint8_t*)weapon + RVA::Weapon_Magazine, 4)) return;
    LONG n = InterlockedIncrement(&g_magSpent);
    if (n <= 30)
        L("[ms] Schuss gezaehlt: Magazin %d -> %d (Waffe %p) -- #%ld", mag, next, weapon, n);
}

// ===========================================================================
//  v134 -- find where an attachment is actually handled
//
//  The test round produced ZERO attachment lines. Not one, anywhere: [wx] empty,
//  [wa] saw only K2_OnEquip / K2_OnUnEquip / UpdateMutableMesh, [pk] on the
//  controller saw only ClientChangeHUD and friends, and the whole log contains
//  the word "Attachment" exactly once -- in a list of RPC names printed at
//  startup. So mounting a sight touches NONE of the objects sp_listen watches:
//  not the pawn, not the held weapon, not a stowed weapon, not the controller.
//
//  Every branch so far starts from an OBJECT we recognise. That is the wrong way
//  round for this bug, because we do not know which object handles it. Turn it
//  around and start from the FUNCTION instead: catch anything whose name
//  contains Attachment, Sight or Scope, on any object at all, and report what
//  that object is.
//
//  ProcessEvent runs thousands of times a second, so the name is resolved once
//  per distinct UFunction and the verdict cached; after that it is a hash probe.
//  Tag [at]. Rides on -nowatchattach like the rest.
// ===========================================================================
struct FnVerdict { void* fn; uint8_t verdict; };   // 0 = unseen, 1 = interesting, 2 = boring
static FnVerdict     g_fnv[2048];
static volatile LONG g_atLogged = 0;

// v148: same cache pattern for the one RPC the gold settlement listens to.
static FnVerdict g_fnMer[256];
static uint8_t FnIsMatchEndResult(void* fn) {
    const uintptr_t h = ((uintptr_t)fn >> 4) * 2654435761u;
    for (int i = 0; i < 4; ++i) {
        FnVerdict& c = g_fnMer[(h + i) & 255];
        if (c.fn == fn) return c.verdict;
        if (!c.fn) {
            wchar_t nm[128] = L"";
            uint8_t v = 2;
            if (ResolveFuncName(fn, nm, 128) && wcsstr(nm, L"ShowMatchEndFinalResult")) v = 1;
            c.verdict = v;
            c.fn = fn;
            return v;
        }
    }
    return 2;
}
static uint8_t FnIsAttachmentish(void* fn) {
    const uintptr_t h = ((uintptr_t)fn >> 4) * 2654435761u;
    for (int i = 0; i < 4; ++i) {
        FnVerdict& c = g_fnv[(h + i) & 2047];
        if (c.fn == fn) return c.verdict;
        if (!c.fn) {
            // v135: the narrow net caught nothing twice. A round with four
            // weapon switches and sights being mounted produced ZERO hits on
            // Attachment / Sight / Scope, so the functions the game actually
            // uses are not named after the thing they do. The reflection table
            // has the generic inventory ones -- ServerMoveItem, EquipItem,
            // Request_EquipItem, AttachToWeaponBySlot, SetAttachmentSlot -- and
            // mounting a sight is, in inventory terms, moving an item into a
            // slot. So widen to Item, Slot and Inventory as well. The per-
            // function cache means this costs one hash probe after the first
            // sighting, whatever the filter.
            wchar_t n[128] = L"";
            uint8_t v = 2;
            if (ResolveFuncName(fn, n, 128)
             && (wcsstr(n, L"Attachment") || wcsstr(n, L"Sight") || wcsstr(n, L"Scope")
              || wcsstr(n, L"Item")       || wcsstr(n, L"Slot")  || wcsstr(n, L"Inventory")
              || wcsstr(n, L"Equip"))
             && !wcsstr(n, L"Tick") && !wcsstr(n, L"OnSaveInventoryData"))   // v154: 457 lines of AI noise ate the quota
                v = 1;
            c.verdict = v;
            c.fn = fn;                 // written last, so a half-filled slot reads as unseen
            return v;
        }
    }
    return 2;
}

static void __fastcall MyProcessEvent(void* obj, void* fn, void* params) {
    // v148: the round's result on its way to the client -> gold settlement
    if (g_goldGain && obj && fn && FnIsMatchEndResult(fn) == 1) {
        __try { GoldOnMatchEndResult(obj, params); } __except (EXCEPTION_EXECUTE_HANDLER) { L("[gg] Ausnahme im Ergebnis-Decoder"); }
    }
    // v134: attachment work, wherever it happens
    if (g_attachWatch && obj && fn && g_atLogged < 500 && FnIsAttachmentish(fn) == 1) {
        InterlockedIncrement(&g_atLogged);
        wchar_t an[128] = L"?", oc[128] = L"?";
        ResolveFuncName(fn, an, 128);
        ClassNameOf(obj, oc, 128);
        int cs = -1;
        __try {
            typedef int (__fastcall* tCs8)(void*, void*, void*);
            cs = ((tCs8)(g_base + RVA::GetFunctionCallspace))(obj, fn, nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) { cs = -1; }
        void* outer = SafePtr((uint8_t*)obj + 0x28);
        wchar_t ouc[128] = L"";
        if (outer && MaybeAlive(outer)) ClassNameOf(outer, ouc, 128);
        uint32_t r[4] = { 0, 0, 0, 0 };
        if (params) SafeCopy(params, r, 16);
        L("[at] t=%lu ms  %ls %p -> %ls  (Callspace %d) | Outer %ls %p"
          " | Parameter %08X %08X %08X %08X",
          (unsigned long)GetTickCount(), oc, obj, an, cs, ouc[0] ? ouc : L"-", outer,
          r[0], r[1], r[2], r[3]);
    }
    // v87: two things went wrong in v86.
    //  1. The shot accounting sat INSIDE the log budget (g_peLogged < 250).
    //     ServerDoSprinting alone burned 164 of those 250 lines in the first
    //     minute, so by the time the player fired, nothing was counted any more.
    //     Accounting now runs unconditionally; only the logging is capped.
    //  2. ServerFireProjectile may be dispatched on the PAWN rather than on the
    //     weapon. v86 only accepted obj == g_watchWeapon. We now accept either
    //     and lock onto whichever kind emits it first, so a shot is never
    //     counted twice.
    if (obj && fn) {
        // v112: any player's weapon or pawn, not just remote 0's.
        int  shooter  = RemoteOfWeapon(obj);
        bool isWeapon = (shooter >= 0) || (obj == g_watchWeapon);
        bool isPawn   = false;
        if (!isWeapon)
            for (int i = 0; i < g_remoteCount && !isPawn; ++i)
                if (obj == g_remote[i].pawn) { isPawn = true; shooter = i; }
        // v97: doors and windows too. The cheap checks above run first; only if
        // they miss do we read the class pointer and compare it against the
        // short table v96's wake path filled in.
        // v100: #16, the perk UI in the aircraft. The perk data sits on the
        // PlayerState and the RPCs go to the PlayerController, so neither the
        // pawn nor the weapon watch above ever sees them. Both are plain
        // pointer compares, so this stays cheap.
        bool isCtrl = false;
        if (!isWeapon && !isPawn && (g_pkLogged < 250 || g_pk2Logged < 150)) {
            for (int i = 0; i < g_remoteCount && !isCtrl; ++i) {
                void* c = g_remote[i].controller;
                if (!c) continue;
                if (obj == c) { isCtrl = true; break; }
                if (obj == SafePtr((uint8_t*)c + OFF::AController_PlayerState)) { isCtrl = true; break; }
            }
        }
        if (isCtrl) {
            wchar_t pn[128] = L"";
            if (ResolveFuncName(fn, pn, 128)
             && wcsncmp(pn, L"Receive", 7) != 0 && !wcsstr(pn, L"Tick")
             && !wcsstr(pn, L"ServerMove") && !wcsstr(pn, L"UpdateCamera")
             && !wcsstr(pn, L"AdjustPosition") && !wcsstr(pn, L"AckGoodMove")) {
                // v102: in v101 the join chatter (GetTargetPawn,
                // ServerUpdateLevelVisibility, spectator and possession traffic)
                // used all 250 slots in the first second, long before the perk
                // phase -- the same budget mistake for the fourth time. Two
                // separate budgets now, so the noisy bucket cannot starve the
                // one we actually care about.
                const bool perkish =
                       wcsstr(pn, L"Perk")   || wcsstr(pn, L"Spin")
                    || wcsstr(pn, L"Skill")  || wcsstr(pn, L"Talent")
                    || wcsstr(pn, L"Ability")|| wcsstr(pn, L"Aircraft")
                    || wcsstr(pn, L"Slot")   || wcsstr(pn, L"Upgrade")
                    || wcsstr(pn, L"UI")
                    // v119: the zone is (also) driven by Client RPCs --
                    // ClientSpawnBlueZone, ClientBlueZone, ClientNotifyPhase...
                    || wcsstr(pn, L"Zone")   || wcsstr(pn, L"Phase");
                const bool joinNoise =
                       wcsstr(pn, L"TargetPawn")     || wcsstr(pn, L"Spectat")
                    || wcsstr(pn, L"LevelVisibility")|| wcsstr(pn, L"WCLevels")
                    || wcsstr(pn, L"ViewTarget")     || wcsstr(pn, L"SetRotation")
                    || wcsstr(pn, L"Possession")     || wcsstr(pn, L"Restart")
                    || wcsstr(pn, L"JoinVoice");
                if (!perkish) {
                    // general bucket: RPCs only, and never the join noise
                    if (joinNoise) { if (g_origProcessEvent) g_origProcessEvent(obj, fn, params); return; }
                    if (wcsncmp(pn, L"Client", 6) != 0 && wcsncmp(pn, L"Multicast", 9) != 0) {
                        if (g_origProcessEvent) g_origProcessEvent(obj, fn, params); return;
                    }
                    if (g_pk2Logged >= 150) { if (g_origProcessEvent) g_origProcessEvent(obj, fn, params); return; }
                    InterlockedIncrement(&g_pk2Logged);
                } else {
                    InterlockedIncrement(&g_pkLogged);
                }
                int cs = -1;
                __try {
                    typedef int (__fastcall* tCs2)(void*, void*, void*);
                    cs = ((tCs2)(g_base + RVA::GetFunctionCallspace))(obj, fn, nullptr);
                } __except (EXCEPTION_EXECUTE_HANDLER) { cs = -1; }
                L("[pk] t=%lu ms  %p -> %ls  (Callspace %d)",
                  (unsigned long)GetTickCount(), obj, pn, cs);
            }
        }

        // --- v131: ATTACHMENTS ON A WEAPON THAT IS NOT IN YOUR HAND --------
        // Alen: "the attachment goes on the weapon but not into the UI and the
        // slot, as long as I am not holding it. It works when I hold it."
        // So this is a SEPARATE bug from the invisible weapon -- and the reason
        // there is not one line about it in any log is that the instrument
        // cannot see it. Every weapon branch so far tests obj against
        // g_remote[i].weapon, which is only the weapon CURRENTLY IN HAND. A
        // stowed weapon is never any of those, so an attachment equipped onto it
        // produces nothing at all in the log.
        // The remote player's other weapons are already tracked -- the level
        // walk collects them into the owned set ([ow] lists them by class). One
        // hash probe rejects everything else, so this stays cheap.
        if (g_attachWatch && !isWeapon && !isPawn && !isCtrl && g_waLogged < 200
         && OwnedSetMayHave(obj) && OwnedByRemote(obj)) {
            wchar_t an[128] = L"";
            if (ResolveFuncName(fn, an, 128)
             && (wcsstr(an, L"Attach") || wcsstr(an, L"Equip")
              || wcsstr(an, L"Slot")   || wcsstr(an, L"Sight")
              || wcsstr(an, L"OnRep")  || wcsstr(an, L"Mesh")
              || wcsstr(an, L"Socket"))) {
                InterlockedIncrement(&g_waLogged);
                int cs = -1;
                __try {
                    typedef int (__fastcall* tCs7)(void*, void*, void*);
                    cs = ((tCs7)(g_base + RVA::GetFunctionCallspace))(obj, fn, nullptr);
                } __except (EXCEPTION_EXECUTE_HANDLER) { cs = -1; }
                wchar_t oc[128] = L"?";
                ClassNameOf(obj, oc, 128);
                // v155 (SDK dump): AttachmentIndices +0x190 is a property of
                // UWeaponReplicatedComponent = weapon+0xC38 (WeaponReplicatedComponent;
                // the same object is also reachable as EquippableActor+0x310
                // ReplicatedComponent). v154 read the WeaponAttachmentComponent
                // (+0x1E00), which holds the slot objects, not the index list.
                void*   attComp = SafePtr((uint8_t*)obj + 0xC38);
                int32_t attNum  = attComp ? SafeI32((uint8_t*)attComp + 0x198) : -1;
                uint32_t r0 = 0, r1 = 0;
                if (params) { SafeCopy((uint8_t*)params, &r0, 4); SafeCopy((uint8_t*)params + 4, &r1, 4); }
                L("[wx] t=%lu ms  NICHT in der Hand: %ls %p -> %ls  (Callspace %d)"
                  " | AttachmentIndices Num=%d | roh %08X %08X",
                  (unsigned long)GetTickCount(), oc, obj, an, cs, attNum, r0, r1);
            }
        }

        // v121: a COMPONENT of the remote pawn (the state machine and the ladder
        // component both live there, not on the pawn itself). One pointer read
        // to reject everything else: UObject::Outer is +0x28.
        if (!isWeapon && !isPawn && !isCtrl && g_csLogged < 400 && g_remoteCount > 0) {
            void* outer = SafePtr((uint8_t*)obj + 0x28);
            bool mine = false;
            for (int i = 0; i < g_remoteCount && !mine; ++i)
                if (outer && outer == g_remote[i].pawn) mine = true;
            if (mine) {
                wchar_t sn[128] = L"";
                if (ResolveFuncName(fn, sn, 128)
                 && (wcsstr(sn, L"CurrentState") || wcsstr(sn, L"CharacterState")
                  || wcsstr(sn, L"ChangeState")  || wcsstr(sn, L"Ladder"))) {
                    InterlockedIncrement(&g_csLogged);
                    int32_t v0 = 0, v1 = 0;
                    if (params) { SafeCopy((uint8_t*)params, &v0, 4); SafeCopy((uint8_t*)params + 4, &v1, 4); }
                    wchar_t oc[128] = L"?";
                    ClassNameOf(obj, oc, 128);
                    L("[cs] t=%lu ms  Komponente %ls %p -> %ls  Parameter %d / %d",
                      (unsigned long)GetTickCount(), oc, obj, sn, v0, v1);
                }
            }
        }

        // v119: anything named Zone/Phase on the GameState or on the zone
        // actor itself (ServerMoveToPhase, DoMoveToPhase, OnRep_BlueZone,
        // SetVisibleBlueZone ...). Pointer compares first, name only on a hit.
        if (!isWeapon && !isPawn && !isCtrl && g_zrLogged < 300) {
            static void* gsCache = nullptr; static int gsTick = 0;
            if ((++gsTick & 1023) == 1 && g_world)
                gsCache = SafePtr((uint8_t*)g_world + OFF::UWorld_GameState);
            if ((gsCache && obj == gsCache) || (g_blueZone && obj == g_blueZone)) {
                wchar_t zn[128] = L"";
                if (ResolveFuncName(fn, zn, 128) && (wcsstr(zn, L"Zone") || wcsstr(zn, L"Phase"))) {
                    InterlockedIncrement(&g_zrLogged);
                    int cs = -1;
                    __try {
                        typedef int (__fastcall* tCs4)(void*, void*, void*);
                        cs = ((tCs4)(g_base + RVA::GetFunctionCallspace))(obj, fn, nullptr);
                    } __except (EXCEPTION_EXECUTE_HANDLER) { cs = -1; }
                    L("[zr] t=%lu ms  %s %p -> %ls  (Callspace %d)",
                      (unsigned long)GetTickCount(), obj == g_blueZone ? "Zone" : "GameState", obj, zn, cs);
                }
            }
        }
        bool isDoor = false;
        if (!isWeapon && !isPawn && !isCtrl && g_dwClsN > 0 && g_drLogged < 400)
            isDoor = IsDoorWindowClass(SafePtr((uint8_t*)obj + OFF::UObject_Class));
        if (isDoor) {
            wchar_t dn[128] = L"";
            if (ResolveFuncName(fn, dn, 128)) {
                // v98: for the THIRD time a lifecycle event flooded the budget --
                // ReceiveTick in v83, ServerMove in v84, and now ReceiveBeginPlay
                // took all 150 slots during the drop, before a single door was
                // touched. Blacklist the engine lifecycle events; everything else,
                // including oddly named Blueprint events, still gets through.
                if (wcsncmp(dn, L"Receive", 7) == 0
                 || wcsstr(dn, L"Tick") || wcsstr(dn, L"Overlap")
                 || wcscmp(dn, L"UserConstructionScript") == 0) {
                    if (g_origProcessEvent) g_origProcessEvent(obj, fn, params);
                    return;
                }
                InterlockedIncrement(&g_drLogged);
                // Callspace says whether this call is meant to leave the host at
                // all: 1 = Remote, 2 = Local, 0 = absorbed. A break sound that
                // never reaches the client would show up as Local or absorbed.
                int cs = -1;
                __try {
                    typedef int (__fastcall* tCs)(void*, void*, void*);
                    cs = ((tCs)(g_base + RVA::GetFunctionCallspace))(obj, fn, nullptr);
                } __except (EXCEPTION_EXECUTE_HANDLER) { cs = -1; }
                L("[dr] t=%lu ms  %p -> %ls  (Callspace %d)",
                  (unsigned long)GetTickCount(), obj, dn, cs);
            }
        }
        if (isWeapon || isPawn) {
            wchar_t nm[128] = L"";
            if (ResolveFuncName(fn, nm, 128)) {
                // --- accounting: always, regardless of the log budget ---
                if (wcscmp(nm, L"ServerFireProjectile") == 0) {
                    const int kind = isWeapon ? 1 : 2;
                    if (g_fireSrcKind == 0) {
                        g_fireSrcKind = kind;
                        L("[ms] Schuss-RPC kommt vom %s", kind == 1 ? "Waffen-Objekt" : "Pawn-Objekt");
                    }
                    // v112: charge the round to the weapon of the player who
                    // fired it, not to remote 0's weapon.
                    void* hit = (shooter >= 0 && g_remote[shooter].weapon)
                              ? g_remote[shooter].weapon : g_watchWeapon;
                    if (kind == g_fireSrcKind) SpendOneRound(hit);
                }

                // --- v129: the move ledger --------------------------------
                // The PC's own log says, every 19.2 seconds to the millisecond:
                //     LogNetPlayerMovement: Warning: CreateSavedMove: Hit limit
                //     of 96 saved moves (timing out or very bad ping?)
                // 36 times in one session, at a perfectly fixed cadence -- so it
                // is not ping, it is a constant leak. The client keeps a saved
                // move until the host acknowledges it; at 96 unacknowledged it
                // throws the lot away and its prediction snaps back. That is the
                // rubber-band off rocks, the ladder letting go after three
                // seconds, and the jitter in the lobby, all one cause.
                // The host DOES answer -- 274 ClientAckGoodMove and 123 small
                // corrections in that round -- but far fewer than the moves that
                // must have been sent. What has never been counted is the other
                // side of the ledger: how many ServerMove calls actually arrive.
                // arrivals vs (acks + corrections) is the leak, exactly.
                if (wcsncmp(nm, L"ServerMove", 10) == 0) InterlockedIncrement(&g_smIn);
                else if (wcscmp(nm, L"ClientAckGoodMove") == 0) InterlockedIncrement(&g_smAck);
                else if (wcsstr(nm, L"AdjustPosition"))          InterlockedIncrement(&g_smAdj);
                {
                    static DWORD lastReport = 0;
                    const DWORD now = GetTickCount();
                    if (g_smIn > 0 && (now - lastReport) > 10000) {
                        lastReport = now;
                        const LONG in = g_smIn, ak = g_smAck, ad = g_smAdj;
                        L("[sm] Zuege: %ld angekommen, %ld bestaetigt, %ld korrigiert"
                          " -> %ld ohne Antwort (%ld%%)%s",
                          in, ak, ad, in - ak - ad,
                          in ? (100 * (in - ak - ad) / in) : 0,
                          (in - ak - ad) > in / 4
                              ? "   <<< der Host verschluckt Zuege, darum laeuft der Puffer"
                                " des Clients alle 19 s voll >>>" : "");
                    }
                }

                // --- v128: RELOAD, with the numbers the host is deciding on ---
                // The 14.09. 18:xx log shows the host cancelling a reload of its
                // own accord:
                //     11892437  ServerStartReload + ShouldTacticalReload
                //     11892562  ServerStopReload + MulticastStopSimulatingReload
                //     11897562  ServerStartReload + ShouldTacticalReload
                //     11897765  ServerStopReload + MulticastStopSimulatingReload
                //     11898968  ClientStopReload
                // Twice started, twice killed within 200 ms, then the client is
                // told to stop. The user sees an empty magazine and cannot
                // reload, so the host and the client disagree about how full the
                // weapon is -- and the host, believing it is already full,
                // refuses. What has never been logged is the host's own numbers
                // at that instant. Magazine +0xDD0, backpack +0xE50 and capacity
                // +0x1A28 are all known, so print them on every reload event.
                // Magazine == capacity at a ServerStartReload is the proof.
                if (wcsstr(nm, L"Reload")) {
                    if (g_rrLogged < 300) {
                        InterlockedIncrement(&g_rrLogged);
                        void* wpn = isWeapon ? obj
                                 : ((shooter >= 0 && g_remote[shooter].weapon)
                                        ? g_remote[shooter].weapon : nullptr);
                        int32_t mag = -1, bp = -1, cap = -1;
                        if (wpn) {
                            mag = SafeI32((uint8_t*)wpn + RVA::Weapon_Magazine);
                            bp  = SafeI32((uint8_t*)wpn + RVA::Weapon_BackPackAmmo);
                            cap = SafeI32((uint8_t*)wpn + RVA::Weapon_MagazineCapacity);
                        }
                        L("[rr] t=%lu ms  %ls  Waffe %p | Magazin %d / Kapazitaet %d,"
                          " Rucksack %d%s",
                          (unsigned long)GetTickCount(), nm, wpn, mag, cap, bp,
                          (mag >= 0 && cap > 0 && mag >= cap)
                              ? "   <<< der Host haelt die Waffe fuer VOLL, darum lehnt er ab >>>"
                              : ((bp == 0 && mag < cap)
                                     ? "   <<< Rucksack leer -- nichts zum Nachladen >>>" : ""));
                    }
                }

                // --- v127: every equip / attach / socket call, on pawn or weapon
                // The [pe] whitelist below was written for ammo and widened for
                // doors; it lets "Weapon" and "Equip" through but not "Attach",
                // "Socket", "OnRep" or "Visib", and it shares one budget with
                // everything else. This branch has its own.
                if (g_attachWatch && g_waLogged < 200
                 && (wcsstr(nm, L"Attach") || wcsstr(nm, L"Equip")
                  || wcsstr(nm, L"Socket") || wcsstr(nm, L"Holster")
                  || wcsstr(nm, L"OnRep")  || wcsstr(nm, L"Visib")
                  || wcsstr(nm, L"Slot")   || wcsstr(nm, L"Mesh"))) {
                    InterlockedIncrement(&g_waLogged);
                    int cs = -1;
                    __try {
                        typedef int (__fastcall* tCs6)(void*, void*, void*);
                        cs = ((tCs6)(g_base + RVA::GetFunctionCallspace))(obj, fn, nullptr);
                    } __except (EXCEPTION_EXECUTE_HANDLER) { cs = -1; }
                    uint32_t p0 = 0, p1 = 0;
                    void*    q0 = nullptr;
                    if (params) {
                        SafeCopy((uint8_t*)params, &p0, 4);
                        SafeCopy((uint8_t*)params + 4, &p1, 4);
                        q0 = SafePtr(params);
                    }
                    wchar_t qc[128] = L"";
                    if (q0 && MaybeAlive(q0)) ClassNameOf(q0, qc, 128);
                    L("[wa] t=%lu ms  %ls %p -> %ls  (Callspace %d) | Param 0 = %p %ls | roh %08X %08X",
                      (unsigned long)GetTickCount(), isWeapon ? L"Waffe" : L"Pawn",
                      obj, nm, cs, q0, qc[0] ? qc : L"", p0, p1);
                }

                // --- v121: MOVEMENT CORRECTIONS, the stone/rubber-band bug -----
                // ClientAdjustPosition is the server telling the client "you
                // are wrong, you are actually here". Its parameters carry the
                // whole answer:
                //   +0x00 float  Timestamp
                //   +0x04 FVector NewLoc          where the server puts him
                //   +0x10 FVector NewVel
                //   +0x20 UPrimitiveComponent* NewBase   what he stands ON
                //   +0x34 bool   bHasBase
                //   +0x36 uint8  ServerMovementMode
                // If he jumps onto a stone and the server sends a correction
                // with bHasBase=0, the server has no stone there -- that is the
                // collision/streaming answer. If bHasBase=1 and the position is
                // nearly identical, it is a timing problem instead, not geometry.
                if (wcsstr(nm, L"AdjustPosition") || wcsstr(nm, L"AdjustRootMotion")
                 || wcscmp(nm, L"ClientAckGoodMove") == 0) {
                    if (g_mvLogged < 400) {
                        InterlockedIncrement(&g_mvLogged);
                        float ts = 0.0f, loc[3] = {0,0,0};
                        uint8_t hasBase = 0, movMode = 0;
                        void* base = nullptr;
                        wchar_t bc[128] = L"";
                        if (params) {
                            SafeCopy((uint8_t*)params + 0x00, &ts,  4);
                            SafeCopy((uint8_t*)params + 0x04, loc, 12);
                            base = SafePtr((uint8_t*)params + 0x20);
                            SafeCopy((uint8_t*)params + 0x34, &hasBase, 1);
                            SafeCopy((uint8_t*)params + 0x36, &movMode, 1);
                            if (base && MaybeAlive(base)) ClassNameOf(base, bc, 128);
                        }
                        // v122: ClientVeryShortAdjustPosition takes only
                        // (float Timestamp, FVector NewLoc) -- there is no base
                        // and no movement mode, so v121 printed garbage for it
                        // (bHasBase=247). Only the full ClientAdjustPosition
                        // carries NewBase/+0x34 bHasBase/+0x36 mode.
                        const bool shortForm = (wcsstr(nm, L"VeryShort") != nullptr);
                        if (shortForm) { base = nullptr; hasBase = 0; movMode = 255; bc[0] = 0; }
                        if (shortForm)
                            L("[mv] t=%lu ms  *** kleine Korrektur *** ts=%.2f -> {%.0f, %.0f, %.0f}",
                              (unsigned long)GetTickCount(), ts, loc[0], loc[1], loc[2]);
                        else if (wcscmp(nm, L"ClientAckGoodMove") == 0)
                            L("[mv] t=%lu ms  %ls (Zug akzeptiert, ts=%.2f)",
                              (unsigned long)GetTickCount(), nm, ts);
                        else
                            L("[mv] t=%lu ms  *** KORREKTUR *** %ls  ts=%.2f -> Server setzt ihn auf"
                              " {%.0f, %.0f, %.0f} | Untergrund %p %ls (bHasBase=%d) | ServerMovementMode=%d",
                              (unsigned long)GetTickCount(), nm, ts, loc[0], loc[1], loc[2],
                              base, bc[0] ? bc : L"(keiner)", hasBase, movMode);
                    }
                }

                // --- v121: CHARACTER STATE MACHINE, the ladder bug -------------
                // The handoff: ServerTryUseLadder requires state 1 and requests
                // state 15; the ladder ended after 3.672 s in their test too.
                // CurrentState is a replicated property with OnRep_CurrentState,
                // and SetCurrentState / ServerSetCurrentState / ClientSetCurrentState
                // are real functions. Logging their first parameter shows the
                // whole transition chain: 1 -> 15 -> 1, and how long it held.
                if (wcsstr(nm, L"CurrentState") || wcsstr(nm, L"CharacterState")
                 || wcsstr(nm, L"ChangeState")) {
                    if (g_csLogged < 400) {
                        InterlockedIncrement(&g_csLogged);
                        int32_t v0 = 0, v1 = 0;
                        if (params) { SafeCopy((uint8_t*)params, &v0, 4); SafeCopy((uint8_t*)params + 4, &v1, 4); }
                        int cs = -1;
                        __try {
                            typedef int (__fastcall* tCs5)(void*, void*, void*);
                            cs = ((tCs5)(g_base + RVA::GetFunctionCallspace))(obj, fn, nullptr);
                        } __except (EXCEPTION_EXECUTE_HANDLER) { cs = -1; }
                        L("[cs] t=%lu ms  %p -> %ls  Parameter %d / %d  (Callspace %d)"
                          "   [1=normal 15=Leiter 2=Flugzeug 3=Absprung 18=Startbereit]",
                          (unsigned long)GetTickCount(), obj, nm, v0, v1, cs);
                    }
                }

                // --- v115: ladders and ledge climbing, OWN budget ---------
                // Rule 8 again: the [pe] whitelist below was written for ammo
                // and later widened for doors. It admits neither "Ladder" nor
                // "Climb", so every ladder RPC this round was invisible. This
                // branch runs BEFORE it and cannot be starved by it.
                //
                // What the binary says about ladders, and why param 0 is the
                // whole question:
                //   ULadderComponent carries two replicated properties --
                //   LadderState (+0xD0, enum, RepNotify OnRep_LadderState) and
                //   UsingLadder (+0xD8, OBJECT POINTER, RepNotify
                //   OnRep_UsingLadder). The climb itself is a custom movement
                //   mode, ECustomMovementMode::CMOVE_Ladder.
                //   execServerTryUseLadder (base+0x236BB10) takes exactly one
                //   parameter: a pointer to the ABravoHotelLadder actor. An
                //   object parameter travels as a NetGUID, and a NetGUID only
                //   exists for an actor the connection knows. If the ladder is
                //   not a net actor on this connection, the host receives NULL,
                //   never enters CMOVE_Ladder, keeps simulating the player as
                //   walking or falling, and ClientAdjustPosition drags him back
                //   to the foot of the ladder -- exactly the reported symptom.
                // So: does the host see a ladder pointer, or a null?
                if (wcsstr(nm, L"Ladder") || wcsstr(nm, L"Climb")) {
                    if (g_ldLogged < 300) {
                        InterlockedIncrement(&g_ldLogged);
                        int cs = -1;
                        __try {
                            typedef int (__fastcall* tCs3)(void*, void*, void*);
                            cs = ((tCs3)(g_base + RVA::GetFunctionCallspace))(obj, fn, nullptr);
                        } __except (EXCEPTION_EXECUTE_HANDLER) { cs = -1; }
                        void*   p0 = params ? SafePtr(params) : nullptr;
                        wchar_t pc[128] = L"";
                        if (p0 && MaybeAlive(p0)) ClassNameOf(p0, pc, 128);
                        L("[ld] t=%lu ms  %p%s -> %ls  (Callspace %d) | Parameter 0 = %p %ls",
                          (unsigned long)GetTickCount(), obj,
                          isWeapon ? " (Waffe)" : " (Pawn)", nm, cs, p0,
                          p0 ? (pc[0] ? pc : L"(Klasse unlesbar)") : L"<<< NULL >>>");
                    }
                }

                // --- logging: whitelist, so the movement chatter cannot flood it ---
                if (g_peLog && g_peLogged < 400) {
                    const bool keep =
                           wcsstr(nm, L"Fire")     || wcsstr(nm, L"Reload")
                        || wcsstr(nm, L"Ammo")     || wcsstr(nm, L"Magazine")
                        || wcsstr(nm, L"Weapon")   || wcsstr(nm, L"Equip")
                        || wcsstr(nm, L"PickUp")   || wcsstr(nm, L"Combine")
                        || wcsstr(nm, L"Bullet")   || wcsstr(nm, L"Shoot")
                        // v99: the kick and the break are very likely PAWN RPCs, and
                        // this whitelist dropped every one of them -- which is why
                        // [pe] held three lines in a round with real interaction.
                        || wcsstr(nm, L"Door")     || wcsstr(nm, L"Window")
                        || wcsstr(nm, L"Glass")    || wcsstr(nm, L"Break")
                        || wcsstr(nm, L"Kick")     || wcsstr(nm, L"Interact")
                        || wcsstr(nm, L"Vault")    || wcsstr(nm, L"Sound")
                        || wcsstr(nm, L"Open")     || wcsstr(nm, L"Destruct")
                        // v161: the chambering traffic was invisible until now
                        || wcsstr(nm, L"Bolt")     || wcsstr(nm, L"Chamber")
                        || wcsstr(nm, L"Stop");
                    if (keep) {
                        InterlockedIncrement(&g_peLogged);
                        L("[pe] t=%lu ms  %p%s -> %ls", (unsigned long)GetTickCount(),
                          obj, isWeapon ? " (Waffe)" : " (Pawn)", nm);
                    }
                }
            }
        }
    }
    if (g_origProcessEvent) g_origProcessEvent(obj, fn, params);
}

static void PokeOwnedActors() {
    if (!g_drv || g_remoteCount == 0) return;
    LadderTick();                 // v122: ladder state transitions
    AttachTick();                 // v127: weapon attachment
    WeaponPushTick();             // v130: make the weapon RepNotifies fire
    // v58y: the watchers are pure reads and always run. Only the pushing --
    // the part that crashed the host twice -- is behind the switch.
    DumpGraphNodesOnce();     // v65, three times per round (v75)
    HookGridGather();         // v76, once
    RefreshWatchedWeapon();
    WatchWeaponFields();
    WatchWeaponHolder();      // v59b
    WatchCharRepFields();
    WeaponRelevance();        // v59c: [mag] + [wl], and the cull repair
    // v58z: everything the remote player owns that is asleep gets woken the
    // same way -- his PlayerState, his amplifier, his vehicles. Pure field
    // write, no engine call, so none of this can reach the branch that
    // crashed the host.
    for (int k = 0; k < g_ownedCount; ++k) {
        void* a = g_ownedActors[k];
        if (a && MaybeAlive(a)) KeepAwakeForClient(a, L"Ein eigener Actor des Remote-Spielers");
    }
    if (!g_ownPoke) return;
    // The pawn, the controller and the PlayerState first: those three exist
    // even before the level walk has found anything.
    for (int i = 0; i < g_remoteCount; ++i) {
        PokeOneActor(g_remote[i].pawn);
        PokeOneActor(g_remote[i].controller);
        OwnedSetAdd(g_remote[i].pawn);            // v58u
        OwnedSetAdd(g_remote[i].controller);
        if (void* ps = SafePtr((uint8_t*)g_remote[i].controller + OFF::AController_PlayerState)) {
            PokeOneActor(ps);
            OwnedSetAdd(ps);                      // the perk data lives here
        }
    }
    // v58w: compact the list as we go -- an actor that no longer passes the
    // checks is removed here and never touched again.
    int keep = 0;
    for (int k = 0; k < g_ownedCount; ++k) {
        void* a = g_ownedActors[k];
        if (PokeOneActor(a)) g_ownedActors[keep++] = a;
        else                 InterlockedIncrement(&g_ownDropped);
    }
    g_ownedCount = keep;
}

// ---------------------------------------------------------------------------
//  v103: #16, and Alen's decisive observation -- against bots WITHOUT the
//  listen server everything displays correctly. So the HUD's late start is
//  caused by our setup, not by the client, and my "client-side ordering
//  problem" verdict was too quick.
//
//  What the client log shows when it finally starts:
//      ABravoHotelPlayerHUD::InitHUD
//      InitGameState OnRepTeamSize Bind
//      ResetIngamePlayers  PlayerInfos Num = 1
//
//  The HUD defers init (ReCheckReadyForInit) and comes up only once it has
//  team/player data -- and it sees exactly ONE player. With bots in the match
//  that number is far too low, which points at the other players' PlayerStates
//  never reaching the client. PlayerInfos is built from them, so the HUD waits,
//  and the perk cannot be drawn until it stops waiting.
//
//  So: how many PlayerStates exist on the host, and what does each look like to
//  the replication graph? Class names are resolved once per CLASS, never per
//  actor. Pure reads.
// ---------------------------------------------------------------------------
static void*   g_clsSeen[512];
static uint8_t g_clsIsPs[512];
static int     g_clsSeenN = 0;

static bool ClassIsPlayerState(void* a, void* cls) {
    for (int i = 0; i < g_clsSeenN; ++i)
        if (g_clsSeen[i] == cls) return g_clsIsPs[i] != 0;
    wchar_t nm[128] = L"";
    const bool ps = ClassNameOf(a, nm, 128) && wcsstr(nm, L"PlayerState") != nullptr;
    if (g_clsSeenN < 512) {
        g_clsSeen[g_clsSeenN] = cls;
        g_clsIsPs[g_clsSeenN] = ps ? 1 : 0;
        ++g_clsSeenN;
    }
    return ps;
}

static void PlayerStateCensus() {
    if (!g_world) return;
    void**  levels = (void**)SafePtr((uint8_t*)g_world + RVA::UWorld_Levels);
    int32_t nLv    = SafeI32((uint8_t*)g_world + RVA::UWorld_Levels + 8);
    if (!levels || nLv <= 0 || nLv > 4096) return;

    void* found[32]; int nFound = 0;
    for (int li = 0; li < nLv && nFound < 32; ++li) {
        uint8_t* lvl = (uint8_t*)SafePtr(levels + li);
        if (!lvl) continue;
        uint64_t* acts = (uint64_t*)SafePtr(lvl + RVA::ULevel_Actors);
        int32_t   nA   = SafeI32(lvl + RVA::ULevel_Actors + 8);
        if (!acts || nA <= 0 || nA > 200000) continue;
        for (int ai = 0; ai < nA && nFound < 32; ++ai) {
            uint64_t raw = 0;
            if (!SafeCopy(acts + ai, &raw, 8)) break;
            if (raw == 0) continue;
            void* a = DecodeActorSlot(raw);
            if ((uintptr_t)a < 0x10000 || (uintptr_t)a > 0x00007FFFFFFFFFFFULL) continue;
            void* vt = SafePtr(a);
            if (!vt || !InModule(vt)) continue;
            void* cls = SafePtr((uint8_t*)a + OFF::UObject_Class);
            if (!cls || !ClassIsPlayerState(a, cls)) continue;
            found[nFound++] = a;
        }
    }

    static int lastN = -1;
    static int reports = 0;
    if (nFound == lastN && reports > 12) return;
    lastN = nFound;
    ++reports;

    L("[ps] %d PlayerState-Actors auf dem Host. Die Anzeige wartet auf PlayerInfos --"
      " im Client-Log stand dort 1.", nFound);
    for (int i = 0; i < nFound && i < 12; ++i) {
        void* a = found[i];
        wchar_t nm[128] = L"?";
        ClassNameOf(a, nm, 128);
        uint8_t ar = 0, d = 0, rp = 0, oro = 0;
        SafeCopy((uint8_t*)a + OFF::AActor_bAlwaysRelevant, &ar, 1);
        SafeCopy((uint8_t*)a + OFF::AActor_NetDormancy, &d, 1);
        SafeCopy((uint8_t*)a + 0x289, &rp, 1);
        SafeCopy((uint8_t*)a + OFF::AActor_bOnlyRelevantToOwner, &oro, 1);
        void* owner = SafePtr((uint8_t*)a + OFF::AActor_Owner);
        L("[ps]   #%d %p %ls  immer-relevant %d, nur-fuer-Besitzer %d, Dormancy %d,"
          " repliziert %d, Besitzer %p",
          i, a, nm, ar & 1, oro & 1, d, rp & 1, owner);
    }
    if (nFound <= 1)
        L("[ps] *** Nur eine PlayerState -- dann kann PlayerInfos auf dem Client gar"
          " nicht groesser werden und die Anzeige wartet zu Recht. ***");
}

static void LootSweep() {
    if (!g_spawnRemote || !g_origBuildingSpawn || !g_firstPcHooked || !g_world || g_remoteCount == 0) return;
    if (!g_buildingNative && !g_vehicleNative) return;   // learned from the first hooked calls
    InterlockedIncrement(&g_sweepCalls);
    void**  levels = (void**)SafePtr((uint8_t*)g_world + RVA::UWorld_Levels);
    int32_t nLv    = SafeI32((uint8_t*)g_world + RVA::UWorld_Levels + 8);
    if (!levels || nLv <= 0 || nLv > 4096) return;
    static int reports = 0;
    int actorsSeen = 0, actorsValid = 0, buildings = 0, nearRemote = 0, vehicles = 0;
    int withTimer = 0, noTimer = 0, unreg = 0, nearAny = 0, nearLoot = 0, lootSrc = 0;
    static int detail = 0;
    // The census resolves nothing per actor (pointer compare only) but it does
    // read a location for every one of ~20000 decoded slots, so only every 8th
    // sweep (~16 s) does it.
    static int censusTick = 0;
    // v58q: every 2nd sweep (~4 s). The v58p round showed the count around the
    // landed remote player climbing 37 -> 726 somewhere inside one 20 s gap;
    // at 4 s the log shows the ramp instead of its endpoints.
    const bool census = (g_remoteCount > 0) && ((++censusTick % 2) == 0);   // ~4 s
    for (int li = 0; li < nLv; ++li) {
        uint8_t* lvl = (uint8_t*)SafePtr(levels + li);
        if (!lvl) continue;
        uint64_t* acts = (uint64_t*)SafePtr(lvl + RVA::ULevel_Actors);
        int32_t   nA   = SafeI32(lvl + RVA::ULevel_Actors + 8);
        if (!acts || nA <= 0 || nA > 200000) continue;
        for (int ai = 0; ai < nA; ++ai) {
            uint64_t raw = 0;
            if (!SafeCopy(acts + ai, &raw, 8)) break;
            ++actorsSeen;
            if (raw == 0) continue;
            void* a = DecodeActorSlot(raw);
            if ((uintptr_t)a < 0x10000 || (uintptr_t)a > 0x00007FFFFFFFFFFFULL) continue;
            void* vt = SafePtr(a);
            if (!vt || !InModule(vt)) continue;
            ++actorsValid;
            // v58g: census of everything standing around the remote player.
            // v58k: wake everything dormant-initial around the remote player.
            // This is the whole point of the build, so it does NOT ride on the
            // census tick -- it runs on every sweep (~2 s).
            if (g_wakeUp && g_drv) {
                uint8_t rep = 0, dorm = 0;
                if (SafeCopy((uint8_t*)a + OFF::AActor_bReplicates, &rep, 1) && (rep & 1) &&
                    SafeCopy((uint8_t*)a + OFF::AActor_NetDormancy, &dorm, 1) && dorm > 1) {
                    float wl[3];
                    if (ActorLocation(a, wl)) {
                        for (int i = 0; i < g_remoteCount; ++i) {
                            float p[3];
                            if (!RemotePawnLocation(g_remote[i], p)) continue;
                            const float wx = p[0]-wl[0], wy = p[1]-wl[1], wz = p[2]-wl[2];
                            if (wx*wx + wy*wy + wz*wz > 30000.0f*30000.0f) continue;
                            if (AlreadyWoken(a)) { InterlockedIncrement(&g_wakeSkipped); break; }
                            void* wc = SafePtr((uint8_t*)a + OFF::UObject_Class);
                            if (PipeTally* wt = PipeFor(wc)) InterlockedIncrement(&wt->woken);
                            WakeActorForClients(a);
                            if (g_wokenActors <= 10) {
                                wchar_t wn[128] = L"?";
                                ResolveFuncName(wc, wn, 128);
                                L("[wk] %ls %p (Dormancy %d) im Umkreis des Remote-Spielers geweckt", wn, a, dorm);
                            }
                            break;
                        }
                    }
                }
            }
            // v58t: is this one of the remote player's own actors (weapon,
            // inventory item, PlayerState)? Collect it -- PokeOwnedActors()
            // then keeps it updating four times a second, which is where the
            // magazine count and the perk data have to come from.
            // v58y: collecting is independent of the poke -- the net-mode
            // substitution (v58u) feeds off the same set and that part is a
            // pure read, so it stays on while the pushing stays off.
            if ((g_ownPoke || g_ownMode) && g_drv && g_ownedCount < 256) {
                bool already = false;
                for (int k = 0; k < g_ownedCount && !already; ++k)
                    if (g_ownedActors[k] == a) already = true;
                // v58w: only actors that actually replicate, and never the
                // short-lived ones (projectiles, bullets, camera helpers) --
                // those die within seconds and a dead pointer in this list is
                // what killed the host when Alen opened a door.
                uint8_t repFlag = 0;
                const bool repl = SafeCopy((uint8_t*)a + OFF::AActor_bReplicates, &repFlag, 1) && (repFlag & 1);
                bool transient = false;
                if (repl && !already) {
                    wchar_t tn[128] = L"?";
                    void* tc = SafePtr((uint8_t*)a + OFF::UObject_Class);
                    if (tc && ResolveFuncName(tc, tn, 128))
                        transient = wcsstr(tn, L"Projectile") || wcsstr(tn, L"Bullet") ||
                                    wcsstr(tn, L"Camera")     || wcsstr(tn, L"Emitter");
                }
                if (!already && repl && !transient && OwnedByRemote(a)) {
                    g_ownedActors[g_ownedCount++] = a;
                    OwnedSetAdd(a);                       // v58u
                    {                                     // v58v: watch the weapon's fields
                        wchar_t wn[128] = L"?";
                        void* wc = SafePtr((uint8_t*)a + OFF::UObject_Class);
                        if (wc && ResolveFuncName(wc, wn, 128) && wcsstr(wn, L"Weapon") && !wcsstr(wn, L"Projectile")) {
                            g_watchWeapon = a; g_weaponSnapS.valid = false;
                            L("[wp] Beobachte ab jetzt die Felder von %ls %p -- ein Schuss zeigt, wo das Magazin liegt", wn, a);
                        }
                    }
                    LONG f = InterlockedIncrement(&g_ownFound);
                    if (f <= 30) {
                        wchar_t on[128] = L"?";
                        void* oc = SafePtr((uint8_t*)a + OFF::UObject_Class);
                        if (oc) ResolveFuncName(oc, on, 128);
                        uint8_t od = 0, orl = 0, orr = 0, orep = 0;
                        float    onuf = 0.f;
                        SafeCopy((uint8_t*)a + OFF::AActor_NetDormancy, &od,  1);
                        SafeCopy((uint8_t*)a + OFF::AActor_Role,        &orl, 1);
                        SafeCopy((uint8_t*)a + OFF::AActor_RemoteRole,  &orr, 1);
                        SafeCopy((uint8_t*)a + OFF::AActor_bReplicates, &orep,1);
                        SafeCopy((uint8_t*)a + 0x2A4, &onuf, 4);
                        // v58v: bReplicates is the number that decides everything here.
                        // A weapon with bReplicates = 0 never sends a property to
                        // anyone, and the magazine has to travel inside the inventory
                        // array instead -- a completely different fix from a weapon
                        // that replicates but whose channel never opens.
                        L("[ow] Eigener Actor des Remote-Spielers: %ls %p (repliziert %d, Dormancy %d, Role %d, RemoteRole %d, NetUpdateFrequency %.1f) -- #%ld",
                          on, a, orep & 1, od, orl, orr, onuf, f);
                    }
                }
            }
            // v58o: is this actor a loot source at all? Ask it the way the
            // game asks. Only for actors near the remote player, so the cost
            // stays a few hundred virtual calls per sweep.
            if (g_lootScan && nearLoot < 4000) {
                float ll[3];
                if (ActorLocation(a, ll)) {
                    for (int i = 0; i < g_remoteCount; ++i) {
                        float p[3];
                        if (!RemotePawnLocation(g_remote[i], p)) continue;
                        const float lx = p[0]-ll[0], ly = p[1]-ll[1], lz = p[2]-ll[2];
                        if (lx*lx + ly*ly + lz*lz > 30000.0f*30000.0f) continue;
                        ++nearLoot;
                        if (void* comp = SpawnComponentOf(a)) {
                            ++lootSrc;
                            InterlockedIncrement(&g_lootSources);
                            FireSpawnComponent(comp, a);
                        }
                        break;
                    }
                }
            }
            if (census) {
                float al[3];
                if (ActorLocation(a, al)) {
                    for (int i = 0; i < g_remoteCount; ++i) {
                        float p[3];
                        if (!RemotePawnLocation(g_remote[i], p)) continue;
                        const float ex = p[0]-al[0], ey = p[1]-al[1], ez = p[2]-al[2];
                        if (ex*ex + ey*ey + ez*ez <= 20000.0f*20000.0f) {
                            TallyClass(SafePtr((uint8_t*)a + OFF::UObject_Class));
                            ++nearAny;
                            break;
                        }
                    }
                    if (g_lastHostLoc[0] != 0.f || g_lastHostLoc[1] != 0.f) {
                        const float hx = g_lastHostLoc[0]-al[0], hy = g_lastHostLoc[1]-al[1], hz = g_lastHostLoc[2]-al[2];
                        if (hx*hx + hy*hy + hz*hz <= 20000.0f*20000.0f)
                            TallyClassHost(SafePtr((uint8_t*)a + OFF::UObject_Class));
                    }
                }
            }
            const bool b = IsChildOfClass(a, g_buildingNative);
            const bool v = !b && IsChildOfClass(a, g_vehicleNative);
            if (!b && !v) continue;
            if (b) ++buildings; else ++vehicles;
            float loc[3];
            if (!ActorLocation(a, loc)) continue;
            bool close = false;
            for (int i = 0; i < g_remoteCount && !close; ++i) {
                float p[3];
                if (!RemotePawnLocation(g_remote[i], p)) continue;
                const float dx = p[0]-loc[0], dy = p[1]-loc[1], dz = p[2]-loc[2];
                close = (dx*dx + dy*dy + dz*dz) <= (b ? 40000.0f*40000.0f : 60000.0f*60000.0f);
            }
            if (!close) continue;
            ++nearRemote;
            // v58f: is this actor registered (root component has a world,
            // +0xB0) and does it already run its own spawn timer (+0x470)?
            // Registered + no timer = the case the game never handles for
            // us (BeginPlay saw a non-standalone net mode, or the level was
            // added while nobody was near). Only those get the manual check.
            uint8_t* root = (uint8_t*)SafePtr((uint8_t*)a + OFF::AActor_RootComponent);
            const bool registered = root && SafePtr(root + 0xB0) != nullptr;
            uint64_t timer = 0;
            if (b) SafeCopy((uint8_t*)a + RVA::Building_SpawnTimer, &timer, 8);
            if (!registered) { ++unreg; continue; }
            if (b && timer != 0) { ++withTimer; continue; }      // the hook path sees these 10x/s anyway
            if (b) ++noTimer;
            if (detail < 12) {
                uint32_t fl = 0; SafeCopy((uint8_t*)a + RVA::Building_SpawnFlags, &fl, 4);
                L("[sw] %s %p in Level #%d (%d Actors) bei {%.0f, %.0f, %.0f}: registriert, Timer %llX, Flags+0x468=%08X -> manuelle Spawn-Pruefung",
                  b ? "Gebaeude" : "Fahrzeug-Spawner", a, li, nA, loc[0], loc[1], loc[2], (unsigned long long)timer, fl);
                ++detail;
            }
            if (b) { InterlockedIncrement(&g_spawnCalls); RunSpawnForRemotePawns(g_origBuildingSpawn, a, "Gebaeude(Sweep)", 40000.0f, true); }
            else if (g_origVehicleSpawn) { InterlockedIncrement(&g_spawnCalls); RunSpawnForRemotePawns(g_origVehicleSpawn, a, "Fahrzeug-Spawner(Sweep)", 60000.0f, false); }
        }
    }
    g_sweepBuildings = buildings;
    g_sweepNear = nearRemote;
    g_sweepTimer = withTimer; g_sweepNoTimer = noTimer; g_sweepUnreg = unreg;
    if (census) { g_censusActors = nearAny; LogTally(20000); }
    if (g_lootScan) {
        static int lootReport = 0;
        if (lootReport < 15 || (lootReport % 5) == 0) {
            L("[lq] %d Actors im Umkreis 300 m geprueft, %d davon sind Loot-Quellen | insgesamt: %ld gefunden, %ld versucht, %ld ausgeloest, %ld schon von uns erledigt",
              nearLoot, lootSrc, g_lootSources, g_lootOpen, g_lootFired, g_lootDone);
            // v58q: which of the six gates of 0x2142DA0 actually blocks us.
            L("[lq] Tore: keine World %ld, kein GameMode %ld, kein GameState %ld, Match-Phase < 4 %ld, kein Owner %ld, Owner nicht Authority %ld%s",
              g_lootGate[1], g_lootGate[2], g_lootGate[3], g_lootGate[4], g_lootGate[5], g_lootGate[6],
              g_gateBypass ? " (Tore 1/2/3/5/6 werden uebersprungen)" : " (-nogatebypass: nichts wird uebersprungen)");
        }
        ++lootReport;
    }
    // v119: which tile VARIANTS does the host hold around the remote player?
    // Every tile ships as SLV-x?_y?_Grid_*_High and _Low (plus numeric LODs).
    // If the host holds _Low where the client holds _High, the server has no
    // small collision -- no stone to stand on -- and corrects the client to
    // its own ground. Every 8th walk (~16 s), levels within 500 m of the pawn.
    if ((reports % 8) == 3 && g_remoteCount > 0) {
        float pp[3];
        if (RemotePawnLocation(g_remote[0], pp)) {
            int nHigh = 0, nLow = 0, nOther = 0, listed = 0;
            for (int li = 0; li < nLv; ++li) {
                uint8_t* lvl = (uint8_t*)SafePtr(levels + li);
                if (!lvl) continue;
                void* pkg = SafePtr(lvl + 0x28);                    // Outer = the tile's UWorld
                wchar_t tn[128] = L"?";
                if (!pkg || !FNameToStr((uint8_t*)pkg + OFF::UObject_Name, tn, 128)) continue;
                if (wcsncmp(tn, L"SLV-", 4) != 0) continue;
                const size_t tl = wcslen(tn);
                const bool isHigh = tl > 5 && wcscmp(tn + tl - 5, L"_High") == 0;
                const bool isLow  = tl > 4 && wcscmp(tn + tl - 4, L"_Low") == 0;
                if (isHigh) ++nHigh; else if (isLow) ++nLow; else ++nOther;
                // distance: nearest of the first 24 valid actors in the tile
                uint64_t* acts = (uint64_t*)SafePtr(lvl + RVA::ULevel_Actors);
                int32_t   nA   = SafeI32(lvl + RVA::ULevel_Actors + 8);
                if (!acts || nA <= 0) continue;
                float best = 1e30f; int tried = 0;
                for (int ai = 0; ai < nA && tried < 24; ++ai) {
                    uint64_t raw = 0;
                    if (!SafeCopy(acts + ai, &raw, 8) || raw == 0) continue;
                    void* a = DecodeActorSlot(raw);
                    if ((uintptr_t)a < 0x10000 || (uintptr_t)a > 0x00007FFFFFFFFFFFULL) continue;
                    float wl[3];
                    if (!ActorLocation(a, wl)) continue;
                    ++tried;
                    const float dx = wl[0]-pp[0], dy = wl[1]-pp[1];
                    const float d2 = dx*dx + dy*dy;
                    if (d2 < best) best = d2;
                }
                if (best < 50000.0f*50000.0f && listed < 16) {
                    ++listed;
                    L("[tl]   %ls  (%.0f m)", tn, sqrtf(best) / 100.0f);
                }
            }
            L("[tl] Walk #%ld: SLV-Kacheln geladen -- %d _High, %d _Low, %d andere; oben die im Umkreis von 500 m um den Remote-Pawn bei {%.0f, %.0f}",
              g_sweepCalls, nHigh, nLow, nOther, pp[0], pp[1]);
        }
    }
    if (reports < 12 || (reports % 30) == 0) {
        L("[sw] Level-Walk #%ld: %d Levels, %d Actor-Slots, %d gueltig dekodiert, %d Gebaeude, %d Fahrzeug-Spawner, %d davon nahe am Remote-Spieler (mit Timer %d, ohne Timer %d, nicht registriert %d)",
          g_sweepCalls, nLv, actorsSeen, actorsValid, buildings, vehicles, nearRemote, withTimer, noTimer, unreg);
    }
    ++reports;
}

static void __fastcall MyBuildingCheckSpawn(void* building) {
    if (!g_origBuildingSpawn) return;
    InterlockedIncrement(&g_spawnCalls);
    if (!g_buildingNative) {
        g_buildingNative = NativeAncestorNamed(building, L"BravoHotelBuilding");
        L("[ls] Klasse BravoHotelBuilding = %p (aus Instanz %p)", g_buildingNative, building);
    }
    // v58n: before handing the call to the game, evaluate the same gates the
    // game is about to evaluate -- with the HOST's controller, which is what
    // the game uses. One in eight calls is enough for the ratio, and it says
    // in plain numbers which line the host path dies on while the host is
    // still in the air, and what changes the moment it touches down.
    if (g_spawnDepth == 0) {
        static LONG gateTick = 0;
        if ((InterlockedIncrement(&gateTick) & 7) == 0) {
            uint8_t* gs = (uint8_t*)GameStateOf(building);
            uint8_t ph = 0;
            if (!gs || !SafeCopy(gs + RVA::GameState_Phase, &ph, 1) || ph != 4) {
                InterlockedIncrement(&g_gPhase);
            } else {
                void* pc = RealFirstPlayerController();
                if (!pc) InterlockedIncrement(&g_gNoPc);
                else {
                    void* pawn = nullptr;
                    __try { pawn = ((tPawnThunk)(g_base + RVA::Pawn_Thunk))(pc, false); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { pawn = nullptr; }
                    float bl[3], pl[3];
                    if (!pawn) InterlockedIncrement(&g_gNoPawn);
                    else if (!ActorLocation(building, bl) || !ActorLocation(pawn, pl)) InterlockedIncrement(&g_gNoPawn);
                    else {
                        const float dx = pl[0]-bl[0], dy = pl[1]-bl[1], dz = pl[2]-bl[2];
                        if (dx*dx + dy*dy + dz*dz >= 30000.0f*30000.0f) InterlockedIncrement(&g_gFar);
                        else {
                            uint8_t fl = 0;
                            SafeCopy((uint8_t*)pc + 0x1928, &fl, 1);
                            if (!fl) InterlockedIncrement(&g_gFlag);
                            else     InterlockedIncrement(&g_gPass);
                        }
                    }
                }
            }
        }
    }
    g_origBuildingSpawn(building);                       // host, as before
    if (g_spawnDepth == 0) {
        // v58f: how many of the game's own timer-driven checks happen around
        // the HOST (400 m)? Compared with the remote counter this shows whether
        // the buildings around the remote player run their timers at all.
        float bl[3];
        if (ActorLocation(building, bl) && (g_lastHostLoc[0] != 0.f || g_lastHostLoc[1] != 0.f)) {
            const float dx = bl[0]-g_lastHostLoc[0], dy = bl[1]-g_lastHostLoc[1], dz = bl[2]-g_lastHostLoc[2];
            if (dx*dx + dy*dy + dz*dz <= 40000.0f*40000.0f) InterlockedIncrement(&g_spawnNearHost);
        }
        RunSpawnForRemotePawns(g_origBuildingSpawn, building, "Gebaeude", 40000.0f, true);
    }
}

static void __fastcall MyVehicleCheckSpawn(void* spawner) {
    if (!g_origVehicleSpawn) return;
    InterlockedIncrement(&g_spawnCalls);
    if (!g_vehicleNative) {
        g_vehicleNative = NativeAncestorNamed(spawner, L"BravoHotelVehicleSpawnActor");
        L("[ls] Klasse BravoHotelVehicleSpawnActor = %p (aus Instanz %p)", g_vehicleNative, spawner);
    }
    g_origVehicleSpawn(spawner);
    if (g_spawnDepth == 0) RunSpawnForRemotePawns(g_origVehicleSpawn, spawner, "Fahrzeug-Spawner", 60000.0f, false);
}

// ===========================================================================
//  v58g: THE CLIENT'S VIEWPOINT INSIDE THE REPLICATION GRAPH
// ===========================================================================
//  Everything the remote player is missing -- floor loot, breakable glass,
//  doors, vehicles -- is a GRID actor. Everything that does work for him --
//  the blue zone, his own pawn and controller -- is an always-relevant or an
//  owner-relevant actor, and neither of those needs a position. The grid node
//  on the other hand gathers cells around ONE point per connection: the
//  ViewLocation of the FNetViewer built in
//  UReplicationGraph::ServerReplicateActors (see RVA::FNetViewer_Ctor for the
//  whole chain). If that point is wrong -- the controller's own actor position,
//  the origin, the host -- the client is sent the cells around the wrong place
//  and sees nothing where he actually stands. The waiting island sits at the
//  world origin, which is exactly where everything DOES work today.
//
//  So: log what the graph really uses, and, for a connection that belongs to a
//  registered remote player, correct the point to that player's pawn when the
//  two are more than 50 m apart. Writing into the freshly built FNetViewer is
//  as narrow as this gets -- it is a stack temporary of the caller, it lives
//  for this one gather, and nothing else in the engine reads it.
//  Switch: -noviewfix (measure only).
// ===========================================================================
typedef void* (__fastcall* tNetViewerCtor)(void* out, void* conn, float dt);
static tNetViewerCtor g_origNetViewer = nullptr;
static bool           g_viewFix    = true;     // -noviewfix

static void* __fastcall MyNetViewerCtor(void* out, void* conn, float dt) {
    void* r = g_origNetViewer ? g_origNetViewer(out, conn, dt) : out;
    if (!out || !conn) return r;
    InterlockedIncrement(&g_viewCalls);
    void* pc  = SafePtr((uint8_t*)conn + OFF::UNetConn_PlayerController);
    void* own = SafePtr((uint8_t*)conn + OFF::UNetConn_OwningActor);
    void* vt  = SafePtr((uint8_t*)out  + OFF::FNetViewer_ViewTarget);
    float loc[3] = { 0.f, 0.f, 0.f };
    SafeCopy((uint8_t*)out + OFF::FNetViewer_ViewLocation, loc, 12);

    int idx = -1;
    for (int i = 0; i < g_remoteCount; ++i)
        if (g_remote[i].controller == pc || g_remote[i].pawn == own) { idx = i; break; }

    float pawn[3] = { 0.f, 0.f, 0.f };
    const bool havePawn = (idx >= 0) && RemotePawnLocation(g_remote[idx], pawn);
    float dist = -1.f;
    if (havePawn) {
        const float dx = pawn[0]-loc[0], dy = pawn[1]-loc[1], dz = pawn[2]-loc[2];
        dist = sqrtf(dx*dx + dy*dy + dz*dz);
    }

    static LONG logged = 0;
    static DWORD lastLog = 0;
    const DWORD now = GetTickCount();
    if (logged < 8 || (idx >= 0 && now - lastLog > 5000)) {
        wchar_t vtn[128] = L"?";
        void* vtc = vt ? SafePtr((uint8_t*)vt + OFF::UObject_Class) : nullptr;
        if (vtc) ResolveFuncName(vtc, vtn, 128);
        int32_t st = SafeI32((uint8_t*)conn + OFF::UNetConn_State);
        L("[vw] Verbindung %p (Status %d): PC %p, OwningActor %p, ViewTarget %p (%ls) -> Blickpunkt {%.0f, %.0f, %.0f}%s",
          conn, st, pc, own, vt, vtn, loc[0], loc[1], loc[2],
          idx >= 0 ? "  [Remote-Spieler]" : "");
        if (havePawn)
            L("[vw]   Remote-Pawn %p steht bei {%.0f, %.0f, %.0f} -- Abstand zum Blickpunkt %.0f",
              g_remote[idx].pawn, pawn[0], pawn[1], pawn[2], dist);
        ++logged;
        if (idx >= 0) lastLog = now;
    }

    if (idx >= 0) {
        InterlockedIncrement(&g_viewRemote);
        memcpy(g_lastViewLoc, loc, 12);
        if (g_viewFix && havePawn && dist > 5000.0f) {
            SafeCopy(pawn, (uint8_t*)out + OFF::FNetViewer_ViewLocation, 12);
            if (vt != g_remote[idx].pawn) {
                void* np = g_remote[idx].pawn;
                SafeCopy(&np, (uint8_t*)out + OFF::FNetViewer_ViewTarget, 8);
            }
            memcpy(g_lastViewLoc, pawn, 12);
            LONG n = InterlockedIncrement(&g_viewFixes);
            if (n <= 6 || (n % 500) == 0)
                L("[vw] Blickpunkt des Remote-Spielers war %.0f Einheiten daneben -> auf den Pawn {%.0f, %.0f, %.0f} gesetzt (#%ld)",
                  dist, pawn[0], pawn[1], pawn[2], n);
        }
    }
    return r;
}

// ---- v58g: what actually stands around the remote player on the HOST? -----
//  The level walk decodes every actor anyway, so tally the CLASSES within
//  200 m of a remote pawn. Pointer comparison per actor, names resolved only
//  when the table is printed. If there are no pickup actors in that list the
//  host never spawned them; if they are there, they exist and the problem is
//  purely that they never reach the client.
//  v58h: two tallies -- one around the remote player, one around the host --
//  so the two halves of the same map can be compared in one line of the log.
struct ClassTally { void* cls; int count; };
static ClassTally g_tally[96];
static int        g_tallyN = 0;
static ClassTally g_tallyH[96];
static int        g_tallyHN = 0;

static void TallyInto(ClassTally* tab, int& num, void* cls) {
    if (!cls) return;
    for (int i = 0; i < num; ++i) if (tab[i].cls == cls) { ++tab[i].count; return; }
    if (num < 96) { tab[num].cls = cls; tab[num].count = 1; ++num; }
}
static void TallyClass(void* cls)     { TallyInto(g_tally,  g_tallyN,  cls); }
static void TallyClassHost(void* cls) { TallyInto(g_tallyH, g_tallyHN, cls); }

// Is this one of the classes the loot question is actually about?
static bool IsLootName(const wchar_t* n) {
    return wcsstr(n, L"ickup") || wcsstr(n, L"Item") || wcsstr(n, L"Weapon") ||
           wcsstr(n, L"Ammo")  || wcsstr(n, L"Supply") || wcsstr(n, L"Loot");
}

static void LogTallyTable(ClassTally* tab, int& num, int radius, const char* who) {
    if (num <= 0) { L("[cn] %s: kein einziger Actor im Umkreis von %d Einheiten", who, radius); return; }
    int total = 0, loot = 0;
    for (int i = 0; i < num; ++i) total += tab[i].count;
    L("[cn] %s: %d Actors im Umkreis von %d Einheiten, %d verschiedene Klassen:", who, total, radius, num);
    bool done[96] = { false };
    // the loot-ish classes first and always, however rare they are
    for (int i = 0; i < num; ++i) {
        wchar_t nm[128] = L"?";
        if (!ResolveFuncName(tab[i].cls, nm, 128) || !IsLootName(nm)) continue;
        done[i] = true; loot += tab[i].count;
        void* cdo = SafePtr((uint8_t*)tab[i].cls + OFF::UClass_DefaultObject);
        float cull = 0.f; uint8_t rel = 0, rep = 0, dorm = 0;
        if (cdo) {
            SafeCopy((uint8_t*)cdo + OFF::AActor_NetCullDistanceSquared, &cull, 4);
            SafeCopy((uint8_t*)cdo + OFF::AActor_bAlwaysRelevant, &rel, 1);
            SafeCopy((uint8_t*)cdo + OFF::AActor_bReplicates, &rep, 1);
            SafeCopy((uint8_t*)cdo + OFF::AActor_NetDormancy, &dorm, 1);
        }
        L("[cn]   *** %4d x %ls   (repliziert %d, immer-relevant %d, Dormancy %d, Cull %.0f m)",
          tab[i].count, nm, rep & 1, rel & 1, dorm, cull > 0.f ? sqrtf(cull) / 100.0f : 0.0f);
    }
    if (loot == 0) L("[cn]   *** KEINE Loot-/Item-Klasse im Umkreis");
    for (int shown = 0; shown < 24; ++shown) {
        int best = -1;
        for (int i = 0; i < num; ++i)
            if (!done[i] && (best < 0 || tab[i].count > tab[best].count)) best = i;
        if (best < 0) break;
        done[best] = true;
        wchar_t nm[128] = L"?";
        ResolveFuncName(tab[best].cls, nm, 128);
        // net settings of that class, straight off its CDO
        void* cdo = SafePtr((uint8_t*)tab[best].cls + OFF::UClass_DefaultObject);
        float cull = 0.f; uint8_t rel = 0, rep = 0, dorm = 0;
        if (cdo) {
            SafeCopy((uint8_t*)cdo + OFF::AActor_NetCullDistanceSquared, &cull, 4);
            SafeCopy((uint8_t*)cdo + OFF::AActor_bAlwaysRelevant, &rel, 1);
            SafeCopy((uint8_t*)cdo + OFF::AActor_bReplicates, &rep, 1);
            SafeCopy((uint8_t*)cdo + OFF::AActor_NetDormancy, &dorm, 1);
        }
        L("[cn]   %4d x %ls   (repliziert %d, immer-relevant %d, Dormancy %d, Cull %.0f m)",
          tab[best].count, nm, rep & 1, rel & 1, dorm,
          cull > 0.f ? sqrtf(cull) / 100.0f : 0.0f);
    }
    num = 0;
}

static void LogTally(int radius) {
    LogTallyTable(g_tally,  g_tallyN,  radius, "Um den REMOTE-Spieler");
    LogTallyTable(g_tallyH, g_tallyHN, radius, "Um den HOST");
}

// ===========================================================================
//  v58c: DORMANCY WAKE-UPS THROUGH THE MASK
// ===========================================================================
//  See RVA::AActor_FlushNetDormancy. Placed level actors (windows, doors, loot
//  boxes) sit in DORM_Initial and are not even in the replication graph until
//  the first FlushNetDormancy; the zone actor and every "dormant-wanting"
//  actor sleeps on the connection after one replication until the next flush.
//  With World->NetDriver masked, AActor::GetNetDriver() returned null inside
//  these two functions and the wake-up never reached the driver -- the host
//  broke the window, the client kept it, no sound; the ammo count reached the
//  client only when something else woke the weapon. Both calls now run with
//  the driver visible.
//
//  Abschalten ohne Neubau:  -nodormfix
// ===========================================================================
typedef void (__fastcall* tActorVoid)(void* actor);
static tActorVoid g_origFlushNetDormancy = nullptr;
static tActorVoid g_origForceNetUpdate   = nullptr;
tActorVoidFwd     g_flushDormancyFwd     = nullptr;   // v58k: same trampoline, usable earlier
tActorVoidFwd     g_forceNetUpdateFwd    = nullptr;   // v58t

// ---------------------------------------------------------------------------
//  v62: the fourth host crash, and this time it was not us calling
//
//  Signature identical to the three before -- AV reading 0x40, top frames
//  0x11E820A / 0x13BADC9 / 0x1394877 / 0x13AB3F7 / 0x13B9734 -- the
//  replication graph's DORM_DormantAll branch dereferencing the per-connection
//  pointer at [rbp+0x1D8], which only exists inside the graph's own
//  per-connection pass.
//
//  What is new is the rest of the stack. It is not our tick this time:
//      UWorld::Tick -> UIpNetDriver::TickDispatch (0x13483D0)
//        -> UNetConnection receive -> UActorChannel bunch handling
//        -> the Blueprint VM -> a native -> UNetDriver (0x43D7960)
//        -> the graph -> crash
//  An incoming RPC from the client reached the graph from outside its own
//  pass. sp_listen is nowhere in that callstack, and the round has not a
//  single [mp] line, so neither the push nor v61's read-only diagnostics ran.
//
//  But we do make that path reachable. Before v58c these two calls were silent
//  no-ops for everyone, because AActor::GetNetDriver reads World->NetDriver
//  and the mask nulls it. We unmask them for EVERY actor -- 2945 real
//  FlushNetDormancy calls in the crashing round -- so the game's own calls now
//  reach the driver, and an actor sitting at DORM_DormantAll walks straight
//  into the branch.
//
//  The wake sites have refused Dormancy 2 and 3 since v58y. This extends the
//  same rule to the game's own calls: for a dormant-all actor we simply do not
//  open the window, so the call falls back to the harmless no-op it was before
//  v58c. Nothing else changes, and every other actor keeps the v58c fix.
// ---------------------------------------------------------------------------
static volatile LONG g_dormRefused = 0;

static bool IsDormantAllActor(void* actor) {
    if (!actor) return false;
    uint8_t d = 0;
    if (!SafeCopy((uint8_t*)actor + OFF::AActor_NetDormancy, &d, 1)) return false;
    return d == 2 || d == 3;          // DORM_DormantAll / DORM_DormantPartial
}

static void __fastcall MyFlushNetDormancy(void* actor) {
    if (!g_origFlushNetDormancy) return;
    InterlockedIncrement(&g_flushCalls);
    bool win = g_maskActive && g_drv && g_world;
    if (win && IsDormantAllActor(actor)) {
        win = false;                   // v62: never open the window for these
        LONG n = InterlockedIncrement(&g_dormRefused);
        if (n <= 30) {
            // v101: name it. "Some dormant actor" was never enough -- if the
            // refused actor is the remote player's PlayerState, that single line
            // is #16.
            wchar_t rn[128] = L"?";
            ClassNameOf(actor, rn, 128);
            L("[dm] FlushNetDormancy fuer %ls %p (DORM_DormantAll) -- Fenster bleibt ZU"
              " (v62-Crashschutz) -- #%ld", rn, actor, n);
        }
    }
    if (win) { MaskOff(); InterlockedIncrement(&g_flushWindow); }
    g_origFlushNetDormancy(actor);
    if (win) MaskOn();
}

// v58x: DoReload, so the log says exactly WHEN the host executed a reload for
// this pawn. With [wp] and [cr] diffing four times a second, one reload plus
// one timestamp is enough to see which field is the magazine -- and whether it
// moves on the host at all.
typedef void (__fastcall* tDoReload)(void* character);
static tDoReload     g_origDoReload = nullptr;
static volatile LONG g_reloadCalls = 0;

static void __fastcall MyDoReload(void* character) {
    if (g_origDoReload) g_origDoReload(character);
    LONG n = InterlockedIncrement(&g_reloadCalls);
    if (n <= 40) {
        bool isRemote = false;
        for (int i = 0; i < g_remoteCount; ++i) if (g_remote[i].pawn == character) isRemote = true;
        void* w = nullptr;
        __try { w = ((tGetCurrentWeapon)(g_base + RVA::Character_GetCurrentWeapon))(character); }
        __except (EXCEPTION_EXECUTE_HANDLER) { w = nullptr; }
        L("[rl] t=%lu ms  DoReload fuer %s %p, aktuelle Waffe %p -- #%ld",
          (unsigned long)GetTickCount(), isRemote ? "den REMOTE-Spieler" : "den Host",
          character, w, n);
    }
}

static void __fastcall MyForceNetUpdate(void* actor) {
    if (!g_origForceNetUpdate) return;
    InterlockedIncrement(&g_forceCalls);
    bool win = g_maskActive && g_drv && g_world;
    if (win && IsDormantAllActor(actor)) {      // v62, same guard
        win = false;
        LONG n = InterlockedIncrement(&g_dormRefused);
        if (n <= 20)
            L("[dm] ForceNetUpdate fuer einen DORM_DormantAll-Actor %p -- Fenster bleibt ZU (v62-Crashschutz) -- #%ld", actor, n);
    }
    if (win) MaskOff();
    g_origForceNetUpdate(actor);
    if (win) MaskOn();
}

// ===========================================================================
//  v117 -- Kanal-Schutz: the actual host crash, caught twice and understood
//
//  Both 14.09. crashes land in the SAME call, from the same place:
//
//    our tick -> URealReplicationGraph::ServerReplicateActors  0x13C6C60
//             -> 0x13C2990 -> 0x13C3DB0
//             -> 0x13C3E9F  call 0x3F81A20   (rcx = Actor, rdx = Connection)
//
//  0x3F81A20 hands the actor to the connection's channel layer:
//    0x43E5D70 (ActorChannels map at Connection+0x3D8/+0x430)
//      -> 0x43E7870 FindOrCreateActorChannel
//         -> 0x2D1BCA0 FWeakObjectPtr::operator=   reads Actor+0xC
//         -> 0x4564C90 -> 0x456EA50                reads Actor+0x40
//
//  Crash 1 (13:13): Actor = 0x7F12038200388E04 -- non-canonical, i.e. freed
//                   and reused memory. Faulted in the weak-pointer assignment.
//  Crash 2 (15:30): Actor = NULL. The weak-pointer assignment survives a null
//                   (it has its own test and writes the "invalid" pair), so it
//                   got one level deeper and died on Actor+0x40 = 0x40.
//
//  Same defect, two shapes: the graph gathers an entry whose actor is no
//  longer usable, and the channel layer dereferences it without checking.
//
//  The guard is the engine's own behaviour, applied to the other argument:
//  0x3F81A20 ALREADY starts with "test rdx,rdx / je 0x3F81BAD", and 0x3F81BAD
//  is a bare ret. So a bad connection makes it return without doing anything.
//  We do exactly that for a bad actor. No engine state is written, no result
//  field is faked, and the caller ignores the return value -- it is the one
//  branch the function is already built to take.
//
//  "Bad" is deliberately strict: null or the first page, non-canonical (bits
//  63:47 not clear -- that is what made crash 1 a GP fault), not 8-byte
//  aligned, or RemoteRole at +0x20F not readable. +0x20F is the first field
//  the original reads, so a pointer that passes cannot fault there either.
//
//  Because the hook sits at the function entry and the entry's second
//  instruction is a rel32 jump (NOT position independent), the trampoline is
//  built from 0x3F81A29 instead -- the instruction after that jump -- and the
//  null-connection test is reproduced here in C.
//
//  Off with -nochanguard.
// ===========================================================================
typedef void (__fastcall* tActorChanPrep)(void* actor, void* conn);
static tActorChanPrep g_origActorChanPrep = nullptr;
static bool           g_chanGuard    = true;    // -nochanguard
static volatile LONG  g_chanGuardHits = 0;
static uintptr_t      g_chanGuardCallers[8] = {};
static int            g_chanGuardCallerN = 0;

// v126: one address test, used for the actor, its vtable and its class.
static bool PtrPlausible(const void* p) {
    const uintptr_t a = (uintptr_t)p;
    if (a < 0x10000)    return false;     // null and the whole first page
    if ((a >> 47) != 0) return false;     // non-canonical -> GP fault
    if (a & 7)          return false;     // a UObject is never odd-aligned
    return true;
}

// v126: the 14.09. 17:09 crash went straight through the v117 guard, and the
// register dump says why in one value:
//     Lesen an Adresse 00000000656E6F5A
//     rdx = r8 = r14 = 00000000656E6F4E
// 0x656E6F4E is the four bytes 4E 6F 6E 65 -- the ASCII text "None". So the
// engine was handed the string "None" where a UObject pointer belongs.
// Reading 0x43E5D70 shows what it actually passes on:
//     0x43E5E5B  mov r8, [rbp+0x40]     ; the actor
//     0x43E5E5F  mov r8, [r8 + 0x20]    ; -> UObject::Class
//     0x43E5E6A  call 0x43E7870         ; and THAT is what gets weak-pointered
// The actor pointer itself was fine -- aligned, canonical, readable -- which is
// exactly why v117 let it through. What was rotten was the object's CONTENTS:
// a freed actor whose memory has been reused still passes every test on its own
// address. So check the two fields the engine dereferences immediately after:
// the actor's own vtable, and its class (plus the class's vtable). Both must
// land inside the game module, which freed-and-reused memory essentially never
// does. This crash would have been caught on the class pointer alone: 0x656E6F4E
// is not even 8-byte aligned.
static bool ActorPtrSane(const void* p) {
    if (!PtrPlausible(p)) return false;
    uint8_t probe = 0;                    // RemoteRole, the first field read
    if (!SafeCopy((const uint8_t*)p + 0x20F, &probe, 1)) return false;
    void* vt = SafePtr(p);                // the actor's own vtable
    if (!PtrPlausible(vt) || !InModule(vt)) return false;
    void* cls = SafePtr((const uint8_t*)p + OFF::UObject_Class);   // +0x20
    if (!PtrPlausible(cls)) return false;
    void* clsVt = SafePtr(cls);           // the UClass's vtable
    if (!PtrPlausible(clsVt) || !InModule(clsVt)) return false;
    return true;
}

static void __fastcall MyActorChanPrep(void* actor, void* conn) {
    if (!conn) return;                    // the original's own first check
    if (g_chanGuard && !ActorPtrSane(actor)) {
        const uintptr_t caller = (uintptr_t)_ReturnAddress() - g_base;
        const LONG n = InterlockedIncrement(&g_chanGuardHits);
        bool fresh = true;
        for (int i = 0; i < g_chanGuardCallerN; ++i)
            if (g_chanGuardCallers[i] == caller) { fresh = false; break; }
        if (fresh && g_chanGuardCallerN < 8) g_chanGuardCallers[g_chanGuardCallerN++] = caller;
        if (fresh || n <= 10 || (n % 500) == 0) {
            void* dvt  = SafePtr(actor);
            void* dcls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
            L("[cg] *** Unbrauchbarer Actor %p an die Kanal-Schicht abgefangen"
              " (VTable %p, Klasse %p) -- Aufrufer base+0x%llX, Treffer #%ld."
              " Wir kehren zurueck, genau wie das Original bei einer Null-Verbindung."
              " OHNE das waere der Host hier abgestuerzt. ***",
              actor, dvt, dcls, (unsigned long long)caller, n);
        }
        return;
    }
    if (g_origActorChanPrep) g_origActorChanPrep(actor, conn);
}

// ===========================================================================
//  v136 -- GUARD THE GRAPH'S OWN PER-ACTOR LOOP
//
//  The 10-player session died after 12 minutes, and [ax] caught it exactly:
//      [ax] *** Ausnahme 0xC0000005 an base+0x13C3E51
//      [ax] Lesen an Adresse FFFFFFFFFFFFFFFF
//      #00 base+0x13C3E51  #01 0x13C310E  #02 0x13C7374  #03 sp_listen ...
//  0x13C3E51 is inside UReplicationGraph's per-actor function 0x13C3DB0:
//      0x13C3DDD  mov rsi, rdx                 ; rdx == the ACTOR
//      0x13C3DF9  test rdx, rdx / je 0x13C4344 ; null -> return 0
//      0x13C3E0A  test [rdx+0x1BB],1 / jne     ; being destroyed -> return 0
//      0x13C3E17  mov eax, [rdx + 0xC]         ; InternalIndex
//      0x13C3E51  test dword [rax+0x20], 0x30000000   <== fault
//  The actor pointer itself looked fine (rsi = 0x21686E10040). Its CONTENTS
//  were dead: the object index at +0xC was garbage, so the decoded
//  FUObjectItem landed in UTF-16 string data (rax = 0x006D00530053894B,
//  non-canonical -> GP fault, reported as -1).
//
//  So it is the same defect as every other host crash -- a dead actor inside
//  the graph's lists -- but it dies EARLIER in the chain than the channel call
//  the v117/v126 guard protects. That guard never got a chance.
//
//  The engine already returns 0 from this very function for a null actor and
//  for one being destroyed, at 0x13C4344 (`xor eax,eax; ret`). Returning 0 for
//  an actor whose vtable or class is not in the module is the same answer to
//  the same question, one step stricter -- no engine state written, and the
//  caller is built to handle it because it handles it twice already.
//
//  The prologue is eight pushes and one lea, all position independent:
//      40 55 | 53 | 56 | 57 | 41 54 | 41 55 | 41 56 | 41 57 | 48 8D 6C 24 F9
//  18 bytes to the next instruction boundary, so the trampoline copies 18 and
//  resumes at 0x13C3DC2. The function takes SIX arguments (it reads [rbp+0x6F]
//  and [rbp+0x77]), so all six are forwarded or the stack ones would shift.
//
//  Off with -nographguard.
// ===========================================================================
typedef void* (__fastcall* tGraphPerActor)(void*, void*, void*, void*, void*, void*);
static tGraphPerActor g_origGraphPerActor = nullptr;
static bool           g_graphGuard    = true;     // -nographguard

static void* __fastcall MyGraphPerActor(void* a, void* actor, void* c, void* d,
                                        void* e, void* f) {
    if (g_graphGuard && actor) {
        InterlockedIncrement(&g_graphGuardSeen);
        if (!ActorPtrSane(actor)) {
            const LONG n = InterlockedIncrement(&g_graphGuardHits);
            if (n <= 10 || (n % 500) == 0) {
                void* dvt  = SafePtr(actor);
                void* dcls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
                L("[gd] *** Toter Actor %p in der Actor-Schleife des Graphen abgefangen"
                  " (VTable %p, Klasse %p) -- Treffer #%ld. Wir geben 0 zurueck, genau wie"
                  " der Graph selbst bei einem Null-Actor. OHNE das waere der Host hier"
                  " abgestuerzt. ***", actor, dvt, dcls, n);
            }
            return nullptr;          // exactly the engine's own 0x13C4344 path
        }
    }
    return g_origGraphPerActor ? g_origGraphPerActor(a, actor, c, d, e, f) : nullptr;
}

// Diagnostics only: who takes the zone actor out of the graph?
typedef void (__fastcall* tRemoveNetActor)(void* drv, void* actor);
static tRemoveNetActor g_origRemoveNetActor = nullptr;
static void __fastcall MyRemoveNetworkActor(void* drv, void* actor) {
    InterlockedIncrement(&g_removeDrv);               // v116
    if (actor && g_blueZone && actor == g_blueZone) {
        static int n = 0;
        if (n++ < 10)
            L("[bz] RemoveNetworkActor fuer den Zonen-Actor %p (Treiber %p), Rueckkehradresse base+0x%llX",
              actor, drv, (unsigned long long)((uintptr_t)_ReturnAddress() - g_base));
    }
    if (g_origRemoveNetActor) g_origRemoveNetActor(drv, actor);
}

// Baut ein Trampolin: kopiert die ersten len Bytes (muessen
// positionsunabhaengig sein) und springt dahinter zurueck. Damit bleibt das
// Original aufrufbar, obwohl sein Anfang ueberschrieben wird.

// ===========================================================================
//  v104: PILLS (capsules/tablets)
//
//  Ported from PILLS-WORKING-HANDOFF.md and its source snapshot
//  (source/pills-working/offline-launcher/native/revival_host.cpp,
//  commit 42e7bc5). That implementation was verified in a live round: red,
//  green and blue spawn and upgrade the correct skill colour, white gives two
//  grades. It was built for the other project's local_solo launcher; the only
//  things changed here are the gating (their kLocalSolo/g_listening becomes our
//  listen-server readiness) and the use of sp_listen's own helpers.
//
//  ALL EIGHT referenced addresses were re-verified against this exact exe --
//  every one lands on a function start, so the handoff's RVAs are valid for
//  build 1.3.0.473797.
//
//  The three failures it fixes:
//    1. White says "max level" although skills are eligible. ItemAbility names
//       Renewal buff 221000337, which is absent from the active legacy buff
//       table, so validation cannot resolve the effect and rejects the item
//       before it ever checks skill caps.
//    2. Use succeeds but nothing upgrades. Consumption does NOT go through
//       FindRow -- it uses a separate FName->uint16 buff-index cache at
//       manager+0x428. A missing name yields index 0 and the native log says
//       "[AddBuffByIndex] There is no Buff 0".
//    3. Only white pills appear. The live recursive loot picker selects the
//       active "Alltablet" row, whose R/G/B weights are zero.
//
//  Nothing is invented: white/black alias onto the EXISTING legacy rows
//  220000104/220000105, and the loot fix copies the row/entry shells into our
//  own storage and changes only the weights, to the values from the shipped
//  TBL-RandomSpawnItem_AI_NoCR ALLtablet row. No source table memory is
//  written. Off with -nopills.
// ===========================================================================
namespace PILL {
    constexpr uintptr_t FindBuffRow      = 0x01B6F620;   // typed FindRow
    constexpr uintptr_t FindBuffIndex    = 0x011657C0;   // map lookup
    constexpr uintptr_t FindSpawnRow     = 0x01F77F50;   // RandomSpawn FindRow
    constexpr uintptr_t MakeFName        = 0x02A595B0;
    constexpr uintptr_t Singleton        = 0x01E9A020;
    constexpr uintptr_t GetBuffTable     = 0x01B76070;   // manager+0x68
    constexpr uintptr_t GetSpawnTable    = 0x01B941F0;   // manager+0x50
    constexpr uintptr_t AddPerkExpClass  = 0x02327E90;   // BHBAddPerkExp StaticClass
    constexpr uintptr_t ConsumeRet       = 0x01BE68FC;   // return addr in the apply path
    constexpr uintptr_t PickerRet        = 0x01F850FC;   // return addr in the recursive picker
}

typedef void* (__fastcall* tFindRowTyped)(void*, const FNameRaw*, const wchar_t*, bool);
typedef int32_t* (__fastcall* tFindIndex)(void*, int32_t*, const FNameRaw*);

static tFindRowTyped g_origFindBuffRow  = nullptr;
static tFindIndex    g_origFindBuffIndex = nullptr;
static tFindRowTyped g_origFindSpawnRow = nullptr;

static void*    g_capsuleBuffTable = nullptr;
static void*    g_capsuleIndexMap  = nullptr;
static FNameRaw g_capsuleBuffNames[2]   = {};   // 221000337 / 221000338
static FNameRaw g_capsuleLegacyNames[2] = {};   // 220000104 / 220000105
static void*    g_capsuleBuffRows[2]    = {};
static volatile LONG g_capsuleHits[2]   = {};

static bool PillsReady() { return g_pills && g_maskActive && g_drv && g_world; }

static bool SameRawName(const FNameRaw* a, const FNameRaw* b) {
    uint64_t x = 0, y = 0;
    return SafeCopy(a, &x, 8) && SafeCopy(b, &y, 8) && x == y;
}

// A row's FString field must equal an exact short literal ("All", "2", "3").
static bool RowStringEquals(uint8_t* row, size_t off, const wchar_t* expected) {
    FString v{};
    if (!SafeCopy(row + off, &v, sizeof(v))) return false;
    const size_t len = wcslen(expected) + 1;
    wchar_t copy[16] = {};
    return len <= 16 && v.Num == (int)len && v.Data
        && SafeCopy(v.Data, copy, len * sizeof(wchar_t)) && wcscmp(copy, expected) == 0;
}

// Runs once, from the driver tick, after the listen server is up. It VERIFIES
// before it enables anything: the Renewal rows really are missing, the legacy
// rows really exist, have the exact BHBAddPerkExp class, selector "All",
// counts "2"/"3", and a nonzero native cache index. Any mismatch disables the
// fallback rather than guessing.
static void PrepareCapsuleBuffFallback() {
    if (!PillsReady() || !g_origFindBuffRow) return;
    static bool attempted = false;
    if (attempted) return;
    attempted = true;
    __try {
        auto singleton    = (void*(__fastcall*)())(g_base + PILL::Singleton);
        void* instance    = singleton();
        void* manager     = instance ? SafePtr((uint8_t*)instance + 0x40) : nullptr;
        auto  getBuffTab  = (void*(__fastcall*)(void*))(g_base + PILL::GetBuffTable);
        void* active      = manager ? getBuffTab(manager) : nullptr;
        if (!active) { L("[pill-data] aktive Buff-Tabelle nicht verfuegbar -- Fallback bleibt aus"); return; }

        auto makeName = (void*(__fastcall*)(FNameRaw*, const wchar_t*, int))(g_base + PILL::MakeFName);
        makeName(&g_capsuleBuffNames[0], L"221000337", 1);
        makeName(&g_capsuleBuffNames[1], L"221000338", 1);

        bool missing[2] = {};
        for (int i = 0; i < 2; ++i)
            missing[i] = !g_origFindBuffRow(active, &g_capsuleBuffNames[i], L"sp_listen Kapsel-Pruefung", false);
        L("[pill-data] aktive Tabelle %p, weiss fehlt=%d, schwarz fehlt=%d", active, missing[0], missing[1]);
        if (!missing[0] && !missing[1]) { L("[pill-data] beide Renewal-Zeilen sind da -- nichts zu tun"); return; }
        if (!g_origFindBuffIndex) { L("[pill-data] Index-Hook fehlt -- Fallback bleibt aus"); return; }

        void* indexMap    = (uint8_t*)manager + 0x428;
        void* wantedClass = ((void*(__fastcall*)())(g_base + PILL::AddPerkExpClass))();
        void* rows[2] = {};
        for (int i = 0; i < 2; ++i) {
            if (!missing[i]) continue;
            makeName(&g_capsuleLegacyNames[i], i == 0 ? L"220000104" : L"220000105", 1);
            auto* row = (uint8_t*)g_origFindBuffRow(active, &g_capsuleLegacyNames[i],
                                                    L"sp_listen Legacy-Kapsel-Pruefung", false);
            int32_t originalIndex = -1, legacyIndex = -1;
            g_origFindBuffIndex(indexMap, &originalIndex, &g_capsuleBuffNames[i]);
            g_origFindBuffIndex(indexMap, &legacyIndex,   &g_capsuleLegacyNames[i]);
            auto* entries = (uint8_t*)SafePtr(indexMap);
            uint16_t buffIndex = 0;
            if (entries && legacyIndex >= 0 && legacyIndex < SafeI32((uint8_t*)indexMap + 8))
                SafeCopy(entries + (size_t)legacyIndex * 0x18 + 0xC, &buffIndex, sizeof(buffIndex));
            if (!row || SafePtr(row + 0x18) != wantedClass
             || !RowStringEquals(row, 0x178, L"All")
             || !RowStringEquals(row, 0x188, i == 0 ? L"2" : L"3")
             || originalIndex != -1 || !buffIndex) {
                L("[pill-data] Legacy-Kapsel %d faellt durch die Pruefung (originalIndex=%d,"
                  " legacyIndex=%d, buffIndex=%u) -- Fallback bleibt aus", i, originalIndex, legacyIndex, buffIndex);
                return;
            }
            rows[i] = row;
            L("[pill-data] Legacy-Kapsel %s -> %s, buffIndex=%u, Stufen=%d geprueft",
              i == 0 ? "221000337" : "221000338", i == 0 ? "220000104" : "220000105", buffIndex, i + 2);
        }
        g_capsuleBuffRows[0] = rows[0];
        g_capsuleBuffRows[1] = rows[1];
        g_capsuleIndexMap    = indexMap;
        g_capsuleBuffTable   = active;
        L("[pill-data] Legacy-Kapsel-Kompatibilitaet steht (weiss=%d schwarz=%d);"
          " native Verbrauchslogik und Grenzen bleiben unangetastet",
          rows[0] != nullptr, rows[1] != nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_capsuleBuffTable = nullptr;
        L("[pill-data] Vorbereitung hat geworfen -- Kapsel-Fallback bleibt aus");
    }
}

// Validation side: only the two missing names, only on the audited table, and
// only when the native lookup itself came back empty.
static void* __fastcall MyFindBuffRow(void* table, const FNameRaw* name, const wchar_t* ctx, bool warn) {
    int capsule = -1;
    if (PillsReady() && table == g_capsuleBuffTable && name) {
        uint64_t key = 0;
        if (SafeCopy(name, &key, sizeof(key))) {
            for (int i = 0; i < 2; ++i) {
                uint64_t wanted = 0;
                memcpy(&wanted, &g_capsuleBuffNames[i], sizeof(wanted));
                if (key == wanted && g_capsuleBuffRows[i]) { capsule = i; break; }
            }
        }
    }
    void* result = g_origFindBuffRow(table, name, ctx, capsule < 0 && warn);
    if (result || capsule < 0) return result;
    if (InterlockedIncrement(&g_capsuleHits[capsule]) <= 3)
        L("[pill-data] fehlender Buff %s aus der aktiven Legacy-Zeile aufgeloest (Abfrage %ld)",
          capsule == 0 ? "221000337" : "221000338", g_capsuleHits[capsule]);
    return g_capsuleBuffRows[capsule];
}

// Consumption side. ONLY the exact apply call is allowed to alias; every other
// map, caller and successful native lookup keeps its own result.
static int32_t* __fastcall MyFindBuffIndex(void* map, int32_t* out, const FNameRaw* name) {
    const uintptr_t caller = (uintptr_t)_ReturnAddress() - g_base;
    int32_t* result = g_origFindBuffIndex(map, out, name);
    if (!PillsReady() || caller != PILL::ConsumeRet || map != g_capsuleIndexMap
     || !out || *out != -1 || !name) return result;
    uint64_t key = 0;
    if (!SafeCopy(name, &key, sizeof(key))) return result;
    for (int i = 0; i < 2; ++i) {
        uint64_t wanted = 0;
        memcpy(&wanted, &g_capsuleBuffNames[i], sizeof(wanted));
        if (key != wanted || !g_capsuleBuffRows[i]) continue;
        result = g_origFindBuffIndex(map, out, &g_capsuleLegacyNames[i]);
        L("[pill-consume] Legacy-Alias %s -> %s, Cache-Platz=%d, Stufen=%d",
          i == 0 ? "221000337" : "221000338", i == 0 ? "220000104" : "220000105", *out, i + 2);
        return result;
    }
    return result;
}

// ------------------------------------------------------- loot: coloured pills
static const wchar_t* kCapsulePools[] = { L"Alltablet", L"pickonetablet", L"AIAlltablet",
                                          L"RSI_BaseSpawnPackage_Tablet_t1" };
static const wchar_t* kCapsuleItems[] = { L"Tablet_R", L"Tablet_G", L"Tablet_B",
                                          L"Tablet_White", L"Tablet_Black", L"None" };
static FNameRaw g_spawnPoolNames[4] = {}, g_spawnItemNames[6] = {};
static bool     g_spawnAuditReady = false;
static volatile LONG g_spawnPoolHits[4] = {};
alignas(16) static uint8_t g_capsuleRow[0x20] = {};
alignas(16) static uint8_t g_capsuleEntries[6 * 0x80] = {};
static void* g_capsuleLootTable = nullptr;
static void* g_capsuleSourceRow = nullptr;

static void LogCapsulePool(void* table, void* row, int pool, uintptr_t caller, const char* origin) {
    __try {
        L("[loot-pool] %s Pool=%ls Tabelle=%p Zeile=%p Aufrufer=0x%llX",
          origin, kCapsulePools[pool], table, row, (unsigned long long)caller);
        if (!row) return;
        auto* entries = (uint8_t*)SafePtr((uint8_t*)row + 0x10);
        const int count = SafeI32((uint8_t*)row + 0x18);
        uint8_t type = 0; SafeCopy((uint8_t*)row + 8, &type, 1);
        if (!entries || count < 1 || count > 64) { L("[loot-pool] Eintragsfeld ungueltig, count=%d", count); return; }
        for (int i = 0; i < count; ++i) {
            auto* e = entries + (size_t)i * 0x80;
            float w = 0; SafeCopy(e, &w, sizeof(w));
            const wchar_t* label = L"unbekannt";
            for (int j = 0; j < 6; ++j)
                if (SameRawName((FNameRaw*)(e + 0x10), &g_spawnItemNames[j])) { label = kCapsuleItems[j]; break; }
            L("[loot-pool]   Typ=%u Eintrag=%d Item=%ls Gewicht=%.6f Buendel=%d",
              type, i, label, w, SafeI32(e + 4));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { L("[loot-pool] Diagnose-Lesen fehlgeschlagen -- Loot bleibt nativ"); }
}

// Build our own copy of the Alltablet row with the shipped AI_NoCR weights.
// The row must match the observed zero-RGB shape EXACTLY, or nothing happens.
static void PrepareColouredCapsulePool(void* table, void* row) {
    static const float observed[6] = { 0.f, 0.f, 0.f, 0.13f, 0.02f, 0.85f };
    static const float restored[6] = { 0.14f, 0.14f, 0.14f, 0.04f, 0.04f, 0.50f };
    if (!row || !g_capsuleBuffTable || SafeI32((uint8_t*)row + 0x18) != 6) return;
    uint8_t type = 0;
    if (!SafeCopy((uint8_t*)row + 8, &type, 1) || type != 2) return;
    auto* entries = (uint8_t*)SafePtr((uint8_t*)row + 0x10);
    if (!entries) return;
    for (int i = 0; i < 6; ++i) {
        auto* e = entries + i * 0x80;
        float w = -1.f;
        if (!SameRawName((FNameRaw*)(e + 0x10), &g_spawnItemNames[i])
         || !SafeCopy(e, &w, sizeof(w)) || w != observed[i] || SafeI32(e + 4) != 1) {
            L("[loot-repair] Alltablet weicht vom geprueften Null-RGB-Pool ab -- unveraendert"); return;
        }
    }
    if (!SafeCopy(row, g_capsuleRow, sizeof(g_capsuleRow))
     || !SafeCopy(entries, g_capsuleEntries, sizeof(g_capsuleEntries))) return;
    for (int i = 0; i < 6; ++i) memcpy(g_capsuleEntries + i * 0x80, &restored[i], sizeof(float));
    void* ownEntries = g_capsuleEntries;
    int capacity = 6;
    memcpy(g_capsuleRow + 0x10, &ownEntries, sizeof(ownEntries));
    memcpy(g_capsuleRow + 0x1C, &capacity, sizeof(capacity));
    g_capsuleSourceRow = row;
    g_capsuleLootTable = table;
    L("[loot-repair] Alltablet-Kopie bereit: R/G/B je .14, weiss .04, schwarz .04, nichts .50");
}

static void PrepareCapsuleLootAudit() {
    if (!PillsReady() || !g_origFindSpawnRow || g_spawnAuditReady) return;
    __try {
        auto makeName = (void*(__fastcall*)(FNameRaw*, const wchar_t*, int))(g_base + PILL::MakeFName);
        for (int i = 0; i < 4; ++i) makeName(&g_spawnPoolNames[i], kCapsulePools[i], 1);
        for (int i = 0; i < 6; ++i) makeName(&g_spawnItemNames[i], kCapsuleItems[i], 1);
        void* instance = ((void*(__fastcall*)())(g_base + PILL::Singleton))();
        void* manager  = instance ? SafePtr((uint8_t*)instance + 0x40) : nullptr;
        if (!manager) return;
        void* table = ((void*(__fastcall*)(void*))(g_base + PILL::GetSpawnTable))(manager);
        if (!table) return;
        g_spawnAuditReady = true;
        for (int i = 0; i < 4; ++i) {
            void* row = g_origFindSpawnRow(table, &g_spawnPoolNames[i], L"sp_listen Kapsel-Loot-Pruefung", false);
            LogCapsulePool(table, row, i, 0, "Bestandsaufnahme");
            if (i == 0) PrepareColouredCapsulePool(table, row);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { L("[loot-pool] Vorbereitung fehlgeschlagen -- Loot bleibt nativ"); }
}

static void* __fastcall MyFindSpawnRow(void* table, const FNameRaw* name, const wchar_t* ctx, bool warn) {
    const uintptr_t caller = (uintptr_t)_ReturnAddress() - g_base;
    void* row = g_origFindSpawnRow(table, name, ctx, warn);
    if (PillsReady() && g_spawnAuditReady && name) {
        for (int i = 0; i < 4; ++i) {
            if (!SameRawName(name, &g_spawnPoolNames[i])) continue;
            const bool replaced = (i == 0) && caller == PILL::PickerRet
                               && table == g_capsuleLootTable && row && row == g_capsuleSourceRow;
            if (replaced) row = g_capsuleRow;
            if (InterlockedIncrement(&g_spawnPoolHits[i]) <= 3) {
                if (replaced) L("[loot-repair] der native Picker benutzt jetzt die AI_NoCR-Gewichte");
                LogCapsulePool(table, row, i, caller,
                    replaced ? "Picker-ersetzt" : (caller == PILL::PickerRet ? "Picker" : "nativ"));
            }
            break;
        }
    }
    return row;
}

static void PillTick() { PrepareCapsuleBuffFallback(); PrepareCapsuleLootAudit(); }


// ===========================================================================
//  v110: the 0x40 crash -- a guard on the null map, taking the engine's OWN
//        not-found path.
//
//  The 13.09. crash log gave the whole thing away. Resolved against that run's
//  image base (0x7FF62BE10000):
//
//      UActorChannel bunch (0x419D3D0 -> 0x41D9E70 -> 0x41DAEB0)
//        -> sp_listen!MyProcessEvent      (pass-through only)
//        -> AActor::ProcessEvent 0x3F98AF0
//        -> Blueprint VM         0x2B2BFD0
//        -> UNetDriver           0x43D7960
//        -> replication graph    0x13B96A0 -> 0x13AB3B7 -> 0x1394820 -> 0x13BABE0
//        -> 0x11E8200            mov eax,[rcx+8]      <== EXCEPTION_ACCESS_VIOLATION
//
//  and the call site explains the address exactly:
//
//      0x13BADC0  lea  rcx, [rdi + 0x38]     ; rdi is NULL
//      0x13BADC4  call 0x11E8200
//      0x11E820A  mov  eax, [rcx + 8]        ; 0x38 + 8 = 0x40
//
//  0x11E8200 is a generic hash-map lookup (the 0x9E3779B9 golden-ratio constant
//  is UE4's TMap hash). The graph hands it a map embedded at +0x38 of an object
//  that is NULL -- not corrupt, null. More players means more of those objects,
//  which is why 16 players crash constantly and 2 players rarely did.
//
//  WHY THIS GUARD IS SAFE, and not a patch-over: the caller already handles
//  "not found", and the function's own not-found path is
//
//      0x11E82E6  mov dword ptr [r11], 0xFFFFFFFF   ; *out = -1
//      0x11E82ED  mov rax, r11                      ; return out
//      0x11E82FA  ret
//
//  with r11 = rdx (set at 0x11E8210). So for a null map we reproduce exactly
//  that: write -1 into the caller's out slot and return it. The caller then
//  takes its existing branch at 0x13BADD4. No invented behaviour, no engine
//  state written, and a correct caller can never reach this path because a
//  correct caller never passes a null map.
//
//  The function is hot and generic, so the guard is one compare before the
//  original is called. The caller's return address is logged, so we learn who
//  actually passes null -- 0x13BADC9 is the one seen so far.
//
//  Off with -nomapguard.
// ===========================================================================
// ===========================================================================
//  v113 -- Absturz-Melder (crash reporter)
//
//  WHY. The engine only writes a callstack into BravoHotelGame.log when the
//  game was started with -log, and the host is not always started that way.
//  The 13.09. 22:12 crash produced exactly one usable line:
//      Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address
//      0xffffffff
//  -- no frames at all, so it could not be told apart from the known 0x40
//  fault. From here on sp_listen records that itself, into its own log, for
//  every run.
//
//  HOW. A first-chance vectored handler. It only READS: it logs and then
//  always returns EXCEPTION_CONTINUE_SEARCH, so the engine's own handling is
//  bit-for-bit unchanged and a handled exception stays handled. Frames come
//  from the module's own unwind data (RtlLookupFunctionEntry +
//  RtlVirtualUnwind), which is what the OS itself uses, so the chain is exact
//  rather than a stack scan.
//
//  Filters, because a first-chance handler sees EVERY exception in the
//  process: only hard faults, and only when the faulting instruction sits in
//  the game exe. sp_listen's own SafeCopy probes memory on purpose and faults
//  constantly -- those are skipped. A fault outside both modules is logged at
//  most twice, so nothing can flood the file, and the whole reporter stops
//  after six reports.
//
//  Off with -nocrashlog.
// ===========================================================================
static bool          g_crashLog   = true;      // -nocrashlog
static PVOID         g_vehHandle  = nullptr;
uintptr_t            g_selfBase   = 0;   // non-static since v150
static uintptr_t     g_selfEnd    = 0;
static uintptr_t     g_exeEnd     = 0;
static volatile LONG g_crashCount = 0;

// End of a loaded module's image, read out of its own PE header.
static uintptr_t ImageEndOf(uintptr_t mod) {
    if (!mod) return 0;
    __try {
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)mod;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(mod + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        return mod + nt->OptionalHeader.SizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Print an address the way the rest of these documents do: as an RVA.
static void NameAddr(uintptr_t a, char* buf, size_t n) {
    if (g_base && g_exeEnd && a >= g_base && a < g_exeEnd)
        sprintf_s(buf, n, "base+0x%llX", (unsigned long long)(a - g_base));
    else if (g_selfBase && g_selfEnd && a >= g_selfBase && a < g_selfEnd)
        sprintf_s(buf, n, "sp_listen+0x%llX", (unsigned long long)(a - g_selfBase));
    else
        sprintf_s(buf, n, "%p", (void*)a);
}

static LONG CALLBACK MyVeh(EXCEPTION_POINTERS* ep) {
    if (!g_crashLog || !ep || !ep->ExceptionRecord || !ep->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;

    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    // Hard faults only. C++ exceptions (0xE06D7363), debugger notifications and
    // the rest are normal traffic and must never reach the log.
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_PRIV_INSTRUCTION  && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_STACK_OVERFLOW    && code != EXCEPTION_IN_PAGE_ERROR)
        return EXCEPTION_CONTINUE_SEARCH;

    const uintptr_t rip = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    // sp_listen's own probes: SafeCopy faults on purpose, constantly.
    if (g_selfBase && rip >= g_selfBase && rip < g_selfEnd)
        return EXCEPTION_CONTINUE_SEARCH;

    // Walk the frames FIRST -- the decision below needs them. Same unwind data
    // the OS uses, so the chain is exact rather than a stack scan.
    uintptr_t frame[40];
    int frames = 0;
    __try {
        CONTEXT w = *ep->ContextRecord;
        for (; frames < 40 && w.Rip; ++frames) {
            frame[frames] = (uintptr_t)w.Rip;
            DWORD64 imgBase = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(w.Rip, &imgBase, nullptr);
            if (!fn) {                       // leaf: return address sits at rsp
                if (!w.Rsp) { ++frames; break; }
                w.Rip  = *(DWORD64*)w.Rsp;
                w.Rsp += 8;
                continue;
            }
            PVOID   handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imgBase, w.Rip, fn, &w,
                             &handlerData, &establisher, nullptr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { /* keep whatever we got */ }

    // v114: if sp_listen called the faulting function DIRECTLY, this is one of
    // our own instrumented engine calls and it already sits inside a __try --
    // the game survives it. The 13.09. 21:27 host crash had sp_listen eleven
    // frames down, so only the two frames right above the fault are checked.
    for (int i = 1; i < frames && i <= 2; ++i)
        if (g_selfBase && frame[i] >= g_selfBase && frame[i] < g_selfEnd)
            return EXCEPTION_CONTINUE_SEARCH;

    // v114: budget per fault site instead of one global count. A recurring
    // handled fault used to burn the whole budget before the real crash, which
    // is exactly what happened on the 40-bot round.
    {
        static uintptr_t site[16] = {};
        static int       siteHits[16] = {};
        static int       siteN = 0;
        static CRITICAL_SECTION cs; static bool csReady = false;
        if (!csReady) { InitializeCriticalSection(&cs); csReady = true; }
        EnterCriticalSection(&cs);
        int idx = -1;
        for (int i = 0; i < siteN; ++i) if (site[i] == rip) { idx = i; break; }
        if (idx < 0 && siteN < 16) { idx = siteN++; site[idx] = rip; siteHits[idx] = 0; }
        const bool skip = (idx < 0) || (++siteHits[idx] > 2);
        LeaveCriticalSection(&cs);
        if (skip) return EXCEPTION_CONTINUE_SEARCH;
    }
    if (InterlockedIncrement(&g_crashCount) > 40)
        return EXCEPTION_CONTINUE_SEARCH;

    char nm[80];
    NameAddr(rip, nm, sizeof(nm));
    L("[ax] *** Ausnahme 0x%08lX an %s (Thread %lu)",
      (unsigned long)code, nm, GetCurrentThreadId());

    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) &&
        ep->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
        L("[ax] %s an Adresse %p",
          op == 0 ? "Lesen" : (op == 1 ? "Schreiben" : "Ausfuehren"),
          (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    }

    const CONTEXT* c = ep->ContextRecord;
    L("[ax] rax=%016llX rbx=%016llX rcx=%016llX rdx=%016llX",
      (unsigned long long)c->Rax, (unsigned long long)c->Rbx,
      (unsigned long long)c->Rcx, (unsigned long long)c->Rdx);
    L("[ax] rsi=%016llX rdi=%016llX rbp=%016llX rsp=%016llX",
      (unsigned long long)c->Rsi, (unsigned long long)c->Rdi,
      (unsigned long long)c->Rbp, (unsigned long long)c->Rsp);
    L("[ax] r8 =%016llX r9 =%016llX r10=%016llX r11=%016llX",
      (unsigned long long)c->R8,  (unsigned long long)c->R9,
      (unsigned long long)c->R10, (unsigned long long)c->R11);
    L("[ax] r12=%016llX r13=%016llX r14=%016llX r15=%016llX",
      (unsigned long long)c->R12, (unsigned long long)c->R13,
      (unsigned long long)c->R14, (unsigned long long)c->R15);

    for (int i = 0; i < frames; ++i) {
        NameAddr(frame[i], nm, sizeof(nm));
        L("[ax]   #%02d  %s", i, nm);
    }
    L("[ax] --- Ende des Berichts ---");
    return EXCEPTION_CONTINUE_SEARCH;   // the engine still handles it as before
}


typedef int32_t* (__fastcall* tMapFind)(void* map, int32_t* out, void* key);
static tMapFind      g_origMapFind = nullptr;
static bool          g_mapGuard    = true;      // -nomapguard
static volatile LONG g_mapGuardHits = 0;
static uintptr_t     g_mapGuardCallers[8] = {};
static int           g_mapGuardCallerN = 0;

static int32_t* __fastcall MyMapFind(void* map, int32_t* out, void* key) {
    // A null owning object turns "lea rcx,[obj+0x38]" into rcx == 0x38, so the
    // whole bogus range is well under one page.
    if (g_mapGuard && (uintptr_t)map < 0x1000) {
        const uintptr_t caller = (uintptr_t)_ReturnAddress() - g_base;
        LONG n = InterlockedIncrement(&g_mapGuardHits);
        bool fresh = true;
        for (int i = 0; i < g_mapGuardCallerN; ++i)
            if (g_mapGuardCallers[i] == caller) { fresh = false; break; }
        if (fresh && g_mapGuardCallerN < 8) g_mapGuardCallers[g_mapGuardCallerN++] = caller;
        if (fresh || n <= 10 || (n % 500) == 0)
            L("[mg] *** Null-Map abgefangen (rcx=%p) -- Aufrufer base+0x%llX, Treffer #%ld."
              " Wir liefern -1 zurueck, genau wie der Nicht-gefunden-Zweig des Originals."
              " OHNE das haette es hier gekracht (AV beim Lesen von 0x40). ***",
              map, (unsigned long long)caller, n);
        if (out) *out = -1;
        return out;
    }
    return g_origMapFind(map, out, key);
}
static void* MakeTrampoline(uintptr_t target, const uint8_t* expect, size_t len) {
    if (memcmp((void*)target, expect, len) != 0) return nullptr;
    auto* st = (uint8_t*)VirtualAlloc(nullptr, 0x100, MEM_COMMIT|MEM_RESERVE,
                                      PAGE_EXECUTE_READWRITE);
    if (!st) return nullptr;
    memset(st, 0xCC, 0x100);
    memcpy(st, (void*)target, len);
    st[len]=0xFF; st[len+1]=0x25; *(uint32_t*)(st+len+2)=0;
    uint64_t back = target + len;
    memcpy(st+len+6, &back, 8);
    return st;
}

// Leitet eine Funktion per 14-Byte-Absolutsprung um.
static bool InstallJmp(const char* what, uintptr_t rva, const void* dest,
                       const uint8_t* expect) {
    uint8_t* p = (uint8_t*)(g_base + rva);
    L("[i] %s @base+0x%llX: %02X %02X %02X %02X %02X %02X %02X %02X",
      what, (unsigned long long)rva, p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);
    uint8_t patch[14];
    patch[0]=0xFF; patch[1]=0x25; *(uint32_t*)(patch+2)=0;
    memcpy(patch+6, &dest, 8);
    if (memcmp(p, expect, 14) != 0) {
        if (p[0]==0xFF && p[1]==0x25) { L("[i] %s bereits umgeleitet", what); return true; }
        L("[!] %s: unerwartete Bytes -> NICHT gepatcht", what);
        return false;
    }
    if (!Poke(p, patch, sizeof(patch))) { L("[!] %s: Patch fehlgeschlagen", what); return false; }
    L("[+] %s -> %p", what, dest);
    return true;
}

// -------------------------------------------- Ersatz fuer World->Listen
// v72: DEFAULT IS "none" AGAIN.
//
// v71 installed URealReplicationGraph and the host died 21 seconds in with:
//
//   LowLevelFatalError [File: ...\Plugins\OptimizationHelpers\Source\
//   ReplicationOptimizer\Public\RealReplicationGraph.h] [Line: 82]
//   Pure virtual not implemented ()
//
// So URealReplicationGraph is ABSTRACT. It declares at least one pure virtual
// (header line 82) that only the game's own subclass -- BravoHotelReplication-
// Graph, from the ReplicationOptimizer plugin, which a client build does not
// carry -- implements. Constructing it directly and letting InitListen drive
// it walks straight into the pure-virtual handler. The class install itself
// worked exactly as intended ("[rg] *** ReplicationDriverClass(+0x180) <-
// URealReplicationGraph ***"); the class is simply not usable on its own.
//
// Worth keeping, because it is new and it is solid: the game's graph lives in
// a plugin called OptimizationHelpers/ReplicationOptimizer, URealReplication-
// Graph is its abstract base, and the gather we have been watching do nothing
// is the stock base-class one. If we ever want the Real graph, the missing
// piece is identifying which vtable slots hold the pure-virtual stub and
// supplying implementations -- not simply selecting the class.
static const char* g_graphChoice = "none";       // -graphreal / -graphbasic

static void InstallRepGraphClass(void* drv) {
    if (!drv) return;
    if (_stricmp(g_graphChoice, "none") == 0) {
        L("  [rg] -graphnone: ReplicationDriverClass wird nicht angefasst");
        return;
    }
    void* existing = SafePtr((uint8_t*)drv + OFF::UNetDriver_RepDriverClass);
    if (existing) {
        L("  [rg] ReplicationDriverClass(+0x180) ist schon %p -> unveraendert", existing);
        return;
    }
    const uintptr_t rva = (_stricmp(g_graphChoice, "basic") == 0)
                            ? RVA::BasicRepGraph_StaticClass
                            : RVA::RealRepGraph_StaticClass;
    typedef void* (__fastcall* tStaticClass)();
    void* cls = nullptr;
    __try { cls = ((tStaticClass)(g_base + rva))(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { cls = nullptr; }
    if (!cls || !InModule(SafePtr(cls))) {
        L("  [rg] StaticClass() fuer '%s' lieferte %p -> nicht gesetzt", g_graphChoice, cls);
        return;
    }
    if (Poke((uint8_t*)drv + OFF::UNetDriver_RepDriverClass, &cls, 8))
        L("  [rg] *** ReplicationDriverClass(+0x180) <- %sReplicationGraph  UClass=%p ***",
          (_stricmp(g_graphChoice, "basic") == 0) ? "UBasic" : "UReal", cls);
}

static bool __fastcall MyListen(void* world, void* url) {
    L(""); L("=== World->Listen(URL)   World=%p  URL=%p ===", world, url);
    void* engine = g_capEngine ? *(void**)(g_capEngine + 0x80) : nullptr;
    if (!engine) { L("  [!] GEngine fehlt -> nichts getan."); return false; }
    L("  GEngine = %p", engine);

    // 1) FName "GameNetDriver" aus NetDriverDefinitions[0].DefName (12 Byte, verbatim)
    void*  defsData = SafePtr((uint8_t*)engine + OFF::UEngine_NetDriverDefs);
    int32_t defsNum = SafeI32((uint8_t*)engine + OFF::UEngine_NetDriverDefs + 8);
    L("  NetDriverDefinitions: Data=%p Num=%d", defsData, defsNum);
    if (!defsData || defsNum < 1) { L("  [!] keine Definitionen"); return false; }
    FNameRaw defName{};
    if (!SafeCopy(defsData, &defName, sizeof(defName))) { L("  [!] DefName unlesbar"); return false; }
    L("  DefName[0] = {Comp=%u, Disp=%u, Num=%u}", defName.Comparison, defName.Display, defName.Number);

    // 2) NetDriver anlegen — ueber den UWorld*-Thunk, FNames als ZEIGER
    auto CreateW = (tCreateNamedNetDriverW)(g_base + RVA::CreateNamedNetDrv_World);
    FNameRaw nameArg = defName, defArg = defName;
    bool created = CreateW(engine, world, &nameArg, &defArg);
    L("  CreateNamedNetDriver(UWorld*) -> %s", created ? "OK" : "FEHLER");

    // 3) WorldContext holen und Driver aus ActiveNetDrivers fischen
    auto GetCtx = (tGetWorldContext)(g_base + RVA::GetWorldContextFromW);
    void* ctx = GetCtx(engine, world);
    L("  FWorldContext = %p  (ctx->World=%p)", ctx, ctx ? SafePtr((uint8_t*)ctx + OFF::Ctx_World) : nullptr);
    void* drv = nullptr;
    if (ctx) {
        void*  aData = SafePtr((uint8_t*)ctx + OFF::Ctx_ActiveNetDrivers);
        int32_t aNum = SafeI32((uint8_t*)ctx + OFF::Ctx_ActiveNetDrivers + 8);
        L("  ActiveNetDrivers: Data=%p Num=%d", aData, aNum);
        for (int i = 0; aData && i < aNum && i < 64; ++i) {
            void* d = SafePtr((uint8_t*)aData + i*OFF::NamedNetDriverStride);
            if (!d) continue;
            FNameRaw dn{}; SafeCopy((uint8_t*)d + OFF::UNetDriver_Name, &dn, sizeof(dn));
            L("    [%d] Driver=%p Name={%u,%u,%u}", i, d, dn.Comparison, dn.Display, dn.Number);
            if (dn.Comparison == defName.Comparison) drv = d;
        }
        if (!drv && aData && aNum > 0)
            drv = SafePtr((uint8_t*)aData + (aNum-1)*OFF::NamedNetDriverStride);
    }
    if (!drv) { L("  [!] kein NetDriver gefunden -> Abbruch."); return false; }
    L("  NetDriver = %p", drv);

    // 4) Verknuepfungen setzen — GENAU wie UWorld::Listen es tut.
    //    World->NetDriver ist eine simple Zuweisung (Offset 0x58 verifiziert:
    //    UWorld::NotifyAcceptingConnection liest [rcx+0x10] mit rcx=World+0x48).
    Poke((uint8_t*)world + OFF::UWorld_NetDriver, &drv, 8);
    //    ABER: Driver->World DARF NICHT direkt gesetzt werden! UNetDriver::SetWorld
    //    macht zusaetzlich das Entscheidende:
    //      [drv+0x148]=World, [drv+0x150]=WorldPackage, [drv+0x218]=World+0x48 (Notify)
    //      und registriert den Treiber an den vier World-Delegates
    //      +0x418 TickDispatch, +0x430 PostTickDispatch, +0x448 TickFlush, +0x460 PostTickFlush
    //    Ohne diese Registrierung wird TickDispatch NIE gerufen -> eingehende Pakete
    //    bleiben ungelesen in der Queue liegen (genau das war der Fehler bis v14).
    auto SetWorld = (tSetWorld)(g_base + RVA::UNetDriver_SetWorld);
    SetWorld(drv, world);
    L("  SetWorld() gerufen -> Driver->World=%p  WorldPackage=%p  Notify=%p",
      SafePtr((uint8_t*)drv + OFF::UNetDriver_World),
      SafePtr((uint8_t*)drv + 0x150), SafePtr((uint8_t*)drv + 0x218));
    L("  World->NetDriver(+0x58)=%p", SafePtr((uint8_t*)world + OFF::UWorld_NetDriver));

    //    UND der Schritt, der bis v17 gefehlt hat:
    //    UWorld haelt pro FLevelCollection einen eigenen NetDriver.
    //    UWorld::SetActiveLevelCollection (RVA 0x4743C90) kopiert diesen jeden
    //    Frame nach World->NetDriver:
    //        add rcx,[rbx+0x200] / imul rcx,rax,0x88 / mov [rbx+0x58],[rcx+0x10]
    //    Steht dort null, wird unser Treiber laufend wieder ausgetragen — genau
    //    das hat der Watchdog gesehen. Original-UWorld::Listen setzt den Driver
    //    deshalb zusaetzlich in den Collections (DynamicSourceLevels + StaticLevels).
    void*  lcData = SafePtr((uint8_t*)world + OFF::UWorld_LevelCollections);
    int32_t lcNum = SafeI32((uint8_t*)world + OFF::UWorld_LevelCollections + 8);
    L("  LevelCollections: Data=%p Num=%d", lcData, lcNum);
    for (int32_t i = 0; lcData && i < lcNum && i < 8; ++i) {
        uint8_t* col = (uint8_t*)lcData + (size_t)i * OFF::LevelCollectionStride;
        void* before = SafePtr(col + OFF::LevelCollection_NetDriver);
        Poke(col + OFF::LevelCollection_NetDriver, &drv, 8);
        L("    Collection[%d] NetDriver: %p -> %p", i, before,
          SafePtr(col + OFF::LevelCollection_NetDriver));
    }

    // 5) FNetworkNotify-Subobjekt bestimmen
    if (!g_notifyOff) g_notifyOff = FindNotifyOffset(world);
    void* notify = g_notifyOff ? (void*)((uint8_t*)world + g_notifyOff) : world;
    if (!g_notifyOff) L("  [!] FNetworkNotify nicht gefunden -> nehme World (kann schiefgehen)");

    // 5b) v71: give UNetDriver::InitBase a replication graph class it can
    //     actually construct. It reads UNetDriver+0x180, and the class the
    //     game configures (BravoHotelReplicationGraph) lives in a server
    //     module a client build does not carry. URealReplicationGraph is in
    //     this binary and overrides ServerReplicateActors with the gather the
    //     base class does not have. Only written when the field is empty, so
    //     a working Engine.ini still wins.
    InstallRepGraphClass(drv);

    // 6) InitListen — Slot zur Laufzeit in der VTable suchen.
    //    ACHTUNG: 0x288 ist InitBase (das ruft InitListen INTERN auf),
    //    UIpNetDriver::InitListen selbst liegt bei Byte-Offset 0x688.
    //    Statt das hart zu setzen, suchen wir die bekannte Funktionsadresse.
    void* vt = SafePtr(drv);
    if (!vt) { L("  [!] Driver-VTable unlesbar"); return false; }
    L("  Driver-VTable = %p (base+0x%llX)", vt, (unsigned long long)((uintptr_t)vt - g_base));

    // Die Treiberklasse UEBERSCHREIBT InitListen. In der VTable des Spiels
    // (base+0x581B170, 146 Eintraege) liegt der Override bei Index 83 = 0x298
    // und ruft intern UIpNetDriver::InitListen (base+0x132C420).
    const uintptr_t cand[2] = { g_base + RVA::DriverInitListen_Override,
                                g_base + RVA::IpNetDriver_InitListen };
    tInitListen init = nullptr;
    for (int k = 0; k < 200 && !init; ++k) {
        uintptr_t v = (uintptr_t)SafePtr((uint8_t*)vt + k*8);
        for (uintptr_t c : cand)
            if (v == c) { init = (tInitListen)v;
                L("  InitListen gefunden: VTable-Offset 0x%X (Index %d) -> base+0x%llX",
                  k*8, k, (unsigned long long)(v - g_base)); break; }
    }
    if (!init) {
        uintptr_t v = (uintptr_t)SafePtr((uint8_t*)vt + OFF::InitListen_VT);
        L("  [i] nicht per Adresse gefunden -> Fallback Offset 0x%llX -> %p",
          (unsigned long long)OFF::InitListen_VT, (void*)v);
        init = (tInitListen)v;
    }
    // SICHERHEIT: niemals etwas aufrufen, das nicht im Modul liegt.
    if (!init || !InModule((void*)init)) {
        L("  [!] InitListen-Ziel %p liegt NICHT im Modul -> Abbruch statt Absturz.", (void*)init);
        return false;
    }
    // v58: the replication graph snapshots per-class settings inside
    // InitListen -- make the zone class always relevant BEFORE that.
    PrepareZoneClassBeforeListen(world);
    L("  InitListen @%p (notify=%p) ...", (void*)init, notify);
    FString err{ nullptr,0,0 };
    bool ok = init(drv, notify, url, false, &err);
    L("  InitListen -> %s", ok ? "OK" : "FEHLER");
    if (err.Data && err.Num > 0) L("  Error: %.200ws", err.Data);
    if (!ok) return false;
    L("  *** LISTEN-SERVER AKTIV ***");
    // Watchdog starten: v15 ist beim Verbindungsversuch in
    // UWorld::NotifyAcceptingConnection gecrasht (RVA 0x4739F61,
    // "cmp [rax+0x90],0" mit rax = World->NetDriver == null).
    // Irgendetwas nullt World->NetDriver nach dem Listen. Der Watchdog
    // protokolliert das und stellt es wieder her.
    g_world = world; g_drv = drv;

    // --- Standalone-Maske scharf schalten ---------------------------------
    // Reihenfolge ist wichtig: erst die beiden Notify-Eintraege umhaengen,
    // DANN den Treiber verstecken. Andersherum wuerde die erste eingehende
    // Verbindung in NotifyAcceptingConnection auf null laufen (v15-Absturz).
    HookNotify(world);
    if (!g_mask) {
        L("  [nm] -nomask gesetzt -> Maske aus, Verhalten exakt wie v32.");
    } else if (!g_origNotifyAccept || !g_origNotifyCtrl) {
        L("  [nm] Notify-Umleitung unvollstaendig -> Maske BLEIBT AUS (zu riskant).");
    } else {
        g_maskDepth = 0;
        g_maskActive = true;
        SetWorldDriver(nullptr);
        // Build the decoy object if either the full decoy OR the join-window
        // decoy is requested. The join-window decoy only presents it briefly
        // inside MyNotifyCtrl, so here we just make sure it exists.
        if (g_joinDecoy && !g_decoy) {
            if (BuildDecoy(drv)) L("  [jd] decoy object ready for join-window use");
            else                 L("  [jd] could not build decoy -> -joindecoy inactive");
        }
        if (g_useDecoy && BuildDecoy(drv)) {
            SetDemoDecoy(true);
            L("  [dc] *** ATTRAPPE EINGEHAENGT *** World->DemoNetDriver=%p",
              SafePtr((uint8_t*)world + OFF::UWorld_DemoNetDriver));
            L("  [dc] Netzmodus laut Faltung: %s", NetModeName());
            L("  [dc] -> Client-RPCs werden ab jetzt verschickt statt lokal ausgefuehrt.");
        } else if (!g_useDecoy) {
            L("  [dc] Attrappe aus (Standard) -> Verhalten wie v33.");
        } else {
            L("  [dc] Attrappe konnte nicht gebaut werden -> nur Maske.");
        }
        L("  [nm] *** MASKE AKTIV *** World->NetDriver und LevelCollections auf null.");
        L("  [nm] Netzmodus fuer die Spiellogik: %s", NetModeName());
    }

    if (!g_watchdog) {
        g_watchdog = CreateThread(nullptr, 0, WatchdogThread, nullptr, 0, nullptr);
        L("  Watchdog gestartet (ueberwacht World->NetDriver)");
    }
    if (!g_origTickFlush) HookTickFlush(drv);
    return true;
}

// -------------------------------------------------- Aufrufstelle umbiegen
static bool PatchLoadMapCall() {
    uintptr_t site = g_base + RVA::LoadMap_CallListen;
    static const uint8_t expect[5] = { 0xE8, 0x0A, 0x15, 0xA6, 0xFC };
    auto* p = (uint8_t*)site;
    L("[i] Aufrufstelle: %02X %02X %02X %02X %02X", p[0],p[1],p[2],p[3],p[4]);
    if (memcmp(p, expect, 5) != 0) { L("[!] weicht ab -> nicht gepatcht"); return false; }
    void* tr = nullptr; SYSTEM_INFO si; GetSystemInfo(&si);
    for (uintptr_t d = si.dwAllocationGranularity; d < 0x40000000ULL && !tr; d += si.dwAllocationGranularity)
        for (int dir = 0; dir < 2 && !tr; ++dir) {
            uintptr_t a = (dir ? site + d : site - d) & ~(uintptr_t)(si.dwAllocationGranularity - 1);
            tr = VirtualAlloc((void*)a, 0x1000, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        }
    if (!tr) { L("[!] kein Speicher in +-2GB"); return false; }
    auto* tb = (uint8_t*)tr;
    tb[0]=0xFF; tb[1]=0x25; *(uint32_t*)(tb+2)=0;
    void* h = (void*)&MyListen; memcpy(tb+6, &h, 8);
    int64_t rel = (int64_t)(uintptr_t)tr - (int64_t)(site + 5);
    if (rel > INT32_MAX || rel < INT32_MIN) { L("[!] rel32 zu weit"); return false; }
    DWORD old;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(int32_t*)(p+1) = (int32_t)rel;
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    L("[+] LoadMap-Aufrufstelle -> Trampolin %p -> MyListen %p", tr, h);
    return true;
}

static DWORD WINAPI Main(LPVOID) {
    char path[MAX_PATH]; GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* s = strrchr(path, '\\'); if (s) strcpy_s(s+1, 32, "sp_listen.log");
    // _fsopen statt fopen: sonst haelt die DLL die Datei exklusiv und das Log
    // ist erst lesbar, wenn das Spiel beendet wurde.
    g_log = _fsopen(path, "w", _SH_DENYWR);
    HMODULE hm = GetModuleHandleA(nullptr);
    MODULEINFO mi{}; GetModuleInformation(GetCurrentProcess(), hm, &mi, sizeof(mi));
    g_base = (uintptr_t)mi.lpBaseOfDll; g_end = g_base + mi.SizeOfImage;
    InitializeCriticalSection(&g_anCs); g_anCsReady = true;
    InitializeCriticalSection(&g_wakeCs); g_wakeCsReady = true;
    L("[+] sp_listen v169 (NEU gegenueber v168: die Eichung des UFunction-Layouts nimmt ZWEI Proben -- StartGame (8 Byte Parameter) gegen Cheatable (0 Byte) -- und sucht die Stelle, die bei der einen 8 und bei der anderen 0 liest. v168 suchte 8 mit einer 2 direkt davor und fand nichts, weil die Felder hier nicht nebeneinander liegen; StartGame lief trotzdem, weil die SDK-Form (float,bool) stimmt. Mit der Eichung darf endlich auch jede ANDERE parameterlose Funktion des CheatManagers per Name laufen. Basis v168: 'cheatable' ruft jetzt BravoHotelCheatManager::Cheatable() -- das ist das Tor des SPIELS (BravoHotelGameInstance::bCheatable). Die erste v167-Runde zeigte: der UE4-CheatManager war laengst da, StartGame wurde sauber aufgerufen und das Spiel wies es trotzdem ab (Host-Log: 'BravoHotelCheat: Failed to apply [Player018: StartGame]'). Ausserdem wird das UFunction-Layout jetzt an StartGame GEEICHT (2 Parameter, 8 Byte laut SDK) statt hinter FunctionFlags geraten -- v167 las dort 17 Parameter und 58688 Byte. Basis v167: -cmdfile. Das Admin-Panel kann in die laufende Runde greifen: tools/sp-listen-alive.js schreibt <id>TAB<Text> in sp_listen_cmd.txt (Pfad mit -cmdfile=<Datei>), diese DLL fuehrt den Befehl im Spiel-Thread aus und antwortet HIER mit '[cmd] <id> ok|fail <Text>'. Drei Faelle: 'cheatable' ruft APlayerController::EnableCheats (legt den CheatManager an, PC+0x620), 'StartGame <Sekunden> <true|false>' ruft BravoHotelCheatManager::StartGame(InDelay,bUseAircraft), jeder andere Name eine Funktion OHNE Parameter am CheatManager oder am Controller. Gefunden wird alles ueber dieselbe Reflexionsliste, die der Magazin-Push schon abgeht -- kein neuer RVA. Ohne -cmdfile passiert nichts, und beim Start steht der Lesezeiger am Dateiende, damit ein Befehl von gestern nicht in die heutige Runde platzt. Basis v166: v165. Die Nachlade-Freigabe wird jetzt DAUERHAFT frisch gehalten, nicht nur bei leerem Magazin. v165 hat das Nachladen nach dem Leerschiessen repariert, aber ein zweiter Weg blieb offen: Waffe ein paar Schuss benutzen, auf die Faeuste wechseln, zurueck auf die Waffe -- dann ist das Nachladen wieder gesperrt, bis das Magazin leer ist und der Puls greift. Grund: weapon+0x21E0, die Freigabe, die CanReload (0x1EF2F80) auf Clients verlangt, ist kein UProperty und wird nie repliziert. Der Waffen-Tick des Clients (0x1F29E10) loescht sie nach Ablauf seines Timers, und nur zwei RepNotifies setzen sie zurueck: OnRep_IsEquipped (0x1F073E0) und OnRep_IsServerChanged (0x1F08E80). Beim Wechsel auf die Faeuste und zurueck sieht der Host gar keinen Wechsel -- die Faeuste sind keine Waffe, der vom Host verfolgte Waffenzeiger bleibt derselbe, bIsEquipped bleibt 1, bIsServerChanged bleibt 1, es gibt also keine AENDERUNG und damit kein RepNotify. Der Client zieht die Waffe selbst und hat danach keine Freigabe mehr. v166 wartet deshalb nicht mehr auf mag == 0, sondern pulst bIsServerChanged (+0x94C Bit1) 1 -> 0 -> 1 rund einmal pro Sekunde, solange eine Waffe in der Hand ist. Die Freigabe ist damit nie laenger als eine Sekunde abgelaufen, egal was sie geloescht hat -- Leerschiessen, Waffenwechsel oder der Timer selbst. Kosten: ein replizierter Bool pro Sekunde und Spieler. Kuerzel [ep], ganz aus mit -noemptypulse, Abstand mit -emptypulsems=N, v165-Verhalten (nur bei leerem Magazin) mit -pulseonlyempty. Basis v162: v161 ZURUECKGENOMMEN: das dauernde Loeschen von bIsNeedBoltAction hat das Nachladen ganz gesperrt, also wird an +0x217A nichts mehr geschrieben (-boltfix stellt v161 wieder her). NEU: -spclient macht aus dieser DLL ein reines Messwerkzeug FUER DEN SPIELER-PC: kein Hosting, keine Schreibzugriffe, nur ABravoHotelCharacter::DoReload (0x1FF12C0, die Taste R) und ABravoHotelRangedWeapon::CanReload (0x1EF2F80, die Pruefung, die das Nachladen verweigert) mit allen Eingangswerten als [cx]-Zeilen. Damit steht fest, welcher Wert beim leeren Magazin kippt, statt ihn vom Host aus zu raten. Vorherige Notiz v161: die v160-Runde zeigt auf dem Host Zustand Idle, kein Nachladen, kein Repetieren -- aber +0x217A steht die ganze Runde auf 01. Die Bitmasken sind umgekehrt zu v160: Bit0 = bIsNeedBoltAction (repliziert), Bit1 = bIsFiring. Der Konstruktor der Waffe setzt Bit0, und die drei Stellen, die es loeschen, laufen fuer die Waffe eines Remote-Spielers hier nie -- der Host repliziert also dauerhaft \"muss noch repetiert werden\". Beim leeren Magazin bleibt der Client damit in EWS_BoltAction stehen, und CanReload verlangt Zustand < EWS_Reloading. v160 loeschte das falsche Bit (0x02). v161 loescht Bit 0x01, sobald es gesetzt ist, und schickt bei leerem Magazin zusaetzlich MulticastStopSimulatingBoltAction; [pe] schreibt jetzt auch die Bolt-/Stop-RPCs mit. Aus mit -noboltfix. LEERES MAGAZIN: der letzte Schuss laesst die Waffe mit Repetierbedarf zurueck -- bPendingBoltAction (+0x1FDC, repliziert, OnRep_BoltAction) und bIsNeedBoltAction (+0x217A Bit1, repliziert) bleiben stehen, weil der Abschluss des Repetierens fuer die Waffe eines Remote-Spielers hier nie laeuft; mit Restmunition raeumt der naechste Schuss das auf, beim letzten nicht. Der Client steht damit in EWS_BoltAction, und CanReload (0x1EF2F80) verlangt Zustand < EWS_Reloading -- deshalb passiert bei R gar nichts, bis die Waffe gewechselt wird. v160 loest die beiden Flags, sobald das Magazin leer ist, und schickt MulticastStopSimulatingBoltAction; [wz] schreibt ausserdem jede Aenderung der replizierten Waffenfelder mit. Aus mit -noboltfix / -nowzlog. Magazin-Push nur noch bei ZUNAHME (Nachladen), nie bei Abnahme (Schuss des Clients, der ihn schon selbst abgezogen hat) -- der verspaetete Push alter Werte hat die leere Schrotflinte beim Client wieder auf 1 gesetzt und ihn in einen Schuss ohne Host-Munition laufen lassen. v157 bestaetigt: ServerStartReload kommt an, Host-Magazin 0 -> 30, Rucksack 60 -> 30. Die HUD-Anzeige des Clients blieb bei 0, weil der ClientSetMagazine-Push seit v156 aus war -- v158 schaltet ihn wieder AN (nie mit 0), -nomagpush aus. NACHLADEN GEFUNDEN: CanReload (0x1EF2F80) verlangt auf dem Client Waffe+0x21E0 != 0; das setzen nur OnRep_IsEquipped (0x1F073E0) und OnRep_IsServerChanged (0x1F08E80). Der besitzende Client zieht die Waffe selbst, das bIsEquipped=1 des Hosts ist fuer ihn keine Aenderung, und bIsServerChanged (+0x94C Bit1, Net+RepNotify) setzt bei uns niemand (Retail: Blueprint des Servers). Ausserdem meldet der Client seine Rucksackmunition (ServerBackPackInTotalAmmoCount) nur bei gesetztem bIsServerChanged -- daher Rucksack 0 auf dem Host. v157 setzt das Bit auf der gehaltenen Waffe jedes Remote-Spielers, sobald der Host bIsEquipped hat, und loescht es beim Ablegen. Kuerzel [sc], aus mit -noservchanged. v156: in der v154-Runde (18:38) kam KEIN ServerReloadWeapon/ServerStartReload beim Host an -- der Client hat den Nachladeversuch selbst verworfen; der Host hielt fuer jede Waffe des Spielers Magazin 0 / Rucksack 0 und der v60-Push schickte 16x ClientSetMagazine(0). v156: Push AUS per Vorgabe (-magpush an, nie mit 0), [wp]-Fenster auf 0x2800 (bReady/CurrentState/bPendingReload der RangedWeapon sichtbar), -dormallowweapons als A/B-Schalter (Waffen schlafen wie vor v154). SDK-Abgleich (v155): SDK-Abgleich: alle DLL-Offsets gegen den Laufzeit-Dump (SDK.zip) geprueft -- GameState+0x4B0/+0x9F2/+0x8D0, GameMode+0x9E8/+0x370, PC+0x390, PS+0x5E0 (BHReplicatedPlayStat, AccountGold +0xE0) / +0x6E8 (GamePlayStatistics), Inventar +0x648/+0x710/+0x5B8, Waffe Magazine +0xDD0 / Rucksackmunition +0xE50 / MagazineCapacity +0x1A28, Zone +0x7C0..+0x7E8 stimmen. Korrektur: AttachmentIndices liegt in der WeaponReplicatedComponent = Waffe+0xC38 (nicht +0x1E00 wie v154 annahm), [wx] liest jetzt dort. Character+0x3A4 ist die obere Haelfte des Mesh-Zeigers (+0x3A0) -- der v130-Absturz war ein zerschossener Mesh-Zeiger, CurrentEquipWeaponID +0x3A4 gehoert zum BravoHotelPlayerSubState-Actor. DORMANCY-SPERRE (v154): DORMANCY-SPERRE: das Spiel legt Waffen 3.5 s nach dem Ziehen (und PlayerStates, Kisten, Zaeune) per SetNetDormancy(DormantAll) schlafen; das Wecken (FlushNetDormancy) laeuft unter der Maske ins Leere bzw. wird vom v62-Crashschutz abgewiesen -- deshalb kamen Aufsatz-Wechsel, Nachladen und Magazinstand erst beim naechsten Waffenwechsel beim Client an. v154 haengt sich in AActor::SetNetDormancy (0x3FA09C0) und weist DormantAll/DormantPartial fuer dynamische Actors ab (Level-Actors nur mit -dormlevel); Actors, die schon schlafend in den Graphen kommen, werden dort auf DORM_Awake gesetzt. Kuerzel [dz], aus mit -nodormblock. AttachmentIndices liegen in der WeaponAttachmentComponent (Waffe+0x1E00, dort +0x190), nicht in der Waffe -- [wx] liest jetzt dort. ZONENSTUFEN (v153): bis zum Start zaehlt die DLL alle ~2 s die PlayerStates (Host + Spieler + KI) und spielt beim Wechsel der Stufe (-zonetiers=16:16,24:24,40:40,64:64,80:999 = Tabelle:<=Passagiere) per erneutem InitTableSetting eine Zeile der neuen Tabelle ein -- die Zone waechst mit den Beitritten. -zoneplayers=N oder -zoneid=N schalten die Stufen ab (v152), -zoneplayers=0 laesst alles dem Spiel. ZONENWAHL (v152): das Spiel waehlt die Play-Zone-Zeile per Zufall aus ALLEN 156 Zeilen von TBL-PlayZone_OrbIsland -- 16-Spieler-Zonen (550 m, 60 s Vorlauf) genauso wie 80-Spieler-Zonen; der Dedicated Server bekam den Index vom Matchmaking. v152 haengt sich in InitTableSetting (0x1CB7D80) und gibt den Index selbst vor: Zufall innerhalb der Zeilen fuer -zoneplayers=N (Vorgabe 64, passt zu ~50 KI-Passagieren), -zoneid=N erzwingt eine Zeile, -zoneplayers=0 laesst alles wie vor v152. Kuerzel [bz]. Alles aus v151 unveraendert: SICHTMODUS: -fpp / -tpp schreiben GameViewType (2 = FPP, 1 = TPP) in jedem Netz-Tick in GameState+0x4B0 (repliziert, OnRep_GameViewType beim Client) und GameMode+0x9E8 (die StartGame-Zeile im Host-Log zeigt den Wert). v150 (-gamesetting, GameSettingString-JSON) ist raus: das JSON wurde angenommen, aber die Feldnamen dieses Builds sind verborgen, es aenderte nichts. Kuerzel [vt], ohne Schalter unveraendert. Alles aus v149 unveraendert: DAS HAUPTBUCH: die Zeilen des Rundenergebnisses (ChangeDeck/AcquireCoin/RandomGold) liegen als TArray<{int32 Wert; uint8 Typ}> an [PS+0x6E8]+0xF0 -- OnPayResult traegt dort ChangeDeck ein (0x21659D0, eine Zeile je Typ, Werte werden addiert). v149 liest diese Zeilen bei Logout direkt aus dem PlayerState: Muenzen = DropCoin+AcquireCoin, Bonus = RandomGold+RandomRankGold, alle Zeilen im Log. Der ProcessEvent-Weg aus v148 lief mit -nopelog gar nicht und bleibt nur als Reserve. Alles aus v148 unveraendert: MUENZEN: die \"+30 Gold\" im Match sind das Item Coin im Inventar, nicht das Kontogold -- v147 sah deshalb \"keine Beute\". Das Ergebnis der Runde (Client-Log: MatchEndResult / AcquireCoin:30 / RandomGold:17) schickt der Host per Client-RPC ShowMatchEndFinalResult; v148 faengt es in ProcessEvent ab, entschluesselt die Zeilen {ENormalType, Wert} (7=AcquireCoin, 13/14=RandomGold) heuristisch, loggt die Rohbytes zur Kontrolle und rechnet bei Logout Beute = Kontogold-Differenz + Muenzen + Bonus ab. Alles aus v147 unveraendert: BEUTE-GOLD -> KONTO: das Gold der Runde liegt im PlayerState ([[PS+0x5E0]+0xE0]), jede Aenderung laeuft ueber SetGold (0x222F200). v147 merkt sich pro Spieler den Stand nach InitNewPlayer (AccountGold), zaehlt Kaeufe ueber das Zahl-Ergebnis (0x220D240) mit und rechnet bei Logout (0x1CB9110, wirft PlayerDisconnected) ab: Beute = Ende - Start + bezahlt. Eine Beute > 0 steht als CommitRequest[CurrencyGain]-Zeile in DIESER Datei; tools/sp-listen-alive.js v1.3 liest sie mit und bucht sie im Backend. Ein Verlust wird nur geloggt, nie gebucht. Kuerzel [gg], AN per Vorgabe, aus mit -nogoldgain. Alles aus v146 unveraendert: GOLD-KAUF IM MATCH: Klassenwahl (Direct 700 / Random 100 Gold) und jeder andere Gold-/Materialkauf laufen ueber CurrencyPay (0x21EF0B0) / MaterialPay (0x21EFAD0). Die uebergeben die Anfrage an einen Delegate, den nur das Dedicated-Server-Modul bindet (Host-Log: CommitRequest[CurrencyPay] wasn't bind function!!). Ohne Bindung liefert der Code das Ergebnis nur im Standalone-Modus (GetNetMode==0) selbst -- auf dem Listen-Server NIE: die Klassenwahl wartet ewig, der Spieler friert ein (PC-Log: CreateSavedMove Hit limit of 96 saved moves). v146 biegt an beiden Stellen den Sprung um (75 04 -> 75 22), so dass der ungebundene Pfad wie Standalone sofort Erfolg meldet. Kuerzel [cp], AN per Vorgabe, aus mit -nopay. Die Lobby-Goldbilanz wird dabei nicht beruehrt. Alles aus v145 unveraendert:  DAS FUEHRENDE FRAGEZEICHEN: v144 traf die richtige Funktion (Host-Log: [lo] Loadout eingespielt), aber das Spiel warf das Outfit-JSON weg: \"JsonObjectStringToUStruct - Unable to parse json=[]\". Der Optionen-Zerleger (0x424ED40) prueft ZUERST Options.Left(1)==\"?\" und liefert sonst einen LEEREN String -- v144 haengte \"{json}?...\" ohne fuehrendes ? an. v145 schreibt \"?\"+Optionen+Original, damit der Zerleger das ? abstreift und bis zum naechsten ? = unser JSON nimmt. Dazu liefert das Backend das JSON jetzt in der Form {UID,Name,PCInfo{...}} wie im Standalone-URL. Kuerzel [lo], AUS per Vorgabe, an mit -loadout. Alles aus v144 unveraendert.)  base=%p pid=%lu t0=%llu", (void*)g_base, (unsigned long)GetCurrentProcessId(), (unsigned long long)time(nullptr));
    { const wchar_t* cl = GetCommandLineW();
      if (cl && wcsstr(cl, L"-nomask"))  { g_mask = false;     L("[i] -nomask erkannt");  }
      if (cl && wcsstr(cl, L"-decoy"))   { g_useDecoy = true;  L("[i] -decoy erkannt -- Attrappe AN (Experiment!)"); }
      if (cl && wcsstr(cl, L"-actormode")   && !wcsstr(cl, L"-noactormode"))
          { g_actorMode = true;  L("[i] -actormode erkannt -- Actors melden NM_ListenServer (Verhalten wie v42-v44)"); }
      if (cl && wcsstr(cl, L"-noactormode")) { g_actorMode = false; L("[i] -noactormode erkannt -- Actor-Netzmodus-Hook AUS (Standard)"); }
      if (cl && wcsstr(cl, L"-norpc"))   { g_rpcFix = false; g_noRpcSwitch = true; L("[i] -norpc erkannt -- RPC-Weiche AUS"); }
      if (cl && wcsstr(cl, L"-remoteonly")) { g_rpcRemoteOnly = true;
          L("[i] -remoteonly erkannt -- Client-RPCs NUR verschicken (Laptop = reiner Server)"); }
      if (cl && wcsstr(cl, L"-rpclog"))    { g_rpcLog = true; g_logCtrlMsg = true; L("[i] -rpclog erkannt -- RPC-Namen + Control-Message-IDs werden geloggt"); }
      if (cl && wcsstr(cl, L"-joindecoy")) { g_joinDecoy = true; L("[i] -joindecoy erkannt -- Join im NM_ListenServer-Modus abwickeln"); }
      if (cl && wcsstr(cl, L"-allowreplay")) { g_blockReplay = false; L("[i] -allowreplay erkannt -- Replay-Recorder NICHT unterdrueckt (Crash-Risiko!)"); }
      if (cl && wcsstr(cl, L"-allowrebase")) { g_noRebase = false; L("[i] -allowrebase erkannt -- Welt-Ursprung darf wieder wandern"); }
      if (cl && wcsstr(cl, L"-ammolog")) { g_ammoLog = true; L("[i] -ammolog erkannt -- jeder Inventory-/Waffen-RPC wird mit Zeitstempel geloggt"); }
      if (cl && wcsstr(cl, L"-nozonefix")) { g_zoneFix = false; L("[i] -nozonefix erkannt -- BlueZone wird nicht angestupst"); }
      if (cl && wcsstr(cl, L"-loadout"))    { g_loadout = true;  L("[i] -loadout erkannt -- Gold/Level/Outfit des beitretenden Spielers werden aus loadouts.txt in die Join-Optionen eingespielt"); }
      if (cl) { const wchar_t* c = wcsstr(cl, L"-cmdfile");
          if (c) {
              g_cmdEnabled = true;
              if (c[8] == L'=') {                     // -cmdfile=<Pfad>, auch in Anfuehrungszeichen
                  const wchar_t* p = c + 9;
                  bool q = (*p == L'"'); if (q) ++p;
                  int n = 0;
                  while (*p && n < 500 && (q ? *p != L'"' : *p != L' ')) g_cmdPath[n++] = *p++;
                  g_cmdPath[n] = 0;
              }
              L("[i] -cmdfile erkannt -- Befehle des Admin-Panels werden aus %ls gelesen",
                g_cmdPath[0] ? g_cmdPath : L"sp_listen_cmd.txt neben dieser DLL");
          } }
      if (cl && wcsstr(cl, L"-fpp")) { g_forceView = 2; L("[i] -fpp erkannt -- die Runde wird auf FPP (GameViewType 2) gezwungen"); }
      if (cl && wcsstr(cl, L"-tpp")) { g_forceView = 1; L("[i] -tpp erkannt -- die Runde wird auf TPP (GameViewType 1) gezwungen"); }
      if (cl) { const wchar_t* z = wcsstr(cl, L"-zoneplayers="); if (z) { g_zonePlayers = _wtoi(z + 13); g_zoneScale = false; L("[i] -zoneplayers=%d erkannt -- %s", g_zonePlayers, g_zonePlayers ? "feste Tabelle dieser Spielerzahl, keine Stufen" : "Zonenwahl bleibt dem Spiel ueberlassen (Zufall ueber alle Zeilen)"); } }
      if (cl) { const wchar_t* z = wcsstr(cl, L"-zoneid="); if (z) { g_zoneId = _wtoi(z + 8); g_zoneScale = false; L("[i] -zoneid=%d erkannt -- feste Zonen-Zeile", g_zoneId); } }
      if (cl) { const wchar_t* z = wcsstr(cl, L"-zonetiers="); if (z) { ParseZoneTiers(z + 11); L("[i] -zonetiers erkannt -- %d Stufen", g_zoneTierN); } }
      if (g_zoneScale) { char t[160] = ""; for (int i = 0; i < g_zoneTierN; ++i) { char b[32]; sprintf_s(b, "%s%d:<=%d", i ? ", " : "", g_zoneTiers[i].rows, g_zoneTiers[i].maxPassengers); strcat_s(t, b); } L("[i] Zonenstufen (Tabelle:<=Passagiere): %s -- waechst mit den Beitritten bis zum Start", t); }
      if (cl && wcsstr(cl, L"-nogoldgain")) { g_goldGain = false; L("[i] -nogoldgain erkannt -- Beute aus der Runde bleibt in der Runde"); }
      if (cl && wcsstr(cl, L"-nopay")) { g_payPatch = false; L("[i] -nopay erkannt -- Gold-/Materialkauf im Match bleibt ohne Ergebnis (Klassenwahl haengt wie vor v146)"); }
      if (cl && wcsstr(cl, L"-noenctoken")) { g_ignoreEncToken = false; L("[i] -noenctoken erkannt -- ein EncryptionToken im NMT_Hello geht wieder an den Delegate (Join aus der Lobby-Suche wird abgewiesen)"); }
      if (cl && wcsstr(cl, L"-nolevelvis")) { g_levelVis = false; L("[i] -nolevelvis erkannt -- Sublevel-Referenzen bleiben wie im Original"); }
      if (cl && wcsstr(cl, L"-nostreamremote")) { g_streamRemote = false; L("[i] -nostreamremote erkannt -- Host streamt nur um sich selbst (Verhalten bis v54)"); }
      if (cl && wcsstr(cl, L"-nohostfirst"))    { g_remoteFirst = false;  L("[i] -nohostfirst erkannt -- alte Reihenfolge: Host ist Primaerpunkt, Remote-Spieler nur Unterpunkt"); }
      if (cl && wcsstr(cl, L"-hostpoint"))      { g_hostPoint = true;     L("[i] -hostpoint erkannt -- der Host behaelt immer einen eigenen Streaming-Punkt, auch im Flieger"); }
      if (cl && wcsstr(cl, L"-nozonerelevant")) { g_zoneRelevant = false; L("[i] -nozonerelevant erkannt -- Zonenklasse wird NICHT bAlwaysRelevant"); }
      if (cl && wcsstr(cl, L"-noaircraftfix")) { g_aircraftFix = false; L("[i] -noaircraftfix erkannt -- DoInAircraft bleibt NM_ListenServer (kein ClientInAircraft)"); }
      if (cl && wcsstr(cl, L"-nospawnremote")) { g_spawnRemote = false; L("[i] -nospawnremote erkannt -- Loot-Spawn-Pruefung nur fuer den Host"); }
      if (cl && wcsstr(cl, L"-nobeginplayfix")) { g_beginPlayFix = false; L("[i] -nobeginplayfix erkannt -- BeginPlay-NetMode der Spawn-Timer bleibt wie im Original"); }
      if (cl && wcsstr(cl, L"-noviewfix"))      { g_viewFix = false;      L("[i] -noviewfix erkannt -- Blickpunkt der Verbindungen wird nur gemessen, nicht korrigiert"); }
      if (cl && wcsstr(cl, L"-nocommitfix"))    { g_commitFix = false;    L("[i] -nocommitfix erkannt -- Kacheln um den Remote-Spieler bekommen die LOD-Entscheidung der Engine"); }
      if (cl && wcsstr(cl, L"-noblockload"))    { g_blockLoad = false;    L("[i] -noblockload erkannt -- die Kachel unter dem gelandeten Remote-Spieler wird nicht blockierend geladen"); }
      if (cl && wcsstr(cl, L"-nopulltiles"))    { g_pullTiles = false;    L("[i] -nopulltiles erkannt -- Kacheln um den Remote-Spieler werden nicht selbst nachgefordert"); }
      if (cl && wcsstr(cl, L"-ownpoke"))        { g_ownPoke = true;       L("[i] -ownpoke erkannt -- eigene Actors des Remote-Spielers werden wieder aktiv aktualisiert (hat den Host zweimal zum Absturz gebracht!)"); }
      if (cl && wcsstr(cl, L"-noownmode"))      { g_ownMode = false;      L("[i] -noownmode erkannt -- eigene Actors des Remote-Spielers bekommen wieder NM_Standalone (Verhalten bis v58t)"); }
      if (cl && wcsstr(cl, L"-noweaponwatch")) { g_weaponWatch = false;  L("[i] -noweaponwatch erkannt -- die Felder der Waffe werden nicht mitgeschrieben"); }
      if (cl && wcsstr(cl, L"-dormawake"))      { g_dormAwake = true;     L("[i] -dormawake erkannt -- NetDormancy wird wieder auf DORM_Awake geschrieben (hat den Host abstuerzen lassen!)"); }
      if (cl && wcsstr(cl, L"-noownrelevant")) { g_ownRelevant = false;  L("[i] -noownrelevant erkannt -- die Graph-Cull-Kopie der eigenen Actors des Remote-Spielers bleibt, wie sie ist"); }
      if (cl && wcsstr(cl, L"-magtest"))       { g_magTest = true;       L("[i] -magtest erkannt -- EINMAL pro Runde wird 99 in Waffe+0xDD0 (Magazine, CPF_Net) geschrieben"); }
      if (cl && wcsstr(cl, L"-magrep"))     { g_magRep = true;  L("[i] -magrep erkannt -- ReplicateActor auf dem Waffenkanal (v73-Versuch, warf eine Exception)"); }
      if (cl && wcsstr(cl, L"-graphbasic")) { g_graphChoice = "basic"; L("[i] -graphbasic erkannt -- UBasicReplicationGraph wird in +0x180 gesetzt, falls leer"); }
      if (cl && wcsstr(cl, L"-graphreal"))  { g_graphChoice = "real";  L("[i] -graphreal erkannt -- URealReplicationGraph (ABSTRAKT! hat den Host mit \"Pure virtual not implemented\" gekillt)"); }
      if (cl && wcsstr(cl, L"-spatialguard"))   { g_spatialGuard = true;  L("[i] -spatialguard erkannt -- Positionsmeldungen fuer dormante Actors werden verworfen (v69-Versuch, hat die Crashes NICHT behoben)"); }
      if (cl && wcsstr(cl, L"-weaponalways"))   { g_weaponAlways = true;  L("[i] -weaponalways erkannt -- gehaltene Waffe bAlwaysRelevant + Neu-Einsortieren (v78-Versuch, hat BlueZone und Spielerposition kaputtgemacht)"); }
      if (cl && wcsstr(cl, L"-weaponrelevant"))   { g_weaponRelevant = true;  L("[i] -weaponrelevant erkannt -- Waffen bekommen wieder bAlwaysRelevant (v67-Versuch, liess Waffen beim Aufheben verschwinden)"); }
      if (cl && wcsstr(cl, L"-nogskick")) { g_gsKick = false; L("[i] -nogskick erkannt -- GameState wird nicht zusaetzlich angestossen"); }
      if (cl && wcsstr(cl, L"-nozonepin")) { g_zonePin = false; L("[i] -nozonepin erkannt -- die Zone wird NICHT wachgehalten (v120-Fix aus)"); }
      if (cl && wcsstr(cl, L"-nozonetoggle")) { g_zoneToggle = false; L("[i] -nozonetoggle erkannt -- die Zonen-Referenz wird NICHT neu angestossen (v122-Fix aus)"); }
      if (cl && wcsstr(cl, L"-nowatchattach")) { g_attachWatch = false; L("[i] -nowatchattach erkannt -- die Waffen-Bindung wird nicht beobachtet"); }
      if (cl && wcsstr(cl, L"-weaponpush")) { g_weaponPush = true; L("[i] -weaponpush erkannt -- die AttachmentIndices werden angestossen (v132: AUS per Vorgabe, weil v130 den Host beim Aufheben einer Waffe abgestuerzt hat)"); }
      if (cl && wcsstr(cl, L"-nomapguard")) { g_mapGuard = false; L("[i] -nomapguard erkannt -- Null-Map-Schutz AUS"); }
      if (cl && wcsstr(cl, L"-nocrashlog")) { g_crashLog = false; L("[i] -nocrashlog erkannt -- Abstuerze werden nicht mitgeschrieben"); }
      if (cl && wcsstr(cl, L"-nochanguard")) { g_chanGuard = false; L("[i] -nochanguard erkannt -- Kanal-Schutz AUS, der Absturz bleibt scharf"); }
      if (cl && wcsstr(cl, L"-nographguard")) { g_graphGuard = false; L("[i] -nographguard erkannt -- Graph-Actor-Schutz AUS"); }
      if (cl && wcsstr(cl, L"-nopills")) { g_pills = false; L("[i] -nopills erkannt -- Kapsel-/Loot-Reparatur bleibt aus"); }
      if (cl && wcsstr(cl, L"-wakedoors")) { g_wakeDoorsOnly = true; L("[i] -wakedoors erkannt -- nur Tueren/Fenster/Zerbrechliches werden geweckt, Kulisse bleibt schlafen"); }
      if (cl && wcsstr(cl, L"-nodormwake")) { g_dormWake = false; L("[i] -nodormwake erkannt -- im Level platzierte DORM_Initial-Actors bleiben fuer den Graphen unsichtbar (Stand vor v96)"); }
      if (cl && wcsstr(cl, L"-nomagtrack")) { g_magTrack = false; L("[i] -nomagtrack erkannt -- Schuesse werden NICHT vom Magazin abgezogen"); }
      if (cl && wcsstr(cl, L"-nopelog"))    { g_peLog = false; L("[i] -nopelog erkannt -- ProcessEvent wird nicht mitgeschrieben"); }
      if (cl && wcsstr(cl, L"-magpush") && !wcsstr(cl, L"-nomagpush")) { g_magPush = true; L("[i] -magpush erkannt -- ClientSetMagazine wird bei Magazin-Aenderung wieder gesendet (v156: AUS per Vorgabe, nie mit 0)"); }
      if (cl && wcsstr(cl, L"-nomagpush"))     { g_magPush = false;      L("[i] -nomagpush erkannt -- ClientSetMagazine-Push AUS"); }
      if (cl && wcsstr(cl, L"-spclient")) { g_clientMode = true; L("[i] -spclient erkannt -- CLIENT-MESSMODUS: es wird NICHTS gehostet und nichts geschrieben, nur CanReload/DoReload mitgeschrieben"); }
      if (cl && wcsstr(cl, L"-boltfix") && !wcsstr(cl, L"-noboltfix")) { g_boltFix = true; L("[i] -boltfix erkannt -- bIsNeedBoltAction wird wieder geloescht (v161-Verhalten, hat das Nachladen ganz gesperrt)"); }
      if (cl && wcsstr(cl, L"-noboltfix")) { g_boltFix = false; L("[i] -noboltfix erkannt -- die Repetier-Flags bleiben nach dem letzten Schuss stehen (Nachladen bei leerem Magazin gesperrt wie vor v160)"); }
      if (cl && wcsstr(cl, L"-nowzlog"))   { g_wzLog   = false; L("[i] -nowzlog erkannt -- Waffenzustand wird nicht mitgeschrieben"); }
      if (cl && wcsstr(cl, L"-pulseonlyempty")) { g_pulseOnlyEmpty = true; L("[i] -pulseonlyempty erkannt -- die Nachlade-Freigabe wird nur noch bei leerem Magazin erneuert (v165-Verhalten: nach einem Waffenwechsel bleibt sie abgelaufen)"); }
      if (cl && wcsstr(cl, L"-noemptypulse")) { g_emptyPulse = false; L("[i] -noemptypulse erkannt -- bIsServerChanged wird bei leerem Magazin NICHT mehr gepulst (Nachladen bleibt nach dem Leerschiessen gesperrt wie vor v165)"); }
      if (const wchar_t* q2 = cl ? wcsstr(cl, L"-emptypulsems=") : nullptr) { int v = _wtoi(q2 + 14); if (v >= 200 && v <= 10000) { g_emptyPulseMs = v; L("[i] -emptypulsems=%d erkannt -- so oft wird die Nachlade-Freigabe bei leerem Magazin erneuert", v); } }
      if (cl && wcsstr(cl, L"-noboltfinish")) { g_boltFinish = false; L("[i] -noboltfinish erkannt -- bPendingBoltAction wird NICHT mehr beendet (Zustand wie vor v164: bei leerem Magazin bleibt der Client in EWS_BoltAction und kann nicht nachladen)"); }
      if (cl && wcsstr(cl, L"-noreloadfinish")) { g_reloadFinish = false; L("[i] -noreloadfinish erkannt -- ein haengendes bPendingReload wird nicht mehr geloescht"); }
      if (cl && wcsstr(cl, L"-scpulse")) { g_scPulse = true; L("[i] -scpulse erkannt -- der v163-Puls von bIsServerChanged ist wieder an (bringt nichts: OnRep_IsServerChanged setzt nur +0x21E0, und das wird nie geloescht)"); }
      if (const wchar_t* q = cl ? wcsstr(cl, L"-boltholdms=") : nullptr) { int v = _wtoi(q + 12); if (v >= 0 && v <= 5000) { g_boltHoldMs = v; L("[i] -boltholdms=%d erkannt -- so lange darf das Repetieren dauern, bevor der Host es beendet", v); } }
      if (cl && wcsstr(cl, L"-noservchanged")) { g_servChanged = false; L("[i] -noservchanged erkannt -- bIsServerChanged wird NICHT gesetzt (Nachladen bleibt gesperrt wie vor v157)"); }
      if (cl && wcsstr(cl, L"-dormallowweapons")) { g_dormAllowWeapons = true; L("[i] -dormallowweapons erkannt -- Waffen duerfen wieder einschlafen (A/B-Test), alles andere bleibt wach"); }
      if (cl && wcsstr(cl, L"-nowakeup"))       { g_wakeUp = false;       L("[i] -nowakeup erkannt -- schlafende Actors um den Remote-Spieler werden nicht geweckt"); }
      if (cl && wcsstr(cl, L"-stateforce"))     { g_stateForce = true;    L("[i] -stateforce erkannt -- die Match-Phase wird fuer den Loot-Check wieder angehoben (v58l-Verhalten)"); }
      if (cl && wcsstr(cl, L"-nolootret"))      { g_lootRet = false;      L("[i] -nolootret erkannt -- Registrierungspfad der Pickups wird nicht mitgeschrieben"); }
      if (cl && wcsstr(cl, L"-nolootscan"))     { g_lootScan = false;     L("[i] -nolootscan erkannt -- Actors werden nicht auf Loot-Komponenten geprueft"); }
      if (cl && wcsstr(cl, L"-nodirectspawn"))  { g_directSpawn = false;  L("[i] -nodirectspawn erkannt -- gefundene Loot-Quellen werden nur gezaehlt, nicht ausgeloest"); }
      if (cl && wcsstr(cl, L"-nogatebypass"))   { g_gateBypass = false;   L("[i] -nogatebypass erkannt -- Loot-Quellen werden nur ueber 0x2142DA0 ausgeloest, kein Umgehen der Tore"); }
      if (cl && wcsstr(cl, L"-nodormblock")) { g_dormBlock = false; L("[i] -nodormblock erkannt -- SetNetDormancy(DormantAll) bleibt erlaubt (Stand v153: Waffen schlafen 3.5 s nach dem Ziehen ein)"); }
      if (cl && wcsstr(cl, L"-dormlevel"))   { g_dormBlockLevel = true; L("[i] -dormlevel erkannt -- auch im Level platzierte Actors duerfen nicht mehr einschlafen"); }
      if (cl && wcsstr(cl, L"-nodormfix")) { g_dormFix = false; L("[i] -nodormfix erkannt -- Dormancy-Weckrufe bleiben maskiert"); }
      if (cl && wcsstr(cl, L"-nosubscale")) { g_subScale = false; L("[i] -nosubscale erkannt -- Sub-Point-Streaming-Skala bleibt 0.5"); } }
    // ---- v162: CLIENT MODE -- measure the reload gate where it lives ------
    //  Everything about the reload has been inferred from the host so far,
    //  and the client's own state (CurrentState, bWantsToFire) is not
    //  replicated, so the host cannot see it. With -spclient this DLL runs on
    //  the PLAYER's machine and does nothing but watch: it hooks
    //      ABravoHotelCharacter::DoReload   0x1FF12C0  (what the R key calls)
    //      ABravoHotelRangedWeapon::CanReload 0x1EF2F80 (the gate that refuses)
    //  and writes one [cx] line whenever the answer or any of its inputs
    //  changes. No host machinery is installed, nothing is written to the
    //  game -- read-only.
    if (g_clientMode) {
        static const uint8_t crProlog[15] = {
            0x40,0x53, 0x56, 0x57, 0x48,0x83,0xEC,0x40,
            0x48,0x8B,0xB9,0xB0,0x02,0x00,0x00 };
        static const uint8_t drProlog[16] = {
            0x40,0x53, 0x48,0x83,0xEC,0x20, 0x48,0x8B,0xD9,
            0x48,0x8B,0x89,0x48,0x06,0x00,0x00 };
        g_origCanReload = (tCanReload)MakeTrampoline(g_base + RVA::Weapon_CanReload, crProlog, sizeof(crProlog));
        if (g_origCanReload && !InstallJmp("ABravoHotelRangedWeapon::CanReload", RVA::Weapon_CanReload,
                                           (void*)&MyCanReload, crProlog))
            g_origCanReload = nullptr;
        g_origDoReloadC = (tDoReloadC)MakeTrampoline(g_base + RVA::Character_DoReload, drProlog, sizeof(drProlog));
        if (g_origDoReloadC && !InstallJmp("ABravoHotelCharacter::DoReload", RVA::Character_DoReload,
                                           (void*)&MyDoReloadC, drProlog))
            g_origDoReloadC = nullptr;
        L("[cx] Client-Messmodus aktiv: CanReload %s, DoReload %s. Bitte im Spiel"
          " ein paar Schuss abgeben, R druecken (geht), dann leerschiessen und R"
          " druecken (geht nicht) -- danach diese Datei schicken.",
          g_origCanReload ? "gehookt" : "NICHT gehookt", g_origDoReloadC ? "gehookt" : "NICHT gehookt");
        return 0;                       // nothing else -- this is not a host
    }

    // ---- v113: crash reporter, installed before anything else can fault ----
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&MyVeh, &self);
        g_selfBase = (uintptr_t)self;
        g_selfEnd  = ImageEndOf(g_selfBase);
        g_exeEnd   = ImageEndOf(g_base);
        if (g_crashLog) {
            g_vehHandle = AddVectoredExceptionHandler(1, &MyVeh);
            L(g_vehHandle
                ? "[ax] Absturz-Melder aktiv -- jede Zugriffsverletzung im Spiel landet ab jetzt MIT Aufrufkette in dieser Datei, auch ohne -log"
                : "[ax] [!] Absturz-Melder konnte nicht installiert werden");
        }
    }
    { // Gegenprobe zur Konstantenfaltung: an base+0x4729C5E muss B8 03 00 00 00
      // stehen ("mov eax,3" = NM_Client). Steht dort etwas anderes, ist meine
      // Analyse fuer diesen Build falsch und die Maske waere Unsinn.
      const uint8_t* p = (const uint8_t*)(g_base + RVA::NetModeFoldProof);
      L("[i] NetMode-Faltung @base+0x%llX: %02X %02X %02X %02X %02X  -> %s",
        (unsigned long long)RVA::NetModeFoldProof, p[0],p[1],p[2],p[3],p[4],
        (p[0]==0xB8 && p[1]==0x03) ? "bestaetigt (NM_Client konstant)"
                                   : "ABWEICHUNG - Maske mit Vorsicht");
    }
    SkipOwnershipCheck();      // <- zuerst: laeuft frueh beim Engine-Start
    static const uint8_t lmProlog[14] = {
        0x48,0x8B,0xC4, 0x48,0x89,0x58,0x20, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55 };
    g_capEngine = MakeCapture(g_base + RVA::LoadMap, lmProlog, 14);
    L(g_capEngine ? "[+] GEngine-Abgriff an LoadMap aktiv" : "[!] GEngine-Abgriff FEHLGESCHLAGEN");
    ForceIpSockets();          // <- MUSS laufen, sonst bleibt es bei Steam-P2P
    if (g_ignoreEncToken) IgnoreEncryptionToken();
    else                  L("[et] -noenctoken -> EncryptionToken geht an den Delegate, Lobby-Joins werden abgewiesen.");
    // v146: in-round gold/material payments get their result at once.
    if (g_payPatch) PatchPayResults();
    else            L("[cp] -nopay -> CurrencyPay/MaterialPay unveraendert (Klassenwahl mit Gold haengt auf dem Listen-Server).");

    // v143: loadout injection. loadouts.txt sits next to the DLL.
    // v147: the same InitNewPlayer hook also starts the gold tracking.
    if (g_loadout || g_goldGain) {
        if (g_loadout) {
            GetModuleFileNameW((HMODULE)g_selfBase, g_loadoutPath, MAX_PATH);
            wchar_t* bs = wcsrchr(g_loadoutPath, L'\\');
            if (bs) wcscpy_s(bs + 1, 32, L"loadouts.txt"); else g_loadoutPath[0] = 0;
        }
        static const uint8_t inpProlog[14] = {
            0x48,0x89,0x5C,0x24,0x18, 0x48,0x89,0x74,0x24,0x20, 0x55, 0x57, 0x41,0x54 };
        g_origInitNewPlayer = (tInitNewPlayer)MakeTrampoline(
            g_base + RVA::GM_InitNewPlayer, inpProlog, sizeof(inpProlog));
        if (g_origInitNewPlayer) {
            if (InstallJmp("ABattleRoyaleGameMode::InitNewPlayer", RVA::GM_InitNewPlayer,
                           (void*)&MyInitNewPlayer, inpProlog))
                L(g_loadout ? "[+] [lo] Loadout-Hook aktiv, Datei: %ls" : "[+] [lo] InitNewPlayer-Hook aktiv (nur Gold-Verfolgung, -loadout ist aus) %ls",
                  g_loadoutPath[0] ? g_loadoutPath : L"");
            else g_origInitNewPlayer = nullptr;
        }
        if (!g_origInitNewPlayer)
            L("[!] [lo] InitNewPlayer-Trampolin FEHLGESCHLAGEN -> Loadout/Gold-Start werden NICHT erfasst");
    }
    if (g_dormBlock) InstallDormBlock(); else L("[dz] -nodormblock -> Dormancy-Sperre aus.");
    if (g_zoneScale || g_zonePlayers || g_zoneId >= 0) InstallZonePick();
    else L("[bz] -zoneplayers=0 -> die Zone bleibt die Zufallswahl des Spiels ueber alle 156 Zeilen.");
    if (g_goldGain) { InstallGoldGain(); if (!g_peLog) L("[gg] -nopelog: der ProcessEvent-Reserveweg (v148) ist aus -- die Hauptbuch-Lesung (v149) braucht ihn nicht"); }
    else            L("[gg] -nogoldgain -> Beute aus der Runde wird nicht verbucht.");
    if (g_levelVis) ForceLevelReferences();
    else            L("[lv] -nolevelvis -> Sublevel-Referenzen werden weiter durch None ersetzt.");
    // 14 Byte, exakt auf Instruktionsgrenze und positionsunabhaengig:
    //   48 89 5C 24 20  mov [rsp+20],rbx | 55 push rbp | 56 push rsi | 57 push rdi
    //   41 54 push r12  | 41 55 push r13 | 41 56 push r14
    static const uint8_t fpsProlog[14] = {
        0x48,0x89,0x5C,0x24,0x20, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55, 0x41,0x56 };
    g_origFindPlayerStart = (tFindPlayerStart)MakeTrampoline(
        g_base + RVA::FindPlayerStart, fpsProlog, 14);
    L(g_origFindPlayerStart ? "[+] FindPlayerStart-Trampolin %p"
                            : "[!] FindPlayerStart-Trampolin FEHLGESCHLAGEN -> kein Patch",
      (void*)g_origFindPlayerStart);
    if (g_origFindPlayerStart)
        InstallJmp("FindPlayerStart", RVA::FindPlayerStart,
                   (void*)&MyFindPlayerStart, fpsProlog);
    // ---- Actor net mode (v42) -------------------------------------------
    // AActor::GetNetMode prolog, 16 bytes to the next instruction boundary,
    // all position independent:
    //   48 89 5C 24 10  mov [rsp+10],rbx | 57 push rdi | 48 83 EC 30 sub rsp,30
    //   48 8B 01        mov rax,[rcx]    | 48 8B D9 mov rbx,rcx
    // (InstallJmp compares the first 14 of them, the trampoline copies all 16.)
    static const uint8_t nmProlog[16] = {
        0x48,0x89,0x5C,0x24,0x10, 0x57, 0x48,0x83,0xEC,0x30,
        0x48,0x8B,0x01, 0x48,0x8B,0xD9 };
    // v45: only installed when explicitly asked for (-actormode). GetNetMode is
    // called extremely often, so in the default configuration we do not put a
    // hook in that path at all.
    if (g_actorMode) {
        g_origGetNetMode = (tGetNetMode)MakeTrampoline(
            g_base + RVA::AActor_GetNetMode, nmProlog, sizeof(nmProlog));
        if (g_origGetNetMode) {
            L("[+] AActor::GetNetMode-Trampolin %p", (void*)g_origGetNetMode);
            if (!InstallJmp("AActor::GetNetMode", RVA::AActor_GetNetMode,
                            (void*)&MyActorGetNetMode, nmProlog))
                g_origGetNetMode = nullptr;
        }
        if (!g_origGetNetMode) {
            L("[!] AActor::GetNetMode-Hook FEHLGESCHLAGEN -> Actor-Netzmodus bleibt Standalone");
            g_actorMode = false;
        }
    }
    // v46: both are on together. The narrow net mode only covers the remote
    // player's own actors, so the RPC switch is still what pushes client and
    // multicast RPCs of everything else out to the client. -norpc wins.
    L("[i] Konfiguration: Actor-Netzmodus %s, RPC-Weiche %s",
      g_actorMode ? "NM_ListenServer nur fuer RemoteRole==AutonomousProxy (Standard)"
                  : "durchgehend NM_Standalone (-noactormode)",
      g_rpcFix ? "AN (Client-/Multicast-RPCs gehen zusaetzlich raus)" : "AUS");
    // APawn::PossessedBy prolog, 16 bytes, position independent:
    //   48 89 5C 24 08  mov [rsp+8],rbx  | 48 89 6C 24 10 mov [rsp+10],rbp
    //   48 89 74 24 18  mov [rsp+18],rsi | 57 push rdi
    static const uint8_t pbProlog[16] = {
        0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10,
        0x48,0x89,0x74,0x24,0x18, 0x57 };
    g_origPossessedBy = (tPossessedBy)MakeTrampoline(
        g_base + RVA::APawn_PossessedBy, pbProlog, sizeof(pbProlog));
    if (g_origPossessedBy) {
        L("[+] APawn::PossessedBy-Trampolin %p", (void*)g_origPossessedBy);
        if (!InstallJmp("APawn::PossessedBy", RVA::APawn_PossessedBy,
                        (void*)&MyPossessedBy, pbProlog))
            g_origPossessedBy = nullptr;
    } else {
        L("[!] APawn::PossessedBy-Trampolin FEHLGESCHLAGEN -> keine Rollen-Kontrolle im Log");
    }
    // ---- v83: AActor::ProcessEvent mitschreiben --------------------------
    // Prolog 15 Byte, alles positionsunabhaengig:
    //   48 89 5C 24 10  mov [rsp+0x10],rbx
    //   48 89 6C 24 18  mov [rsp+0x18],rbp
    //   57              push rdi
    //   48 83 EC 20     sub rsp,0x20
    if (g_peLog) {
        static const uint8_t peProlog[15] = {
            0x48,0x89,0x5C,0x24,0x10, 0x48,0x89,0x6C,0x24,0x18, 0x57, 0x48,0x83,0xEC,0x20 };
        g_origProcessEvent = (tPE)MakeTrampoline(
            g_base + RVA::AActor_ProcessEvent, peProlog, sizeof(peProlog));
        if (g_origProcessEvent) {
            if (!InstallJmp("AActor::ProcessEvent", RVA::AActor_ProcessEvent,
                            (void*)&MyProcessEvent, peProlog))
                g_origProcessEvent = nullptr;
            else
                L("[pe] ProcessEvent wird fuer Waffe/Pawn des Remote-Spielers mitgeschrieben");
        } else {
            L("[pe] ProcessEvent-Prolog passt nicht -> kein Mitschreiben");
            g_peLog = false;
        }
    }

    // ---- Replay-Recorder abschalten (v43) -------------------------------
    // UGameInstance::StartRecordingReplay wird auf einen leeren Rumpf
    // umgeleitet. 14-Byte-Prolog (alles mov [rsp+x],reg, positionsunabhaengig):
    //   48 89 5C 24 08  mov [rsp+8],rbx | 48 89 74 24 10 mov [rsp+10],rsi
    //   48 89 7C 24     (Anfang von mov [rsp+18],rdi -- Rest egal, Original
    //                    wird nie mehr angesprungen)
    if (g_blockReplay) {
        static const uint8_t srrProlog[14] = {
            0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x74,0x24,0x10, 0x48,0x89,0x7C,0x24 };
        InstallJmp("UGameInstance::StartRecordingReplay",
                   RVA::GameInstance_StartRecordingReplay,
                   (void*)&MyStartRecordingReplay, srrProlog);
    } else {
        L("[rp] -allowreplay -> Replay-Recorder bleibt aktiv (kann in DeltaSerializeFastArrayProperty crashen).");
    }
    // ---- Welt-Ursprung festnageln (v44) ---------------------------------
    // 14-Byte-Prolog, exakt auf Instruktionsgrenze und positionsunabhaengig:
    //   48 8B C4        mov rax,rsp      | 48 89 58 18  mov [rax+18],rbx
    //   55 push rbp | 56 push rsi | 57 push rdi | 41 54 push r12 | 41 55 push r13
    if (g_noRebase) {
        static const uint8_t snoProlog[14] = {
            0x48,0x8B,0xC4, 0x48,0x89,0x58,0x18, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55 };
        if (InstallJmp("UWorld::SetNewWorldOrigin", RVA::UWorld_SetNewWorldOrigin,
                       (void*)&MySetNewWorldOrigin, snoProlog))
            L("[or] Welt-Ursprung bleibt bei {0,0,0} -> Serverkoordinaten == absolute Koordinaten");
        else
            L("[!] [or] Origin-Hook fehlgeschlagen -> Rebasing bleibt aktiv (Vault-Teleport moeglich)");
    } else {
        L("[or] -allowrebase -> Origin-Rebasing bleibt aktiv (Verhalten wie v43).");
    }
    // ---- Streaming um Remote-Spieler (v55) ------------------------------
    // UWorldComposition::UpdateStreamingState(FVector*, FVector*, int, int, float)
    // prolog, 15 bytes to the next instruction boundary, position independent:
    //   48 8B C4        mov rax,rsp        | 48 89 58 18  mov [rax+18],rbx
    //   44 89 48 20     mov [rax+20],r9d   | 48 89 50 10  mov [rax+10],rdx
    // (InstallJmp compares the first 14, the trampoline copies all 15.)
    if (g_streamRemote) {
        static const uint8_t usProlog[15] = {
            0x48,0x8B,0xC4, 0x48,0x89,0x58,0x18, 0x44,0x89,0x48,0x20, 0x48,0x89,0x50,0x10 };
        g_origUpdStream = (tUpdStream)MakeTrampoline(
            g_base + RVA::WorldComp_UpdateStreamingState, usProlog, sizeof(usProlog));
        if (g_origUpdStream) {
            L("[+] UpdateStreamingState-Trampolin %p", (void*)g_origUpdStream);
            if (!InstallJmp("UWorldComposition::UpdateStreamingState", RVA::WorldComp_UpdateStreamingState,
                            (void*)&MyUpdateStreamingState, usProlog))
                g_origUpdStream = nullptr;
        }
        if (g_origUpdStream)
            L("[ws] Host laedt die Welt-Kacheln zusaetzlich um jeden Remote-Spieler (Loot/Tueren/Fenster fern vom Host)");
        else {
            L("[!] [ws] Streaming-Hook FEHLGESCHLAGEN -> Host streamt nur um sich selbst");
            g_streamRemote = false;
        }
    } else {
        L("[ws] -nostreamremote -> Host streamt nur um sich selbst.");
    }
    // ---- v58: UNetDriver::AddNetworkActor (zone instance fallback) --------
    // prolog 16 bytes, position independent:
    //   48 89 5C 24 08  mov [rsp+8],rbx | 57 push rdi | 48 83 EC 20 sub rsp,20
    //   48 8B DA        mov rbx,rdx     | 48 8B F9 mov rdi,rcx
    if (g_zoneRelevant) {
        static const uint8_t anaProlog[16] = {
            0x48,0x89,0x5C,0x24,0x08, 0x57, 0x48,0x83,0xEC,0x20, 0x48,0x8B,0xDA, 0x48,0x8B,0xF9 };
        g_origAddNetActor = (tAddNetActor)MakeTrampoline(
            g_base + RVA::NetDriver_AddNetworkActor, anaProlog, sizeof(anaProlog));
        if (g_origAddNetActor) {
            L("[+] UNetDriver::AddNetworkActor-Trampolin %p", (void*)g_origAddNetActor);
            if (!InstallJmp("UNetDriver::AddNetworkActor", RVA::NetDriver_AddNetworkActor,
                            (void*)&MyAddNetworkActor, anaProlog))
                g_origAddNetActor = nullptr;
        }
        L(g_origAddNetActor ? "[bz] Zonen-Actor wird beim Eintritt in den Graphen auf bAlwaysRelevant geprueft"
                            : "[!] [bz] AddNetworkActor-Hook FEHLGESCHLAGEN -> nur der CDO-Patch vor InitListen bleibt");
    }
    // ---- v58 (re-done v56): loot spawn around remote pawns -----------------
    if (g_spawnRemote) {
        // UWorld::GetFirstPlayerController, first 14 of its 21 bytes:
        //   83 B9 78 02 00 00 00  cmp dword [rcx+0x278],0 | 7E 0C jle | 48 8B 89 70 02 (00 00) mov rcx,[rcx+0x270]
        static const uint8_t fpcBytes[14] = {
            0x83,0xB9,0x78,0x02,0x00,0x00,0x00, 0x7E,0x0C, 0x48,0x8B,0x89,0x70,0x02 };
        g_firstPcHooked = InstallJmp("UWorld::GetFirstPlayerController", RVA::World_GetFirstPlayerController,
                                     (void*)&MyGetFirstPlayerController, fpcBytes);
        // ABravoHotelBuilding::CheckSpawnByStandalone, 15 bytes:
        //   48 8B C4 mov rax,rsp | 48 89 58 08 | 48 89 70 10 | 48 89 78 18
        static const uint8_t bsProlog[15] = {
            0x48,0x8B,0xC4, 0x48,0x89,0x58,0x08, 0x48,0x89,0x70,0x10, 0x48,0x89,0x78,0x18 };
        g_origBuildingSpawn = (tCheckSpawn)MakeTrampoline(
            g_base + RVA::Building_CheckSpawn, bsProlog, sizeof(bsProlog));
        if (g_origBuildingSpawn &&
            !InstallJmp("ABravoHotelBuilding::CheckSpawnByStandalone", RVA::Building_CheckSpawn,
                        (void*)&MyBuildingCheckSpawn, bsProlog))
            g_origBuildingSpawn = nullptr;
        // ABravoHotelVehicleSpawnActor::CheckSpawnByStandalone, 16 bytes:
        //   48 89 74 24 18 mov [rsp+18],rsi | 57 push rdi | 48 83 EC 40 sub rsp,40
        //   48 8B 01 mov rax,[rcx] | 48 8B F9 mov rdi,rcx
        static const uint8_t vsProlog[16] = {
            0x48,0x89,0x74,0x24,0x18, 0x57, 0x48,0x83,0xEC,0x40, 0x48,0x8B,0x01, 0x48,0x8B,0xF9 };
        g_origVehicleSpawn = (tCheckSpawn)MakeTrampoline(
            g_base + RVA::Vehicle_CheckSpawn, vsProlog, sizeof(vsProlog));
        if (g_origVehicleSpawn &&
            !InstallJmp("ABravoHotelVehicleSpawnActor::CheckSpawnByStandalone", RVA::Vehicle_CheckSpawn,
                        (void*)&MyVehicleCheckSpawn, vsProlog))
            g_origVehicleSpawn = nullptr;
        L("[ls] Loot-Spawn um Remote-Spieler: GetFirstPlayerController %s, Gebaeude %s, Fahrzeuge %s",
          g_firstPcHooked ? "OK" : "FEHLT", g_origBuildingSpawn ? "OK" : "FEHLT", g_origVehicleSpawn ? "OK" : "FEHLT");
        if (!g_firstPcHooked) L("[!] [ls] ohne GetFirstPlayerController-Hook bleibt der Loot beim Host");
    } else {
        L("[ls] -nospawnremote -> Loot-Spawn-Pruefung nur fuer den Host.");
    }
    // ---- v58i: the world composition commit for tiles at a remote player ---
    {
        // UWorldComposition::CommitTileStreamingState, 22 bytes, all stores:
        //   48 89 5C 24 08 | 48 89 6C 24 10 | 48 89 74 24 18 | 48 89 7C 24 20 | 41 54
        static const uint8_t ctProlog[22] = {
            0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10,
            0x48,0x89,0x74,0x24,0x18, 0x48,0x89,0x7C,0x24,0x20, 0x41,0x54 };
        g_origCommitTile = (tCommitTile)MakeTrampoline(
            g_base + RVA::WorldComp_CommitTile, ctProlog, sizeof(ctProlog));
        if (g_origCommitTile &&
            !InstallJmp("UWorldComposition::CommitTileStreamingState", RVA::WorldComp_CommitTile,
                        (void*)&MyCommitTile, ctProlog))
            g_origCommitTile = nullptr;
        L(g_origCommitTile
              ? "[ct] Kachel-Entscheidungen der Engine werden fuer den Remote-Spieler korrigiert"
              : "[!] [ct] CommitTileStreamingState-Hook FEHLGESCHLAGEN -> Kacheln bleiben wie die Engine sie will");
    }
    // ---- v58j: the three gates between a host actor and a client -----------
    {
        // UReplicationGraph::ReplicateSingleActor, first 15 bytes:
        //   48 8B C4 mov rax,rsp | 55 53 56 57 | 41 54 41 55 41 56 41 57
        static const uint8_t rsProlog[15] = {
            0x48,0x8B,0xC4, 0x55, 0x53, 0x56, 0x57,
            0x41,0x54, 0x41,0x55, 0x41,0x56, 0x41,0x57 };
        g_origRepSingle = (tRepSingle)MakeTrampoline(
            g_base + RVA::RepGraph_ReplicateSingleActor, rsProlog, sizeof(rsProlog));
        if (g_origRepSingle &&
            !InstallJmp("UReplicationGraph::ReplicateSingleActor", RVA::RepGraph_ReplicateSingleActor,
                        (void*)&MyReplicateSingleActor, rsProlog))
            g_origRepSingle = nullptr;
        // UActorChannel::SetChannelActor, 16 bytes:
        //   48 89 5C 24 18 | 55 56 57 | 41 54 41 55 41 56 41 57
        static const uint8_t scProlog[16] = {
            0x48,0x89,0x5C,0x24,0x18, 0x55, 0x56, 0x57,
            0x41,0x54, 0x41,0x55, 0x41,0x56, 0x41,0x57 };
        g_origSetChannelActor = (tSetChannelActor)MakeTrampoline(
            g_base + RVA::ActorChannel_SetChannelActor, scProlog, sizeof(scProlog));
        if (g_origSetChannelActor &&
            !InstallJmp("UActorChannel::SetChannelActor", RVA::ActorChannel_SetChannelActor,
                        (void*)&MySetChannelActor, scProlog))
            g_origSetChannelActor = nullptr;
        L("[pl] Weg der Actors zum Client wird gezaehlt: ReplicateSingleActor %s, SetChannelActor %s",
          g_origRepSingle ? "OK" : "FEHLT", g_origSetChannelActor ? "OK" : "FEHLT");
    }
    // ---- v58g: the client's viewpoint inside the replication graph ---------
    {
        // FNetViewer::FNetViewer, 15 bytes, position independent:
        //   48 89 5C 24 08 mov [rsp+8],rbx | 48 89 74 24 10 mov [rsp+10],rsi
        //   57 push rdi | 48 83 EC 40 sub rsp,0x40
        static const uint8_t nvProlog[15] = {
            0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x74,0x24,0x10, 0x57, 0x48,0x83,0xEC,0x40 };
        g_origNetViewer = (tNetViewerCtor)MakeTrampoline(
            g_base + RVA::FNetViewer_Ctor, nvProlog, sizeof(nvProlog));
        if (g_origNetViewer &&
            !InstallJmp("FNetViewer::FNetViewer", RVA::FNetViewer_Ctor,
                        (void*)&MyNetViewerCtor, nvProlog))
            g_origNetViewer = nullptr;
        L(g_origNetViewer
              ? (g_viewFix ? "[vw] Blickpunkt jeder Verbindung wird protokolliert und fuer Remote-Spieler auf den Pawn korrigiert"
                           : "[vw] Blickpunkt jeder Verbindung wird nur protokolliert (-noviewfix)")
              : "[!] [vw] FNetViewer-Hook FEHLGESCHLAGEN -> kein Blickpunkt-Log");
    }
    // ---- v110: null-map guard, the 0x40 crash -----------------------------
    if (g_mapGuard) {
        static const uint8_t mapProlog[16] = {
            0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x7C,0x24,0x10, 0x8B,0x41,0x08, 0x49,0x8B,0xD8 };
        g_origMapFind = (tMapFind)MakeTrampoline(g_base + 0x011E8200, mapProlog, sizeof(mapProlog));
        if (g_origMapFind &&
            !InstallJmp("Null-Map-Schutz (0x11E8200)", 0x011E8200, (void*)&MyMapFind, mapProlog))
            g_origMapFind = nullptr;
        if (!g_origMapFind) { g_mapGuard = false; L("[mg] [!] Haken fehlgeschlagen -- Schutz AUS"); }
        else L("[mg] Null-Map-Schutz aktiv: eine Null-Map liefert -1 statt zu stuerzen");
    } else {
        L("[mg] -nomapguard erkannt -- der 0x40-Absturz bleibt scharf");
    }

    // ---- v117: channel guard, the actual host crash -----------------------
    if (g_chanGuard) {
        // entry bytes, for the patch: test rdx,rdx | je rel32 | mov [rsp+8],rbx
        static const uint8_t acpEntry[14] = {
            0x48,0x85,0xD2, 0x0F,0x84,0x84,0x01,0x00,0x00, 0x48,0x89,0x5C,0x24,0x08 };
        // trampoline body, taken AFTER the rel32 jump so it stays position
        // independent: mov [rsp+8],rbx | mov [rsp+0x10],rbp
        static const uint8_t acpTail[10] = {
            0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10 };
        g_origActorChanPrep = (tActorChanPrep)MakeTrampoline(
            g_base + 0x03F81A29, acpTail, sizeof(acpTail));
        if (g_origActorChanPrep &&
            !InstallJmp("Kanal-Schutz (0x3F81A20)", 0x03F81A20,
                        (void*)&MyActorChanPrep, acpEntry))
            g_origActorChanPrep = nullptr;
        if (!g_origActorChanPrep) { g_chanGuard = false; L("[cg] [!] Haken fehlgeschlagen -- Schutz AUS"); }
        else L("[cg] Kanal-Schutz aktiv: ein unbrauchbarer Actor wird vor der Kanal-Schicht abgefangen");
    } else {
        L("[cg] -nochanguard erkannt -- der Kanal-Absturz bleibt scharf");
    }

    // ---- v136: guard the graph's per-actor loop ---------------------------
    if (g_graphGuard) {
        static const uint8_t gdEntry[14] = {
            0x40,0x55, 0x53, 0x56, 0x57, 0x41,0x54, 0x41,0x55, 0x41,0x56, 0x41,0x57, 0x48 };
        static const uint8_t gdBody[18] = {
            0x40,0x55, 0x53, 0x56, 0x57, 0x41,0x54, 0x41,0x55, 0x41,0x56, 0x41,0x57,
            0x48,0x8D,0x6C,0x24,0xF9 };
        g_origGraphPerActor = (tGraphPerActor)MakeTrampoline(
            g_base + 0x013C3DB0, gdBody, sizeof(gdBody));
        if (g_origGraphPerActor &&
            !InstallJmp("Graph-Actor-Schutz (0x13C3DB0)", 0x013C3DB0,
                        (void*)&MyGraphPerActor, gdEntry))
            g_origGraphPerActor = nullptr;
        if (!g_origGraphPerActor) { g_graphGuard = false; L("[gd] [!] Haken fehlgeschlagen -- Schutz AUS"); }
        else L("[gd] Graph-Actor-Schutz aktiv: ein toter Actor wird aus der Actor-Schleife"
               " des Graphen herausgehalten");
    } else {
        L("[gd] -nographguard erkannt -- der Absturz in der Actor-Schleife bleibt scharf");
    }

    // ---- v104: pills -------------------------------------------------------
    //  Three hooks, all with byte-verified, position-independent, instruction-
    //  aligned prologues taken from this exact exe. Each one calls the original
    //  first and only ever adds a fallback when the native lookup came back
    //  empty on the audited table/caller.
    if (g_pills) {
        static const uint8_t buffRowProlog[14] = {
            0x48,0x89,0x5C,0x24,0x10, 0x48,0x89,0x6C,0x24,0x18, 0x56, 0x57, 0x41,0x56 };
        static const uint8_t buffIdxProlog[15] = {
            0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10, 0x48,0x89,0x74,0x24,0x18 };
        static const uint8_t spawnRowProlog[14] = {
            0x48,0x89,0x5C,0x24,0x10, 0x48,0x89,0x6C,0x24,0x18, 0x56, 0x57, 0x41,0x56 };

        g_origFindBuffRow = (tFindRowTyped)MakeTrampoline(
            g_base + PILL::FindBuffRow, buffRowProlog, sizeof(buffRowProlog));
        if (g_origFindBuffRow &&
            !InstallJmp("Pillen: FindBuffRow", PILL::FindBuffRow,
                        (void*)&MyFindBuffRow, buffRowProlog))
            g_origFindBuffRow = nullptr;

        g_origFindBuffIndex = (tFindIndex)MakeTrampoline(
            g_base + PILL::FindBuffIndex, buffIdxProlog, sizeof(buffIdxProlog));
        if (g_origFindBuffIndex &&
            !InstallJmp("Pillen: FindBuffIndex", PILL::FindBuffIndex,
                        (void*)&MyFindBuffIndex, buffIdxProlog))
            g_origFindBuffIndex = nullptr;

        g_origFindSpawnRow = (tFindRowTyped)MakeTrampoline(
            g_base + PILL::FindSpawnRow, spawnRowProlog, sizeof(spawnRowProlog));
        if (g_origFindSpawnRow &&
            !InstallJmp("Pillen: FindSpawnRow", PILL::FindSpawnRow,
                        (void*)&MyFindSpawnRow, spawnRowProlog))
            g_origFindSpawnRow = nullptr;

        L("[pill] Haken gesetzt: FindBuffRow %s, FindBuffIndex %s, FindSpawnRow %s",
          g_origFindBuffRow ? "OK" : "FEHLT",
          g_origFindBuffIndex ? "OK" : "FEHLT",
          g_origFindSpawnRow ? "OK" : "FEHLT");
        if (!g_origFindBuffRow || !g_origFindBuffIndex) {
            g_pills = false;
            L("[pill] [!] ohne beide Buff-Haken waere nur die halbe Kette da -- Pillen-Code AUS");
        }
    } else {
        L("[pill] -nopills erkannt -- Pillen bleiben im Auslieferungszustand");
    }

    // ---- v58c: dormancy wake-ups through the mask ---------------------------
    if (g_dormFix) {
        static const uint8_t fndProlog[17] = {
            0x40,0x53, 0x48,0x83,0xEC,0x20, 0x48,0x8B,0xD9, 0x33,0xD2, 0x8B,0x89,0xC0,0x01,0x00,0x00 };
        g_origFlushNetDormancy = (tActorVoid)MakeTrampoline(
            g_base + RVA::AActor_FlushNetDormancy, fndProlog, sizeof(fndProlog));
        if (g_origFlushNetDormancy &&
            !InstallJmp("AActor::FlushNetDormancy", RVA::AActor_FlushNetDormancy,
                        (void*)&MyFlushNetDormancy, fndProlog))
            g_origFlushNetDormancy = nullptr;
        static const uint8_t fnuProlog[16] = {
            0x40,0x53, 0x48,0x83,0xEC,0x20, 0x80,0xB9,0x0F,0x02,0x00,0x00,0x03, 0x48,0x8B,0xD9 };
        g_origForceNetUpdate = (tActorVoid)MakeTrampoline(
            g_base + RVA::AActor_ForceNetUpdate, fnuProlog, sizeof(fnuProlog));
        if (g_origForceNetUpdate &&
            !InstallJmp("AActor::ForceNetUpdate", RVA::AActor_ForceNetUpdate,
                        (void*)&MyForceNetUpdate, fnuProlog))
            g_origForceNetUpdate = nullptr;
        // v58x: DoReload 0x1FF12C0. Sixteen bytes, all position independent,
        // ending exactly on an instruction boundary:
        //   40 53                rex push rbx
        //   48 83 EC 20          sub rsp,0x20
        //   48 8B D9             mov rbx,rcx
        //   48 8B 89 48 06 00 00 mov rcx,[rcx+0x648]
        static const uint8_t drProlog[16] = {
            0x40,0x53, 0x48,0x83,0xEC,0x20, 0x48,0x8B,0xD9, 0x48,0x8B,0x89,0x48,0x06,0x00,0x00 };
        g_origDoReload = (tDoReload)MakeTrampoline(
            g_base + RVA::Character_DoReload, drProlog, sizeof(drProlog));
        if (g_origDoReload &&
            !InstallJmp("ABravoHotelCharacter::DoReload", RVA::Character_DoReload,
                        (void*)&MyDoReload, drProlog))
            g_origDoReload = nullptr;
        L("[rl] DoReload-Hook %s", g_origDoReload ? "OK" : "FEHLT");
        g_flushDormancyFwd = (tActorVoidFwd)g_origFlushNetDormancy;   // v58k
        g_forceNetUpdateFwd = (tActorVoidFwd)g_origForceNetUpdate;   // v58t
        L("[dm] Dormancy-Weckrufe durch die Maske: FlushNetDormancy %s, ForceNetUpdate %s",
          g_origFlushNetDormancy ? "OK" : "FEHLT", g_origForceNetUpdate ? "OK" : "FEHLT");
        // diagnostics: zone removals
        static const uint8_t rnaProlog[16] = {
            0x48,0x89,0x5C,0x24,0x08, 0x57, 0x48,0x83,0xEC,0x30, 0x48,0x8B,0xD9, 0x48,0x8B,0xFA };
        g_origRemoveNetActor = (tRemoveNetActor)MakeTrampoline(
            g_base + RVA::NetDriver_RemoveNetworkActor, rnaProlog, sizeof(rnaProlog));
        if (g_origRemoveNetActor &&
            !InstallJmp("UNetDriver::RemoveNetworkActor", RVA::NetDriver_RemoveNetworkActor,
                        (void*)&MyRemoveNetworkActor, rnaProlog))
            g_origRemoveNetActor = nullptr;
    } else {
        L("[dm] -nodormfix -> FlushNetDormancy/ForceNetUpdate bleiben unter der Maske wirkungslos.");
    }
    // ---- RPC-Weiche (v37) ----------------------------------------------
    // 19 Byte Prolog, alles positionsunabhaengig, exakt auf Instruktionsgrenze:
    //   48 89 5C 24 18  mov [rsp+18],rbx | 55 push rbp | 56 push rsi | 57 push rdi
    //   41 56 push r14  | 41 57 push r15 | 48 81 EC 90 00 00 00  sub rsp,0x90
    static const uint8_t csProlog[19] = {
        0x48,0x89,0x5C,0x24,0x18, 0x55, 0x56, 0x57, 0x41,0x56, 0x41,0x57,
        0x48,0x81,0xEC,0x90,0x00,0x00,0x00 };
    g_origCallspace = (tCallspace)MakeTrampoline(
        g_base + RVA::GetFunctionCallspace, csProlog, sizeof(csProlog));
    if (g_origCallspace) {
        L("[+] GetFunctionCallspace-Trampolin %p", (void*)g_origCallspace);
        if (!InstallJmp("GetFunctionCallspace", RVA::GetFunctionCallspace,
                        (void*)&MyGetFunctionCallspace, csProlog))
            g_origCallspace = nullptr;
    } else {
        L("[!] GetFunctionCallspace-Trampolin FEHLGESCHLAGEN -> RPC-Weiche aus");
    }
    PatchLoadMapCall();
    L("[+] bereit.");
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE hm, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(hm); CreateThread(nullptr,0,Main,nullptr,0,nullptr); }
    return TRUE;
}
