# JaiDOOM — DOOM reimplemented in pure JaiScript

A from-scratch reimplementation of the DOOM engine as JaiScript scripts run by the standalone
`jaiscript.exe` runner, rendering to the terminal with the doom-cli method (sub-cell block
glyphs + 24-bit SGR). Written from the publicly documented WAD/engine formats (Unofficial Doom
Specs et al.); no engine source is transcribed. Test data: `wads/doom1.wad` (shareware 1.9,
freely distributable, md5-verified) and Freedoom 0.13 (BSD) — both gitignored.

## Ground rules

- **Pure JaiScript first.** Everything that CAN be script IS script. The only C++ we accept
  (when P4 needs interactivity) is a small generic console block in `tools/jaiscript/main.cpp`:
  `read_key()/key_down(name)/clock_ms()/sleep_ms(n)/term_size()` — capabilities a script cannot
  express, useful to any terminal game, no engine changes. Until then every phase gate runs
  batch (fixed-dt autopilot frames, files or stdout).
- **GLOOM discipline throughout** (it is the proven architecture): flat typed `array<int>`/
  `array<float>` globals for all hot state, `var&` for every hot array argument, free functions
  on the hot path, palette-index framebuffer + interned escape strings, one frame = one string,
  parallel admission rules respected so `parallel_for`/`parallel_transform` stay available.
- **Fidelity target**: faithful 16.16 fixed-point playsim + BAM angles on int64 (checked
  overflow is safe: 16.16×16.16 products stay < 2^62). Trig tables generated at boot via
  `math::` (low-bit deviation from id's tables — visually identical; demo-lump sync would need
  exact tables + rndtable and is a P7 option, not a P0 constraint).

## Byte access (SUPERSEDED — see Source/JaiScript/docs/char_semantics.md)

Bytes are language-native now: `s[i] + 0` reads the unsigned byte, comparisons promote the
same way, and `to_char(n)` / `\xNN` literals mint bytes. Both language rounds were landed by
Dev off this project's feedback — the WAD parser was the forcing function. Decode stays lazy
per-lump into cached typed arrays; never eager whole-WAD.

History, for the war story: before promotion existed, `wad.jai` self-bootstrapped a decoder by
collecting all 256 distinct chars from the WAD itself, insertion-sorting them by comparison
(char order = byte order up to sign) with `'A'==65` anchoring away signedness, and later a
base64-LUT variant. Retired in full once the contract landed.

## Rendering (doom-cli method)

Palette-index framebuffer `PIX` (`array<int>`, subpixel resolution) -> character cells packing
N subpixels each: half 1x2 (U+2580, fg=top bg=bottom), quad 2x2, sextant 2x3 (bright/dark
partition by luma vs cell mean, glyph by bright bitmask, doom-cli's algorithm) -> truecolor SGR
with escape elision (emit fg/bg only on change — GLOOM's improvement over doom-cli's
full-SGR-per-cell). Light diminishing goes through COLORMAP (authentic banding), palette
flashes switch PLAYPAL banks (all 14 banks pre-interned at boot). Render resolution = terminal
subpixel grid (ARGS-set width, default ~100 cols; no term-size query in the runner yet);
projection is width-parameterized exactly like every source port.

## Module map (scripts/)

wad.jai      WAD directory, byte decode bootstrap, lump cache            [DONE P1]
map.jai      map lumps -> flat parallel typed arrays                     [DONE P2]
vid.jai      palette intern, utf8, framebuffer, cell blitters            [DONE P2 half]
tables.jai   finesine/finetangent/tantoangle/xtoviewangle, fixed helpers [P3]
defs.jai     lump-derived constants, mobj/state tables (own authoring)   [P5]
render.jai   BSP walk, seg clip/project, visplanes, masked/sprites       [P3/P5]
tex.jai      PNAMES/TEXTURE1 composite cache, flats, picture decode      [P3]
sim.jai      35Hz tics, P_TryMove-family collision, slide, blockmap      [P4]
spec.jai     lifts/floors/stairs/teleports/lights/damage/secrets/boss    [DONE P4/P6]
things.jai   mobj pool, states machine, monster AI, combat               [P5]
hud.jai      status bar, fonts, automap, menus, intermission             [DONE P6]
game.jai     game state, episode flow, autopilot harness                 [P4]
main.jai     entry: boot, mode select (batch gates / interactive)        [P4]
demo.jai     demo record/playback + state hash (determinism harness)     [DONE R2]
sound.jai    DS* -> wav cache, positional stereo variants                 [DONE R2]
mus.jai      MUS -> MIDI transcode, MAPxx music table, looping player     [DONE R2]
anim.jai     switch pairs, animated flats                                 [DONE R2]
save.jai     whole-sim JSON save/load                                     [DONE R2]
p*_*.jai     phase gates (each phase proves itself before the next)

## Phase gates

P1 WAD loader ......... GREEN (p1_verify.jai: 1264 lumps, PLAYPAL/COLORMAP exact, E1M1 counts exact)
P2 map data + automap . GREEN (p2_automap.jai: E1M1 silhouette unmistakable, things placed, player @)
P3 BSP renderer ....... GREEN (p3_view.jai: textured E1M1 view, 0 unfilled columns, p3_view.bmp)
P4 playsim ............ GREEN (jaidoom.jai -- selftest: movement/collision/doors;
                        p4_smoke.jai: interactive boot order + level transition)
P5 things ............. GREEN (p5_view.jai: sprites/hitscan/AI/drops/infighting/barrels/lost souls, p5_view.bmp)
P6 presentation ....... GREEN (p6_hud.jai: STBAR/menus/intermission; p6_specials.jai: lifts/floors/
                        stairs/teleports/secrets/tag-666; jaidoom.jai -- flowtest: full screen flow)
P7 perf + feel ........ GREEN (p7_bench.jai: 9.7 ms/frame @ 120x78 autopilot vs 28.5 budget, p7_view.bmp;
                        weapon bob, viewz easing on stairs/lifts, muzzle-flash light boost, FPS counter)
P8 legacy ............. GREEN (p8_doom2.jai: freedoom2 MAP01 + doom2 monsters/music/skies/progression,
                        p8_doom2.bmp; flowtest grew the demo-determinism hash pin + nightmare checks)

## Language showcase pass (2026-07-12)

Post-P7 sweep making the codebase demonstrate JaiScript's marquee features without moving the
perf band (measurements in PERF_BASELINE.md's showcase ledger):

- things.jai .... `monster_brain` coroutine per mobj (handles are values, stored in a hetero array)
- spec.jai ...... `Mover` class + `mover_run` coroutine drive lifts/floors/stairs (cold path)
- game.jai ...... `Door`/`Weapon` classes; jaidoom.jai `Tally` class for intermission records
- vid.jai ....... `vid_row_half` = shared serial/parallel blit row body; `blit_half` runs it through
                  `parallel_transform(ROWIDX, ...)` when `BLIT_WORKERS != 0` (GLOOM's pattern, byte-
                  identical output). Measured honestly: loses at 120x78 (region barrier > 39 rows of
                  work), wins the stage at 200x140 — default serial, knob kept
- tex.jai ....... `tex_composite` pure parallel body single-sources lazy `tex_prepare` AND the
                  map-start warm pass
- render.jai .... `tex_warm_map` composites every referenced wall texture at map start via
                  `parallel_transform` (~1.7x vs serial; first-sight frame spikes moved into load)
- Hot paths stay flat typed-array free functions (IDIOMATIC_COST ruling: classes/coroutines are
  for cold paths; the mobj pool, PIX, clip arrays keep parallel admission)

## Round 2 (2026-07-12): the full game

Tracks 1-3: rocket launcher (weapon 5, MISL projectile + radius damage + rocket-jump shove),
powerups (berserk/invis/radsuit/allmap/goggles/soul/backpack with palette tints and wear-off
warnings), sector light effects (blink/strobe/glow/flicker), player gravity (real z, air arcs,
landing thud/dip), positional audio (pure-script stereo wav variants by distance tier + pan),
classic typed cheats (iddqd/idkfa/idfa/idclip/idclev/idbehold*/iddt), whole-sim JSON save/load
(quicksave Z / quickload X + menu entries), STBAR ARMS panel.

Track 4 (legacy):
- **Demos** (`demo.jai`): `-- record demo.json` / `-- playdemo demo.json`, and the same
  machinery from the menu — DEMOS -> RECORD NEW GAME (arms the tape, then the normal skill
  pick; a REC badge shows top-right; the tape writes at level exit or quit, then disarms) and
  PLAY DEMO (filename entry field, prefilled from the last-used name persisted in
  jaidoom_net.json; missing files report in place; esc during playback drops the tape back to
  the title, a DEMO badge shows while it rolls; tape runout hands the controls to the player).
  Default name "demo.json" both ways, so record-then-play round-trips with zero typing. A
  demo = seed, map, skill, nightmare flag + one 6-int tuple per tic (FWD/SIDE/TURN x10000,
  USE, FIRE, weapon). `math::random_seed` pins the engine RNG at both ends, GAME_TIC restarts
  at 0, recording quantizes CMDs in place so record == replay bit-exactly; the HUD face rolls
  its own LCG so presentation can't perturb the sim stream. flowtest records 200 scripted
  tics, replays them, and asserts the mix32 state hash (positions/hp/tic/sectors) is
  identical — the project's determinism pin — and drives the whole menu path end to end
  (arm, disarm-on-esc, write-at-exit, prefill, replay-to-identical-hash). A demo browser
  would need a host list_files() builtin; the editable-name field is the design until then.
- **DOOM II / freedoom2** (`-- freedoom2 MAP01`): MAPxx naming end to end (progression
  MAP01..MAP30, secret 15->31->32->16), MAPxx music table (D_RUNNIN.., 11+ cyclic), skies by
  map third (SKY1/2/3), INTERPIC intermission fallback, doom2 end text. New monsters admitted
  by an art probe (full anim letters must exist): cacodemon 3005/HEAD (BAL2 fireball), hell
  knight 69/BOS2 (BAL7), SS trooper 84/SSWV, chaingunner 65/CPOS. Skipped (no MINFO row,
  spawns skip silently): revenant, mancubus, arachnotron, archvile, pain elemental,
  spiderdemon, cyberdemon, icon fixtures, SSG + doom2-only pickups.
- **Nightmare** (skill 5, M_NMARE): hard spawn mask + FAST_MONSTERS (speed x1.5, attack
  cooldowns halved at use sites — MINFO untouched) + corpse respawn ~30s where they fell
  (DSTELEPT + TFOG fog, kill tally can pass 100% like vanilla).

## Netplay (up to 16 players: a NetServer star, organized as classes)

Pure script over the runner's generic networking: the host owns a `NetServer` (N framed
clients on one port, per-client signals), each client one `NetChannel`. `netplay.jai` is
class-organized:

	Player            slot / name / frags / deaths / pose / ghost mobj index / lerp
	├─ LocalPlayer    me: pose reads the live P_* globals, builds STATE actions
	└─ Puppet         a remote player: PLAY-sprite ghost driven by the wire
	Session           slot-keyed PLAYERS, the action dispatch table, tic/frame/respawn
	├─ HostSession    NetServer hub: HELLO -> slot + roster, GO start, RELAYS, co-op authority
	└─ ClientSession  one channel to the host; receives host-authored + relayed actions

The mobj pool stays flat (parallel-admission rules); classes hold indices into it. The free
`np_*` functions remain the boundary the game files call — they delegate to `SESSION`.

**Relay semantics:** clients only talk to the host. STATE/FIRE/DIE/RESPAWN apply on the host
and fan out to the other clients (client-to-client latency = 2 hops; the host is the clock).
DAMAGE carries a victim slot and routes to that machine only. FRAGS is a host-broadcast
table (suicide decrements the victim's own score). The host quitting ends the session; a
client quitting frees its slot (BYE_SLOT) and play continues.

**Lobby:** MULTIPLAYER -> HOST DEATHMATCH / HOST CO-OP / JOIN GAME (editable address field,
persisted to `jaidoom_net.json`; every lobby/entry row is width-fitted so narrow terminals
clip nothing). Joiners HELLO (proto + wad fingerprint only — the only refusals besides a
full server), get WELCOME (slot + roster + mode/map/skill, all adopted), and wait; the host
watches the join count and presses ENTER to launch everyone (GO). No mid-game joins.
CLI: `-- host [port] [coop]` / `-- join <host> [port]`, default 28666. 'o' toggles the
frag scoreboard overlay in-game.

**Deathmatch** (each side authoritative for itself): every remote player is a ghost mobj
(state 11, PLAY sprite) lerped between ~17Hz STATE updates; my shots resolve against ghosts
locally and send DAMAGE to that ghost's player; no monsters; frags replace ARMS on the bar;
deathmatch spawns cycle by slot+deaths with an occupancy nudge (no telefrag). Rockets are
VISIBLE remotely: the shooter announces PROJ {id, spr, snd, pos, momenta} once at spawn and
EXPL at the pop — remote machines fly a streamed no-damage twin by dead reckoning (doom
projectiles are straight lines, so the spawn packet is the whole flight) and snap it to the
EXPL point; damage stays shooter-resolved exactly as before.

**Co-op** (host-authoritative): both machines walk the same THINGS lump, so map-placed
things share pool indices — the client spawns its monsters as state-12 PUPPETS at the same
slots, driven by the host's MONS stream (~9Hz dirty-tracked, ~2s keyframes, flat
space-joined int strings — nested containers can't ride from_json, see below). The host
owns doors/movers/exits/items/monster AI: client USE/crossings replay on the host at the
client's borrowed pose — a TELE snap comes back ONLY when a genuine teleport special
(39/97) moved that borrowed pose (the echo is double-gated: moved-vs-borrowed baseline AND
teleport line type, so door/lift replays can never masquerade as teleports), sector heights
stream as SECT diffs EVERY tic (only dirty sectors ride, so an idle map costs nothing and a
moving door lags the client by at most one 2-unit step), projectiles stream as PROJ/EXPL
(host announces every authoritative spawn — monster fireballs, host rockets, replayed
client rockets with the owner slot skipped since it keeps its local cosmetic twin;
receivers dead-reckon a contact-free twin with a ~10s lifetime guard for a lost EXPL),
item claims validate host-side (TAKE -> ITEM echo stamped with the winner's slot, monster
drops by net id), barrels explode host-side (BOOM mirrors the visual), and untargeted
monsters pick the NEAREST live player across the host and every puppet — attacks on a
ghost land on that player as routed DAMAGE, and hurting a monster aggros it onto the
shooter's puppet. Level exit is host-owned in BOTH modes (deathmatch too): a client's own
LEVEL_DONE never acts locally — it travels up as EXIT (once per map, secret-exit flag
included) and the host answers NEXT with its tallies, forcing everyone's intermission.
Clients hold there: a key sends READY once, the footer reads WAITING FOR HOST, esc reaches
a held menu (RESUME returns to the tally, QUIT works, gameplay is unreachable). The host
shows N/M READY, auto-STARTs the moment everyone still connected is ready (recounted fresh,
so a mid-intermission disconnect can't wedge it), and its own key works after a 10s grace
if someone idles. START travels the whole session together; an empty START ends the episode
on both sides' end screens, where a key returns to the title and BYEs the session down
cleanly (a host BYE mid-anything tears the client session down without a crash). Co-op
deaths respawn at your own numbered start with a pistol, KEYS KEPT.

Known edges: players don't collide with each other; powerup visuals aren't mirrored on
ghosts; the client's own rocket is a cosmetic twin of the host's authoritative one (each
side sees exactly one; the twin's blast point can differ by a step at a wall); streamed
projectile twins fly contact-free (a fireball can visually cross a puppet the host's real
one killed a packet earlier — EXPL reconciles within a net tick); puppet interpolation
rubber-bands past ~150ms latency (client-to-client sees two hops of it); light shows and
secret tallies are per-machine (NEXT carries the host's numbers); with 16 players on
classic maps the handful of DM spawns forces the occupancy nudge often — expect
spawn-adjacent chaos by design. Engine edges worth knowing:
maps/arrays read OUT of a from_json result cannot be cloned or passed as script-function
arguments ("missing engine pointer") — the protocol hoists primitives at the dispatch site
(consecutively: interleaving a foreign container read between from_json reads trips it too)
and ships batches as flat strings; and a bare typed-array read as a map-literal VALUE
(`{"x": MO_X[i]}`) serializes as a reference wrapper — hoist pool reads into locals before
building actions. Bandwidth at 29 awake monsters ~ 1.2KB per MONS keyframe; 16-player STATE
relay ~ 16 x 17Hz x ~90B = ~25KB/s at the host, trivial on LAN.

Decorations: the THINGS walk spawns map decor as state-10 mobjs from the DECOR table
(things.jai; type -> sprite/anim/radius, art-gated like doom2 monsters, anim letters probed
from the WAD so animated art cycles). Solidity rides MO_RAD: columns/lamps/candelabras
block the player (pos_ok, with an escape rule so an overlap never wedges) and monsters
(mobj_pos_ok); gore ships radius 0 and never collides. Decor joins the SAME shared walk as
monsters/pickups, so co-op pool indices stay aligned (p9 pins the walk signature host vs
client). Decor is not shootable and does not block shots.

Presentation: level exits melt — the classic screen wipe (vid.jai wipe_begin/tic/overlay:
the departing frame slides down in ragged, neighbor-correlated columns over the fresh
intermission/end screen). It runs only in play()'s draw path on its own LCG; gates and the
sim never see it (flowtest pins termination, monotonic fall, and math::random isolation).

Final gate roster, all GREEN: p1_verify, jaidoom selftest, p4_smoke, p5_view (incl. the
decoration census/collision/render section), p6_specials, p6_hud (incl. WAITING FOR HOST
footer + mid-melt composite), jaidoom flowtest (menu/address/persistence + wipe pins),
p7_bench, p8_doom2, p9_netplay (dm + co-op + exit-sync rounds + the INVERTED harness: a
real ClientSession vs a scripted fake host with ~150ms-late deliveries).

## Verification culture

Every phase has a machine-checkable gate script (counts vs known-good, hashes, file previews)
plus an eyeball artifact (ANSI frame / ASCII silhouette). Determinism: fixed TICK, one seeded
RNG, GLOOM-style state hash once the sim exists; interpreter-vs-vm parity via --backend once
hot. Perf on the Release runner only (`out/build/x64-Release BENCHMARKS/bin/jaiscript.exe`).
