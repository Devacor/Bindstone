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

- **A range-for over an unproven container as a block saves nothing.** A variable declared `int`, `float` or `bool` can take a cell
  of its declared kind, an element of any other kind missing at the landing onto the general `ITER_NEXT`, which converts it. Spatial's
  neighbour loop then closes into a block and measures 1.002x on an idle machine (7 interleaved rounds, noise 1.016): the loop is
  memory-bound, so the cells have nothing to save. The change is not in the VM.
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

## Receiver fields and method grafts

The general path reads a member of a frame slot's instance through a member temp (`MEMBER_TEMP`: the read's member cache, the value
into a compiler temp, in operand order). The same read over a member-class body's bare field (`current_tcb.state` in a `Scheduler`
method) is `MEMBER_TEMP_THIS`, where it was a stack read and a landing; a statement-position store onto such a base
(`current_tcb.state = v;`) is one `SET_MEMBER_VALUE`, where it was a load, a store and a pop.

- **Member temps fold into their consumer.** When the instruction after one or two member temps consumes their temps (a typed or dynamic
  arithmetic, a typed or dynamic compare-jump, a typed declaration's move, a member store's value), the first member temp becomes
  `MFOLD`: one dispatch reads each field where it lives by the member temp's own rows (an element's object in place, else the member
  cache row: the receiver's class at its method epoch names the slot), lands it in its temp as the member temp would, and runs the
  consumer's own fast rows (a member store runs whole). A field the rows do not serve (no instance, another class or epoch, a getter, a
  host member) runs the member temps and the consumer as compiled; a consumer row that declines continues at the consumer. A jump into the consumer, a cell region's span and the
  debugger see the instructions as compiled, and every temp holds what the member temp leaves in it, so lifetimes and texts are the
  general form's.
- **Method grafts take the frame's receiver.** Besides a local, a graft's receiver is the frame's `this` (a sibling's bare `m()`,
  `this.m()`) or a bare field holding a handle (`current_tcb.m()`), read where it lives. The clone compiles the body's bare names as the
  caller's own operands: on `this` the body's fields are the caller's this-field operands; on a field or a local they are member reads
  of the receiver, which fold; a program-scope name the checker resolved past every member is the caller's this-global operand when the
  caller's class takes no member of it, else the call stays. The guard is the call's own resolution, per receiver class at its method
  epoch: the class resolves the method to the grafted body, each bare field to the declaration the method's class sees, and no
  program-scope name the body reads to a member; a body that reads program-scope names holds only while the caller's frame layout holds
  and its environment is the program's. A graft compiled in a method body calls the body's sibling methods (named bare, proven by the
  checker) on its receiver, where each grafts a level down as it would in the method's own frame; the guard checks each sibling from
  the call site's class as it checks the fields (a method of the receiver's class, public to the caller, no field or getter of the
  name), and a parallel worker takes the pinned call. A graft compiled in a free function keeps such a method's call: its siblings
  graft in the method's frame, and the clone's could not resolve the body's program-scope names. `this` and a local receiver graft a
  body that calls under the same node and loop limits; a field receiver grafts only a body that makes no call (a call could move the
  receiver), and only one that stores into no other object and into no field named like the receiver's field (either could be the
  caller's own field). The checker stamps no frame-receiver graft for a method a derived class overrides: its guard would send most
  receivers to the real call.
- **A bool method in a condition grafts as branches.** `if (r.m())`, `while (r.m())` over a body that is one proven `bool` return
  compiles the return's expression as the condition's own branches behind the guard, and the fallback branches on the real call: no
  result temp, no landing, no truth test. A raise inside the expression names the method's frame and its return's line.

Measured (Release LTCG, interleaved and pinned, against the base at an equal-length path; base2/base is the noise row; 9 rounds for the
apps, 5 for the games, 3 for the kernels):

| program | build / base | base2 / base |
|---|---|---|
| Richards | 0.924 | 1.002 |
| DeltaBlue | 0.994 | 0.999 |
| bytecode | 1.006 | 0.999 |
| spatial | 0.986 | 0.998 |
| field kernel (`cur.a = cur.a + cur.b` and kin in a method) | 0.839 | 0.999 |
| setter kernel (`current.mark_held()`) | 0.778 | 1.020 |
| condition kernel (`if (current.held_or_suspended())`) | 0.660 | 0.999 |
| GLOOM, OSSUARY (serial and 8 workers), JaiDOOM, Gator Quest, crawler, demoreel | 0.993 to 1.009 | 0.987 to 1.007 |
| the kernel suite | 0.97 to 1.01, string2float_dyn 1.022 | 0.987 to 1.004 |

Every hash held. string2float_dyn runs none of the changed code and compiles to the same manifest; an earlier build of the same
change measured it at 0.997. Richards' five methods inlined by hand measured 0.900 on the base, so the grafts reach most of that.

The shape costs that measured:

- **The member store keeps its own shape.** Restructuring `SET_MEMBER_VALUE` around a shared fast row (for the fold to call) made
  bytecode, which runs no member store, 2.8% slower: the dispatch loop inlines the store. The fold runs a member-store consumer whole.
- **A fold reads a field by its member temp's own rows.** Declining an element reference (`Entity& e = entities[i]`) made every
  spatial fold miss onto the member temp: 1.025 against 0.986.

What was left out, measured:

- **Handle results.** DeltaBlue's `input()`/`output()` return handles. Grafting them with the result in a temp (declared by the
  return's table, moved out where the statement reads it, the temp killed at the statement's end) and a graft in a store's target
  made DeltaBlue about 15% slower (single runs): the temp's declaration, the move and the kills cost more than the frame. The form
  that pays substitutes a one-return body's expression for the call in place, behind the guards, with the statement compiled once
  more as written for a guard that fails; it needs the expression proven unable to raise or call, so a raise names the frames the
  call's would.
- **Bodies that call, on the frame's receiver, and overridden methods.** In single runs on a busy machine, grafting bodies that call
  (`current_tcb.run()`, `scheduler.queue(v)`, their receiver held in a temp) cost Richards about 28%, and grafting the stubs of methods a
  derived class overrides (the guard fails, the fallback pays the guard and the landing) about 6% more.

## Levers, by expected value

1. **Method grafts as tight as the method.** Built for the frame's receiver and for bool conditions ("Receiver fields and method
   grafts" above). What remains is DeltaBlue's: `input()`/`output()` return handles, and their tight form substitutes a one-return
   body's expression for the call in place (a statement compiled twice, behind the guards and as written); a result temp measured
   slower than the call.
2. **Builtin calls that run no user code inside cell loops.** Builtin methods carry no effect facts: none records that `contains`
   or `[]` with an int or string key, `size()` or a scalar `push` cannot run user code. With those facts a cell loop could take such a
   call as an operation (a door into the builtin, guarded on the receiver's kind); a boundary operation is not enough, since the hot
   case is a condition (`if (!buckets.contains(key)) continue;`) and boundaries take whole statements only.
3. **Fewer instructions per call.** The receiver load, the probe and the call, the return and its landing are separate dispatches
   through the two-level main switch. `LOAD r; CALL_METHOD 0` and `PROBE_CALLEE; CALL_FROM_SCRATCH 0` have nothing between the
   receiver or callee and the call (internal/call_site_identity.md, "Eliding the probe"); a call or return that continues at its
   target without re-entering the loop head saves the frame reload. internal/call_path.md takes the switch, the probe pair and
   the method rows this way (Richards 0.928, DeltaBlue 0.896); the receiver load stays its own instruction.
4. **Reference containers typed for proofs.** A reference's referent can change kind through another name, so a reference is
   dynamic to the checker; a tentative type from its declaration, which every consumer already guards, would let `a.size()`,
   element reads and builtin calls through `array<T>&` and `map<K, V>&` parameters take the typed paths.

Spatial's remaining gap is memory: an entity is a control block, a 72-byte header and four cells over three lines, and a
neighbour visit touches the slot, the header and the cells. Flat storage for typed-field classes is the lever there.
