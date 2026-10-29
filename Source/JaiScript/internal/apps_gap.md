# The benchmark apps against Lua

The four apps in `Source/JaiScriptComparisons/apps` (Richards, DeltaBlue, the bytecode VM, the spatial hash) are ordinary
programs: classes with `shared_ptr` links, small methods, range-fors over containers and maps. This note records where the
VM's time goes on them, why, and what the measurements say about the levers.

## Operation counts are not the gap

| app | JaiScript operations | Lua instructions | JaiScript ns per operation | Lua ns per instruction |
|---|---|---|---|---|
| richards | 82.8M | 103.7M | 11.2 | 3.3 |
| deltablue | 86.9M | 87.8M | 12.2 | 4.6 |
| spatial | 195M (7% in cells) | 172M | 18 | 10 |
| bytecode | 184M (148M cell dispatches) | 433M | 3.8 | 1.7 |

Operations from a `JAISCRIPT_VM_COVERAGE` build (a cell instruction counts as the general instruction it stands for), Lua
instructions from a count hook, times from the Release LTCG baseline. The programs execute as many operations as Lua executes
instructions; each operation costs three times as much. Bytecode, the one app already faster than Lua, runs in cells. Richards
and DeltaBlue run 92% and 98% of their operations on the general form, untyped: objects and handles keep them out of the typed
tiers, so their cost is the general form's per-operation cost.

## Where the time goes

A stack-walking sampler (dbghelp `StackWalk64`, main thread, about 400 Hz) over a Release `/O2 /Ob2 /Z7` runner without LTCG,
on a loaded machine; shares of all samples, each cost inclusive of its callees. Forcing `field_slot_value` inline measured
nothing either way.

| cost | richards | deltablue | spatial | what it is |
|---|---|---|---|---|
| call and return | 27% | 31% | 1% | `CALL_METHOD` (7.8M in a Richards run, 7.0M frames entered in all), `PROBE_CALLEE` + `CALL_FROM_SCRATCH` for bare names (5M `input()`/`output()` in DeltaBlue), the lean frame push (`enter_script_method_sliced`: about 400 instructions of record setup, spread with no hot spot), `return_with_result`, `pop_script_frame_core`, `land_call_result`. A method call and return costs about 40 ns (Lua 20 ns, the `mcall` kernel) |
| dispatch | 27% | 26% | 11% | `run_dispatch`'s own samples, the small handlers it inlines included. The main switch is a two-level jump table (byte index, then target): its load chain and the mispredicted indirect jump are 5% of Richards alone; every call and return re-enters through it |
| field and global access | 20% | 15% | 12% | bare fields in methods (`LOAD`/`STORE slot=env`, the this-field rung: layout generation, class compare, `field_slot_value` called out of line, 2-3%), `GET_MEMBER_THIS`, `SET_MEMBER*`, `MEMBER_TEMP`; program-scope constants through the environment cache (8M per Richards run, two compares and a pointer chase each); a null stored into a handle field takes `store_popped_value_full` (convert, push, pop) |
| handle counts | 7% | 8% | 11% | `copy_handle` and `destroy_handle` are out of line and switch on the storage index: every receiver load, argument and field read of a handle pays a call and an indirect jump. On spatial the copy is the first touch of a cold object, so it carries the cache miss |
| element references | - | 2% | 30% | `Entity& other = entities[j]`: a pooled `reference_holder` minted per neighbour (container count, live-reference count, the slot's tag), the admission's class check, then each `other.x` dereferences container, slot and instance again. Most of it is the miss on the entity's lines |
| map lookup | - | - | 11% | `buckets.contains(key)`: the slot, then the entry (a second line). Lua's table holds most of these dense int keys in its array part |
| iteration | 1% | 4% | 13% | `ITER_NEXT`, `script_array::get` boxing an element, `range_for_bind` |

No virtual call, `std::function` or `checked_result` shows in the hot paths: handlers are direct calls returning `op_status`.
The indirect jumps that cost are the dispatch switches and the handle switches.

## Why the typed tiers miss these programs

The coverage census names the refusals:

- Richards: the scheduler loop's head reads a field (`while (current_tcb != null)`, "loop head form", 24% of operations); every
  other method is straight-line code over handles.
- DeltaBlue: loops that call methods ("call boundary (method)", 22%), straight-line code over handles everywhere else.
- Spatial: `for (int j : bucket)` (36% of operations) and `for (Entity& e : entities)` were refused at their heads: a reference
  container (`array<int>& bucket`, an `array<Entity>&` parameter) is dynamic to the checker, so the binding of a declared `int j`
  is never proven conversion-free. The loops around them hold `buckets.contains(key)`, a builtin call the cells refuse as an effect
  (30%), though an int key runs no user code.

A reference local or parameter is dynamic for every proof, so `a.size()`, element reads and builtin calls through it all take the
general form. That is sound (the referent of a reference can change kind through another name), and it is the most common reason
ordinary code over containers stays general.

## What was measured

- **A range-for over an unproven container is a block.** A variable declared `int`, `float` or `bool` takes a cell of its declared
  kind; an element of any other kind misses at the landing onto the general `ITER_NEXT`, which converts it. Spatial's neighbour loop
  closes (4-7% faster on a loaded machine, interleaved; the loop is memory-bound). No game script declares a scalar range-for variable
  (JaiDOOM's and Gator Quest's manifests hold the same chunks).
- **Wider method grafts lose.** Grafting `current_tcb.is_held_or_suspended()` and the `mark_as_*` setters (a bare-field receiver
  read into a temp, the body's program-scope constants read as `::name` under the receiver-class guard) compiled and kept every
  result, and made Richards 10-20% slower. The same five methods inlined by hand in the source make it 10.6% faster. The graft's
  shape is the cost: the receiver copy into a temp, the guard, the receiver's fields read through `GET_MEMBER`/`MEMBER_TEMP` on a
  `var` temp (the callee's own body reads them as this-field operands), `::name` reads through `get_global_environment()` (a
  `shared_ptr` copy and a hash lookup, uncached), and the result through a general `DECL_VAR`. A method graft pays off only once
  its clone compiles as tight as the method's own body.

## Levers, by expected value

1. **Method grafts as tight as the method.** Under the guard's exact receiver class the body's fields are known slots: read them as
   this-field operands of the receiver (no temp copy for a field or `this` receiver), keep the callee's typed lowering, land a
   scalar result in a typed temp, and resolve program-scope reads through the environment cache. Then widen eligibility (bare
   method calls on `this`, field receivers, handle results outside closed loops). Richards' five setters and predicates are a
   measured 10%; DeltaBlue's `input()`/`output()` are 5M of its 8M calls.
2. **The call protocol.** A lean method frame writes dozens of record fields, four container sizes (three of them a division by a
   non-power-of-two element size), the receiver through an out-of-line `operator=`, `reset_call` out of line, and checks the
   receiver's layout again (`bind_member_layout`) after the site's inline cache checked its class. A record that keeps what every
   lean entry of one body writes alike, as raw frames do, is the shape that removes most of it.
3. **Zero-argument calls in one instruction.** `LOAD r; CALL_METHOD 0` and `PROBE_CALLEE; CALL_FROM_SCRATCH 0` have nothing between
   the receiver or callee and the call, so one instruction can resolve and enter: a dispatch and a pending-callee push and pop per
   call (internal/call_site_identity.md, "Eliding the probe").
4. **Builtin calls that run no user code as block operations.** `map.contains(k)` and `m[k]` with an int or string key, `a.size()`,
   `a.push(v)` of a scalar: a cell loop can take them as boundary operations, guarded on the receiver's kind.
5. **Handle counts without the switch.** The strong handle alternatives can share one count offset in their control blocks (a
   static assertion per build); a copy is then one increment and a release one decrement, the switch left to the last release.
6. **A flat main switch.** One-level dispatch for the main switch (the hot-op replicas already are), and a call or return that
   continues at the callee's first instruction without re-entering the loop head.

Spatial's remaining gap is memory: an entity is a control block, a 72-byte header and four cells over three lines, and a
neighbour visit touches the slot, the header and the cells. Flat storage for typed-field classes is the lever there.
