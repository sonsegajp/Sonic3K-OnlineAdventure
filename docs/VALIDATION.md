# Shared-world validation

1.1.0-preview.15 (network protocol 15), October 7, 2026. Development preview; not published.

## Final build results

All checks below ran on one unchanged private DLL (`540113505a8a6b36…`) against separate copies of the unchanged official executable. The production DLL (`00017b8101ad67c6…`) was built with `OA_TEST_CONTROL=OFF`, reached native Data Select in the official executable, and has no private test socket.

| Run | Result |
| --- | --- |
| Lava Reef mine boss through Hidden Palace arrival | Pass |
| Hidden Palace Knuckles fight, theft scene, teleporter, Sky Sanctuary | Pass |
| Flying Battery Act 2 laser set piece | Pass |
| Flying Battery miniboss (native plunger) | Pass |
| Death Egg Act 2 boss, exits, capsule rescue, final-act transition | Pass |
| Death Egg final boss through the party ending | Pass on a standalone rerun; the batch run crashed inside the fixture |
| Doomsday Sonic-only access and spectators | Pass |
| Doomsday boss through the party ending | Pass |
| Slotted badnik art slots | Pass |
| Core regression, 13 checks | 13 / 13 pass |
| Eight players, visible windows, network impairment, live palette and minimap | Pass |

After the batch, one UI-only change limited keyboard and gamepad navigation in the overlay to menus a player opens. The in-game status panel no longer takes focus. That build passed a real-keyboard check (keys reach the game after clicking the panel), the eight-player visible run and the production startup check. The rest of the batch was not repeated for it.

The bounded 29-root boss matrix was not rerun on this build; those results come from earlier builds. Several fixtures were intermittent during this session (Death Egg act 2 mine fight, Doomsday missile routing, the core goal-sign check). They passed on this build, but repeated failures deserve investigation rather than reruns.

## Changes covered by this round

- Lava Reef's mine boss starts on every client, and its capsule, results, exit and the Hidden Palace arrival are shared.
- Level changes without a results screen wait for the whole party: Lava Reef 3, Death Egg 3, Sky Sanctuary into the Death Egg, and both endings.
- The Death Egg final boss runs through fingers, chest, head, escape and the party ending. Players inside an upper or lower teleporter capsule when the Act 2 boss falls can still ride back to the exit floor.
- Doomsday is played only by Sonic players when all seven emeralds are collected. Tails and Knuckles players watch a Sonic player's live view, and everyone enters the ending together.
- Hidden Palace's Knuckles fight and theft scene play on every screen, with Knuckles' health shared and no double-counted hits. The teleporter takes the party to Sky Sanctuary.
- Flying Battery Act 2's laser set piece runs identically on every client, and level music returns for everyone after it and other minibosses.
- Slotted badniks such as Rhinobots no longer leak their art slots on other clients. Before, they and the end sign sharing their slot array could stop appearing.

## What is shared

The session has a canonical object registry keyed by epoch, internal layout, visible act, and object identity. Native object addresses, including multipart platform parents at offset $2E, are translated to identities across clients. One client simulates each object's authoritative state; nearby clients retain native collision and rendering and reconcile to that state. Ownership can move with contact, distance, or disconnection.

Consumed objects and destroyed objects remain recorded even when a player walks away. Monitor claims grant exactly one reward to the collector; an unconfirmed local break request is not replicated as another player's collision. Terrain edits use conditional updates so an old local copy cannot undo a newer change. Foreground refreshes write only the native foreground name table and leave game RAM and character art untouched. Ordinary rings, dropped rings, and attracted rings use shared identities. Personal player inventory, cameras, checkpoint respawn positions, result screens, entrance controllers, and private special-stage trips remain personal.

The v0.5.2 adapter also corrects a native stack-bookkeeping defect in four verified column/ice-block routines. Two word-sized temporary saves were not counted against a longword restore, causing return slots to accumulate and eventually overwrite the graphics queue. The unmodified executable reproduced the Ice Cap crash under its strict stack checker. The adapter corrects bookkeeping at the native function entries without changing the executable file or editing generated game code.

The gameplay world uses replicated authoritative objects and terrain. recomp-net and rbengine run the confirmed shared progression model, including barriers and emerald awards. This is not full-machine gameplay rollback, and a zero-desync progression result does not prove every native level script is synchronized.

## Test environment

- Separate processes of the unchanged official Windows x64 v0.5.2 executable.
- Executable SHA-256: `390d3a3448a70afabd0e0b04e703a2a299fa961fc4182b8e73396b3377185dfd`.
- Native integration fixtures use a private build with `OA_TEST_CONTROL=ON`; it is not distributed. Fixtures place players/objects or seed a specific trigger, then check the original game's collision and transition behavior.
- The packaged build uses `OA_TEST_CONTROL=OFF`. Production startup and absence of its private control socket are checked separately.

## Coverage

The accompanying evidence report records the final results of this build's checks. Test scripts and their scope:

| Check | What it establishes |
| --- | --- |
| `shared_world_test` | Eight contenders grant one monitor reward; canonical state survives ownership changes, reordered updates, offscreen unloads and late observation. Codec truncation, terrain merging and internal-layout separation are checked. Foreground refreshes preserve RAM and all VRAM outside the foreground name table, with native block flips and invalid/wrapped coordinates. |
| `remote_motion_test` | Eight simulated remote streams with jitter, loss, stalls, warps and tick wrap; bounded interpolation and no frozen repeated frames during continuous motion. Native 2048/4096-pixel vertical loops preserve interpolation, viewport visibility and carry distances. |
| `test_carried_contacts.py` | Three rendered native clients: a carried Knuckles launches from a spring and is released, is hurt by a badnik and is released, and dropped rings appear for observers without subtracting their inventory. |
| `test_shared_rings.py` | Native ordinary-ring and lightning-attraction contention awards one collector and consumes the ring for all three clients. |
| `test_shared_native_objects.py` | A monitor and badnik created on one client appear in different native slots on the others; the monitor reward goes only to its collector and the badnik's destruction is shared. |
| `test_shared_terrain.py` | A controlled edit of level geometry propagates to all three clients, redraws a stationary camera and accepts a subsequent edit from a different player. This does not prove every natural wall-breaking script. |
| `test_shared_platform_goal.py` | A native collapsing platform is shared; the first player at a shared sign waits while distant players keep control; all three ultimately advance together. |
| `test_shared_fire_phase.py` | Native Angel Island fire/layout replacement holds for a distant player, then loads the burned section on all three clients. Death recovery, a fresh process rejoin, and another death after that rejoin preserve the layout and restore the player beside the party. Native placement cursors are rebuilt after the camera jump to prevent respawn-table writes from corrupting camera limits. |
| `test_native_shared_boss.py` | Native Angel Island miniboss contacts alternate between three players, with shared health and defeat. The fixture isolates combat from normal arena traversal. |
| `test_shared_campaign_objects.py` | Controlled shared-monitor and collector-only reward checks in Act 1 of the eleven other regular zones. The test-only stage request changes IDs at the native level-loader entry so an old event script cannot run with a new zone ID. The fixture completes native entrance sequences, including the jump out of Ice Cap's snow and Sandopolis's sand, before manipulating a monitor. This is an object-pipeline smoke check, not a playthrough of those acts. |
| Eight-player adventure regression | All-character intro, separated title-card positions, remote sprites, private special-stage entry/return, shared emeralds, next unfinished special stage, reconnects and the all-player clear barrier under transport impairment. |
| `test_lrz_exit_native.py` | Lava Reef's native mine boss through the flying capsule, shared results, each client's boss exit and camera widening, and the party-gated arrival in Hidden Palace. |
| `test_hpz_knuckles_native.py` | Hidden Palace's Knuckles fight with eight shared hits and no double counting. Each client then plays the native theft scene, its own scripted walk and the teleporter, and the party reaches Sky Sanctuary. Knuckles runs his own AI on each screen; only his health is shared. |
| `test_fbz2_laser_native.py` | Flying Battery Act 2 laser set piece: native trigger, side walls, seven laser cycles, defeat, camera release, level music restored on every client, and continuation past the arena. |
| `test_boss_fbz_button_v7.py` | Flying Battery miniboss: native plunger contacts and the boss's own punch through zero health and defeat. |
| `test_dez_exit_native.py` | Death Egg Act 2 boss defeat, every client's own exit controller, the first player held at StartNewLevel while others remain, then everyone entering the final boss layout together. |
| `test_dez_final_native.py` | Death Egg final boss: fingers, chest and head phases on three clients, the escape, and the party ending. Boss health is never written. |
| `test_doomsday_spectator.py` | With seven emeralds, only Sonic players enter Doomsday; Tails/Knuckles players are held and receive a Sonic player's live view. |
| `test_ddz_boss_native.py` | Doomsday: missile redirection through shared health, the Super flight phase, defeat and the party ending. |
| `test_slotted_badniks.py` | Five shared Rhinobot destructions leave every client's VRAM slot bits clean, and a later Rhinobot still appears on every client. Before the fix one destruction leaked bits, so slotted badniks, and the end sign sharing their slot array, could stop appearing. |
| Bounded boss matrix (`test_campaign_boss_v6.py` and earlier versions) | Positioned contacts and shared health through zero for the inventoried boss roots. This matrix was not rerun on the final build; its passes come from earlier private builds and are labeled that way. Encounters with dedicated fixtures above were rerun on the final build. |
| `test_production_mod.py` | The distributed DLL loads into the unchanged official executable and reaches Data Select with its private test socket absent. This is a startup smoke test, not an interactive multiplayer test of the production archive. |

## Coverage limits

This is not a completed campaign-wide acceptance test. Foreground/background event scripts are not all covered by the generic entity registry. Angel Island's internal fire phase has an explicit confirmed barrier; other zone-specific mechanisms, seamless transitions and every boss must still be tested in normal traversal.

Additional work includes long sessions with many dynamic spawns, native object-pool exhaustion, unusual object deletion paths, manual allocations outside the normal allocator, simultaneous contacts on complex multipart bosses, changes during native asset loading, reconnects during every zone-specific phase, wide-area network conditions and sustained user-perceived motion/input/audio checks. Eight-player progression tests do not replace these checks.

The release includes no ROM, game executable, saves, generated game source, test-memory sockets or game assets.
