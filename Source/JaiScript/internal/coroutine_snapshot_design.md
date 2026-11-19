# Paused coroutines as data

A coroutine handle saves and loads: `to_json` / `to_binary` write a paused coroutine and the matching
loaders rebuild it at the same point, in the same engine or in a fresh engine running the same code. The interpreter and
the VM write the same bytes for the same program state and each loads what the other wrote. The kernel is `detail/coroutine_snapshot.hpp` + `source/implementation/coroutine_snapshot.cpp`;
each backend supplies two functions (`paused_coroutine_frame`, `pause_coroutine_at`).

Suspendable runs (`engine::start`, `script_run`) are a different thing: a run keeps a native stack (a minicoro fiber) and
is not data. Nothing here applies to them.

## What makes it possible

Neither backend keeps a paused coroutine on a native stack. The interpreter keeps a call frame (its locals by the parser's
slot index) and a stack of continuation points, one per construct around the yield; the VM keeps a frame window (the same
slots), an operand stack and its try / iteration / counted-loop stacks. A function's locals live in parser slots on both
backends, a slot is never reused by another declaration, and no cell block ever runs on a fiber frame, so every local of a
paused coroutine is a tagged `script_value` in its parser slot on either backend.

So the saved form describes the frame in the AST's terms, and each backend rebuilds its own representation from that.

## The yield site

`detail::coroutine_sites(parameters, body)` numbers every `yield` of a body in source order (statements in order, an
expression's children in evaluation order; a nested function's or a lambda's yields are its own). For each site it
records, from the AST alone:

- the **path**: the constructs between the body and the yield, outermost first (block and the statement entered, if
  branch, while / for / range-for body, case or default body);
- the **locals in scope**, in declaration order: parameters, the declarations ahead of the path in each block on it, a
  for's initializer, each range-for's variable;
- the **range-for loops** and the **switches** on the path;
- a **refusal**, when a coroutine paused there is not saved (below).

The number is the resume point. The VM compiler takes the numbers from the same function and writes the site's number
into its `op_yield` (operand c); the interpreter notes the `yield_expr` it suspended at and looks its number up at a save.
Nothing about a site is stored on the AST, so the `.jaibite` form is unchanged.

## What a frame holds at a site

Only a yield that is its own statement (`yield e;`, `yield;`) is saved. At such a site nothing of an enclosing
expression is pending, and a frame is exactly:

| State | Saved as |
|---|---|
| a local in scope | its value; a slot declared with a type holds the bare value and is converted to the type on load, an `auto` / `var` slot holds the value beside the declared type it carries when that is an enum or a typed container (`{"as": type, "value": v}`), which a load converts back |
| a local the parser boxed (captured by reference, bound by `T&`) | its value; the load boxes it again |
| `T& r = x` over another local | the index of `x` in the site's locals; the load shares one cell between them |
| `const T& r = make()` | the temporary it owns, in a one-element array |
| a range-for | its range and its place: the element index (array, string) or the entry's key (map); a coroutine range holds its place itself |
| a range-for's variable | by value: its value. By reference over an array, or a structured binding's hidden entry variable over a map: nothing, the load makes the cursor from the loop's range and place |
| a switch | its subject, when a later case's test can observe it (`switch_subject_held`); a name tested against literals holds none |
| a `for` / `while` / `if` / block / try block | nothing: the path says which statement runs next |

A range-for over an lvalue walks that storage (a push in the body lengthens the loop, a by-reference variable writes
through). The save therefore names the storage rather than copying it when the range is the same node as a local's
value (`{"local": i}`), a field of the receiver (`{"field": name}`) or a program variable the range expression names
(`{"global": name}`); the load takes the node from there, so the loop walks the restored local, the restored receiver's
field, or the program's variable of that name. A range nothing else holds (a temporary) is saved by value. A range that
is shared and is none of those is refused.

The kernel reads a frame through `coroutine_frame_view` (the slots in place, each loop's range and place, each switch's
subject) and hands a backend a `coroutine_frame` (the slots by index, the loops, the subjects) to pause a coroutine with.

### The interpreter

Capture reads the saved call frame's locals and walks the continuation points outermost first: a range-for's point holds
its range and index (a map's index is the entry's ordinal, turned into its key), a switch's point holds its subject.

Pause: `call_function` runs with a pending pause. It builds the activation exactly as a first resume does (frame,
environment, member scope), then instead of binding parameters and running the body it places the slots, pushes the
continuation point each construct on the path would have pushed while a yield unwound through it (innermost first, the
`i` / `i + 1` and body-done rules included) with a fresh scope environment for each construct that owns one, and
suspends through the code a yield suspends through. A later `next()` is an ordinary resume.

### The VM

A chunk compiled from a body with yields carries `yield_sites`: per site, the `op_yield`'s ip and the **resume steps**,
what the constructs around the yield keep on the frame's stacks while their bodies run, outermost first: a scope
environment (`op_scope_push`), a try record (the `op_try_push`), an iteration state (the `op_iter_init`), a counted
loop's state (the `op_cfor_prep`), a stack-form switch's `[subject, fallthrough]` pair, and a marker for a branch-form
switch. The compiler pushes and pops a step where it emits the op. A chunk with no yield has no table and compiles as
before; no instruction changes.

Capture checks the fiber stands just past the site's `op_yield`, reads the window in place, each iteration state in
order, and each switch pair's subject from the operand stack (above the window sit only those pairs and the yield's own
null).

Pause: the fiber is opened as a first resume opens it, its stacks are made live, the window is filled, then each step is
re-made by the code that makes it in a run: `acquire_block_env`, `exec_try_push`, `exec_iter_init` over the saved range
(then its index, or `script_map::standing_at(key)`), `counted_for_entry_state`, a pushed pair. The yield's null goes on
top and the frame's ip is set past the `op_yield`. Nothing about a record's layout is duplicated.

A counted loop's state is rebuilt from the loop variable's slot; its fast flag is recomputed, and the fast and general
forms compute the same thing.

## Typed regions, proofs, hostile saves

A save is untrusted input. A load checks everything before it changes the handle: the function exists and its code
matches; the site number is one of the function's yields and one a coroutine is saved at; the locals, loops and switch
subjects match the site's counts; each typed local converts to its declared type through the no-script table a field
load uses (`field_store_convert_no_script`), and an `auto` local whose initializer the checker typed int, float, bool,
char or string (`variable_decl::inferred_type`) converts to that type, else the load fails naming the local; a loop's range is something a loop
walks and its place is inside it; an alias names an earlier local. Proofs are not saved: the code that runs after a load
is the engine's own compiled or checked code, whose typed instructions test their operands' tags and fall back to the
general form on a miss, and a fiber frame never runs a cell block. A value of the wrong kind in an `auto` slot therefore
takes the general path or raises the ordinary error; it cannot be read as a raw cell.

Sizes are the archives' own: fixed-width little-endian, counts bounded by the remaining bytes. A truncated or malformed
archive fails in the reader before any record is looked at.

## Identity

A coroutine is written as a shared handle whose object is its record: `{"$id": N, "$type": "coroutine", "$val": record}`
at its first handle and `{"$id": N}` at every later one (binary: the shared-handle tag and id, then an object record
named `coroutine`). Copies of a handle share one coroutine after a load, as they did. `coroutine` is a keyword, so no
class has the name. The archive walks the record's values on its explicit frame stack: a coroutine a frame holds (a
local, a range) is written the same way, cycles included, and nesting costs memory, not native depth.

The loader makes the handle (empty, failed) when it opens the record, registers it under the id, loads the record's
values, then fills the handle. A handle to the coroutine inside its own record finds it.

### The receiver

A method coroutine's `this` is a pointer in C++ terms, so it is saved as one:

1. the receiver is an instance the archive is writing in place around the coroutine (the object holds its own
   coroutine, directly or through containers): `"owner": k`, the count of such instances out to it. The load takes the
   k-th instance being loaded in place around the record: `this` is that object in the restored graph;
2. otherwise it is written as a shared handle, `"this": {"$id": M, ...}`: the save's `shared_ptr`s to the receiver and
   every coroutine of it share the id, so they share the object after the load. A receiver nothing else in the save
   holds comes along as the coroutine's own object.

The archives give no identity to an instance written by value, so one case has no representation: the receiver is
written by value somewhere that is not around the coroutine (a sibling field). Writing it again as the coroutine's own
object would split one object into two, so the save refuses, whichever of the two the archive reaches first. Holding
the receiver in a `shared_ptr`, or keeping the handle inside the receiver, saves. Lifting the refusal needs an identity
for value instances in the archive format.

### Copy and restore agree

Dev's ruling (2026-11-17): a lambda or coroutine held by its own object rebinds to the copy when the object is copied. The
general rule: copying a value rebinds, inside the copy, every callable that was bound to an instance inside the original.
The value copied is the root whatever it is (an object, an array, a map, a nesting of them); handles to objects outside the
copy stay shared. For an object that holds its own coroutine, a copy behaves as an archive round trip of the object.

- **The handle is held by its own receiver** (`class Brain { int ticks = 0; auto run = think(); coroutine int think()
  {...} }`). `Brain b = a;` and `Brain b = from_binary(to_binary(a));` give the same `b`: a second coroutine paused at the
  same yield whose `this` is `b`. `b.run.next()` changes `b.ticks` and leaves `a` and `a.run` alone.
- **The receiver is outside the copied value** (`auto h = a.think(); auto g = h;`). A copy shares: `g` is `h`. A save and a
  load of `h` alone carries the receiver as the coroutine's own object (`"this": {"$id": M, "$type": "Brain", "$val":
  ...}`), so the loaded coroutine acts on a loaded copy of `a` that only it holds. Here the two differ, and it is the
  handle's own copy, not an object's.
- **The receiver is a sibling** (`array<Npc> duo; duo[0].partner = duo[1].walk();`). `var copy = duo;` gives `copy[0]` a
  coroutine whose `this` is `copy[1]`, in an array a variable holds as in one an object's field holds. The archive gives
  an instance written by value no identity (above), so `to_json(duo)` refuses (`its receiver is saved by value elsewhere in
  the same save`); and `to_json` writes a lambda as null where `to_binary` refuses it, so no save keeps
  `duo[0].other = duo[1].bump`. A copy takes more than a save does; where both succeed they agree.

The kernel is `detail/copy_rebind.hpp` + `source/implementation/copy_rebind.cpp`, and the copy of an instance decides
whether it runs. A copy asks its class one memoized fact, `class_definition::copy_care()` (the copy facts' epoch, so a
definition settles it and a reload renews it):

- **0**: no field anywhere in the class's value tree (its own, a subclass's, a by-value field's class's) can hold a
  function or a coroutine, and no level declares a copy constructor. The copy is `class_instance::copy_members_level`,
  the members' copies, and nothing else: no flag is set, read or tested for it.
- **`copy_care_watch`**: a `var` or `auto` field, a function, a container of either. The members' copies run under a
  watch: `deep_copy_work::watched` counts the watched instances open around the value being copied, and where the copy
  meets a function or a coroutine handle while one is open (`clone_level`'s two cases) it asks once whether that callable is
  bound to an instance (`binds_class_instance`; a function value's node keeps the answer, `script_function_node::
  binds_instance`, so a lambda bound to nothing is one byte compare from then on). The outermost watched instance, when
  its members are done and one was met, runs the kernel over itself and its copy (`class_instance::end_watched_copy`).
  Below `k_inline_copy_depth`, where members are steps on the work list, the end of the watch is a step after them.
- **`copy_care_constructor`**: the user copy constructor builds the copy (`deep_copy_with_care`), and a class that can
  also hold a callable is rebound after it.

A container is watched the same way, so the root of the copy is the value copied and not the first object in it.
`clone_level` opens the watch around the elements of an array of objects, of an untyped array and of a map: one counter
step before the loop and one after it, with nothing per element, and the loops are the ones that copy a container of plain
data. A typed buffer (`array<int>`, `array<string>`, the vector kinds) never reaches the counter. The counter is the whole
test because no fact about a container's type could stand in for it: a map and an untyped array hold values of any kind,
and an array of objects holds whichever subclass was stored, so only the instances' own classes say what they can hold
(`copy_care`). Whichever watched copy is outermost (the root array, map or instance) runs the kernel when a bound
callable was met under it: `script_value::end_watched_copy` over the container and its copy,
`class_instance::end_watched_copy` over an instance and its copy. A by-value argument, return and capture are `clone()`s
of the value passed, so each is its own root.

Every route that copies a class instance reaches `class_instance::deep_copy_level` through `script_value::clone()`: a
declaration or a store into a value slot, by-value arguments and non-elided returns, by-value captures, element and entry
copies of containers, container copies, `cast<T>`, `new T(v)`, a value made from a handle. `=` into an existing object
keeps the object and assigns its members (`implicit_copy_assign`), which watches the members' copies the same way and
rebinds onto the assigned object. The parallel detach refuses a function value and an object holding one (`value is not
value-semantic (function)`), so no worker copy holds a closure to rebind.

The kernel walks the source value and its copy side by side on an explicit work list (fields slot by slot, array
elements by index, map entries in order while their keys match), pairs each instance the source holds by value with its
copy, and notes every function and coroutine the copy holds. Then, per noted site:

- a **lambda** whose scopes up to the first instance scope (its capture scope, the carrier of other captures, the method
  scope of a captured `this`) name a paired instance (`this`, or the object a `[weak_this]` points at) is rebuilt over copies
  of those scopes: the instance's copy for `this` and `weak_this`, a clone for each by-value capture (walked in turn), the
  same cell for each by-reference capture. The function keeps its body and compiled chunk. One source closure gives one
  copy (`made`), so two fields holding one lambda hold one lambda in the copy;
- a **bound method** (`script_callable` with a receiver, `bound_method_fn`) takes the paired receiver;
- a **method coroutine** whose receiver is paired gets a new handle at once (a handle inside its own frame finds it), then
  `coroutine_record_into(..., coroutine_capture::copy)` reads its state exactly as a save does, the record's values are
  cloned and walked (a lambda local bound to the object rebinds), and `copy_coroutine_record` rebuilds the frame through
  `load_coroutine_record` with the receiver's copy: the same checks, the same backend `pause_coroutine_at`. Capture and
  load are one kernel for both purposes; `coroutine_capture` only changes the words of a reason and admits function
  values, which a copy holds and an archive cannot.

A coroutine whose state a save refuses (paused inside an expression or a catch block, running, a local that is a reference
the save cannot name, a local a live lambda shares, a range over unnamed storage, a function redefined after it started)
has no copy. The object's copy is still made, because copies happen in places a script cannot guard (an element store,
a by-value argument), and the copy's handle is `coroutine_handle::mark_uncopied`: never done, raising `Cannot resume
coroutine 'f': it was not copied with its object: <reason>` at `next`, `next(n)` and `advance`, and refusing a save with
the same reason.

A user copy constructor runs as it always does; the pass runs after it, so what it took from its source that is bound to
the source (`f = other.f;`) is bound to the copy once the copy is complete.

## The function

A record names its function (`"function"`: a program function's name, `ns::name`, or `Class::name` for the class that
declares the method; `"kind"`: `function`, `method`, `static`) and carries a code (`"code"`): 16 hex digits, the FNV-1a
hash of the declaration's structural key written as parsed (`ast_writer::as_parsed`: no locations, no slots, no symbol
ids, nothing a run patches). The same source gives the same code on any engine and either backend.

- **Save**: the coroutine's function must be the program's current definition (a current declaration of that name has
  the same code). A coroutine suspended across a hot reload that changed its function keeps running its old body and is
  refused; one whose function was redefined with identical code saves, and loads onto the current declaration.
- **Load**: the engine must define that function with that code; a missing or changed function fails the load naming
  it. The loader finds a program function through its `coroutine_mint` value, a namespace function in the namespace's
  table, a method by minting through the class's own dispatcher (so the handle's scope and access context are what a
  call gives it).
- A coroutine declared inside a function has no name a load could find it by, and is refused.

## Refusals

A save that cannot be exact fails with `Cannot save coroutine 'f': <reason>`; it never writes a guess.

| Reason | Why |
|---|---|
| it is running | its frame is live on the backend's stacks |
| it is paused inside an expression, and only a yield that is its own statement is saved | the operands already evaluated live in backend-specific shapes: the interpreter parks them per expression node in a layout its evaluation lane picks at run time, the VM keeps them on its operand stack and pending-callee list in the layout its fused forms produce. A backend-neutral form needs one contract per expression kind for which values are pending and how each backend rebuilds its own layout |
| it is paused inside a catch block | the caught error is more than the catch variable: the engine's caught-error entry (kind, line, stack) and the exception a bare `throw;` rethrows. The interpreter itself rebuilds that exception from the value on a resume inside a catch, so the two backends hold different things there |
| its body declares a function, a class, an enum or a namespace ahead of the yield | those live in a scope environment of the frame as function values and definitions, which archives do not hold |
| its local 'x' holds a function value / it holds a function value | archives hold no function values; a frame restored without one would run on with null |
| its local 'x' is shared with a function value | a lambda captured the local by reference; the cell would be split from it |
| its local 'x' is a reference the save cannot name | a reference to an element, a field, a map entry or a host property: the save would have to name where the referent lives |
| its local 'x' is a by-reference loop variable over a map | the pair's value half is a reference into the map |
| its range-for over 'x' walks storage the save cannot name | the range is shared with something that is not a local, a field of the receiver or a named program variable |
| its receiver is saved by value elsewhere in the same save | above |
| its function was redefined after it started / its class no longer declares the function it runs / its function is declared inside another function | above |

A copy reads the same table (the words say `was` and `copy`); only the function-value rows do not apply to it.

Each of the first eight is a scope decision, not a limit of the approach: a place encoding (root and path) for
references, operand contracts per expression kind, and a saved caught-error entry would lift them.

## Lifetimes

A loaded coroutine is paused by the same code a yield pauses with, so what it owns is what a coroutine that ran to that
yield owns, held where that one holds it: a drop destroys its locals in the same order (the backends' coroutine state
destructors: a loop's interior before its range, innermost loop first, then the window last slot first). A load runs no
constructor for a frame: the values in it were built by the archive's own load (which constructs class instances as
it always does). A load that fails leaves the handle as it was made, with no function and no frame; the objects already
loaded die as the failed load unwinds.

## Limits

The memory cap charges a loaded coroutine what a started one is charged. A pause runs no script; a `next()` on a loaded
coroutine is an ordinary step under the caller's budget.

## Known edges

- A map loop saved after its current entry was erased: the VM continues after the erased key; the interpreter's resume
  counts entries and skips the next one. The backends already differ there without a save.
- A value keeps only what an archive keeps: a typed container inside an untyped one loads untyped, as it does in any
  archive.
