# The reference model (cells era, 2026-07)

What a `jaiscript` reference IS, after stage C deleted mode 1. One page; the
load-bearing rules live in `invariants.md` §3, the runtime shapes in
`source/tests/vm/vm_backend_tests.cpp` (`ref_*` / `escape_*`).

## A reference is an owner-pinned handle

`script_value` variant index 10 holds `strong_ptr<reference_holder>` (value.hpp). The
holder is in exactly ONE of four modes, each of which OWNS or PINS what it refers to —
there is no raw-pointer mode and no "is my target still alive?" question:

| mode | pin | resolution per touch |
|------|-----|----------------------|
| CELL | holder owns the value in 32B inline storage (Lua upvalue; one `make_strong` alloc, memory_cap-charged) | `cell()` |
| ELEMENT | `strong_ptr` to the owning vector + index | recompute address, bounds-check (shrink ⇒ "removed array element") |
| FIELD | `shared_ptr` to the owning instance + field id | `find_field_value(id)` (removed field errors; hot-reload safe) |
| MAP-ENTRY | `strong_ptr` to the owning map + key in the inline storage | `find(key)` (erased entry errors) |

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
(chars are values), map KEYS (`kv.first` — ordering invariant), computed properties
and C++-backed members whose accessors return copies, missing map keys (reads never
insert), and member-final chains over COMPUTED subscripts (`objs[i+1].v` — bind a
`var&` row first).
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
NAME rides the store path, not the holder.

## The consumption contract

ALL consumption goes through `deref()` / `assign_through()` (re-resolve per touch,
chained refs collapse) or, for the immediate-use assignment twins,
`reference_holder::resolve_target()` (single-level, mode-based). Never cache a
`script_value*` obtained from a reference across anything that can touch the owner —
push/erase/hot-reload between resolve and use is exactly what the modes exist to
survive. New consumers use these three entry points, nothing else.
