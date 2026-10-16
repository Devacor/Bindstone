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
- `convert_script_function` captures the token in the wrapper it returns.

## Emit

`receiver::notify`/`predicate` test `engine_gone()` beside `blocked()` and `invalid()`: `engine_bound_` (a plain bool,
false for C++ receivers, so they pay one byte test) and then `engine_alive_.expired()`. A skipped predicate returns
true, as a blocked one does, so `emit_while` continues. A converted callback called after its engine died throws
`Cannot call a script function after its engine was destroyed` before it touches the engine.

## Release

Skipping is the easy half. A skipped receiver still holds its payload: the text, or a `std::function` holding the
script function value with its captured environment, class instances and nested closures, all of which carry the raw
engine pointer. Releasing them must not run script or read the engine.

That holds because engine teardown is already fail-soft: `tearing_down` is set, the backend is reset so
`get_execution_backend()` is null, and class definitions are detached (`detach_for_teardown`), so a script destructor
reached by a later release finds no backend and does not run. Values the engine no longer owns are released through
their own `strong_ptr` counts and never consult the engine on the way down. The payload is released, never run.

Evidence: an ASAN build (`/fsanitize=address`) of a probe that connects a capturing lambda, a nested closure, a bound
method and a text receiver, then destroys the engine and emits, calls a kept converted callback, and drops the
emitter and the receivers in every order, reports no use-after-free on either backend. The Signals suite tests
`script_receivers_skip_once_their_engine_is_destroyed`,
`script_receivers_held_past_their_engine_and_emitter_release_quietly` and
`a_kept_script_callback_throws_once_its_engine_is_destroyed` hold the contract (they crash without the token).

If a later change makes a release consult the engine (an engine-owned release queue, say), the payload must instead
be leaked when the token is expired: hold it behind a pointer the receiver's destructor does not free once
`engine_gone()`.

## Not covered

A `script_value` the host holds directly is the host's to drop before the engine. Releasing one afterwards is safe for
the same reason as above, but no other operation on it is.
