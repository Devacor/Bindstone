# Closure conversion

A lambda the VM makes inside a function, over that function's frame-slot locals, keeps its captures on its function object
and runs without a capture environment. Behavior is unchanged on both backends; the interpreter and vm-reference keep the
environment form (they are the oracles the converted form is held to).

## The environment form

`vm_backend::exec_closure` builds a capture `environment` whose parent is the lambda's program scope, defines each captured
name in it (`clone_for_capture` for a copy, `share_env_ref` for `[&]`) and stores it as `script_defined_function::closure_env`.
A call installs that env as the frame's parent, and the body reaches a captured name through the environment-addressed forms
of the name ops and their lookup caches.

## The converted form

- **When.** `vm_backend::converted_form_of` judges a closure node once, at its first creation: no `this`, no init-capture, no
  yield, no nested function frame (a nested lambda, function or parallel body), no chained outer names, no reference return,
  parameters in slots `0..p-1`, every captured name a slot of the creating frame (an explicit capture's `outer_slot` or the
  node's `outer_slot_plan`), no captured name the body declares again or reaches as a reference declaration. A creation then
  converts when it is a function frame (not the program frame), not a parallel worker, under the global program scope, with
  every captured slot live, and every `[=]`/`[&]` body name outside the layout resolving to a global or to nothing.
- **Layout.** The captured names in the order the environment form defines them: the default-capture names, then the
  explicit captures, then the remaining outer slots. Destruction runs in reverse of that order, as the environment's does.
- **Values.** `script_defined_function::captures` (one boxed `converted_captures`: the values, the ids, the environment twin)
  holds per name: the shared cell for `[&]`; the copy itself for a name the body only reads (no store, increment, call or
  construction argument, method receiver, reference declaration or by-reference loop over it) when the copy is a scalar or a
  string; else a cell holding the copy, so a write lasts to the next call.
- **Body.** A copy of the lambda's body, made through the AST serializer (`ast_frame_context` remaps the copy's own slots past
  the parameters up by the capture count and gives each captured identifier its capture slot), compiled with one synthetic
  `T&` parameter per capture (`parameter::binds_capture`, slots `p..p+n-1`). Its first instruction, `op_bind_captures`
  copies the closure's values into those slots; loads dereference a cell,
  stores write through it. The function's `body` stays the lambda's own; `backend_body_cache` is the copy's chunk.
- **Entry.** `closure_env` is null, so the closure enters through the capture-free paths; `vm_backend::enter_frame(frame&,
  function)` points `frame::captures` at the function's values (`bind_frame_captures`), read by `op_bind_captures` once the
  window stands.

## Contracts kept

- **Copies and shares.** A copy is `clone_for_capture` of the source at creation and `[&]` shares the source's cell, exactly
  as the environment form defines them.
- **Globals at call time.** A global is never captured: the body reads it by name through the program scope when it runs.
- **Code that runs a body by name.** The parallel builtins (`parallel_transform`, `parallel_reduce`) and region admission of a
  global function value take `script_defined_function::environment_form`: a twin over one environment holding the closure's
  values. The compile-time inliner, the call-site cache, inline holds and frame programs treat a converted closure as
  capturing (they never run its body without its captures, and never pin it past its frame).
- **Release.** `~script_defined_function` releases a chain of closures each holding the last on a work list; the cell-cycle
  collector follows a definition to its captured cells and twin.
- **Debugger.** The locals walk leaves out the capture slots (the environment form lists no captures among a frame's locals).
- **Hot reload.** The converted form lives on the closure node of a chunk: a reloaded unit compiles new nodes; a closure made
  before keeps its function, whose chunk it pins.
- **jaibite.** Nothing persisted changes.

## Cost

A capturing closure drops its environment (288 bytes and its control block) for one box and its values; a captured read is a
slot load instead of an environment lookup.
