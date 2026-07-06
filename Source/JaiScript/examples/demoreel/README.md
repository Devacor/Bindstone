# jai_demoreel — a demoscene tech reel written in JaiScript

Eight auto-advancing truecolor scenes, transitions, title cards, and a finale where
the word JAISCRIPT ignites out of a wall of rainbow fire — every effect coded in
`scenes/*.jai`. The C++ host (`main.cpp`) is a thin shell: a VT console, a
monotonic clock, non-blocking keys, a seeded rng class, and the frame pump.
Everything you see is script.

The reel is also an engine showcase: it keeps **two live engines** warm — one
tree-walking interpreter, one bytecode VM — running the same scripts, and lets you
flip between them mid-run. It doubles as a parity test: in `--smoke` mode both
backends must produce **byte-identical** frame streams.

## Build

Part of the JaiScript CMake tree (target `jai_demoreel`):

```
cmake --build out/build/x64-Debug --target jai_demoreel
cmake --build "out/build/x64-Release BENCHMARKS" --target jai_demoreel
```

The exe prefers the source `scenes/` dir (so hot reload edits the real files),
falling back to `demoreel_scenes/` copied next to the binary.

## Run

```
jai_demoreel                     # the reel, VM backend active, 30 fps pacing
jai_demoreel --backend interp    # start on the interpreter instead
jai_demoreel --smoke             # headless: both backends, frame-hash parity + perf
jai_demoreel --smoke --frames N  # smoke frame budget (default 1050)
jai_demoreel --capture I         # print scene I as plain text (ANSI stripped)
jai_demoreel --reload-test       # headless hot-reload + backend-swap self-test
jai_demoreel --precompiled       # boot from demoreel.jaib (saved on first run)
```

Keys while running:

| key | action |
| --- | ------ |
| `TAB` | swap live backend (interpreter <-> VM); scene state re-synced, stateful scenes replay up to 3 sim-seconds |
| `r` | hot-reload every `scenes/*.jai` from disk into BOTH engines |
| `h` | toggle the HUD (scene, ACTIVE BACKEND, frame ms, fps) |
| `f` | freeze the clock (render keeps running) |
| `n` / `p` / space / arrows | next / previous scene |
| `1`..`8` | jump straight to a scene |
| `q` / `ESC` | quit |

The HUD shows the active backend and its frame cost, plus the last measured cost
of the other backend — press TAB and watch the ms number move.

## The scenes

| # | scene | what it is |
| - | ----- | ---------- |
| 1 | PLASMA | the classic summed sine field, 96-color cycling truecolor palette, all integer table lookups per cell |
| 2 | STARFIELD | 3D perspective star streaming with speed-scaled trails (flat parallel arrays on purpose) |
| 3 | DONUT | the rotating torus homage, z-buffered, luminance-shaded ASCII, molten copper |
| 4 | JULIA | morphing Julia set (c orbits the cardioid), escape-count colored, half-res doubled columns |
| 5 | FIRE | bottom-seeded convection fire that ignites into RAINBOW fire mid-scene (house signature) |
| 6 | POWDER | falling-sand toy: sand / water / ember / plant / smoke, each element a script class, spawners keep it evolving |
| 7 | BOIDS | murmuration with fading trails and one hungry hawk |
| 8 | FINALE | a fire wall out of which "JAISCRIPT" ignites, burns in rolling rainbow flame, and remains glowing as the fire dies |

Between scenes: dissolve / wipe / shutter transitions (two grids merged per cell
into a combined palette). Title card with a demoscene handle on every scene.

## Measured numbers

Release (`x64-Release BENCHMARKS`), 100x40 = 4,000 truecolor cells per frame,
seed 20260705, fixed dt 1/30, whole reel incl. transitions (min of 3 runs of
`--smoke --frames 2200`); 2026-07-05 VM-perf branch:

| backend | ms/frame (mean) | fps | boot (parse) | boot (.jaib load) |
| ------- | --------------- | --- | ------------ | ----------------- |
| interpreter | 47.0 | ~21 | 7.8 ms | 3.5 ms |
| **VM** | **39.8** | **~25** | 6.6 ms | 3.2 ms |

- VM is **1.16-1.19x** faster than the interpreter over the whole reel. Most of a
  frame is the shared render pipeline (palette-indexed string assembly through the
  builtin container methods), which is backend-neutral; the VM's edge comes from
  the scene sims (convection, escape iterations, boids pairs).
- Parity: frame streams **byte-identical** across backends over 2,200 frames x3
  and over 3,300 full-duration frames (covers the rainbow-fire window and every
  finale phase). Debug build parity also green.
- Debug build: ~843 / ~679 ms per frame (interpreter / VM) — iterate in Release
  for this demo.
- `--reload-test`: hot reload of all scenes into both engines + TAB-style state
  sync, green.

## Console performance (the write is half the frame)

The smoke numbers above are script-only: a live frame also has to get through the
console, and legacy conhost parses VT escapes ~16x slower than Windows Terminal
(measured: the identical ~47 KB plasma stream costs **1.2 ms** to write in Windows
Terminal, **~19 ms** in conhost). The HUD is honest about this now: its headline
frame cost is `sim + draw` (script update + frame string, plus the console write),
split out so you can see which side you're bound on. `--bench N --scene I` runs N
real frames and reports the same split headlessly (`--bench-out FILE` to save it).

What the host does about it (all post-processing in `main.cpp` — scenes and the
`--smoke` frame stream are untouched, so parity hashes don't move):

- **Escape trimming.** Scripts already emit a color escape only when the palette
  index changes; the host additionally drops any SGR group that wouldn't change
  terminal state, and (default on conhost) rewrites 24-bit colors to the xterm-256
  cube — shorter escapes and coarser quanta = longer runs. `--truecolor` keeps
  24-bit with per-channel merge tolerance (`--tol N`, default 8); `--no-filter`
  is the byte-exact legacy stream.
- **Terminal detection.** `WT_SESSION` distinguishes Windows Terminal from legacy
  conhost. Conhost defaults to 256-color mode and shows a one-line tip on the
  title card ("Windows Terminal renders this reel much faster"); everything else
  defaults to truecolor + tolerance merging.
- **One buffered write per frame**, via `WriteConsoleA` on a real console.
- **`--diff`** row-diff redraw: only rows that changed since the last frame are
  rewritten (cursor-move + row). Not the default — plasma/fire change every cell
  every frame and defeat it — but starfield's write drops to ~1 ms.

Measured on conhost (Release, 120 real frames per scene, ~110x29 window):

| scene | bytes/frame raw -> written | write ms raw -> filtered | fps |
| ----- | -------------------------- | ------------------------ | --- |
| plasma | 29.6 KB -> 15.2 KB (2.6x) | 19.1 -> 14.1 | 23.8 -> 25.0 |
| fire | 34.2 KB -> 9.7 KB (3.6x) | 23.4 -> 8.2 | 18.7 -> 27.8 |
| finale | 29.9 KB -> 10.9 KB (3.0x) | 19.4 -> 9.7 | 13.4 -> 14.9 (sim-bound) |
| starfield (`--diff`) | 5.3 KB -> 2.4 KB | 1.0 | 82 uncapped |

And if it still *feels* like one frame per second: check you're not running the
Debug build (~0.7 s of sim per frame — see above).

## Live coding

1. Run `jai_demoreel`.
2. Open `scenes/plasma.jai` in an editor; change palette constants (e.g. the
   `gfx.rainbow_rgb` phase offsets, `ncolors`, or the field frequencies in
   `render`).
3. Press `r` in the console window.

Both engines re-execute every scene file (hot reload with instance migration) and
the current scene rebuilds — the change appears without restarting. `main.jai` is
deliberately never reloaded: it owns the persistent globals.

## Determinism / parity

`--smoke` runs N frames headless at a fixed dt on the interpreter, then the VM,
FNV-1a-hashing every returned frame; the hashes must match. All scene randomness
goes through the host's seeded xorshift `Rng` class, so a one-cell divergence
between backends fails the run. Env helpers: `JAI_DEMOREEL_SMOKE_FULLDUR=1`
(full interactive scene durations), `JAI_DEMOREEL_DUMP_FRAME=N` (write frame N of
each backend to a file for diffing), `JAI_DEMOREEL_TRACE=1` (per-frame progress
for crash localization). Deliberately NOT part of the main test suite.

## --precompiled

`engine->jaibite()` parses the concatenated scene source once; `--precompiled`
saves the parsed AST as `demoreel.jaib` on first boot and loads it afterwards.
Measured: parse ~8-12 ms vs .jaib load ~3.2-3.5 ms (and the .jaib saved by the
interpreter engine loads into the VM engine — `jaibite_load` re-interns symbols
per engine, so one file serves both backends).

## Text captures

DONUT (`--capture 2`, ANSI stripped — luminance-shaded torus):

```
     ,.--....., . .. . .. :......= ......!  ..   .    .*     ~    !
     ,.,., .... .. . .-.. ............   ..   .   *     . ,   * :*
      . .... ....... . .- ...:..... = ..   .!   .  . .    * ,
      ...... ......... ...-......  ..  ...   .   ..   .  .   ,  ~ :!=
      ..... ..... .............:...  ..;  .   ..   . ! .  .      !
       .... . ... . ........-. ..  ..  ..  ..  =   .       . !,  -  :;=
        ........  ..., ,......-.. ..  .  ;.  .   .   . !.          ! ~:
         .,,,,-,,., .,.-...:.~.  .  :  .  ..  .  =.  .   .  .  !.  -   ;
          .,--.~.:.:= ; .!.!...;. .. .~ .  .   .  .   .  .=  .  .  ,= - ;
```

BOIDS (`--capture 6` — the flock, trails, and the hawk `@`):

```
                                            ^ :  ^.^^
                                         */^: ^^ .^ *
                                      ./ . /:^:*^ ^  ^
                                         /.:/ ^.^ ^  ^
                                  /       /:*^.^. ^  :
                                 .* /./ :/.  : .  .
                  @        :/      :*   .
                          . :/  :/   */
```

FINALE afterglow (brightness map of the raw frame as the fire dies — the word
stays lit in rainbow flame):

```
               ....#..#####..###..########....#####..###..#####. .#.
               ....#..#####..###..#####.##....###.#..##...###.#  .#.
               ...##..#####..###...####.##....####...###..####   .#.
               ..###..#. .#..###...####.##....#.#....###..#.     .#.
                #..# .#. .#..###...#..# #.##..#. #...###..#.     .#.
                  ##   #   # ##### ####   #### #   # ##### #       #
```

(PLASMA / JULIA / FIRE / POWDER are solid background-color scenes — stripping the
ANSI leaves only spaces, so they don't caption well in text. Run the reel.)

## Language feedback (dogfooding notes — recorded honestly)

Everything below was hit while writing this demo on the VM-perf branch
(2026-07-05 tree, with another agent's uncommitted engine changes linked in).
Minimal repros were extracted for each; the workarounds ship in the scene
comments. The single biggest lesson first:

**JaiScript is value-semantic everywhere.** Assignment, method/function/lambda
parameters, returns, and even array-element reads deep-copy — for arrays AND
script-class objects. (`docs` say "copy = shallow", which describes the C++
`script_value` handle, not language-level assignment.) Mutation flows through
`var&` reference parameters / reference declarations, or through the owning
object's own fields. The first draft of this reel silently rendered
palette-entry-0 washes for every scene because `render(var grid)` mutated a
copy — and `--smoke` stayed green because both backends produced identical
blanks. Parity testing cannot catch "consistently wrong"; capture your frames.

**Bugs worked around (each with a small repro):**

> **Triage update (2026-07-05, VM-perf `b5c118de..`):** every finding below was
> reproduced (or bisected) at HEAD and resolved; pinning regression tests live in
> `source/tests/language/demoreel_regression_tests.cpp`. The reel itself now
> exercises the fixed paths: the graveyard is gone (scenes are destroyed at every
> transition), `render_rows` declares `prev` in the loop body again, and plasma
> passes raw `c[0], c[1], c[2]` element args into typed params.

1. **FIXED** (`53526626`: frame exit clears only the frame's OWN method env).
   ~~Free-function / lambda calls inside class methods poison implicit-self~~
   (both backends). After `g = mk();` where `mk` is a free script function,
   every later field read/write in that method throws `Undefined variable`, and
   `this` stops being an object. Method calls, constructor calls, builtins, and
   host calls are safe.
   ```jaiscript
   function mk() -> var { return [1]; }
   class R { var g = null; R() { g = mk(); } }   // Undefined variable 'g'
   ```
   Workaround: shared helpers live on a `Gfx` class (global instance `gfx`);
   scene factories are an if-chain method instead of an array of lambdas.

2. **FIXED** (`b5c118de`: method frames reserved 0 slots, so a mid-loop DECL_VAR
   reallocated the locals vector and dangled the counted-for cached pointers).
   ~~VM: loop-body locals assigned from a nested scope corrupt the enclosing
   loop~~ (method context only). An inner `for` assigning an outer-loop body
   local makes the outer loop run once — or forever. A plain `if` (no `else`)
   assigning a loop-body local does the same; `if/else`, `while`, and range-for
   are fine. Method-level locals are immune.
   ```jaiscript
   class G { int t(int w, int h) {
       int rows = 0;
       for (int y = 0; y < h; ++y) {
           int prev = -1;
           for (int x = 0; x < w; ++x) { prev = x; }
           rows = rows + 1;
       }
       return rows;   // VM: 1, expected h
   } }
   ```
   Workaround: every local assigned from a deeper scope is hoisted to method
   level; single-branch clamps became same-level ternaries. Declaring the same
   local name in sibling if/else branches also aliased slots (nondeterministic
   across process runs) — hoisted too.

3. **FIXED** (was already fixed at HEAD by `b80f12fd`: interp ++/-- resolves
   slot-based locals). ~~Interpreter: `++x` on an enclosing-scope local inside a
   for body throws `Undefined variable 'x'`~~ (VM fine; `x = x + 1` fine on both).

4. **FIXED for locals** (`9e5fea2f`: typed declarations and slot stores convert
   like assignment — `int d = 4.7` truncates to 4). Typed *fields* remain dynamic
   (declared field types are discarded at runtime; both backends agree) — pending
   a Dev ruling, pinned by `typed_field_assignment_stays_dynamic_parity`. The
   host still exposes `itrunc()`/`ifloor()`; script has no explicit float→int
   cast expression.

5. **FIXED** (`5ef9c0be`: overload matching derefs lvalue reference arguments).
   ~~Raw element / nested-field arguments misresolve against typed parameters~~ —
   `p.add_bg(c[0], c[1], c[2])` resolves and converts; host candidates too.

6. **NO LONGER REPRODUCES at HEAD** (likely lived in the uncommitted engine state
   this reel was built against). ~~`var` fields class-lock on script objects —
   interpreter only.~~ `var` fields stay dynamic on both backends; pinned by
   `var_field_retypes_across_script_classes` (member and unqualified in-method
   shapes). The `cur = null;` resets were removed from the reel.

7. **NO LONGER REPRODUCES at HEAD** (verified with the graveyard removed over a
   2,200-frame Release run on both backends, byte-identical; also re-verified
   against the pre-fix engine — the crash lived in the uncommitted engine state
   this reel was built against). ~~Destroying a script-class instance mid-run
   segfaults the interpreter~~ — the graveyard is gone; scenes are destroyed at
   every transition now.

8. **BELIEVED FIXED by `b5c118de`** (same root as finding 2: method frames
   reserved 0 slots, so many-block-scope methods reallocated their locals vector
   mid-frame — any held pointer/reference into it dangled with exactly this
   delayed-detonation signature). ~~Progressive corruption with delayed
   detonation~~ — not reproducible at HEAD (the original pre-workaround scene
   shapes no longer exist to retest byte-for-byte); the split methods stay.

9. **Host-callable poisoning NO LONGER REPRODUCES at HEAD** (fixed by
   `74b438d8`: reentrant execute isolation — saved call/value stacks, no mid-run
   pool reset); pinned by `coroutine_in_host_callable_no_state_leak`. Coroutine
   *methods* remain a parse error — that is a missing feature (the class-member
   parser never accepts `coroutine`), not a bug; needs a Dev decision to build.
   The finale still drives its sequence off scene time (works fine).

**Paper cuts:** error text reaching the host keeps `{0}` placeholders and drops
script context; `override` is required to redefine a base method (good check,
surprising the first time); no script-side float→int cast.

**What carried the demo:** parity discipline is real — thousands of frames
byte-identical across two completely different execution engines, in Debug and
Release. Hot reload with instance migration works exactly as advertised. jaibite
save/load worked on the first try, including cross-engine symbol relocation.
`var&` reference parameters, once discovered, are exactly the right tool —
`f(obj.field)` and `f(arr[i])` lvalue refs included. Template literals,
switch-on-string, and the builtin container methods (`push`, `join`, `clear`,
`repeat`, `substr`) are pleasant, and `dynamic_binder` made the seeded rng a
one-liner per method. A full scripted render pipeline — simulation, palette
lookup, run-length escape assembly, string join — moves 4,000 truecolor cells at
~25 fps on the VM.
