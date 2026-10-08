# Copy and compile-time census: accidental copies and run-time re-derivation

This census counts every value copy and every run-time lookup the VM performs over the four game workloads and the kernel suite. It
attributes each event to the C++ site that ran it, the instruction that was executing and the script line behind it. It then
classifies the significant sites and ranks the waste by cost share and fixability.

Counts are **[measured]** (exact, from a counting build). Causes read from the code are **[read]**. Costs in nanoseconds and shares
of run time are **[inferred]** from a per-event cost model (below) against each workload's Release time. The shares carry roughly a
2x uncertainty in either direction. The counts do not.

The baseline is `copy-census` at master `4ac3d3010`. Function names are the stable anchors; line numbers refer to that commit.

## Headline

- **Copies and lookups are a minor share of these programs.** Summing every copy, refcount and lookup site measured here gives roughly
  2-5% of run time per game workload. For the kernels it is 1-10%; the largest figures (`dict`'s map hashing, `tree`'s field
  references and stack copies) sit in lever B's and lever C's areas. The rest of the time is dispatch and the call path, which earlier
  censuses cover.
- **Deep copies are rare in the games.** GQ makes 0.09-0.11 deep copies per 1000 VM instructions, DOOM 0.009 and OSSUARY 0.004.
  GLOOM's 0.28 are dominated by one copy the script asks for (`pix = G.bg`). Three quarters of GQ's deep copies come from one gap: a
  grafted (inlined) call's by-value parameter binds without the read-only proof.
- **Refcount traffic is the largest copy cost by volume.** Per 1000 instructions there are 10-20 strong increments in GQ and DOOM,
  159 in GLOOM and 600-800 in `tree`. Most come from transient handles: a callee pinned per call, an element reference minted per
  read, a holder copied only to inspect it, and typed string elements boxed by builtins.
- **Run-time re-derivation is mostly cold.** Interning, type interning, class lookup by name and chunk compiles happen at load and
  import time (tens of thousands per run, never per tick). The hot exceptions:
  - enum members, which are read by hashing their name on every cell-block entry (GQ, 3.2M per run);
  - references to globals, which are resolved through the environment on every execution (DOOM, 2.5M);
  - global names read inside a method, which re-run the member-shadowing probe on every read (OSS 394k, sort 394k, GLOOM 177k), with
    an atomic `std::shared_ptr` copy each time;
  - per-instance interning of `_cpp_object` during construction (`particles`).

## The tool

A coverage build (`-DJAISCRIPT_VM_COVERAGE=ON`) carries the census in `include/jaiscript/detail/copy_census.hpp`, implemented at the
end of `source/implementation/vm/vm_coverage.cpp`. Without the define, every hook compiles to nothing.

| Site group | Hook | What it records |
|---|---|---|
| deep copy | `script_value::clone` (a counting wrapper around `clone_uncounted`), `class_copy_assign`, `unshare_string_node` | the outermost copy is one event. Nested copies and copied elements add weight. The source node's uniqueness (`uniquely_held`'s test) is recorded, and the source and the copy are timed to their deaths in VM instructions |
| elision misses | `bind_parameters` (both clone sites), `exec_decl_var_full` | the callee or local name, and why the read-only proof or the prvalue elision did not apply |
| elision hits | NRVO in `materialize_return`, the parameter alias, prvalue elision, the local alias, `take_for_assignment`, the element store's instance move, the store's prvalue move | one event per avoided copy |
| refcount | `strong_ptr` copy constructor, `script_value` copy constructor and assignment of refcounted kinds, `get_class_instance` | the address of the copy and the copied kind (`get_class_instance`: its caller stack) |
| lookups | symbolizer `intern`/`find`/`get_string`, environment get, get_ref, assign, define, contains, `get_value_ptr`, `vm_storage_lookup` and resets, `get_class_definition`, `get_type_info`, `class_definition::field_slot` (scan or hash) and `get_method`, `script_value_hash` (map key hashing), `convert_for_typed_slot_core` (verdict), `validate_container_homogeneous`, `vm_compiler::compile_*` | one event each |

Each event is keyed by (site, flags, executing opcode, caller). The caller is a stack of six frames for deep copies, elision events and
lookups, and the return address for refcount events. At exit, DbgHelp symbolizes the callers with their inline chains (the build adds
`/Z7` and `/DEBUG`). Script lines come from the coverage rows: `coverage_general` and `coverage_cell` publish the current instruction.

`JAI_COPY_CENSUS=<file>` writes four outputs at process exit:

- `<file>`: the report;
- `<file>.tsv`: one row per site, flags, opcode and caller;
- `<file>.lines.tsv`: per script line;
- `<file>.names.tsv`: per callee or local name.

Two kinds of events come from the coverage build itself and are excluded from every figure below:

- `coverage_observe`'s `deref_view`, which copies the receiver;
- `cast_dynamic_class`'s holder copy, which the coverage receiver table makes.

Not counted:

- `std::shared_ptr` traffic: the atomic counts on `object_holder::data`, `get_class_instance` and `std::static_pointer_cast`;
- the interpreter;
- parallel workers (all runs are serial or one-worker).

### Cost model [inferred]

| Event | ns |
|---|---:|
| strong increment (with its later decrement) | 1.5 |
| `script_value` copy of a refcounted kind (copy, destroy, both counts) | 3 |
| deep copy of an instance or a `var` container | 30 + 5 per copied value |
| deep copy of a typed array | 40 + 0.3 per element |
| string-keyed map hash and probe | 14 |
| int-keyed map hash | 3 |
| hashed field-slot lookup | 8 |
| scanned field-slot lookup | 1 + 0.4 per slot |
| `get_method` | 14 |
| environment hash lookup (`get_value_ptr`) | 12 |
| uncached environment walk | 25 |
| intern | 25 |

Run time is the Release figure from `docs/PERFORMANCE.md`:

- GQ: 1.075 / 2.340 ms per tick × 2000;
- GLOOM: 0.724 ms per tick × 600;
- DOOM: 1.51 ms per tic × 3984;
- OSSUARY: 138.9 ms per frame × 5.

For the kernels it is 3.5 runs of the `bench/kernels/RESULTS_2026-09-26.md` per-run time. That file is older and slower than master,
so kernel shares are on the low side.

## Workloads

All hashes match their references; the counting build changes no result.

| Workload | Run | VM instructions | State |
|---|---|---:|---|
| GQ2 | `jaiscript … gatorquest.jai -- smoke ticks 2000 bots 2` | 589M | STATE_HASH 3218840367 |
| GQ4 | `… bots 4` | 1529M | STATE_HASH 4257627332 |
| GLOOM | `jai_gloom --smoke --ticks 600 --seed 666 --workers 0` | 64.5M | 1503537018, frame parity OK |
| DOOM | `p10_timedemo.jai -- 120x78` | 1200M | 1746772898 |
| OSS | `jai_demoreel --perf 5 --scene 15 --w 100 --h 40 --workers 1` | 324M | f5ca54c216432104 |
| kernels | `bench/kernels/jai/*`, `profile.jai` replaced by exactly three runs; interop omitted (needs `jai_kernel_host`) | 1-137M each | checksums as published |

### Totals

| Workload | Deep copies | Copied values | Source uniquely held | Source dies in the same instruction | String detaches | `script_value` copies | Strong increments | Per 1k instructions: deep / value copies / increments |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| GQ2 | 65,652 | 649,846 | 2% | 0% | 2,654 | 2.65M | 9.45M | 0.111 / 4.50 / 16.0 |
| GQ4 | 133,546 | 1.29M | 2% | 0% | 2,909 | 4.63M | 16.0M | 0.087 / 3.03 / 10.5 |
| GLOOM | 18,210 | 8.21M | 47% | 33% | 29,594 | 1.35M | 10.2M | 0.282 / 21.0 / 159 |
| DOOM | 11,299 | 5.23M | 5% | 5% | 237,015 | 13.1M | 23.8M | 0.009 / 10.9 / 19.8 |
| OSS | 1,458 | 44,691 | 27% | 28% | 978 | 0.51M | 1.22M | 0.004 / 1.58 / 3.75 |
| tree (typed) | 0 | 0 | | | 0 | 17.5M | 22.7M | 0 / 459 / 596 |
| dict (typed) | 0 | 0 | | | 0 | 9.64M | 13.6M | 0 / 410 / 580 |
| fib_recursive (typed) | 0 | 0 | | | 0 | 135 | 13.07M | 0 / 0 / 182 |
| particles (typed) | 700,000 | 3.50M | 14% | 43% | 0 | 4.50M | 7.65M | 17.5 / 113 / 192 |
| sort (typed) | 4 | 400,004 | 0% | 0% | 0 | 0.70M | 1.19M | 0 / 49 / 83 |
| nbodies (typed) | 10 | 80 | | | 0 | 0.53M | 2.51M | 0 / 6.8 / 32 |

The arithmetic kernels (`fib_loop`, `primes`, `mandelbrot`, `spectral_norm`, `exp`, `sha256`) copy nothing and touch no refcount in their
timed loops.

## Ranked findings

Share is the inferred share of run time on the workload where the site costs most, with the other workloads it reaches. Fixability:
H means local and mechanical, M means a kernel change with a semantic argument, L means it needs a design change or a Dev ruling.
The owner column marks findings that sit in another lever's area.

| # | Finding | Kind | Evidence [measured] | Share [inferred] | Fix | Owner |
|---|---|---|---|---|---|---|
| 1 | Enum members read by hashing their name on every cell-block entry | front-load | GQ2 3.26M, GQ4 3.23M string-keyed `script_map::find` from `env_operand_value` | GQ2 ~2%, GQ4 ~1% | H (cache the entry) / L (fold to a constant) | cells (general-op); hash caching is lever C |
| 2 | Builtins box typed string elements (`join`'s two `for_each` passes, `script_array::get` on `str`) | refcount | GLOOM 5.04M increments from `render.jai:577` `parts.join("")`; dict 3.0M; DOOM 0.90M; string2float 0.24M | GLOOM ~2-4%, string2float ~1.4%, dict ~0.6% | H | unowned |
| 3 | Callee pinned by a strong copy on every call (`raw_call_enter`, `exec_probe_callee`, `park_callee`) | refcount | fib_recursive 13.07M (one per call); DOOM 3.26M; GQ4 2.60M; tree 2.59M; OSS 0.32M | fib_recursive ~2-4%; DOOM, GQ, tree ~0.1-0.4% | M | call path (no current lever) |
| 4 | Read-only proofs refuse class-typed declarations initialized from a program-scope or local chain, and graft parameters bind without any proof | copy | GQ4 111,097 `Item` copies at `bind_graft_parameter` (77% of GQ's deep copies) plus 2,457 refused `item_stat` binds and 7,030 refused locals; particles 200,000 constructor binds | GQ ~0.2-0.5%, particles ~0.5% | M | unowned |
| 5 | A reference to a global is resolved through the environment on every execution (`DECL_REF_IDENT`) | front-load | DOOM 2.46M `get_value_ptr` (`draw_seg`'s nine `var& x = GLOBAL`) plus 3.72M reference copies in `decl_ref_alias` | DOOM ~0.5-1% | H | lever B (reference caches) |
| 6 | An unqualified name inside a method re-runs the member-shadowing probe at every resolution: `get_class_instance` (a holder copy plus an atomic `std::shared_ptr` copy), a field-slot lookup and a method lookup | front-load, refcount | OSS 394k probes (`probe_member_scope` from `member_scope_env_lookup` 293k, from `fused_ident_value` at `PROBE_CALLEE` 75k), with 338k field scans, 138k field hashes and 186k `get_method`s; sort 394k; GLOOM 177k plus 108k field hashes; GQ4 1.82M obj-array element reads box the holder (`script_array::get`) | OSS ~1.5-2%, GLOOM ~1%, sort ~0.7% | M | unowned (member scope); lever B adjacent |
| 7 | Constructor arguments copied twice, and `_cpp_object` interned per instance | copy, front-load | particles: 200,000 `capture_argument` copies then 200,000 `bind_parameters` copies of the same arguments; 600k run-time interns at construction and copy; tree 40k interns at `NEW` | particles ~1.4%; tree ~0.1% | M (double copy) / H (intern) | intern: lever A (instance layout) |
| 8 | Dying temporaries copied into containers and through references; typed copy assignment allocates a fresh buffer | copy | DOOM `FLATS[slot] = flat_pix(nm)` copies 498 × 4096 elements (source dies in the same instruction); GLOOM `pix = G.bg` copies 592 × 13,601 elements into a new buffer; OSS `__jai_parallel_tchunk` stores 400 × 101 through a reference | GLOOM ~0.5%; DOOM, OSS <0.1% | M (moves) / H (buffer reuse) | lever A (copy-assign, A9 moves) |
| 9 | Field references re-resolved by id on every dereference (`deref_slow` → `find_field_value` → `field_slot`, hashed above 16 slots) | front-load | tree 11.2M scans of a 4-slot class (`reference_binding_admits`, `ref_write_through`, `bind_site_reference`: three resolutions per out-parameter use); GQ4 738k hashed on the 48-slot `Character` (`array<Item>& worn = CHARS[slot].worn` read through `fused_ident_value` and `LOAD`); GLOOM 39k on the 65-slot `Game` | tree ~2.7%; GQ ~0.15% | M | **lever B** (reference slot caches) |
| 10 | Element and field references minted per read (a pooled holder plus two increments) | refcount | GQ4 6.26M increments from `INDEX_FUSED`/`INDEX`; DOOM 4.42M; tree 2.31M field references | ~0.15-0.4% | M | **lever B** |
| 11 | `script_value` copies pushed for reads (stack loads, member pushes, temps) | refcount | tree 17.5M; dict 9.6M; DOOM 13.1M; GQ4 4.6M | tree ~5%, dict ~4%, games 0.2-0.9% | L (borrowed reads) | lever B (handle receivers); cells: general-op |
| 12 | String-keyed map hashing at run time | front-load | dict 3.26M (55% of its lookups); DOOM 1.78M string keys (`sprite_lookup`, `wad.jai:143`) | dict ~6%, DOOM ~0.4% | M | **lever C** |
| 13 | Instance fields set by id at construction (`create_instance` → `set_field` → `find_field_value` and `materialize_field`) | front-load | GLOOM 4 × 24k scans; tree 40k identity conversions in `stamp_literal_field_initializers` | <0.2% | M | **lever A** (instance plans) |
| 14 | Typed-slot table run to an identity verdict at declarations | front-load | GQ4 968k `identity_exact` (`find_tile_near`'s `TileDef td = TILES_DEF[...]`, an aliased local) | GQ4 ~0.1% | H | unowned |
| 15 | Unique instance temporaries copied by `T_PUSH` | copy | particles 100,000 (source unique, dies in the same instruction); GQ 529 | particles ~0.3% | M | **lever A9** |
| 16 | Shared empty string detached before its first append (`compound_append_string`, `exec_concat_append`) | copy | DOOM 237k detaches of empty strings | DOOM <0.1% | H | unowned |

Rows 9-13 and 15 are other levers' areas and are listed for completeness. Rows 1-8 are written up below.

## The top eight

### 1. Enum members are hashed by name on every cell-block entry

**Evidence [measured].** In GQ2, 3,257,213 of 3,373,427 map-key hashes are `map_string_key_probe` lookups from `env_operand_value`. In
GQ4 the figure is 3,234,628. The lines are `gq_combat.jai:434` (2.12M, the burn check in `status_tick`) and `gq_ai.jai:369` (885k,
`ai != Ai.summon`), each inside a loop the cells run.

**Cause [read].** `vm_backend::env_operand_value` resolves an enum member operand (`er.member_node != k_invalid_u32`) as
`map.find(map_string_key_probe{member->member})`: it hashes the member's text and probes the enum's map.

`cells_enter` reuses an environment operand's last pointer only when `member_node == k_invalid_u32` (the `env_reuse` test), so every
block entry re-hashes each enum member it reads. A block entered once per mob per tick pays one string hash per enum member per entry.

**Change.**

- (a) **Without a ruling.** Let `cells_enter` reuse an enum member operand's entry pointer under the same rule as plain operands (the
  environment and its epoch hold), plus one condition: the enum variable's map node is the node the pointer came from. `script_map`
  entries never move until erased, so the pointer stays valid while the node lives and the entry is not erased. A store into an
  existing member keeps the pointer valid and the cell re-reads the value through it. Only an erase invalidates it, so the reuse
  needs an erase serial on the map (lever C's `script_map`) or the ruling in (b).
- (b) **The front-loading fix.** If enum maps are immutable after their declaration runs, the compiler folds `Enum.member` and
  `Enum::member` to an int constant guarded by the enum's definition epoch, exactly as a function call site is guarded. The cell
  block then reads a constant. This needs a Dev ruling on whether script may store into an enum's members. The checker already
  rejects an unknown member at load (`no value 'zz' in enum 'Dir'`).

**Safety.** (a) changes nothing observable while the map is unchanged. (b) requires the ruling. A redefinition of the enum (an
include re-run) moves the definition epoch and invalidates the fold.

**Gain [inferred].** About 45 ms per GQ run: ~2% of GQ2, ~1% of GQ4. DOOM, GLOOM and OSSUARY read no enums in cells.

### 2. Builtins box typed string elements

**Evidence [measured].** GLOOM: 5,044,290 of its 10.2M strong increments are `script_array::for_each` boxing `str`-kind elements
inside the `join` builtin. They come from one line, `render.jai:577` (`return parts.join("");`, 4.2k parts per frame). `dict` makes
3.0M increments in `script_array::get` on a `str` array (`INDEX_STORE_FUSED`, `LOAD`). DOOM makes 0.90M (`INDEX_FUSED`,
`CALL_BUILTIN`, `CALL_METHOD`) and string2float 0.24M.

**Cause [read].** `join` (`init_builtin_method_registries`, `interpreter.cpp:679`) walks the node twice through
`script_array::for_each`, once to size the buffer and once to append. On a typed `str` node, `for_each` materializes each element as
a boxed `script_value`, which copies its `strong_ptr<script_string>`. Read-only element access through `script_array::get` boxes the
same way.

**Change.** Give `join` a `str`-kind lane that iterates `node->strs()` by `const strong_ptr<script_string>&` in both passes (the
`i64`/`f64` kinds format their payload directly). Give read-only consumers of a `str` element a borrowed `const script_string&`
accessor beside `get`. The same pattern applies to every builtin that reads through `for_each` without keeping the element (the
`builtin_runs_script` list excluded).

**Safety.** The element is read, never stored or exposed, so no handle outlives the call. The bytes appended are identical. A
parallel region does not change it: reads take no counts at all, which is what the refcount-silent rule requires.

**Gain [inferred].** GLOOM: 5M boxings at 3-4 ns each, ~2-4% of the run. string2float ~1.4%, dict ~0.6%, DOOM <0.1%.

### 3. The callee is pinned by a strong copy on every call

**Evidence [measured].**

| Workload | Callee pins | Where |
|---|---:|---|
| fib_recursive | 13,069,846 (exactly one per call) | `raw_call_enter` at `C_ARGS` |
| DOOM | 3.26M | 2.13M `PROBE_CALLEE` + 1.13M `C_PROBE` |
| GQ4 | 2.60M | |
| tree | 2.59M | |
| OSS | 0.32M | |

**Cause [read].** `vm_backend::raw_call_enter` takes `strong_ptr<script_function> pin = site.ic_pin` and moves it into
`call_record::direct_pin`. `exec_probe_callee` and `park_callee` do the same into the record's pin. The pin keeps a running function
alive if its definition is replaced mid-call. The site's inline cache already holds `ic_pin`, so a second count is taken only to cover
that rare replacement.

**Change.** Retire, don't pin. When an IC refill or a redefinition drops a function, its handle moves to an engine-owned retired list
that is cleared when the outermost host entry returns, or at the next definition epoch with no frame of it live. Calls then borrow the
site's `ic_pin` (one raw pointer in the record).

**Safety.** A function object outlives every frame running it, because nothing frees a retired function before the frames above the
host entry are gone. A redefinition in the middle of a call (an import or include re-run inside it) retires the old function instead
of freeing it, so the running frame keeps a live body. The list holds one entry per replaced definition per host entry. Parallel
workers take no counts either way.

**Gain [inferred].** fib_recursive ~2-4% (one increment, one decrement and a record write per call); DOOM, GQ and tree ~0.1-0.4%.

### 4. Read-only proof gaps: class-typed declarations from a chain, and graft parameters

**Evidence [measured].** 77% of GQ's deep copies (111,097 in GQ4, 10 values each) are `Item` copies in `vm_backend::bind_graft_parameter`
from `player_bonus` (`gq_items.jai:198`, `item_stat(worn[i], code)`, inlined). The same callee refuses the read-only proof when it is
called normally: 2,457 binds are flagged "read-only proof refused; argument shared". The locals `it` (4,492), `d` (2,538), `wpn` (333)
and `md` (116) are flagged "local alias refused".

A probe isolates the cause. Four copies of `item_stat` differ only in their bodies:

- `int stat_full(Item it, int code) { … ItemDef d = ITEMS[it.base]; … }` is refused (4,000 of 4,000 binds copy);
- the same body without the `ItemDef d` declaration (reading `ITEMS[it.base].aff_code[k]` in place) is proven (0 copies).

In particles, the `Particle(Vec3 p, Vec3 v)` constructor's binds copy for the reason "no alias function" (constructors are not proof
candidates).

**Cause [read].**

- (a) In the admission walk's proof mode, a declaration `T x = init;` escapes the root when `conversion_may_run_code(T, init)`
  (`parallel_transform.cpp:784`). For a class `T`, that compares `static_class(init)` with `T`'s name. `static_class` (`:684`)
  types only identifiers and element-rooted chains. A chain rooted at a program-scope variable (`ITEMS[it.base]`) or at a local
  (`c.pack[i]`) answers empty, so the walk assumes a converting constructor might run and refuses the whole function. The
  `proof_chain_type` next to it already types both kinds of chain.
- (b) `bind_graft_parameter` (`vm_backend.cpp:11840`) binds a graft's by-value parameter with `derefed.clone()` or `converted.clone()`
  unconditionally. `bind_parameters`' by-value lane (whose comment it claims parity with) consults `readonly_parameter_alias` and the
  prvalue elision (`uniquely_held` over a stack argument) first.

**Change.**

- (a) `static_class` falls back to `proof_chain_type(e.get())` for any member or subscript chain, so a chain whose static type is
  exactly `T` (a declared field or element, never `var`) converts nothing.
- (b) The graft builder stamps the graft's parameter `variable_decl` with its callee (the `script_defined_function` the graft guard
  pins) and the parameter index. `bind_graft_parameter` then takes the same two exits as `bind_parameters`, before its clones:
  - `readonly_parameter_alias(*engine_, fn, i, true)`, which shares the handle;
  - a uniquely held, non-reference `arg`, which moves.

**Safety.**

- (a) The static type is the declared type, and a `T` slot taking a `T` value runs the identity row (no constructor, no `to_*`). An
  element typed `T` may hold a subclass, but a declaration copies it by value without user code, exactly as the element-rooted case
  already argues.
- (b) The graft runs only while its guard holds the same callee, the proof is cached against `definition_epoch()`, and the elision is
  the C++17 rule `bind_parameters` already applies. `nocopy`, host objects and script destructors are refused inside the proof
  (`parameter_alias_allows`, the destructor rule).
- Gates: `parameter_alias_differential_gate`, and `jaiscript_fuzz --no-param-alias` with grafts on and off.

**Gain [inferred].**

- GQ: ~122k instance copies per GQ4 run at ~80 ns each (an allocation, 10 fields, a later free); with (a) and (b) together, ~10 ms,
  ~0.2-0.5%.
- particles: the constructor case needs (c), below, as well.
- (c) Extending the proof to constructors (a by-value constructor parameter that the body only stores into a field and reads) is a
  separate design: the store into a field is an escape by today's rules. Finding 7's move is the cheaper route there.

### 5. A reference to a global is resolved on every execution

**Evidence [measured].** DOOM: 2,456,931 `environment::get_value_ptr` calls at `DECL_REF_IDENT`. They are `draw_seg`'s
`var& cliptop = CLIPTOP;` … `array<int>& palcm = PALCM;` (`render.jai:342-350`, 168,547 each per run), plus 3.72M reference copies in
`decl_ref_alias`. queen and sha256 show the same opcode (5.9k, 7.8k).

**Cause [read].** `DECL_REF_IDENT` over a program-scope name resolves the name through the environment's hash (`get_value_ptr`) on
each execution, then shares or boxes the cell (`decl_ref_alias`, `share_env_ref`). `LOAD` of the same names hits the per-instruction
environment cache (`env_lookup_cached`: `{env, epoch}` → pointer) almost always.

**Change.** `DECL_REF_IDENT` consults the instruction's `env_lookup_cache_entry` like `LOAD`. A hit hands the cached cell pointer to the
reference bind, and a miss runs today's lookup and fills the cache. When the global is already boxed in a cell (the second execution
on), the bind is one shared-holder copy and no lookup.

**Safety.** The cache's validity rule (the environment serial and epoch, moved by any define, reset or parent change) is the one `LOAD`
relies on. The cached pointer targets stable deque storage. Program-scope shadowing by unit is respected because the cache is keyed on
the environment the lookup ran against.

**Gain [inferred].** 2.46M × ~12 ns ≈ 30 ms per DOOM timedemo, ~0.5%; up to ~1% with the copy share. This belongs with lever B's
reference work.

### 6. The member-shadowing probe is re-run for every unqualified name in a method

**Evidence [measured].** Every `get_class_instance` call the census sees in the hot loops comes from the member scope:

| Workload | Probes | From `member_scope_env_lookup` (operand reads) | From `fused_ident_value` (callee and identifier reads) | From `member_scope_has_method` | Field lookups behind them | `get_method` behind them |
|---|---:|---:|---:|---:|---|---:|
| OSS | 394,001 | 292,941 | 74,939 | 18,301 | 325k scans (11 slots) + 126k hashes (57 slots) | 182k |
| sort | 394,151 | 394,139 | | | 394k scans (2 slots) | |
| GLOOM | 176,834 | 113,182 | 14,627 | 25,083 | 128k hashes (65 slots) | 55k |

The operand reads sit in cell-block entries and typed operations (OSS `CELLS_CFOR_BACK`, `CELLS_ENTER`, `T_MUL_FF`; sort's comparator
lambda), and the callee reads at `PROBE_CALLEE` (OSS `build`, `<script>:635`).

**Cause [read].** Inside a method, `fused_cmp_operand_env` (`vm_backend.cpp:6108`) and `fused_ident_value` route every name that is
not a frame local through the member-shadowing ladder (`member_scope_env_lookup`, `vm_backend.hpp:1640`). For a name that is *not* a
member (a global such as `PIX`, `G`, a free function), `probe_member_scope` (`member_scope.hpp:321`) runs the negative probe on every
read:

- `this_val.get_class_instance()`: a holder copy plus an atomic `std::shared_ptr<class_instance>` copy and release;
- `class_def->field_slot(sym)`: a scan or a hash;
- `find_field_value(sym)` for runtime-added fields;
- `get_method(sym)` up the chain;
- the static chain;
- for pointer-only sites, `member_scope_has_method`: the instance and the method lookup a second time.

Only then does the read reach the environment cache. The positive case (a name that is a field) has a proof (`proven_field_cell`);
the negative case has none.

**Change.** Give the negative verdict a cache with the proof's shape. The identifier node, or the operand's cache slot, records
`(class_definition* of this, definition epoch) → not a member`. A later read whose receiver's class and epoch match skips the probe,
provided the instance has no runtime-added fields (`overflow_fields_` empty, one pointer test), and goes straight to
`env_lookup_cached`. The checker can stamp the static half at load: the declaring class chain declares no field, method or static of
that name. The run-time key covers subclasses, since the dynamic class may add one. Independently, `probe_member_scope` and
`member_scope_has_method` read the instance through a raw `class_instance*` (the `TYPEID_OBJECT` handle's `data.get()`) instead of
`get_class_instance()`'s two copies. The probe runs no script, and the frame's `scope_this` slot holds the handle throughout.

**Safety.** The answer to "does class C at definition epoch E have a member named n" cannot change without moving E: hot reload,
class redefinition and `extend` all move the definition epoch or re-class the instance, so its class pointer changes. Runtime-added
fields are excluded by the overflow test. The lookup order (locals, members, globals) is unchanged, because the cache only remembers
that the member rung answered nothing.

**Gain [inferred].** The probe costs ~30-50 ns (the atomics dominate). OSS 394k probes ~1.5-2% of the run, GLOOM ~1%, sort ~0.7%.
The raw-pointer half alone recovers about half of that. Other holder copies of the same kind, smaller:

- `exec_call_method`'s receiver holder (sort 100k, DOOM 104k);
- `script_array::get` boxing an `array<Class>` element's holder for a read (GQ4 1.82M: `DECL_VAR` 961k, `MEMBER_TEMP` 548k).

### 7. Constructor arguments copied twice; `_cpp_object` interned per instance

**Evidence [measured].**

- particles' `Particle(Vec3(…), Vec3(…))` makes three copies of each `Vec3` temporary:
  - `capture_argument` (`initialize_members`, 200,000);
  - `bind_parameters` (`push_method_frame`, 200,000, "no alias fn; args not on stack");
  - the field store `pos = p` (`clone_for_assignment`, 199,998).
- The particle is copied again on `push` (finding 15).
- particles makes 901,772 run-time interns: `make_particles` 400k, top level 200k, `Particle` 200k and `T_PUSH` 200k. tree makes 39,997
  at `NEW`.

**Cause [read].**

- A constructor whose class has code-running field initializers captures its arguments ahead of the initializers
  (`construction_runs_code`, `capture_argument`). The capture already deep-copies each by-value argument into the argument vector.
  `bind_parameters` then copies it a second time: the prvalue elision applies only to stack arguments (`args_are_stack`), and this
  vector is not the stack, although the captured copy is held by nothing else.
- `class_instance::get_cpp_object_field_id()` (`class_definition.hpp:3906`) caches the interned `_cpp_object` id per instance
  (`cpp_object_field_id_`), so every new instance interns the text once, on its first deep copy or field write.

**Change.**

- When the argument vector is the capture's own (built by `initialize_members` or the later-capture path) and an argument is
  uniquely held, the bind moves it, as it moves a stack prvalue.
- The `_cpp_object` id becomes engine-constant: interned once, at engine construction, next to the other well-known ids. The
  per-instance member goes, which also shrinks every instance by 8 bytes (lever A's layout).

**Safety.** The captured copy is owned by the argument list alone (the capture made it, nothing else names it), so moving it is the
C++17 elision that `bind_parameters` already applies to stack prvalues. Interning is idempotent, so the id is the same value
precomputed.

**Gain [inferred].** particles ~1.4% (200k instance copies plus 600k interns), tree ~0.1%. The games construct far less per tick.

### 8. Dying temporaries copied into containers; copy assignment allocates

**Evidence [measured].**

- DOOM `anim.jai:100`, `FLATS[slot] = flat_pix(nm)`: `flat_pix` returns `FLAT_CACHE[name]`, a required copy (`tex.jai:220`, 496 ×
  4,097 elements). The element store then copies the returned temporary again (498 × 4,097; the source dies in the same instruction
  74%, within 8 instructions 18%) at `convert_container_element:327`.
- GLOOM `render.jai:355` (`STRIPS[sidx] = build_strip(…)`, 1,388 × 34; the source dies in the same instruction 95%).
- OSS `__jai_parallel_tchunk` (400 × 101) and DOOM (33 × 11,443) store a uniquely held typed array through a reference with
  `clone_for_assignment` (`ref_store_through_named`); the source dies in the same instruction 100%.
- GLOOM `render.jai:331`, `pix = G.bg;` (the script's deliberate copy, 592 × 13,601 elements) allocates a fresh 109 KB buffer and frees
  the old one every frame.

**Cause [read].**

- `convert_container_element_taking` moves only a uniquely held *class instance*. A uniquely held array or map falls to
  `convert_container_element`, whose nested-container row returns `inner.value->clone_element()` when the boundary leaves the node in
  place.
- The reference store's kernel calls `clone_for_assignment` without `take_for_assignment`'s uniqueness test.
- A typed-array assignment into a slot holding a typed array of the same kind replaces the node, through `clone`, instead of assigning
  into it.

**Change.**

- `convert_container_element_taking` moves a uniquely held array or map whose boundary verdict is "fits as it is" (the rebuilt case
  already moves).
- `ref_store_through_storage_with` takes `take_for_assignment` instead of `clone_for_assignment`.
- A store of a typed array into a slot whose current node is uniquely held and of the same kind assigns the buffer in place
  (`assign_buffer` reuses its capacity: C++'s `vector::operator=`).

**Safety.**

- `uniquely_held` means no other handle, slot or element can observe the source, and moving it is the C++17 rule the instance case and
  `take_for_assignment` already apply. Arrays and maps run no destructors themselves, and their elements move with them, so no
  destructor runs earlier or later.
- In-place assignment is unobservable when the destination node is uniquely held. No reference to an element of the old buffer can
  exist, because a reference pins the node and so makes it shared.

**Gain [inferred].**

- GLOOM: the allocation and free of 592 × 109 KB, ~0.5%.
- DOOM: 2M element copies, <0.1%.
- These are large per event (4-109 KB) and grow with asset sizes. The moves are lever A9's family; the in-place assignment is lever
  A's copy-assign.

## Where the existing proofs and moves fire

Elision hits per run [measured] (the census's `move_hit` site):

| Mechanism | GQ4 | GLOOM | DOOM | OSS | Misses, and why |
|---|---:|---:|---:|---:|---|
| `local_alias` (read-only by-value locals) | **960,699** | 0 | 0 | 0 | GQ: `it`, `d`, `wpn`, `md` (7,479) refused by the declaration gap (finding 4a); 8,000 `q` (`players_tick`) correctly refused, because the source is overwritten next. GLOOM, DOOM, OSS: every declaration copy is "no alias home": top-level code, methods and `var` slots are not candidates |
| `parameter_alias` (read-only by-value parameters) | 1,355 | 0 | 0 | 0 | GQ4: 2,457 direct `item_stat` binds refused by the declaration gap (4a); 111,097 graft binds never consult it (4b); constructors and methods are not candidates (particles: 200,000) |
| prvalue elision of a by-value argument | 0 | 0 | 0 | 2 | particles: a constructor's arguments are a vector, not the stack (finding 7) |
| return: own slot moves (NRVO) | 2,443 | 2,676 | 57 | 411 | the remaining return copies are lvalue reads of live storage (`flat_pix`'s and `sprite_lut`'s cache entries): required |
| return: temporary handed out | 1,364 | 2,401 | 202 | 15 | |
| `take_for_assignment`, store and element-store moves | 0 | 199 | 39 | 0 | instances only: arrays and maps copy (finding 8); tree's 19,998 prvalue stores move |
| per-type destructor gate | | | | | no workload declares a script destructor; the gate never refuses |

GQ2's figures scale with GQ4's (local alias 151,721; parameter alias 1,240; NRVO 1,277). The kernels' proofs fire only in `tree`
(19,998 prvalue stores) and on the few NRVO returns of their setup code.

## Classification of the remaining copy volume

- **(a) Required by value semantics.** The cache returns (`flat_pix`, `sprite_lut`), `pix = G.bg`'s contents, `sort`'s
  `array<int> c = t`, GQ's `array<string> q = IN_QUEUE[slot]` (the source is overwritten right after: a move idiom the language does
  not have), and the strings pushed into containers (one count per stored element).
- **(b) Elidable.**
  - Graft and declaration-gap copies (finding 4);
  - dying container temporaries (finding 8);
  - captured constructor arguments (finding 7);
  - every transient count of rows 2, 3, 6 and 10;
  - `T_PUSH` of a unique instance (lever A9).
- **(c) Double copies.**
  - `FLATS[slot] = flat_pix(nm)`: the return copy, then the store copy;
  - the constructor capture, then the bind (finding 7);
  - `join`'s two boxing passes (finding 2);
  - tree's field store through a reference: `ref_field_store`'s `clone_for_assignment` handle copy followed by
    `typed_store_convert`'s copy of the same handle, 2.1M + 2.1M.

## Front-loading summary

| Re-derivation | Hot? [measured] | Note |
|---|---|---|
| string interning | no: 17k-43k per game run, all at load, import, class or function declaration, except `particles` (finding 7) and GLOOM's `NEW` (9.5k) | |
| `get_string` | no: ≤82k per run, error texts and declarations | |
| `type_info` by structure or name | no: ≤12k per run, at load; DOOM `CALL_METHOD` 1.3k | |
| class lookup by name | no: ≤6k per run, at import | |
| chunk compiles | no: 99-331 per run, each chunk once | |
| environment walks (`vm_storage_lookup`) | small: GLOOM 17k (`LOAD`), others ≤6k | |
| environment defines at run time | GLOOM 28.9k at `NEW` (construction scopes) | lever A (construction) |
| enum member hashing | **yes**: GQ 3.2M | finding 1 |
| global reference resolution | **yes**: DOOM 2.5M | finding 5 |
| member-shadowing probe for non-member names in methods | **yes**: OSS 394k, sort 394k, GLOOM 177k | finding 6 |
| field slot by id | **yes**: tree 11M, GQ 0.8M (field references re-resolved per dereference) | lever B |
| method lookup by id | OSS 186k, GLOOM 58k (all from the member-shadowing probe) | finding 6 |
| identity conversions the checker proved | GQ4 968k | finding 14 |
| map key hashing of constant keys | GQ enum probes (finding 1); DOOM string keys 1.8M | lever C |
| container homogeneity walks | 0 in every workload | |

## Reproducing

```
cmake -S . -B out/build/x64-CopyCensus -G Ninja -DCMAKE_BUILD_TYPE=Release -DJAISCRIPT_LTCG=OFF -DJAISCRIPT_VM_COVERAGE=ON ^
      "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /Z7" "-DCMAKE_EXE_LINKER_FLAGS=/DEBUG /INCREMENTAL:NO"
cmake --build out/build/x64-CopyCensus --target jaiscript_runner jai_gloom jai_demoreel
set JAI_COPY_CENSUS=<out>\gq2.txt
jaiscript --no-debug --no-pause --backend=vm --coverage=gq2.cov.txt scripts/gatorquest.jai -- smoke ticks 2000 bots 2   (examples/gatorquest)
```

The other workloads run as in `docs/fast_path_census.md`, each with its own `JAI_COPY_CENSUS` file. The kernels run with a
`profile.jai` that calls the kernel exactly three times. A census run costs 2-5x a coverage run (GQ4 13 s, DOOM 13 s).
