# VM execution coverage: where game-shaped programs run outside the fast paths

Four workloads, counted instruction by instruction: how much executed work runs inside cell blocks, how much runs as typed
`op_t_*` instructions on the general form, how much runs on the general form untyped, and why the compiler left each general
instruction where it is. Counts are **[measured]** (exact, a counting build); time shares are **[measured]** from the coarse
profile; the cost of a lever is **[inferred]** from both.

## The tool

A diagnostic build (`cmake -DJAISCRIPT_VM_COVERAGE=ON`, or `/DJAISCRIPT_VM_COVERAGE` in `CMAKE_CXX_FLAGS`) counts every
instruction where it runs: the dispatcher counts general instructions per chunk and ip, the block loop counts cell
instructions per copy ip (a fused pair counts both halves, so a cell instruction and a general instruction are the same unit:
one original instruction). Each chunk is classified once, when it first runs, from what the compiler recorded:

- the closed loop, straight-line region or frame program whose general form an instruction is, with the run-time block
  entries, entry refusals (by cause) and misses of that block;
- the refused loop holding it (`chunk::cell_refusals`: the class and the first offending instruction, named with its operator,
  operand shapes and the checker's class, or the head's instructions for a head refusal);
- a loop the translation never attempted (a back edge no closed or refused loop stands for);
- the straight-line spans the region builder dropped, recorded only in this build (`chunk::straight_notes`: too little work, a
  span whose statement boundaries close too little work, or the translation's refusal);
- otherwise the instruction's own family (a call, a member access, an element access, a return).

General-form member and method operations record their receivers (script class instance by class, host object, array, map,
string) and untyped arithmetic records its operand kinds. The report ranks reasons by executed operations with their top source
sites, and adds the calls made on the general form by callee with the inliner's rule when it kept the call.

Everything is instance state: each vm backend owns its tables, the engine keeps every backend's tables (the parallel workers'
included) and builds the report from them (`engine::vm_coverage_report`, `engine::vm_coverage_reset`). A build without the
define compiles every hook to nothing; `vm_coverage_available()` says which build this is and the other two throw.

Hosts: the runner's `--coverage=<file>` (reset at the measured phase of `--benchmark-main`), `jai_gloom --coverage FILE` (reset
by `--profile-reset-tick`), `jai_demoreel --coverage FILE`.

Normal builds carry no cost: OSSUARY Release VM serial, master `50ebdb96` against this branch, 16 interleaved runs under the
timing lock: medians 242.4 and 242.2 ms/frame (the two batches split +0.5% and -0.9%, inside the 2-3% spread).

## Workloads

| workload | run | operations | in cells | general typed | general untyped |
|---|---|---:|---:|---:|---:|
| Gator Quest | smoke, 2000 ticks, 2 bots | 320.7M | 18.4% | 32.1% | 48.8% |
| OSSUARY | scene 15, 100x40, 5 frames, 1 worker | 340.5M | 90.4% | 7.2% | 2.3% |
| GLOOM | smoke, 600 ticks, seed 666, serial | 75.1M | 86.5% | 4.3% | 9.0% |
| JaiDOOM | p10 timedemo, 120x78 | 1305.8M | 91.1% | 3.9% | 4.6% |

Block entries and resumes on the dispatcher are 0.2-0.8% of operations in every workload. No frame program ran in any of them.

Time, from the coarse profile (`JAISCRIPT_VM_COARSE_PROFILE`, the same runs):

| workload | in blocks (entry + flush) | call push + pop + method calls | the rest (the general dispatcher) | a general op costs |
|---|---:|---:|---:|---:|
| Gator Quest | 9.9% (1.3%) | 15.1% | 75.0% | 2.2x a cell op |
| OSSUARY (worker) | 70.0% (12.7%) | 1.9% | 28.1% | 4.1x |
| GLOOM | 50.2% (4.3%) | 14.4% | 35.4% | 6.4x |
| JaiDOOM | 60.1% (6.8%) | 8.9% | 31.0% | 7.1x |

The three renderers are 86-91% cells by count and 50-70% by time; Gator Quest, the one workload written as game logic in the
current idioms, is 18% cells by count and 10% by time.

## Gator Quest

Typed arrays of structure (`MOB_X[m]`, `MOB_HP[m]`), typed locals and small helper functions: the shape of game logic the
language now recommends. 81% of its executed operations run on the general form, and the reasons are mostly the region
compiler's, not the representation's.

| reason | ops | share |
|---|---:|---:|
| loop refused: translation gap | 60.7M | 18.9% |
| loop refused: loop head form | 55.1M | 17.2% |
| straight-line: scalar op outside any span (small function bodies) | 25.9M | 8.1% |
| loop refused: effect (call-argument order, an edge out) | 22.4M | 7.0% |
| straight-line: span below the region minimum | 16.6M | 5.2% |
| straight-line: untyped compare / branch | 16.5M | 5.2% |
| loop refused: member access | 15.7M | 4.9% |
| straight-line: typed element reads | 13.8M | 4.3% |
| closed loop: misses back to the general form | 7.4M | 2.3% |

What sits behind the top rows (`coverage/gatorquest.txt` has every site):

- **A size call in a loop head.** `for (int m = 0; m < MOB_KIND.size(); ++m)`: the head is `LOAD, CALL_BUILTIN size, BINARY,
  JUMP_IF_FALSE`, which the head form does not take. `status_tick` alone is 12.0% of the run; ten more loops of the same form
  add 5.2%.
- **A comparison chain as a value inside a graft.** `in_world` (`return x >= 0 && y >= 0 && x < W_W[w] && y < W_H[w];`),
  inlined into `find_target`'s neighbour scan and `fill_rect`, is a translation gap at the `>=` value: 15.4%. `bfs_path`'s
  `||` condition (`JUMP_IF_TRUE`) is 3.3%.
- **An enum constant read as a member.** `ai != Ai.summon` in `ai_tick` is a `GET_MEMBER` on the enum: 4.9%.
- **Call-argument order.** `mob_at(w, MOB_X[m] + dx, MOB_Y[m] + dy)` in `bot_nearest_hostile` refuses because a computed
  argument would run ahead of an earlier verbatim load: 3.4%; `cave_automaton`'s `||` scan 2.7%.
- **Small functions called from those loops.** 3.8M general calls; the top callees and the inliner's rule: `walk_at` 1.08M and
  `mob_at` 0.80M ("an unproven argument": the argument reads a program-scope typed array, which the checker does not prove
  under imports), `mob_live` 0.27M and `cheb` ("a call in the body"), `tile_now` and `bot_walkable` ("a declaration the block
  cannot land"). Their bodies run as straight-line general code: the 8.1% + 5.2% + 5.2% + 4.3% rows above.

Members are not where Gator Quest spends: member and method operations on the general form are 0.9% of operations (array
receivers 51%, script class instances 41%: value classes with strings such as `MonDef` and `Character` 32.5%, the flat scalar
record `Item` 8.6%). Untyped arithmetic is int/int 99%, and 29.3M of its 35.8M operations carry the checker's proof (class 1):
the lowering's shape rules left them general (comparisons feeding `&&` and `||`), not missing inference; 6.6M are unproven.

## OSSUARY

| reason | ops | share |
|---|---:|---:|
| closed loop: iterations before the block entry | 15.7M | 4.6% |
| straight-line: scalar op outside any span | 10.1M | 3.0% |
| straight-line: span whose statement boundaries close too little work | 2.2M | 0.6% |
| closed loop: boundary statement | 0.8M | 0.2% |
| everything else | 3.3M | 1.0% |

`sk_trace`'s march loops (`skull.jai:403-438`, `403-414`, `240-267`, `367-380`) enter their blocks after one general
iteration, and every ray re-enters them: 27.7k entries of the main loop with about 180 general operations each. The
straight-line work between those loops (`skull.jai:191-232`, `327-342`) never forms a region, and two long statements
(`skull.jai:449`, 86 scalar operations, and `527`, 145) end no span at a statement boundary. Block entry and flush are a
further 12.7% of the worker's time. There is no member traffic and no untyped arithmetic of note (0.3% of operations,
float/float 70%).

## GLOOM

General operations are 13.3% of the count and about half the time: iterations before a block entry 3.9% (with
`render_view_impl` `render.jai:423-451` and `accumulate_hash` `game.jai:288-291` running about 3.5k general operations per
block entry), straight-line untyped loads and stores 1.3%, effect refusals 1.2%, call-boundary refusals 0.9%. Method calls
are 9.1% of the time. Member receivers on the general form are `Game` (var and handle fields) 34.7%, the flat scalar record
`Particle` 28.8% and `WallChunk` (a value class) 14.7%; the hottest member sites are `MEMBER_TEMP kind` at `render.jai:424`
and `game.jai:289`. Calls on the general form: `tile_at` and `tile_solid` ("a return the block cannot land").

## JaiDOOM

JaiDOOM predates typed arrays and is more `var`-heavy than current scripts, so its general-form rows split in two:

- **What the VM should handle**: calls (5.5% of operations, an estimated quarter of the time: `bsp_node` recursive,
  `sprite_prepare` "a call in the body", `point_side` "an unproven argument", `draw_seg` and `bsp_subsector`, `lump_num` and
  `sprite_lookup` "a parameter type the graft does not take"), iterations before a block entry (2.0%, `draw_seg`
  `render.jai:366-465` 8.9M operations over 152k entries) plus 6.8% of the time in entry and flush, map lookups (`has` on the
  sprite tables: map receivers are 53% of member operations) and `substr`, `while (true)` heads (`bar_num`, 0.1%).
- **What typed declarations would already lower**: untyped arithmetic whose operands the checker left unproven (4.7M, 0.4% of
  operations) and mixed int/float arithmetic (2.1M), and part of the straight-line untyped loads and stores (12.9M, 1.0%). Under
  the standing ruling these are fixed by typing the scripts, not by speculative typing in the VM.

## The structural levers

The estimates below take a lever's general-form operations at the workload's measured cost of a general operation; moving
them into cells saves the difference between that and a cell operation's cost, so each figure is a ceiling. They overlap
where one loop has several reasons. Gator Quest and OSSUARY weigh more than JaiDOOM.

| lever | Gator Quest | OSSUARY | GLOOM | JaiDOOM |
|---|---|---|---|---|
| Region compiler coverage of current idioms: size calls in loop heads, comparison and logical chains as values (in grafts too), enum constants as constants, call-argument order, `while (true)` heads | 43% of ops, ~48% of time | 0.4%, ~1% | 2.5%, ~9% | 0.2%, ~1% |
| Calls: inlining by cost, raw calls from the general form to loop-free typed leaves, a cheaper general call | 28% of ops, ~31% of time, plus 15% in call machinery | 4%, ~12% | 5%, ~18%, plus 14% | 5.5%, ~26%, plus 9% |
| Block entry: enter blocks further out (loops inside frame programs: no per-loop entry, flush or first general iteration) | 3%, ~3% | 5%, ~15%, plus 12.7% entry and flush | 4%, ~15%, plus 4.3% | 2%, ~10%, plus 6.8% |
| Checker inference: argument proofs over program-scope typed arrays (unlocks the `walk_at` / `mob_at` grafts), unproven scalar operands | feeds the call lever; 2% of ops unproven | 0.1% | 0.6% | 0.4% (typing the script) |
| Flat records and non-escaping small classes (aggregate lowering) | 0.9% of ops are member operations, 8.6% of those on a flat scalar record | none | 1.1% member operations, 29% of those flat (`Particle`), plus 9% of time in method calls | none |
| String and map lanes | 0.8% of untyped arithmetic is string | none | 15% of untyped arithmetic is string | map `has` and `substr`, ~3% of time |

What the census supports:

1. **Calls are the largest lever everywhere**: the call machinery plus the small functions' bodies on the general form are a
   third of Gator Quest's time and a sixth to a quarter of the renderers'. No frame program ran in any workload: a typed leaf
   without a raw call of its own forms none, so `walk_at`, `mob_at`, `cheb`, `tile_at` and `point_side` pay the general call and
   run their bodies on the general form. Inlining by cost, raw calls from general code to such leaves and argument proofs over
   program-scope typed arrays act on the same calls.
2. **Region compiler coverage is Gator Quest's lever**, and it is translation work rather than a new representation: half of
   the run is in loops refused by a handful of shapes (the size call in a head, chains of comparisons as values, an enum
   constant, argument order). The renderers barely show it, because their hot loops already close.
3. **Block entry is the renderers' lever**: OSSUARY spends about a quarter of its time entering blocks and running the
   iteration before each entry, GLOOM and JaiDOOM about a sixth.
4. **Flat records and lowered small classes have little to move in these workloads**: member operations on the general form
   are at most 1.1% of executed operations, and the flat scalar records among their receivers are a fraction of that. GLOOM's
   method calls (9% of time) are the closest candidate. The lever matters for code written with objects in its hot loops, which
   none of these four is.
5. **String and map lanes** show up only in JaiDOOM's sprite and lump tables and GLOOM's text assembly, a few percent of time.

## Reproducing

Each report is in `coverage/` (`gatorquest.txt`, `ossuary.txt` with lines mapped to the scene files, `gloom.txt`,
`jaidoom.txt`). The commands:

```
jaiscript --no-debug --no-pause --backend=vm --coverage=gator.txt scripts/gatorquest.jai -- smoke ticks 2000 bots 2   (examples/gatorquest)
jai_demoreel --perf 5 --scene 15 --w 100 --h 40 --backend vm --workers 1 --coverage oss.txt                           (examples/demoreel)
jai_gloom --smoke --backend vm --ticks 600 --seed 666 --workers 0 --coverage gloom.txt                                 (examples/gloom)
jaiscript --no-debug --no-pause --coverage=doom.txt p10_timedemo.jai -- 120x78                                         (examples/jaidoom/scripts)
```

The demoreel concatenates its scene files into one source, so its report's lines are lines of that text; `ossuary.txt` maps them
back to the files. The counting build changes no result: the same stream and state hashes (OSSUARY `f5ca54c216432104`, GLOOM 600
ticks `1503537018`, Gator Quest `3237070200`, the timedemo `1746772898`) as the normal build, and the Foundry suites green on it
(3723 tests on the vm).
