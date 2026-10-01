# The reference model (cells era, 2026-07)

What a `jaiscript` reference IS, after stage C deleted mode 1. One page; the
load-bearing rules live in `invariants.md` §3, the runtime shapes in
`source/tests/vm/vm_backend_tests.cpp` (`ref_*` / `escape_*`).

## A reference is an owner-pinned handle

`script_value` variant index 10 holds `strong_ptr<reference_holder>` (value.hpp). The
holder is in exactly ONE of seven modes, each of which OWNS or PINS what it refers to —
there is no raw-pointer mode and no "is my target still alive?" question:

| mode | pin | resolution per touch |
|------|-----|----------------------|
| CELL | holder owns the value in 32B inline storage (Lua upvalue; one `make_strong` alloc, memory_cap-charged) | `cell()` |
| ELEMENT | `strong_ptr` to the owning vector + index | recompute address, bounds-check (shrink ⇒ "removed array element"); on a typed node the boxed view in the holder's scratch is rebuilt only when the slot's handle changed (`script_array::view_current`, obj/str kinds). A by-ref range-for in a function body re-points ONE holder per iteration when nothing else shares it (`element_cursor_holder`); a retained alias keeps its element |
| FIELD | `shared_ptr` to the owning instance + field id | `find_field_value(id)` (removed field errors; hot-reload safe) |
| MAP-ENTRY | `strong_ptr` to the owning map + key in the inline storage | `find(key)` (erased entry errors) |
| CPP-MEMBER | the receiver handle + the C++ object (`_cpp_object`, an extended object's weak handle locked, a raw holder's data or anchor) | the inline storage holds a `cpp_bound` box aimed at the C++ member itself: reads decode it live, writes are the box's typed memory store |
| PROPERTY | the receiver handle + the property's field id | the getter, re-run into the inline scratch (`materialize_property`); assignment re-resolves and runs the setter - for a read-only property that setter IS the rejection, `Cannot assign to 'x': it is read-only`, exactly what `obj.name = v` raises |
| COMPONENT | `strong_ptr` to the holder of the math value's storage (any of the modes above) + the component index | the component read into the inline scratch (`materialize_component`); assignment is the component store `v.x = value` makes (`store_component`: the component's narrowing and texts) into the value the base names now, then the base commits its own scratch (a typed vector element, a PROPERTY's setter) |

CPP-MEMBER / PROPERTY are the two shapes a `dynamic_binder` `.property()` binds to
(`detail/ref_lvalue.hpp` `bind_cpp_property_ref`, one kernel for both backends). A
member-pointer property whose type the box decodes (`script_value::cpp_bound_encoding`:
the integer widths, bool, float/double, `std::string`) is CPP-MEMBER — the binder records
a member-address accessor beside the getter/setter
(`class_definition::register_property_member`) — unless a script `_get_`/`_set_` override
sits in the chain; lambda / member-function accessor pairs, class-, container- and
`script_value`-typed member pointers, and read-only properties are PROPERTY. A same-named
script field shadows the property for the reference exactly as it does for reads and
stores (FIELD mint). Both modes re-resolve the receiver's class per touch (a reload that
adds a shadowing script field re-points a PROPERTY reference at that field; a retired
script layer reports at bind and on PROPERTY touches, while a CPP-MEMBER pin keeps the
object alive). `resolve_target()` answers null for both — a missed immediate-use consumer
fails loudly instead of detaching the box or writing the scratch.

Copying a reference copies the handle (aliases share the holder). Binding never
allocates except the first boxing of a variable.

## Where references come from

- **Variables** (params, locals, globals) box into a CELL: escape-marked declarations
  (`parser.cpp ref_escape_marker`: bare-ident call args, `auto& x = y` sources, by-ref
  captures, ref-return operands) box at declaration; anything the marker can't see
  boxes ON DEMAND at the first bind through the one box-in-place + share kernel pair
  (`share_env_ref` / `share_boxed_env_storage`). The variable's storage BECOMES the
  handle; reads/stores handle the boxed form transparently, and direct assignment to
  the boxed name writes the cell inner with the variable's typed-store enforcement.
- **`arr[i]` / `obj.field` / chains** resolve to ELEMENT/FIELD refs via the shared
  `detail/ref_lvalue.hpp` resolver (ref args, ref returns) or the subscript-lvalue
  paths. **`m[k]`** lvalues mint MAP-ENTRY refs.
- **Math components** (`v.x`, `pos.y`, `a[i].z`, `obj.pos.x`, `S::v.x`, `col.g`; Dev ruling 2026-09-30) mint a
  COMPONENT ref over the base's own reference (an identifier boxes its variable, the rest resolve as above) wherever
  a reference binds: ref args, `var&`/`T&` declarations, `cast<T&>`, ref returns
  (`detail/math_component_store.hpp` `component_reference`, both backends). A vector local whose component is bound
  that way stays boxed (the aggregate lowering's escape rule).
- **C++ properties** (`obj.prop`, `this.prop`, the bare `prop` inside a hybrid's methods and
  lambdas, chains ending in one, `[&]` captures of one) mint CPP-MEMBER / PROPERTY refs on
  every receiver kind: hybrid layers, script-constructed C++ objects, plain wrappers of
  extended objects, raw non-owning handles (`T&` returns, `add_global_ref` objects). A
  chain may pass THROUGH a property (`f(this.child.x)`) when its getter is native.

## What binds (decls, ref params, ref returns)

Binding is two-tiered; both tiers land in the same four holder modes:

- **Tier 1 (structural, bind-time)**: bare identifiers (share/box the storage) and
  chains of plain member steps with identifier/int-literal subscript indices resolve
  from the AST via `detail::resolve_ref_lvalue` — pure pointer chasing, never runs
  script (the bind point sits mid-call-sequence where re-entering script is illegal).
  Chain resolution happens AFTER all args evaluate (pinned:
  `tier1_bind_time_ordering_pinned`).
- **Tier 2 (evaluated lvalues)**: everything else that EVALUATES to a reference binds
  by sharing the evaluated holder — a reference VALUE is an lvalue. Subscript and map
  reads over lvalue bases mint owner-pinned refs with full index generality, so
  computed indices (`grid[y+1][x]`), member-expr indices (`arr[o.idx]`), map keys
  (`m["k"]`, `m[k]`), and typed-array elements all bind; index expressions run exactly
  once, in argument order. `var&`/`auto&` declarations additionally resolve
  member-FINAL initializers (`auto& p = G.player`) through the Tier-1 kernel, since
  field reads evaluate to copies.

What stays a non-lvalue: rvalues (arithmetic, calls, literals), string subscripts
(chars are values), map KEYS (`kv.first` — ordering invariant), script fields with a
user `_get_`/`_set_` anywhere in their chain, member METHOD names, missing map keys
(reads never insert), member-final chains over COMPUTED subscripts (`objs[i+1].v` —
bind a `var&` row first), and any property bind inside a parallel region (workers reach
neither accessors nor raw member memory).

A script `string&` parameter (lambdas and host calls of script functions included) binds a
string argument that is no lvalue to a writable temporary CELL: a script string shares its
copy-on-write node, a C++-bound one detaches from native storage, writes stay private, and an
escaping capture or a reference return pins the cell (`detail::make_reference_argument_temporary`).
Another `T&` parameter takes a temporary only when the callee never writes through it
(`detail/ref_param_writes.hpp`); a reference declaration never does.
- **Ref returns** (`int& f()`, `-> auto&`): the return operand binds like a ref arg
  (ident ⇒ share/box; chain ⇒ resolver; call ⇒ pass-through) and the epilogue hands
  the HANDLE out via `detail::ref_return_pass_through` — non-lvalues and referent
  base-type mismatches reject there. Coroutines cannot return references (parse
  error). Value-returning functions still FLATTEN references at the epilogue.

## Escape is legal

The pin is the lifetime: a reference returned, stored in a global, captured by `[&]`,
or held across `yield`s and separate `execute()`s keeps its target alive. Frames and
scope envs dying underneath a reference is a non-event. Engine teardown with live
cells is clean (cells are reachable only through engine-owned storage or host-held
`script_value`s). "Reference target environment has been destroyed" no longer exists.

Deliberate boundaries:
- Plain assignment (`x = r`, `m[k] = r`) stores a VALUE (deref + detach/clone rules) —
  script code cannot smuggle a raw reference into a container by assignment; aliasing
  is opt-in at declarations, params, captures, and returns.
- `[&]` capture of a call-frame SLOT local snapshots by value (pinned:
  `ref_escape_via_lambda_after_frame_death`); env-stored variables capture as live
  cells. Unifying these is a possible stage D.

## Constraint capture

ELEMENT/FIELD/MAP-ENTRY refs carry their container's element / declared field / map
value type as `container_element_type`; stores through the ref enforce it with the
same text as the direct store (`ref_store_through`, route-independence). CELL refs of
plain locals are unconstrained (plain-local parity); the typed enforcement for a boxed
NAME rides the store path, not the holder. CPP-MEMBER refs are constrained by the box:
a store converts with the bound-variable rules (`as<script_int>()` etc. — a mistyped
store raises the same conversion text the direct `obj.prop = v` setter raises and
leaves the member alone; narrow integer widths truncate like an `add_global_ref` int32
does, where the direct setter range-checks). PROPERTY refs are constrained by the
setter alone.

## The consumption contract

ALL consumption goes through `deref()` / `assign_through()` (re-resolve per touch,
chained refs collapse) or, for the immediate-use assignment twins,
`reference_holder::resolve_target()` (single-level, mode-based). Never cache a
`script_value*` obtained from a reference across anything that can touch the owner —
push/erase/hot-reload between resolve and use is exactly what the modes exist to
survive. New consumers use these three entry points, nothing else. In-place mutators
(`++`/`--` on an identifier, the string mutators) that write the deref'd value must
`commit_scratch()` when `scratch_ref_holder()` answers (typed elements and PROPERTY refs
hand back a scratch); compound assignment routes those refs through
`ref_compound_store_constrained`.

## Strings are values behind shared nodes

A string `script_value` holds one `strong_ptr<script_string>` node and every copy
boundary shares it (O(1) copies; `clone()` / `clone_for_assignment` stay shallow for
strings). Value semantics hold because every in-place string write - the 17 mutators
(`to_upper`, `trim`, `replace_all`, ...), `+=` on a variable or bare field, a host
`std::string&` - goes through ONE gate, `script_value::unchecked_as_string_ref()`: a node
any other holder shares is replaced by a private copy first (copy-on-write, memory_cap
charged deferred), so the write lands in the storage the script named and nowhere else.
Mutators therefore bind their receiver LVALUE like a `string&` parameter
(`detail/string_mutator_receiver.hpp`: an identifier's cell, a subscript read's reference,
a field / C++ property through the ref-lvalue kernel, or the root of a mutator chain -
`t.trim().to_upper()` keeps writing to `t`); a call result or computed property is a
temporary and its mutation stays in the temporary. Map keys follow from this: `kv.first`
(by value or by reference), `keys()`, `to_array()`, predicate arguments and the variable a
key was inserted through all share the key's node - no per-iteration copy - and the first
write through any of them detaches, so the key itself is never written; the static checker
additionally rejects every mutation spelling on `kv.first` it can see (`cannot mutate
'kv.first': map keys are read-only`, `docs/static_checking.md` Map keys). Bound
(`cpp_bound`) strings are the live C++ member and keep write-through
(`docs/invariants.md` 11).
