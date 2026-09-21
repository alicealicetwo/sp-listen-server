# Changelog

* **v174** — diagnostics for the open replication bugs: `[cs]` callspace log per net function, per-class actor-channel counters, `DoorState`/HP probe on pushed doors. No behaviour change.
* **v173** — blue zone fixed (zone discovery before the first join); `-noanimtick`; ack/correction split in the movement counters.
* **v172** — perk push after the perk RPCs; door/window push (`-nodoorpush`); `-zonepolicy` experiment; zone-channel close diagnostic; graph per-class counters.
* **v171** — `-noperkwake`; movement counters; `-rpcnames`.
* **v170** — clean restructure of v169 (12,800 → 4,037 lines, 21 sections, one switch table). Same hooks, RVAs and defaults; ProcessEvent verdict cache; diagnostics removed.
* **v42–v169** — see `docs/history/`.
