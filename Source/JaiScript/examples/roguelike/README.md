# jai_rogue — JAI'S DESCENT

A complete ASCII roguelike whose **entire game lives in JaiScript** (`scripts/*.jai`,
~2,700 lines). The C++ host (`main.cpp`) is deliberately thin: a VT-enabled console,
a blocking `read_key()`, a seeded deterministic `Rng` class, file IO for saves, and
command-line flags surfaced as globals. Everything else — dungeon generation, FOV,
combat, monster AI, items/affixes, identify-by-use, leveling, saves via
`to_json`/`from_json`, the UI itself — is script.

It doubles as a **cross-backend determinism test**: the same seed must produce the
same state hash on the tree-walking interpreter and the bytecode VM.

## Build

Part of the normal JaiScript CMake build (`JAISCRIPT_BUILD_EXAMPLES=ON` by default):

```bash
# from Source/JaiScript (VsDevCmd + Ninja, same as the test builds)
cmake --build out/build/x64-Debug --target jai_rogue
# exe: out/build/x64-Debug/bin/jai_rogue.exe
```

The exe prefers the *source* `scripts/` directory (so `--dev` hot reload edits the
files you have open); a `rogue_scripts/` copy is placed next to the binary for
distribution.

## Play

```bash
jai_rogue.exe                     # new run, seed 1337, VM backend
jai_rogue.exe --seed 7 --backend interpreter
jai_rogue.exe --load              # continue a saved run
jai_rogue.exe --dev               # 'R' re-imports scripts mid-game (hot reload)
jai_rogue.exe --god               # tourist mode
```

Needs a Windows 10+ console at least 80x28 (ANSI colors via VT processing).

| key | action |
|---|---|
| `h j k l` `y u b n` / arrows | move / attack (8-way) |
| `.` | wait |
| `g` | pick up |
| `i` | inventory — letter to inspect, then `e` use/equip, `d` drop |
| `q` | quaff first potion |
| `E` | equip the best upgrade in the pack |
| `z` | class skill (auto-targets nearest visible enemy) |
| `>` | descend (on stairs) |
| `c` / `?` | character sheet / help |
| `S` / `Q` | save & quit / quit |
| `R` | hot-reload scripts (with `--dev`) |

Ten floors down, permadeath, a boss on 10. Killing him is the win. You catch
your breath (+1 HP / 5 turns) only while nothing is watching; murals and
potions do the serious mending.

## The world

The persistent folk are anthro, digitigrade **Kin**. Two legends watch over the
Descent: **JAI**, the jackodile of clever escapes (his statues `&` bless your
evasion), and **DEVACOR**, the wingless dragon whose **rainbow fire** lit the world
(his murals `%` heal you once per floor). The final boss, **Vexadrach the
Hollow-Flame**, swallowed Devacor's rainbow fire and choked the color out of it —
he breathes gray "hollow fire" until you carve the real thing back out of him.
Victory releases it: the finale cycles all seven colors across the arena.

**Species** (stat nudges + a trait): Jackodile-kin (Slip-Jaw: +20 evasion when
bloodied), Drake-kin (Scaleblood: half burn damage), Vulpen (Keen Eye: +8% crit),
Brockin (Too Stubborn To Die: survive one killing blow per floor), Pangolix
(Curl Up: +3 def when dropped below a third).

**Classes**: Warden (melee, Bulwark Slam 250%), Emberwright (caster, Emberbolt +
burning), Thornscout (skirmisher, cheap Thorn Volley, high evasion), Hexweaver
(debuffer, Wither Hex −atk/−def + drain).

**Bestiary** (trope-twisted): Grublin, Grublin Slinger, Gloop, Snarlbat, Scrappit
(pack AI), Rattlekin, Grinnest (mimic — that `?` on the floor might be teeth),
Orglis, Hexmoth, Marrowmaw, Cinderwisp, Drakelet, and Vexadrach. AI behaviors:
chaser, ranged skittisher, pack, erratic, sluggish, mimic — and the boss, whose
telegraph→breath→recover choreography is a genuine **coroutine** (`BOSS_CO`)
holding its phase state across turns. (Per-monster coroutine brains were the
original design; the language's value semantics vetoed it — see Dogfooding
notes.) The smoke autopilot and the rainbow finale are coroutines as well.

**Items**: weapon/armor/trinket slots; rarity common→legendary with affix
generation ("Grubby Shiv of Nipping", "Vicious Fang Blade of the Ember");
potions/scrolls are identify-by-use with per-seed appearance shuffles; timed
effects (burning, regen, rage, stoneskin, hexes) are serializable data records
ticked each turn — which is why buffs survive save/load exactly.

## Determinism smoke test

`--smoke` runs a headless, deterministic autopilot (BFS to the stairs, fights what
blocks it, sips potions when scared, picks level-up boons from the rng) and prints
a running FNV-fold `STATE_HASH` of the world after every turn. Same seed ⇒ same
hash, **on both backends**:

```bash
jai_rogue.exe --smoke --seed 99 --turns 250 --backend vm
jai_rogue.exe --smoke --seed 99 --turns 250 --backend interpreter
# STATE_HASH must match between the two runs
```

It is intentionally *not* part of the Foundry suite — it's a standalone parity
canary you can point at any seed. Verified at the time of writing: six seeds x
1,500 turns hash-identical across backends, and a `--god --smoke --turns 3000`
run reaches and kills Vexadrach on both (prints `victory: true`, same hash).
Release runs ~50 turns/s; Debug ~2 turns/s. Ungodded pilots die on floors 3–10
(median ~5-6), which is the intended curve for a tactics-free speedrunner.

Scripted input is also available for replayable interactive tests:
`--input "11;jjjllg>" --quiet` feeds keys directly to `read_key()` — each
character is a key, `;` stands in for enter and `^` for esc, and the game
hard-quits when the string runs out.

## Multiplayer design sketch (not implemented)

The clean seam is already in this example: **the game is a pure function of
(seed, key stream)**, and the host owns nothing but IO.

- **Server-authoritative turn loop.** One JaiScript `engine` per session
  (JaiScript's zero-static design means engines are fully isolated — sessions
  can't leak into each other, and one process can host many). The server runs
  `game.jai` exactly as here; clients are dumb terminals sending keys.
- **Protocol.** Client → server: `{"session": id, "key": "j"}`. Server → client:
  either the full frame string (trivial, telnet-style) or a JSON state delta
  (`to_json` of the visible slice — the save-game serializer already produces
  exactly this shape; FOV filtering happens server-side so clients can't peek).
- **Where MV slots in.** MV's asio networking (`Source/MV/Network`) provides the
  session transport; each connection pins a session actor that owns the engine
  and serializes key events into its turn queue (turn-based = no tick loop, just
  request/response). Spectators subscribe to the same frame stream read-only.
- **Persistence.** The save format *is* the sync format: rejoin = `load_game()`
  from the session's last `to_json` snapshot.
- **Anti-cheat for free.** Clients never hold the rng; the seed and all rolls
  live server-side, and the smoke hash doubles as a desync detector between
  replicas.

## Files

```
main.cpp            thin host: console, keys, Rng, file IO, flags
scripts/state.jai   persistent globals (G, RNG) — excluded from hot reload
scripts/util.jai    colors, hashing, direction tables
scripts/data.jai    species/classes/bestiary/items/affixes/flavor
scripts/entities.jai Entity/Monster/Player/Item classes + timed effects
scripts/dungeon.jai rooms+corridors gen, FOV, BFS
scripts/items.jai   loot gen, inventory, potions/scrolls
scripts/combat.jai  attack resolution, XP, class skills
scripts/ai.jai      monster behaviors + the boss fight coroutine
scripts/ui.jai      frame composition, modals, rainbow finale
scripts/game.jai    Game class, turn loop, saves, autopilot
scripts/main.jai    imports + entry + hot reload list
```

Known scope cuts: the boss coroutine restarts its phase pattern after a load
(his stats persist; his choreography forgets); monster pathing is
greedy-with-slide rather than full A*.

## Dogfooding notes (what building this taught us about JaiScript)

The full write-up lives with the commissioning report; headlines, all
reproduced in-code with comments at the workaround sites:

- **Value semantics are real.** Objects deep-copy on assignment, `var` params,
  array push, and function return. Everything that mutates the world goes
  through `var&` parameters, chained lvalue assignment, methods, or `auto&`
  range-for; entity lookups return *indexes*, never objects.
- **Coroutine handles only live in plain variables.** Storing one in a field,
  array, or map deep-copies — and `coroutine_handle` refuses to copy (throws).
  Hence the named global slots in `state.jai` and procedural per-monster AI.
- Bugs found and reported: interpreter-only "Type mismatch" for
  `if (m.has(k)) { int_field = m[k]; }` inside a ctor (VM converts); unary
  minus on any container-element read throws on both backends; script-class
  *methods* reject container-element reads as typed args ("No matching
  overload") where free functions accept them; `from_json` trees cannot be
  deep-copied at all ("missing engine pointer", uncatchable) — the loader does
  schema-driven leaf reads instead.
