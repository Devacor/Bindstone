# The Idiomatic Tax — GLOOM rewritten in modern class-based JaiScript

**2026-07, VM-perf branch.** `examples/gloom_idiomatic/scripts/` is a full rewrite of the
GLOOM benchmark scripts in "modern 2020+ C++" style: subsystem classes owning their state,
an enemy class *hierarchy* with virtual coroutine brains, self-updating projectile objects,
typed def classes instead of string-keyed records, range-for over index loops, methods with
implicit self instead of free functions passing the world blob around. Same binary
(`jai_gloom.exe --scripts <this folder>/scripts`), same tick structure, same entity counts,
same algorithms — the point is measuring the cost of *style*, not changing the work done.

**Equivalence is bit-exact, which is stronger than the task required.** Every benchmarked
run of the rewrite produced the identical `STATE_HASH` *and* the identical frame-stream
FNV hash as canonical, on both backends, at every length tried:

| ticks (seed 666) | STATE_HASH | frame hash | matches canonical |
|---|---|---|---|
| 100 | 2414528911 | f91b1c4e8a22d08f | yes (both) |
| 600 | 1503537018 | dfd969ed5a336dca | yes (both) |
| 3000 | 4080154357 | 0ccc249e860ef7f4 | yes (both) — also the frozen spec checkpoint |

So the A/B comparison below is between two programs doing *provably identical work* down
to the last RNG draw and the last rendered byte; only the code shape differs.

## 1. The numbers

Protocol: one machine (2025 MSI, 285HX), same binary, `--smoke --ticks 600 --seed 666`,
alternating A/B/A/B, four pairs, default workers; each invocation runs interpreter then vm.
Loading/boot are outside the timed window (the host times only the tick loop).

| pass | canonical interp | idiomatic interp | canonical vm | idiomatic vm |
|---|---|---|---|---|
| 1 | 10.752 | 11.610 | 7.182 | 7.759 |
| 2 | 11.266 | 11.825 | 7.422 | 7.397 |
| 3 | 11.536 | 10.927 | 6.943 | 7.217 |
| 4 | 10.832 | 11.076 | 7.269 | 7.493 |
| **mean ms/tick** | **11.097** | **11.360** | **7.204** | **7.467** |
| **ratio** | | **×1.024** | | **×1.037** |

Supplementary points (same protocol, single runs):

| run | canonical | idiomatic | ratio |
|---|---|---|---|
| 600t serial (`--workers 0`), interp | 14.571 | 14.085 | ×0.967 |
| 600t serial (`--workers 0`), vm | 8.972 | 9.487 | ×1.057 |
| 3000t default workers, interp | 9.814 | 9.806 | ×0.999 |
| 3000t default workers, vm | 6.362 | 6.810 | ×1.070 |

**Headline: the idiomatic tax on GLOOM is ~0% on the interpreter and ~4–7% on the VM.**
Run-to-run spread on this machine is ±4%, so the interpreter delta (7 pairs: +8.0, +5.0,
−5.3, +2.3, −3.3, +2.5, −0.1%) is indistinguishable from noise, while the VM delta is small
but *consistently positive* (7 pairs: +8.0, −0.3, +3.9, +3.1, +5.7, +2.8, +7.0% — six of
seven) — a real effect on the order of +4%.

**Why the VM pays more than the tree-walker (hypothesis):** the VM's recent wins are
fast paths that reward canonical's flat shape — fused binary ops that sink into local
slots/decls, INDEX fast paths on slot-addressed locals and globals. The idiomatic style
replaces flat global reads (`G.px`) with two/three-hop member chains (`G.player.px`) and
free-function calls with dynamically-dispatched method calls, both of which route through
the general member-access and call machinery and defeat some fusions. The interpreter was
already paying general-path prices everywhere, so restructuring moves it much less.

**Where the tax comes from, ranked (hypothesis):**

1. **Member-chain depth in per-entity code.** `G.player.px`, `G.world.solid_at(...)`,
   `G.fx.spawn(...)` — every hop is a handle deref + field lookup. The Pilot class and the
   enemy brains are full of these (canonical read `G.px` in one hop). This is most of the
   VM delta.
2. **Method dispatch vs free-function call.** Every `world.solid_at()`, `e.art_name()`
   (now *virtual*), `fx.spawn()` binds self and dispatches per instance. Call counts are
   roughly unchanged (folding `tile_solid(tile_at(x,y))` into `solid_at(x,y)` even removed
   some), but each call is slightly heavier.
3. **Def-class instances vs record maps** — `ENEMY_DEFS[kind].pain` element-reads
   (deep-copies) an 8-field class instance where canonical deep-copied an 8-entry map.
   Comparable, small.
4. **Shot/Item objects vs primitive rows** — `new Shot(...)` per projectile and a per-tick
   deep copy of survivors (element reads copy even shared_ptr elements, so `kept.push(shots[i])`
   forks the instance). Canonical rebuilt 6-slot records; cost-equivalent, a handful per tick.
5. **What contributes nothing:** the ray chunks, the particle steps and the glyph-row
   builders — the *majority* of the tick — are byte-identical free functions over identical
   flat data, because the parallel admission rules forced them to stay that way (§3).

One deliberate control experiment: a "naive idiomatic" variant of the sprite blit (chained
`art.pix[ay * aw + ax]` member reads per pixel instead of the ref-parameter helper) measured
11.371 / 7.198 ms/tick — the same within noise, still bit-exact. At GLOOM's billboard load
the member-chain fear doesn't materialize; it would matter in a per-pixel stage like walls
or row-building, which admission keeps flat anyway.

## 2. What was restructured, per file

| file | canonical | idiomatic |
|---|---|---|
| state.jai | `RNG` global | unchanged |
| util.jai | free math/palette helpers | unchanged by design — namespace-style helpers are idiomatic C++ too; the palette tables **must** stay global (captured by a parallel body) |
| defs.jai | string-keyed record tables (`w["cd"]`) | `WeaponDef` / `EnemyDef` / `ItemDef` / `Art` classes with typed fields + constructors (`w.cd`); pixel-art data table unchanged |
| maps.jai | free `load_map`/`tile_at`/`mappack` fns writing `G.*` | `class World`: grid + queries (`tile_at`, `solid_at`, `spot_free`, `los_clear`, `wall_dist`), mappack snapshot, `bfs_field(gx, gy, key_r, key_b)` (keys became parameters), `clear_tile`; `class Item`; map data + boot validators stay free |
| pure.jai | 2 pure parallel bodies | **UNCHANGED — cannot be made class-based** (§3) |
| particles.jai | `ParticlePool` + free `fx_*` burst kit + `PART_COLS` global | `ParticleSystem` with the burst kit as methods (`G.fx.explosion(x, y)`), color ramp as a `cols` field, `spread()` private |
| enemies.jai | one `Enemy` class, kind-switch brains | **hierarchy**: `Grunt`/`Spitter`/`Turret`/`Warden : Enemy`, each overriding `coroutine void make_brain()` (virtual dispatch mints the right brain), polymorphic `art_name()`, template-method `on_death()` hook; `turret_shot` → `Turret.shoot()`, `summon_adds` → `Warden.summon_pack()` |
| combat.jai | free LOS/hitscan/shot-record pipeline | `class Shot` with `tick()` + private `explode_hex/fire`; LOS/wall_dist moved to World, hitscan/damage/fire to Player |
| sim.jai | free player pipeline mutating `G.*` | `class Player`: pose/vitals/arsenal + `move`, `use_action`, `fire`, `hitscan`, `damage`, `check_pickups`, `tick_timers`; private `use_tile`/`open_door`/`give_ammo` |
| render.jai | ~20 file-scope globals + free functions | `class Renderer`: view geometry, zbuf, tone tables, strip cache, sprite-LUT cache, ray workspaces, automap; draw pipeline decomposed into methods; **PIX/VW/ROWIDX/QUADG/RESET stay global** and `gloom_row_quad` stays free (§3) |
| hud.jai | free HUD fns + `G.msg*` fields | `class Hud` owning the message ticker + all screens |
| game.jai | 50-field `Game` blob + pilot coroutine on it | `Game` as orchestrator over typed subsystem handles + `class Pilot` (brain handle, goal memory, command builder); spawn factories |

LOC: 3848 vs canonical 3623 (+6%) — almost entirely constructor boilerplate in defs.jai
(no named arguments / designated initializers) and the class scaffolding.

## 3. Parallel-admission compromises (the idiomatic ceiling)

SAFE mode was kept throughout (no `engine::allow_unsafe_parallel`), same
`parallel_for`/`parallel_transform` usage as canonical. The admission rules draw a hard
boundary through the middle of the OO design:

1. **pure.jai could not be touched.** Parallel bodies may touch only their element
   parameter and locals — no member access, no methods, no `var&` aliases of captured
   names. A `Ray` or `Particle` class is unadmittable by construction (and script-class
   instances cross regions by deep clone anyway).
2. **The pixel grid and glyph/palette tables must stay globals** (`PIX`, `VW`, `ROWIDX`,
   `QUADG`, `RESET`, `PAL_FG/BG/LUM`): `gloom_row_quad` reads them as captured state, and
   captured reads only admit globals — member access on a captured receiver is rejected.
   The Renderer is a class wearing a global frame buffer.
3. **`gloom_row_quad` stays a free function** — a parallel_transform body can't be a method.
4. **The ray workspaces stay flat primitive records** (all-primitive rows are what the
   barrier can prove exclusive).
5. What *did* survive: `parallel_for` over a **class field from inside a method** works
   (`ParticleSystem.update`, `Renderer.draw_world` — the loop is in the method, the body is
   free). So the class can own the parallel data; it just can't process it with methods.

This is itself the report's clearest finding: **the hotter the code, the less idiomatic it
is allowed to be.** The OO layer stops exactly at the performance-critical boundary — which
is also why the measured tax is so small: the language forced the expensive 70% of the tick
to stay identical.

## 4. Subjective assessment (the author's honest opinion)

**What was genuinely satisfying — better than canonical:**

- **Virtual coroutine brains are the best thing I've written in any scripting language.**
  `class Turret : Enemy` overriding `coroutine void make_brain()`, the base `tick()` calling
  it virtually, phase state living in the coroutine frame, pain-interrupt = null the handle —
  the whole bestiary architecture is *four subclasses and zero switches on the tick path*.
  The mandatory `override` keyword and the `super(...)` ctor delegation both behaved exactly
  like the C++ instincts predicted, first try. The rewrite ran **bit-exact on the first
  full run** — after 3.8k lines of restructuring, that says the class semantics are solid.
- **Typed def classes kill the stringly-typed record tax.** `w.cd` instead of `w["cd"]`;
  a typo is an error, not a null; 8-argument constructors caught transposed args at the
  write site. The `EnemyDef` table reads like a designated-initializer block wants to exist
  (see priorities below).
- **`private:` sections are real.** `ParticleSystem`'s cursor and `Renderer`'s ~15 caches
  are actually hidden, where canonical had every global exposed to every file.
- **Template-method hooks work**: `on_death()` for the Warden's explosion. I later
  confirmed `super::die()` also exists (C++-spelled `super::method()`); I'd initially
  guessed `super.die()` and the parse error pointed me to the right spelling — decent
  diagnostics.
- The engine *warned* about `Player.reset()` colliding with the builtin shared_ptr
  `reset()` — canonical got silently bitten by this once. Good regression.

**What fought me:**

- **The reference-binding asymmetry is the #1 ergonomic tax, and idiomatic style makes it
  worse.** `auto& w = WEAPONS[i]` binds; `auto& p = G.player` does not (member expressions
  can't be `var&`-DECL targets), yet `f(G.player.zbuf)` as a ref ARGUMENT is fine. Class
  decomposition multiplies member expressions, so the asymmetry bites constantly: the Hud
  and Pilot spell `G.player.hp` dozens of times where C++ (or Python!) would write
  `auto& p = ...;` once; `blit_sprite` exists *only* because `auto& apix = art.pix` won't
  bind. Fixing this one rule would remove most of the rewrite's ugliness.
- **Element-read deep-copy vigilance doesn't get easier with classes.**
  `var e = enemies[i]` silently forks the enemy; `kept.push(shots[i])` forks the shot (I
  kept it — it's cost-equivalent to canonical's record rebuild — but you cannot *retain a
  handle* out of an array cheaply). Every loop needs the `auto&` reflex, and the compiler
  doesn't check it. In a reference-semantics language (Squirrel, Python, Lua) this entire
  hazard class doesn't exist.
- **No partial classes / extension methods** forced layout compromises: World couldn't
  spread across maps.jai + combat.jai the way a C++ class spreads across .cpp files, so
  combat had to be *redistributed* (Shot/Player/Turret) rather than kept as one file-domain.
  It worked out — arguably better design — but the language chose for me.
- **No value-type structs** (the big one, see below): a `Vec2` was unthinkable — every
  ray step would mint handles or deep-copy — so positions stay `x`/`y` scalar pairs and
  the math reads like 1985. Same for the def tables and particle rows.
- Small frictions: two method-declaration syntaxes (`void f()` vs `function f() -> var`);
  constructor boilerplate (`name = name_;` × 8 per def class); I worked probe-first (tiny
  scripts through the real binary) because the binding/copy rules are subtle enough that
  guessing wrong costs a debugging session — in Python or Lua I'd never need the probes,
  and that *is* data about the language.

**Net:** writing this felt like writing a small, opinionated C++ — the type system, the
classes and the coroutines carry real weight, and the determinism story (bit-exact across
two backends and any worker count) is something none of the other five languages here can
even express. But the value-semantics vigilance is a constant background hum, and the
idiomatic surface I was asked to write is visibly *fighting* two rules (member-ref decls,
element-read copies) that the canonical style was designed around.

## 5. Cross-language comparison (author-satisfaction ranking)

I read all four ports (`examples/gloom/ports/{lua,squirrel,chai,python}` + NOTES.md + the
five-way report in `docs/GLOOM_COMPARISON.md`) with this rewrite fresh in hand. Ranking is
**author satisfaction for this game's shape** — ergonomics, expressiveness, safety,
refactorability, how much the language helps or fights — not raw speed (that table lives in
GLOOM_COMPARISON.md: Lua 1.4 ms/tick · Python 2.5 · Squirrel 2.9 · JaiScript VM ~6.9 today
· Chai 273).

**1. JaiScript (idiomatic, this folder).** Wins on the two things that matter most in
game code: *brains* and *types*. Coroutine **methods** with implicit self and virtual
override are strictly better than every alternative here — Lua needs `resume(br, self)`
plumbing and `coroutine.create(fn)` ceremony; Python/Squirrel generators are close but
their classes are looser; Chai has nothing. Typed fields/locals catch real bugs
(`int lineh = 1.0 * vh / dist` being the truncation you want, everywhere), and the
two-backend + any-worker-count bit-exactness is a determinism contract no other column
offers. Docked for: the `auto&` vigilance, the member-ref-decl asymmetry, and the fact
that its hottest 70% must be written in a C-flavored subset (§3).

**2. Python.** The frictionless one. `__slots__` classes, generators as brains (1:1 with
the reference), dicts and f-strings everywhere, `p = G.player` aliases freely — I would
have written this port in half the time with zero probes. Docked below JaiScript for
exactly what makes it fast to write: nothing is typed, nothing is checked, refactoring a
3.6k-line game on vibes and grep is how sim bugs are born, and its determinism story is
"one interpreter, one thread." Satisfaction while *writing*: highest. Confidence while
*changing*: lowest of the top tier.

**3. JaiScript (canonical baseline).** Same language virtues (it already used classes +
coroutine methods for Game/Enemy/Pool — it's a hybrid, not really "procedural"), and its
flat-global style *matches the grain of the VM and the parallel rules* — the code and the
admission boundary never argue. Docked for the stringly-typed record tables (`w["cd"]`
everywhere), the 50-field God-object, and free-function pipelines whose data-flow you
reconstruct by reading `G.` prefixes. It's the shape the language currently rewards, and
that's precisely the feedback: the *rewarded* shape shouldn't be below the *idiomatic* one.

**4. Lua 5.4.** The pragmatist's pick: fastest by an order of magnitude, coroutines 1:1,
`setmetatable` classes are shabby but honest, and the port notes read like a quiet
afternoon (one compile error total). Docked for: 1-based indexing vigilance across an
index-heavy raycaster (the port's own top friction), `local` noise and global-by-default
hazards, zero typing, and class syntax that is a *pattern*, not a feature.

**5. Squirrel 3.2.** The dark horse — reference-semantic classes plus generator methods
reproduced the JaiScript architecture nearly token-for-token (its NOTES: the brains "ported
as brains", zero design work, and the whole `auto&`/deep-copy tax "has no Squirrel
equivalent"). Docked for the ecosystem: immutable strings with no builder and *no
array-join* (the renderer needed a hand-rolled pairwise merge), `.tointeger()` at ~40
truncation sites, reserved-word collisions (`base`), and a raw-stack C API that costs 404
adapter lines. A great language core wearing a threadbare stdlib.

**6. Modern C++ (the hypothetical seventh port).** As an author I'd enjoy the *types* and
suffer everything else: C++23 `std::generator` finally makes the brains expressible, but
each brain becomes a coroutine returning a generator object with explicit lifetime
discipline; the registries become `vector<unique_ptr<Enemy>>` with the usual
iterator-invalidation landmines the script languages simply don't have; designated
initializers make the def tables *nicer* than any script here; and the edit-run loop is a
link step instead of a keystroke (GLOOM's authors iterated live — REFERENCE.md's whole
milestone log is script-reload cadence). For the engine: obviously. For *this* layer —
gameplay logic tuned by re-running a seed — C++ is the wrong altitude, which is the entire
reason embedded scripting exists.

**7. ChaiScript.** The control group. No coroutines, so every brain was hand-lowered into
`bphase`/`bt` state machines — the port's Enemy class carries *nine* fields that exist only
to simulate a coroutine frame, +44% LOC on the brains, and the lowering discipline needed a
comment block longer than some brains just to keep the RNG stream aligned. Add 273 ms/tick
and a runtime that can't compute `sqrt` without the adapter binding it, and the verdict
writes itself: the *architecture you can't express* is the tax that dwarfs every other
line-item in this report.

**What this says about language priorities** (the actionable part):

1. **Value-type structs are the right next feature.** They're the missing piece in
   *three* places at once: `Vec2` math in brains/combat (ergonomics), literal-friendly
   def tables (replacing 60 lines of ctor boilerplate), and — the sleeper — **parallel
   admission**: a struct-of-primitives row is statically provable where "array that
   happens to hold primitives" needs a barrier walk. `struct Particle { int kind; float
   x... }` could make pure.jai class-shaped *without* widening the trust boundary.
2. **Let `var&`/`auto&` declarations bind member expressions** (owner-pinned, exactly like
   the existing ref-ARGUMENT machinery — the four holder modes already exist). This is the
   single highest-leverage ergonomic fix; it would delete the ugliest pattern in this
   rewrite (`G.player.` × 30) and the `render_view_impl`-style entry splits in canonical.
3. **Generator-only coroutines would have been enough for GLOOM** — every brain and the
   pilot only ever `resume()` with no arguments and yield values outward. If restricting
   to generators buys implementation freedom (flatstack, VM frames), nothing in this
   codebase objects — *provided coroutine **methods** with implicit self survive*, because
   those are the crown jewel.
4. Named arguments / designated init for data classes; `partial class` or free methods on
   classes — nice-to-haves, in that order.

## 6. Reproduce

```
cd "out/build/x64-Release BENCHMARKS"
./bin/jai_gloom.exe --smoke --ticks 600 --seed 666                                    # canonical
./bin/jai_gloom.exe --smoke --ticks 600 --seed 666 --scripts ../../../examples/gloom_idiomatic/scripts
```

Both print interpreter + vm ms/tick and must land on STATE_HASH 1503537018 / frame hash
dfd969ed5a336dca. `--ticks 3000` → 4080154357 (the frozen spec checkpoint). `--workers 0`
runs the same scripts serially (the 1-vs-N determinism check).
