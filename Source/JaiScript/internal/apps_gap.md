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
- **Handle counts without the switch measure nothing.** On a 64-bit host every strong alternative keeps its object at one
  control-block offset, so a copy can increment and a release that is not the last can decrement without the storage-kind switch.
  The call kernel, Richards and DeltaBlue moved within the noise (+1.2%, +1.7%, -1.6% median): the switch's indirect jump is not
  where the count traffic costs.
- **The lean call record's bookkeeping measures nothing.** Writing what every lean entry of one body writes alike once per record
  (return type and conversion, the declaration's pin, name, unit and frame flags), and inlining `enter_frame`, `reset_call` and the
  unit sync, moved the call kernel 1.7% and Richards not at all. A call costs about 40 ns spread over its push, its pop, the return's
  conversion check and landing, the receiver's count and two dispatches; no single step dominates.

## Levers, by expected value

1. **Method grafts as tight as the method.** Under the guard's exact receiver class the body's fields are known slots. The tight form
   needs a typed operand for "field s of the instance in slot r" that the typed rows and the cell translation read (today a receiver
   field is a `MEMBER_TEMP` over a `var` temp), re-validated after any call the body keeps, and a typed temp for a scalar result.
   A bare method call on `this` needs none of it: the caller's frame holds the same receiver, so the clone can keep the body's bare
   names and compile them as the caller's this-field and this-global operands, guarded by the receiver class resolving the method to
   the grafted body and by the two classes resolving each bare name alike. DeltaBlue's `input()`/`output()` then need handle results
   (outside closed loops, every return proven, no destructor in the program) and a graft in a store's target
   (`output().value = ...`, refused today as "a call in a store's target").
2. **Builtin calls that run no user code inside cell loops.** Builtin methods carry no effect facts: none records that `contains`
   or `[]` with an int or string key, `size()` or a scalar `push` cannot run user code. With those facts a cell loop could take such a
   call as an operation (a door into the builtin, guarded on the receiver's kind); a boundary operation is not enough, since the hot
   case is a condition (`if (!buckets.contains(key)) continue;`) and boundaries take whole statements only.
3. **Fewer instructions per call.** The receiver load, the probe and the call, the return and its landing are separate dispatches
   through the two-level main switch. `LOAD r; CALL_METHOD 0` and `PROBE_CALLEE; CALL_FROM_SCRATCH 0` have nothing between the
   receiver or callee and the call (internal/call_site_identity.md, "Eliding the probe"); a call or return that continues at its
   target without re-entering the loop head saves the frame reload.
4. **Reference containers typed for proofs.** A reference's referent can change kind through another name, so a reference is
   dynamic to the checker; a tentative type from its declaration, which every consumer already guards, would let `a.size()`,
   element reads and builtin calls through `array<T>&` and `map<K, V>&` parameters take the typed paths.

Spatial's remaining gap is memory: an entity is a control block, a 72-byte header and four cells over three lines, and a
neighbour visit touches the slot, the header and the cells. Flat storage for typed-field classes is the lever there.
