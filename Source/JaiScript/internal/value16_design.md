# The 16-byte value

`script_value` is 16 bytes on every host: an 8-byte payload and an 8-byte tag word.

```
bytes 0..7    payload       int64 / double / char / bool / one strong_ptr / weaker_ptr / parallel borrow
bytes 8..15   tag word      bits 0..7   storage index (TYPEID_*), read as one byte
                            bit  8      k_engine_only: the pointer above is the value's engine, not a type
                            bits 9..63  a 512-aligned pointer: the declared type (type_info) or the engine
```

`k_value_tag_alignment` (type_info.hpp) is the one alignment both `type_info` and `engine` declare and the pointer mask
derives from; `script_value` static-asserts that the flag sits above the index byte and below that alignment, and only
`stamp()`, `set_stamp()`, `set_engine_tag()`, `get_engine()`, `stamped_as()` and `is_outlived()` decode the word. Every
interned type goes through one engine routine (`intern_type`) that sets its `owner`. Review Regressions
`tag_word_stamps_and_engines` pins alignment, stamped, unstamped and literal words, the outlived sentinel and the import
restamp.

## Decisions

- **One value type, not two.** The brief proposed a 16-byte internal value and a 32-byte host value converted at the
  boundary. Every host-facing container accessor (`script_array` elements, map entries, `as<std::vector<...>>`, signal
  arguments, archive reads) hands out `script_value&` into internal storage, so a second type would put a conversion
  (or a proxy) on every one of them and double the surface both backends and the binders compile against. The tagged
  word keeps one type, keeps `get_engine()` and `get_type_info()` as they were, and leaves the public API source
  compatible: no host, example or docs sample changes.
- **The engine is reachable from the value with no static state.** A stamped value's engine is its type's `owner`
  (every `type_info` the engine interns, and every chunk-owned declared type, records the engine that made it). An
  unstamped value with an engine (`script_value(std::monostate{}, eng)`, a value whose stamp was cleared) stores the
  engine pointer itself with `k_engine_only`. A value with neither (an AST literal) has a zero tag, as before. No
  table, no `thread_local`, no static lookup: multiple engines on multiple threads stay independent.
- **The declared type stays a pointer, not a 32-bit id.** A 32-bit id needs a table to resolve, and the table needs
  the engine, which the value no longer carries separately; the pointer resolves itself and keeps every hot guard
  (`type_info_raw() == expected`) a compare of one masked word. `type_info` and `engine` are `alignas(512)`, which
  frees bits 0..8 for the index and the flag while keeping every high bit of the pointer, so tagged heap pointers
  (Android's top-byte tags, MTE) survive untouched.
- **The index stays a byte load.** `storage::idx_()` reads the word's low byte (little-endian, static-asserted), so
  dispatch on the index is the same `movzx` it was. Payload assignments write the index byte and leave the type bits;
  copy and move constructors carry the whole word; the value's copy and move assignments set the tag bits before the
  payload, keeping the release-last order (invariants §2c).
- **The interpreter moves with the VM.** There is one value type, so both backends hold the same 16 bytes.
- **32-bit hosts.** The tag word stays 8 bytes (a 4-byte pointer above the index), so the value is 16 bytes there too.

## Costs

- `get_engine()` on a stamped value is one dependent load (`type_info::owner`). The VM and interpreter keep their own
  `engine_` and read it from context on hot paths; the value's engine is read on boundary and cold paths.
- `type_info` objects round up to 512 bytes each (the engine interns a few hundred).
- `set_engine` on a stamped value cannot name a different engine than its stamp's (Debug-asserted); no caller does.

## Ledger

| stage | sha | notes |
|---|---|---|
| 1 layout | 91a6c4a7e | 16-byte value. A/B vs 3f2f97007, 5 rounds: dict -13/-8%, particles -6/-7%, sort +26/+29% (store-forwarding stall: a byte index store, then an 8-byte tag read), fib_recursive_dyn +18%, GLOOM +1.3%, OSSUARY +3.8%, Gator +3.0%, JaiDOOM 0% |
| 2 whole-word tag | e1683c27e | index written as the whole word; one-compare `stamped_as` at the VM landings; type_info slabs. A/B 5 rounds (busy machine, both sides ~10% slower in absolute terms): sort -16/-19%, dict -12/-9%, particles -7/-8%, fib_recursive_dyn -10%, queen_typed -6%; GLOOM +1.8%, OSSUARY +1.8%, JaiDOOM +5.3% (1.70 vs 1.79 ms/tic), Gator -0.8%; mandelbrot_dyn +9%, spectral_norm +5%, string2float_dyn +6%. Peak memory: container kernels -8..-19 MB, Gator +6.8 MB, JaiDOOM +1.6 MB, GLOOM +0.7 MB |

| rebase | (this commit) | onto b0031f17d. Debug: interpreter 4622/4622, vm 4622/4622; vm-reference aborts in Review Regressions `cross_engine_reference_no_foreign_twin_stamp`: engine B stores a B-stamped int into engine A's array, B dies, A mints an element reference and reads the freed B type (the stamp-lifetime hazard below) |

| held values | fc55b931d, f3204b611 | Debug: interpreter, vm, vm-reference 4624/4624 each |

Against master b0031f17d built at a path of the worktree's length, 5 interleaved rounds under the timing lock, medians
(hashes held everywhere; memory is peak commit):

| workload | Shipping (LTCG), fc55b931d | Release, fc55b931d | Shipping, + inline copies (reverted) |
|---|---|---|---|
| GLOOM serial | -3.8% | -3.9% | +1.7% |
| JaiDOOM timedemo | +1.3% | 0.0% | +0.6% |
| Gator Quest | +1.8% | +2.7% | +1.1% |
| OSSUARY | +1.2% | +1.6% | +2.2% |
| dict dyn / typed | -7.2 / -8.1% | -7.9 / -6.3% | -6.2 / -4.9% |
| sort dyn / typed | -14.5 / -15.9% | -14.4 / -16.0% | -15.4 / -17.0% |
| particles dyn / typed | -9.5 / -5.8% | -6.5 / -6.3% | -4.4 / -8.2% |
| tree dyn / typed | -1.9 / -4.9% | -1.0 / -4.3% | -2.6 / -4.4% |
| fib_recursive_dyn | -4.4% | -5.0% | -5.3% |
| fib_loop_typed | +11.6% | +10.8% | +6.3% |
| mandelbrot_typed | +9.1% | +7.6% | +3.6% |
| string2float_typed | +6.1% | +6.3% | +5.3% |
| float2string dyn | +7.2% | +7.3% | +3.2% |
| primes dyn / typed | +2.5 / +1.6% | +3.4 / +3.9% | +2.9 / +3.1% |
| nbodies dyn | +1.7% | +2.6% | +3.1% |
| peak memory | Gator -1.2 MB, dict -16.7 / -6.7 MB, particles -19 / -17.5 MB, sort -8.3 MB; others within 0.5 MB | same | same |

The inline-copy change (copy constructor and assignment inline, scalars copied as two words without dispatch) did not
measure and was reverted. GLOOM's sign flips between two builds that differ only in code it does not run (-3.8% to
+1.7%): whole-program deltas within about 4% are per-build code layout in the register-cell loop, not the value.

Gates at f3204b611: `battery.sh --build` BATTERY GREEN (Debug and Release suites on three backends, fuzz,
fuzz-vm-reference, every example hash, timedemo, LSP, amalgamation); `gates.sh android` built the library and suites with
NDK 27.2 for armeabi-v7a (the 32-bit build); Bindstone client Release built, `-verifyassets` 76 passed, `-loginshot 60`
60/60 frames, `-workbenchsmoke` 120 frames; `mv_tests` 146/146.

Verdict: shelve. GLOOM's -3.8% on Shipping does not survive a rebuild that changes nothing GLOOM runs, Gator Quest
regresses 1.8% (Shipping) and 2.7% (Release), and the typed scalar kernels regress 4-12%; the container kernels' 5-17%
wins and their peak-memory drops are real but are not a whole-program win.

Quiet-machine re-run (f3204b611 vs master b0031f17d, equal paths, 7 interleaved rounds, medians [base min-max / v16
min-max]; hashes GLOOM 3580805725, JaiDOOM 1746772898, Gator 3218840367, OSSUARY f5ca54c216432104 on both sides):

| workload | Shipping | Release | base vs base (Shipping, 2 rounds) |
|---|---|---|---|
| GLOOM | -0.5% [224-236 / 222-241 ms] | -1.3% [230-236 / 223-233] | +5.2% |
| JaiDOOM | -0.7% [1.51-1.53 / 1.51-1.54] | 0.0% | -0.3% |
| Gator Quest | +0.5% [764-780 / 766-774 us] | +1.7% [760-771 / 770-791] | -0.7% |
| OSSUARY | +4.3% [135-154 / 140-146 ms] | +3.3% [137-139 / 141-151] | +1.2% |
| sort dyn / typed | -16.6 / -16.0% | -17.1 / -16.6% | +1.5 / 0.0% |
| dict dyn / typed | -5.9 / -4.7% | -8.4 / -5.4% | +2.1 / +0.4% |
| particles dyn / typed | -9.7 / -6.9% | -10.0 / -7.0% | -2.3 / +7.1% |
| tree dyn / typed | -3.1 / -2.2% | -3.1 / -5.4% | -1.6 / -0.4% |
| fib_recursive_dyn | -3.9% | -3.6% | +1.1% |
| exp_dyn | -3.5% | -4.1% | +0.3% |
| fib_loop_typed | +6.3% | +5.0% | +0.6% |
| string2float_typed | +6.0% | +6.1% | +1.2% |
| float2string dyn / typed | +5.1 / +5.7% | +3.8 / +4.8% | +0.7 / -0.4% |
| primes dyn / typed | +3.1 / +3.1% | +4.6 / +3.0% | +0.4 / +0.4% |
| nbodies dyn / typed | +3.0 / +1.3% | +2.3 / +2.0% | -1.8 / -0.7% |
| mandelbrot typed | +2.1% | +3.6% | +1.2% |

The verdict stands: no whole program wins by 1.5% on either configuration, and OSSUARY regresses past 1.5% on both (its
v16 range sits above the base median in both). The same-binary floor reaches 5% on GLOOM and 7% on one kernel.

### S0: where the regressions come from (RIP sampler over /Z7 builds of f3204b611 and b0031f17d, equal paths)

| workload | v16 vs base | profile delta (pp of samples) | attribution |
|---|---|---|---|
| fib_loop_typed | +5-6% | `cells_run_frame` 98.5 -> 98.6; its dispatch head 30.8 -> 41.5 | register allocation in `cells_run_frame`: base keeps the image base in r9 across dispatch, v16 rematerializes it with two `lea` per dispatched cell op (disassembly at the head, `+0x133..0x169`). The function reads no value header on that path: no tag RMW, mask, engine chain or `stamped_as` |
| primes_typed | +3% | `cells_run_frame` 97.4 -> 97.3, the same head | the same register-allocation shift |
| OSSUARY | +3-4% | `cells_run_frame` 46.2 -> 48.2 (+2.0), `exec_typed_arith` -0.6, out-of-line `storage::storage` +0.4 | mostly the same shift (OSSUARY is half register-cell code; Gator and JaiDOOM run far less of it), plus copy-constructor calls now emitted out of line |
| string2float_typed | +6% | `run_dispatch` +3.0, `store_popped_value` +0.7, `get_array_storage`/`get_reference_holder` +0.6 each (now out of line), `exec_call_builtin` -1.6, `truncate` -1.0 | value mechanics: accessors that stopped inlining and stamp writes on the generic store path; partly offset by cheaper truncation and copies |
| float2string_dyn | +4-5% | `run_dispatch` +3.6, `set_type_info` +1.1 (base: `operator=` 1.1), `range_for_bind` +1.4, `var_stamp` +0.6, `exec_iter_next` -1.7 | stamp writes: a range-for's var binding restamps each element (`set_type_info` -> tag RMW, `var_stamp`) where base assigned the type pointer |

The typed scalar loops and most of OSSUARY regress from MSVC's register allocation in one 29 KB function that never
touches the value layout, so the brief's hypotheses (a)-(c) do not explain them. Hypothesis (a) does explain the
string kernels' few points: generic stores that restamp (`set_type_info`) and accessors that fell out of inlining.

### Option 2 (8197c756a): targeted fixes

`cells_run_frame` stabilization was tried first, on master: moving `exec_c_load`, `exec_c_index_store` and
`exec_c_back` out of line made the dispatch head identical across master and value16 (insensitive to the value layout),
but in the slower form (the image base rematerialized per dispatch) and the moved rows cost 15-23% on the typed loops,
so it was dropped. The base-vs-base floor is the same binary twice: it is machine noise, not code layout.

The value fixes: `set_type_info` writes nothing when the stamp is unchanged and runs the array demote out of line; the
storage copy is inline with the scalar case first (`copy_handle` out of line); `get_array_storage` and
`get_reference_holder` inline. Same protocol (7 rounds, equal-path master base, same session floor):

| workload | Shipping | Release | base vs base | was (f3204b611, Shipping) |
|---|---|---|---|---|
| GLOOM | -1.1% | -1.0% | -2.3% | -0.5% |
| JaiDOOM | -0.7% | 0.0% | +1.0% | -0.7% |
| Gator Quest | +0.4% | +0.7% | +0.6% | +0.5% |
| OSSUARY | +0.3% | +0.8% | -0.1% | +4.3% |
| OSSUARY in Lua 5.4.8 (same session) | 175.5 / 174.7 ms | | | |
| sort dyn / typed | -18.0 / -16.9% | -17.8 / -17.7% | +2.7 / +1.1% | -16.6 / -16.0% |
| dict dyn / typed | -6.6 / -6.5% | -8.1 / -11.5% | -4.2 / +1.2% | -5.9 / -4.7% |
| tree dyn / typed | -4.8 / -5.7% | -4.6 / -6.6% | -0.8 / +1.1% | -3.1 / -2.2% |
| particles dyn / typed | -5.7 / -8.0% | -5.9 / -5.5% | -0.2 / -0.3% | -9.7 / -6.9% |
| exp dyn | -5.4% | -4.4% | +1.3% | -3.5% |
| fib_recursive dyn | -4.0% | -3.2% | -1.7% | -3.9% |
| float2string dyn / typed | -1.7 / -3.7% | 0.0 / -3.1% | -0.1 / +1.5% | +5.1 / +5.7% |
| string2float dyn / typed | -1.6 / +6.2% | -2.2 / +6.2% | -1.5 / -0.7% | +2.4 / +6.0% |
| fib_loop typed | +3.3% | +4.0% | +2.1% | +6.3% |
| primes typed | +3.4% | +3.6% | -1.2% | +3.1% |

Attribution (sampler): OSSUARY's `cells_run_frame` share is now within 0.4 pp of master (it was +2.0), and no other
function moves more than 0.6 pp. The function's codegen changed again (0x732c to 0x6bc0 bytes) with the inline copy;
its dispatch head still rematerializes the image base, so the typed loops keep 3-4%. string2float_typed's +6% is the
range-for var bind (`range_for_bind` +1.1 pp, `element_cursor_holder` +0.6) against a cheaper `exec_iter_next` (-3.0).
JaiScript runs OSSUARY 21% faster than Lua 5.4.8 on both builds; value16 does not move that gap. LuaJIT has no OSSUARY
port.

Verdict: below the bar. No whole program wins by 1.5%, and none regresses past it. The cell-to-value boundary
(`cells_enter` 4.8%, `cells_writeback` 4.0%, `typed_land_float` 1.7%, `cell_land` 0.3% of OSSUARY) is the remaining
surface an S2 redesign (frame cells as 16-byte values) would act on: about a tenth of OSSUARY.

### Gator's +6.8 MB is a measurement artifact

An allocation histogram (live blocks at peak by request size) over `gatorquest smoke ticks 10` shows one row moving:
154,271 blocks of 80 bytes in base become 156,152 blocks of 128 bytes on the branch. These blocks are the `filename` strings of
AST `source_location`s (`ast_reader::location`): every node copies the full script path. The base tree was built in
`D:\v16base` (a short path), the branch in the worktree (58 characters), so each copy crossed a heap size class. Interned
types: 158 in Gator (80 KB at 512 bytes each). With equal-length checkout paths the delta is gone; base builds for later
measurements sit at a path of the worktree's length.

### Stamp lifetime (Dev ruling 2026-10-23: values die with their engine)

A stamp points into its engine's memory, so a value must not be read after its engine is gone. Inside the engine that holds
by construction. At the edge:

- **Plain values are borrows.** A `script_value` C++ receives (a bound function's argument, a container element, a signal's
  argument) is valid for the call, like a C++ reference parameter. No copy, no count, no check on those paths.
- **Held values are registered, not pinning.** `held_value` (value.hpp) is a `script_value` plus its engine pointer and a link
  in the engine's registry (an intrusive list under a mutex in `engine::implementation`). `execute`, `execute_file`,
  `get_variable` and `hold` return one. The registry is touched only when a host constructs, copies, moves, assigns or drops a
  held value. Teardown (`~engine`, right after suspended runs are cancelled and before program-scope values die, then once more
  at the end for holds a destructor made) unlinks each held value under the lock, marks it outlived (engine null), swaps in the
  outlived sentinel and releases the payload outside the lock while the engine still runs destructors. The lifetime token
  expires after the whole destructor, so signal receivers and callbacks a held instance's destructor fires still run.
- **The outlived sentinel** is `TYPEID_INVALID` under a tag of `k_engine_only` with a null pointer, a word no live value
  carries: `get_engine()` and `get_type_info()` read null without a load. `held_value` hides the reading accessors (`as<T>`,
  `as_int`, `to_string`, `deref`, the conversion operators...) with versions that throw `Value outlived its engine.` first;
  `valid()` and `get_engine()` answer without throwing. The import below throws the same text for a sliced copy.
- **Values cross engines by copy.** `engine::make_value(script_value)`, `add_global`, `execute`'s instance variables and so
  `convert_script_function` arguments test `owns_value` (the value's engine is this one, or none) and otherwise run
  `import_value`: numbers, strings, utf8 strings and math values are remade with this engine, arrays and maps convert on an
  explicit work stack (typed buffers copy as buffers when their element type translates, strings are copied), and objects,
  functions and references (including `cpp_bound` and parallel borrows) throw `Cannot pass an object|a function|a reference
  from another engine: ...`. The check is one tag test and at most one load at those host APIs; no script path runs it.
- **Builtin type records stay per engine.** `type_info` carries mutable per-engine state (`owner`, which `get_engine()` reads,
  and the reference-twin caches written at run time), so one shared static record per builtin cannot serve every engine
  without retiring `get_engine()` from stamps (phase B). A reserved "builtin int" tag would put a branch on every stamp guard.
  The import restamps instead: a translated type is looked up in the receiving engine by name (scalars) or rebuilt from its
  translated parameters (arrays, maps), never interned from the foreign record (its symbol ids are the other engine's).

Tests: Review Regressions (in the Element Read Elision suite) `held_values_die_with_their_engine`,
`held_value_copies_and_moves_across_teardown`, `held_values_from_a_host_thread`, `cross_engine_values_import_by_copy`,
`callbacks_go_inert_with_their_engine`.

## Verdict at stage 2

This is below the landing bar. The container-heavy kernels win by 7-19%, but no whole program wins by 1.5%, and GLOOM,
OSSUARY and JaiDOOM regress past it. A RIP sampler over /Z7 builds shows the register-cell code (`cells_run_frame`, which
is 46-82% of those programs) running the same instructions under different register allocation: MSVC re-spills the
image base in the dispatch head of that 29 KB function. The value layout is not on that path, so the whole-program
deltas fall inside the per-build codegen variance of the cell loop. Gator's memory rises because every interned type
is now 512 bytes.
