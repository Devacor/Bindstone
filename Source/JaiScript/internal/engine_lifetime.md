# Engine lifetime and host-held script callbacks

A host can keep script callbacks past the engine that made them: a script receiver on a C++ emitter (the emitter
belongs to a host object that outlives the engine), or a `std::function` from `convert_script_function`. The destroy
order must never crash the host. Script receivers of a dead engine are skipped; nothing a live engine does changes.

## The token

`engine::lifetime_` is a `shared_ptr<const void>` declared ahead of `impl`, so it is alive through the whole teardown
(class detachment, backend reset, every `impl` member) and expires only once the engine is gone.
`engine::lifetime_token()` hands out a `weak_ptr` to it; the free function `engine_lifetime_token(engine*)`
(engine.cpp) is what `signal_decl.hpp` calls, since that header sees only a forward-declared engine.

The token is taken while the engine is live and never derived from a pointer that may dangle:

- a function receiver (`signal_emitter::connect(id, script_value)`) binds the function's engine at connect;
- a text receiver takes the emitter's token, which `signal_emitter::script_engine(eng)` records when the host sets
  the engine, so a text receiver connected after the engine died gets an expired token instead of reading the dead
  engine;
- `receiver::script_engine(eng)` (an emitter re-arming its receivers, an archive load) rebinds a script receiver;
- an observable property's script `on_change` (`property.on_change(fn)`, `on_<name>_change(fn)`, schema_binding.cpp)
  is a C++ receiver wrapping the script function, so it binds the function's engine explicitly after its connect;
- `convert_script_function` captures the token in the wrapper it returns.

## Emit

`receiver::notify`/`predicate` test `engine_gone()` beside `blocked()` and `invalid()`: `engine_bound_` (a plain bool,
false for C++ receivers, so they pay one byte test) and then `engine_alive_.expired()`. A skipped predicate returns
true, as a blocked one does, so `emit_while` continues. A converted callback called after its engine died throws
`Cannot call a script function after its engine was destroyed` before it touches the engine.

## Release

Skipping is the easy half. A receiver holds a payload: the text, or a `std::function` over the script function value
with its captured environment, class instances and nested closures, all of which carry the raw engine pointer. The
engine takes the payload back before it dies, so what the function binds or captures is destroyed while script still
runs, and whatever the host drops afterwards holds nothing of the engine.

What a host object keeps of a script function is a callback's hold: a `held_value` on the second of the engine's two
registry lists (`engine::hold_callback`, `held_value::callback_`). `convert_script_function`'s wrapper holds its
function that way, as do a receiver's saved function and a loaded source receiver's captures (`receiver_save`) and the
observable properties' `on_change` wrappers. `~engine` releases the plain holds first, as ever, and the callbacks'
holds after the program scope's reverse release and the coroutines (`release_held_values(true)`), the last made first;
a moved hold keeps its place (`engine::move_held`), so the order does not depend on how the host moved its
`std::function`s. A receiver on an emitter the program owns never reaches that step: it dies with its emitter in the
program scope's turn. From the release on `engine_quiet(engine*)` is true, so every receiver bound to the engine is
skipped while the rest of the teardown runs destructors that may emit, and a kept `std::function` throws
`Cannot call a script function after its engine was destroyed` (its hold is no longer `valid()`).

The rest of the teardown stays fail-soft for what the engine cannot see: `tearing_down` is set, the backend is reset so
`get_execution_backend()` is null, and class definitions are detached (`detach_for_teardown`), so a script destructor
reached by a later release finds no backend and does not run. Values the engine no longer owns are released through
their own `strong_ptr` counts and never consult the engine on the way down.

Evidence: an ASAN build (`/fsanitize=address`) of a probe that connects a capturing lambda, a nested closure, a bound
method and a text receiver, then destroys the engine and emits, calls a kept converted callback, and drops the
emitter and the receivers in every order, reports no use-after-free on either backend. The Signals suite tests
`script_receivers_skip_once_their_engine_is_destroyed`,
`script_receivers_held_past_their_engine_and_emitter_release_quietly` and
`a_kept_script_callback_throws_once_its_engine_is_destroyed`, and Class Builder
`observable_property_script_receivers_skip_once_their_engine_is_destroyed`, hold the contract (they crash without the
token).

A new place that wraps a script function in a C++ receiver must bind it the same way, and hold the function through
`engine_access::hold_callback`: the emit can only skip what it knows belongs to an engine, and the teardown can only
release what is on its list.

If a later change makes a release consult the engine (an engine-owned release queue, say), the payload must instead
be leaked when the token is expired: hold it behind a pointer the receiver's destructor does not free once
`engine_gone()`.

## Coroutine handles

A host owns a coroutine through its `shared_ptr<coroutine_handle>`, which can outlive the engine, and a suspended
coroutine's backend state reaches the engine as it dies (`release_suspended_values`). So the engine keeps every live
handle on an intrusive list (`engine::register_coroutine`, the handle's constructor and destructor; no lock: a worker
cannot mint or drop a handle, and a host thread drops one only while the engine is idle). `~engine` calls
`retire_coroutines()` after the program scope's reverse release and before the backend goes: each handle still alive,
the last made first, gives up its backend state, receiver, arguments and values while script destructors still run, and
names no engine afterwards. A retired handle is `failed` (`completed` stays), so every resume reaches
`resume_refusal`, which answers `Value outlived its engine.` before the engine argument is read. Retiring also ends a
coroutine held by a field of its own receiver, so that cycle's destructors run at teardown.

A program-scope function value that owns an object dies in the reverse release with the objects, arrays and maps
(`environment::dies_at_teardown`): a lambda (its captures) and a bound method (its receiver). A declared function stays
for the destructors that call it. No VM call-site cache pins a bound method (`park_callee`): its receiver dies with the
last value that binds it.

## Instances still alive

After the program scope, the coroutines and the callbacks' holds, an instance still alive is one a reference cycle
keeps or one a plain `script_value` of the host's names. Every class keeps its live instances on its registry list, so
`engine::destroy_surviving_instances` pins them all and `class_definition::run_surviving_destructors` runs the
destructor bodies, each once, with no member taken:

- The order is the last made first, by the birth each destructor class's instance takes when it is allocated
  (`object_block::birth`, 8 bytes beside the definition pin; an instance a parallel region makes takes it at the join,
  one armed by a re-class has none and goes last).
- One instance goes as its death would: each level's body under `dispatch_level_scope`, then the instances that level's
  members alone hold, the members last declared first, each through the arrays and maps only it holds in the order their
  release would take (`solely_held_instances`), then the bases. "Alone" is a use count of one beside the teardown's own pin.
- An instance one member alone holds is not a start: the walk climbs `sole_holder` to the instance no member alone holds,
  and a ring of sole holders starts at its last made member.

Nothing is released while script can run, so no destructor meets an object with members taken: the typed slots a
destructor reads through another survivor hold what their declarations say. The pins outlive the backend and the
detach; `release_surviving_instances` then moves every member out of every pinned instance (no instance dies inside
another's release, so nothing recurses) and drops the pins, which frees the cycles. An instance a teardown destructor
makes and leaves in a survivor is released there without its destructor, as a held value made then is.

## Not covered

A `script_value` the host holds directly is the host's to drop before the engine. Releasing one afterwards is safe for
the same reason as above, but no other operation on it is. A class whose definition the engine no longer lists (one a
redeclaration replaced while instances of the old one lived on) is not walked: its instances are released as before.
