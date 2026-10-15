# JaiDOOM — DOOM reimplemented in pure JaiScript

A from-scratch reimplementation of the DOOM engine as JaiScript scripts run by the standalone
`jaiscript.exe` runner, rendering to the terminal with the doom-cli method (sub-cell block
glyphs + 24-bit SGR). Written from the publicly documented WAD/engine formats (Unofficial Doom
Specs et al.); no engine source is transcribed. Test data: `wads/doom1.wad` (shareware 1.9,
freely distributable, md5-verified) and Freedoom 0.13 (BSD) — both gitignored.

## Ground rules

- **Pure JaiScript first.** Everything that CAN be script IS script. The only C++ is the
  runner's small generic console block in `tools/jaiscript/main.cpp`:
  `key_down(name)/read_key()/term_cols()/term_rows()` — capabilities a script cannot express,
  useful to any terminal game, no engine changes (clocks and sleeps are the stdlib's `time::`).
  The gate scripts run batch (fixed-dt autopilot frames, files or stdout).
- **GLOOM discipline throughout** (it is the proven architecture): flat typed `array<int>`/
  `array<float>` globals for all hot state, `var&` for every hot array argument, free functions
  on the hot path, palette-index framebuffer + interned escape strings, one frame = one string,
  parallel admission rules respected so `parallel_for`/`parallel_transform` stay available.
- **Fidelity target**: faithful 16.16 fixed-point playsim + BAM angles on int64 (checked
  overflow is safe: 16.16×16.16 products stay < 2^62). Trig tables generated at boot via
  `math::` (low-bit deviation from id's tables — visually identical; vanilla demo-lump sync
  would need exact tables + rndtable, so JaiDOOM's demos are its own format, `demo.jai`).

## Byte access (see Source/JaiScript/internal/char_semantics.md and guide ch. 9, "Binary data")

The WAD is one string. Structured reads go through `binary::reader` (`lump_reader(idx)` puts one
at a lump's first byte; little-endian, the format's order): map lumps read record by record, the
16-byte directory entry is the `binary::layout` `LUMP_ENTRY` (its name a `binary::text(8)` field: lump
names are NUL-padded and may fill all 8 bytes), PNAMES/TEXTURE1 and patch headers read with
`int16`/`uint32`/`text(8)`. Bulk bytes (COLORMAP, PLAYPAL banks, flats, sound samples)
come out of `to_bytes` as `array<uint8>` and land in the `array<int>` tables the renderer indexes (one widening copy per lump). The files the game writes (WAV, MIDI, BMP) are built
with `binary::writer` (tags, samples and pixel rows through `bytes`, which writes them as they are). Post data inside patch columns is still walked byte by byte (`WAD[post] + 0`
reads the unsigned byte). Texture compositing is a `parallel_transform` body, which admits no host
method calls, so each patch's column table is decoded ahead of it (`tex_patches_prepare`) into
typed arrays. Decode stays lazy per-lump into cached typed arrays; never eager whole-WAD.

## Rendering (doom-cli method)

Palette-index framebuffer `PIX` (`array<int>`, subpixel resolution) -> character cells packing
N subpixels each: half 1x2 (U+2580, fg=top bg=bottom), quad 2x2, sextant 2x3 (bright/dark
partition by luma vs cell mean, glyph by bright bitmask, doom-cli's algorithm) -> truecolor SGR
with escape elision (emit fg/bg only on change — GLOOM's improvement over doom-cli's
full-SGR-per-cell). Light diminishing goes through COLORMAP (authentic banding), palette
flashes switch PLAYPAL banks (all 14 banks pre-interned at boot). Render resolution = terminal
subpixel grid (`term_cols()`/`term_rows()`, or a width set by ARGS);
projection is width-parameterized exactly like every source port.

## Module map (scripts/)

jaidoom.jai        entry: playable game, title/menus, CLI modes (selftest, flowtest, record,
                   playdemo, freedoom2, host, join)
boot.jai           fetches and SHA-256-verifies Freedoom and the shareware WAD, unpacks, decodes
                   sound and music into cache/
wad.jai            WAD directory and lump access through binary::reader, lump cache
map.jai            map lumps -> flat parallel typed arrays
tex.jai            PLAYPAL/COLORMAP, flats, patches, TEXTURE1 composites, PALCM
render.jai         BSP walk, textured walls and flats, COLORMAP light, sky, sprites
vid.jai            palette intern, framebuffer, cell blitters, BMP/utf8 output, melt wipe
game.jai           playsim: 35Hz tics, movement, blockmap collision and slide, doors, use lines
spec.jai           specials: Mover class + lifetime coroutines, teleports, stairs, lights,
                   damage floors, secrets, boss triggers
things.jai         sprites, mobj pool, monster AI (per-mobj brain coroutine), combat, decor
hud.jai            status bar, face, automap, title, menus, intermission, episode end
anim.jai           switch pairs, animated flats
sound.jai          DS* -> WAV, positional stereo variants
mus.jai            MUS -> MIDI transcode, MAPxx music table
demo.jai           demo record/playback + state hash (determinism harness)
save.jai           whole-sim JSON save/load
netplay.jai        up to 16 players: Player/Session class hierarchy over NetServer/NetChannel
jaidoom_demo.jai   scripted fly-through demo on the stock runner
extract_audio.jai  standalone sound/music extraction into cache/
jaidoom_net.json   persisted lobby address and last demo name
p1_verify .. p9_netplay   gate scripts (below)
p10_timedemo.jai   uncapped demo replay with a per-stage time split (profiling)

## Gate scripts

p1_verify.jai ..... WAD loader: 1264 lumps, PLAYPAL/COLORMAP exact, E1M1 counts exact
p2_automap.jai .... E1M1 top-down: silhouette, things placed, player @
p3_view.jai ....... first-person E1M1 view, 0 unfilled columns, p3_view.bmp
jaidoom.jai -- selftest   movement/collision/doors
p4_smoke.jai ...... interactive boot order + level transition
p5_view.jai ....... sprites/hitscan/AI/drops/infighting/barrels/lost souls, decor, p5_view.bmp
p6_hud.jai ........ STBAR/menus/intermission, WAITING FOR HOST footer, mid-melt composite
p6_specials.jai ... lifts/floors/stairs/teleports/secrets/tag-666
jaidoom.jai -- flowtest   full screen flow, menu/address persistence, demo-determinism hash pin,
                   nightmare checks, wipe pins
p7_bench.jai ...... headless autopilot frame time at 120x78 against the 28.5 ms (35fps) budget;
                   weapon bob, viewz easing, muzzle-flash light boost, FPS counter
p8_doom2.jai ...... freedoom2 MAP01 + doom2 monsters/music/skies/progression, p8_doom2.bmp
p9_netplay.jai .... deathmatch + co-op + exit-sync rounds, plus a real ClientSession against a
                   scripted fake host with ~150ms-late deliveries

## Language features in the codebase

- things.jai .... `monster_brain` coroutine per mobj (handles are values, stored in a hetero array)
- spec.jai ...... `Mover` class + `mover_run` coroutine drive lifts/floors/stairs (cold path)
- game.jai ...... `Door`/`Weapon` classes; jaidoom.jai `Tally` class for intermission records
- vid.jai ....... `vid_row_half` = shared serial/parallel blit row body; `blit_half` runs it through
                  `parallel_transform(ROWIDX, ...)` when `BLIT_WORKERS != 0` (GLOOM's pattern, byte-
                  identical output). Default serial: at play size the region barrier costs more
                  than the rows; the parallel form pays off at large terminals
- tex.jai ....... `tex_composite` pure parallel body single-sources lazy `tex_prepare` AND the
                  map-start warm pass
- render.jai .... `tex_warm_map` composites every referenced wall texture at map start via
                  `parallel_transform`, so first-sight frame spikes move into load
- Hot paths stay flat typed-array free functions: classes and coroutines are for cold paths,
  and the mobj pool, PIX and clip arrays keep parallel admission

## The full game

Rocket launcher (weapon 5, MISL projectile + radius damage + rocket-jump shove),
powerups (berserk/invis/radsuit/allmap/goggles/soul/backpack with palette tints and wear-off
warnings), sector light effects (blink/strobe/glow/flicker), player gravity (real z, air arcs,
landing thud/dip), positional audio (pure-script stereo wav variants by distance tier + pan),
classic typed cheats (iddqd/idkfa/idfa/idclip/idclev/idbehold*/iddt), whole-sim JSON save/load
(quicksave Z / quickload X + menu entries), STBAR ARMS panel.

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
  (arm, disarm-on-esc, write-at-exit, prefill, replay-to-identical-hash). Demos are chosen
  through the editable-name field; there is no demo browser.
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
EXPL point; damage stays shooter-resolved.

**Co-op** (host-authoritative): both machines walk the same THINGS lump, so map-placed
things share pool indices — the client spawns its monsters as state-12 PUPPETS at the same
slots, driven by the host's MONS stream (~9Hz dirty-tracked, ~2s keyframes, flat
space-joined int strings). The host
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
spawn-adjacent chaos by design. Bandwidth at 29 awake monsters ~ 1.2KB per MONS keyframe; 16-player STATE
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

## Verification culture

Every subsystem has a machine-checkable gate script (counts vs known-good, hashes, file
previews) plus an eyeball artifact (ANSI frame / ASCII silhouette). Determinism: fixed TICK, one
seeded RNG, GLOOM-style state hash; interpreter-vs-vm parity via --backend. JaiDOOM is not one
of the `tools/ci/gates.sh` gates. Perf on the Release runner only (`out/build/x64-Release BENCHMARKS/bin/jaiscript.exe`).
