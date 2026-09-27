# Load-bearing invariants (VM-perf era, 2026-07)

Things that are easy to break silently. Each has a source anchor; if you're about to violate
one, read the anchor first. Background: `thin_value_spec.md` / `thin_value_rebaseline.md`.

## 1. script_value layout gates

`value.hpp:1858-1860`: `static_assert(sizeof(script_value) == 32 && alignof(script_value) == 8)`
plus nothrow-move; `value.hpp:1671-1672` additionally pins every variant alternative as
nothrow-move (`valueless_by_exception` + the noexcept move ctor depend on it). Layout is
8B `type_info_ptr` + 8B `engine*` + 16B variant (8B max payload + index). The `engine_` and
`type_info_` members are **the language**, not fat: zero-statics/multi-engine and the
var/auto/int type ladder live in them — never evict either to buy payload bytes. A new variant
alternative must be pointer-sized and nothrow-move or the asserts fire.

## 2. strong_ptr single-pointer rules

`strong_ptr.hpp`: the handle is ONE object pointer; the control block is *derived* at a negative
offset (`cb_from_object`, via `cb_storage_offset = offsetof(control_block<T>, storage)`
— never hardcode the offset: over-aligned T and the Debug-only magic member both move it).
Sound only because `make_strong` and `detail::adopt_pooled` are the only producers of a non-null
handle: the raw-`T*` ctor is deleted and the cross-type converting ctors are deleted by design
(a Derived→Base conversion would adjust the pointer off the storage head AND slice the
static-type destroy). `adopt_pooled` exists solely for the per-engine `reference_holder` block
pool (`script_value::reference_holder_pool`, value.cpp): pooled blocks derive `control_block<T>`
without moving `storage`'s offset (static_assert-gated) and reset counts + Debug magic on reuse,
so the derivation invariant is preserved. `ptr_` MUST always be exactly `control_block<T>::storage`'s head.
Enforced: negative `is_constructible` static_asserts (strong_ptr_tests.cpp:224-225),
`sizeof == sizeof(void*)` gates (strong_ptr.hpp:412-414), and a Debug canary
`cb_magic_live = 0x5B0CB10C` (:22) asserted at every derivation (:106) and scrambled to
`0x0DEADCB0` on dealloc — a canary assert means a dead/foreign block, i.e. someone minted a
handle outside make_strong or used it after free.

## 2b. Frame slot pointers survive stack growth via the ONE chokepoint

Flat-stack era (stage 2): vm frame slots live in value-stack WINDOWS
(`frame::window_base/window_live`, slot k at `stack_[window_base + k]`), and the stack is a
flat vector that reallocates on growth. The VM caches raw pointers into slots across
dispatch iterations (`counted_for_state.var/end_ptr/step_ptr`, `exec_cfor_prep` — end/step
may point INSIDE a slot's int payload). Sound only because ALL growth funnels through
`value_stack::grow_push` (vm_backend.hpp), which calls `rebase_window_pointers` — a
byte-offset rebase of exactly that enumerated pointer set — before the push. THE RULE:
never cache a raw pointer into stack/window storage across anything that can push, unless
you add it to `rebase_window_pointers` in the same commit; references never qualify
(owner-pinned holders, §3). Windows are created FULL (null placeholders up to `local_count`
behind `window_live`), so operand temps always start above the whole window. Fiber frames
(`vm_coroutine_state::locals`) still use `call_frame` vectors and keep the pre-stage-2 rule:
reserve the full `local_count` up front, never reallocate mid-frame (the 2026-07 demoreel
finding-2 failure mode was silent wrong loop counts in Release). The interpreter's
`call_stack_` frames keep the old rule unchanged.

## 3. Reference cells (mode 1 is DELETED, 2026-07 stage C)

The old ref-anchor rule is dead machinery: `reference_holder` no longer has a raw
`target` pointer or a `weak_ptr<environment> sourceEnv` — a reference NEVER holds a
raw address + lifetime guess. Exactly six owner-pinned holder modes exist
(`reference_holder`, value.hpp; long-form: docs/reference_model.md): CELL (the holder
OWNS the value in inline storage — Lua upvalue), vector element (container+index
re-resolve), instance field (owner+id re-resolve), map entry (pinned map + key
re-resolve), CPP-MEMBER (receiver + C++ object pinned; the inline storage is a
cpp_bound box aimed at the member) and PROPERTY (receiver pinned; getter into the
inline scratch per touch, setter on assign-through). Rules:

- Holder blocks come from the per-engine pool (`script_value::acquire_reference_holder`,
  value.cpp — one pop + placement-new per mint, memory_cap-charged for cells). The pool is
  single-threaded: parallel workers DO mint cell references inside their bodies, so acquire
  falls back to plain self-deleting `make_strong` blocks while a region is active (blocks
  self-describe their release via `dealloc_fn`, so both kinds coexist). Cells are minted
  ONLY by `make_cell_reference`; the boxed value is never itself a reference.
- Subscript reads whose consumer provably discards identity skip the holder mint entirely
  (`detail/transient_read.hpp`, parse-time classifier consumed by BOTH backends; runtime
  gate `has_custom_binary_ops_`). Any NEW lvalue-consuming syntax must keep its operand
  positions unmarked there — see the checklist comment at the classifier. Escape-marked
  declarations (`parser.cpp` ref_escape_marker) box at decl/bind; unmarked storage
  boxes ON DEMAND at the first ref bind through the ONE box-in-place + share kernel
  pair (`vm_backend::share_env_ref` / interpreter `share_boxed_env_storage`; ref-param
  binds ride it via `bind_reference_to_storage`). The vm kernel must demote active
  counted-for fast states (they cache raw payload pointers).
- Escape is LEGAL: a reference returned (incl. typed ref returns `int& f()`), stored,
  captured, or held across yields/executes keeps its target alive through the holder.
  Never reintroduce a raw-pointer + weak-env reference to frame slots or env storage;
  "Reference target environment has been destroyed" must never come back as an error.
- Every write path resolves by MODE, never by cached address: deref()/assign_through()
  re-resolve per touch; the immediate-use assignment twins go through
  `reference_holder::resolve_target()` (value.cpp). A new consumer must do one or the
  other — a stored `script_value*` into a container/frame is the bug class this
  section exists to prevent.
- Reference binding is STATELESS: the call site travels as an argument
  (vm `bind_parameters(site, caller_locals, caller_code)`; interpreter
  `call_site_context`). Opaque `std::function` boundaries hand it over via a single
  consumed-once pending pointer (`pending_site_ctx_` / `pending_call_ctx_`), shelved
  across external invocations by `external_call_guard`. Never add a second stateful
  metadata channel.
- Direct assignment to an escape-boxed variable writes the cell WITH the variable's
  typed-store enforcement (`names_value_decl` / `store_flag_ref_alias` pick the
  semantics); writes through ref aliases keep the unconstrained store-through rules.
- Ref returns pass the HANDLE out through the ONE epilogue kernel
  (`detail::ref_return_pass_through`). By-value returns materialize at the return site
  through `detail/return_conv.hpp` (`classify_return_operand` / `materialize_return_value`
  / `return_own_slot_payload`): the callee's own slot moves out, every other lvalue read
  and every reference result deep-copies through `clone_for_assignment`, a temporary
  moves; the copy runs BEFORE the typed conversion so a converted value is never copied
  twice. One site per backend (interpreter `visit_return_stmt_t`; vm `materialize_return`
  from `exec_return_ident`, `exec_return_convert`, `return_from_script_frame` and the
  `op_return` entry branch, the reference flatten in `return_with_result` /
  `convert_return_value` / `entry_return_finish`). Never add a return door that hands a
  callee-nameable handle out; the top-level program's `return` is the one deliberate exception.

## 4. S8 bound-operand prologue (TYPEID_CPP_BOUND = 14)

Since the thin-value fold, cpp-bound values report raw storage index 14, not their semantic
shadow index. Any NEW binary/unary/compound handler that dispatches on `raw_storage_index()`
MUST normalize bound operands first via `bound_decoded_temp()` (value.hpp:350) — copy the
prologue shape from an existing handler (e.g. `handle_add`, interpreter_dispatch.cpp:47-49, and
its VM twin). The failure mode is SILENT: index 14 falls into an erroring `type_mismatch`
default, or worse a wrong-constant default (`same_as` → false). Raw-index *fast-path guards*
(`idx == TYPEID_INT`) are safe as-is — they exclude bound values and fall through.

## 5. VM run_dispatch MSVC Debug frame ceiling

`run_dispatch` (vm_backend.cpp:7101) executes calls in-loop, so its native frame sits on the
`JAI_MAX_CALL_DEPTH` recursion path in Debug. Each `VM_TRY_OP` case adds a temp to that frame;
the ceiling was reached, so ops added since go through the grouped `exec_extended` dispatch
(:7203-7226) with `VM_TRY_OP_SHARED` (:7060), which reuses one function-scope temp. Follow that
pattern for new opcodes — do NOT add fresh `VM_TRY_OP` cases to the main switch. Canary:
`recursion_depth_error` (vm_backend_tests.cpp:246) must fail *catchably*; if the Debug frame
grows too big, that test instead kills the exe with a native stack overflow (0xC00000FD,
surfaces in the shell as exit code 127) mid-suite.

## 6. Backend parity is byte-identical, including error text

Interpreter and VM must produce identical results AND identical error codes/messages; the suite
runs green on both (`--backend=vm`). Preferred mechanism: shared kernels in `detail/` used
verbatim by both backends — `detail/ref_lvalue.hpp` ("shared VERBATIM ... parity by
construction") is the model, `detail/builtin_methods.hpp` the older one. Legacy mechanism:
byte-parallel twins (`is_truthy` in interpreter.hpp:872 vs vm_backend.cpp:965; the nine
arithmetic/ordering handlers in interpreter_dispatch.cpp vs vm_backend.cpp) — if you touch one
twin, touch the other in the same commit. New shared logic goes in `detail/`; don't grow the
twin population.

## 7. Benchmark methodology

The Foundry benchmark harness (Release BENCHMARKS config) reports **integer µs/iteration with
±50% run-to-run variance** (Source/JaiScript/CLAUDE.md "Benchmarks"). Compare min-of-3 runs,
and never claim a sub-µs win from it — anything under ~1 µs needs a dedicated ns micro-bench
(see `execution_mode_metrics.md` for the methodology that resolved the 0.4 µs regime).

## 8. Typed array representation follows the declared type (2026-09-02)

- `array<int>` / `array<float>` / `array<bool>` / `array<char>` / `array<string>` are raw
  buffers by construction (`script_array::kind_t` i64/f64/b8/c8/str) and `array<UserType>`
  is a buffer of `obj_slot{handle, tag}` (kind obj, exact-class homogeneous, the slot tag
  reproduces the boxed value on read); `array<var>` (and a literal with no spelled or
  inferable element type) is the ONLY `script_value`-vector form.
  A uniform `auto` literal infers its element type. There is no flag and no Debug/Release
  difference; converting between forms is an explicit assignment, nothing demotes silently.
- The tag is the node kind: `as_array()` / `checked_as_array()` are the `array<var>` view and
  return ONE consistent error on a typed node (`typed_array_vector_view_message`). Readers use
  `script_array::for_each` / `get`; writers use `set` / `push`; `bound_array<T>&` mutates the
  buffer in place and binds only element types that fit the kind.
- A raw fast lane tests the KIND it was written for (`kind() == i64 || kind() == f64`), never
  `is_typed()` — byte and string kinds are typed too and must take the enforcing `get`/`set`
  path (fuzz seed 129 stored an int into a string buffer through an `is_typed()` lane).
- A `checked_result` message is a NON-OWNING view for literals: dynamic text (a caught
  exception's `what()`, a built string) is interned through the engine's symbolizer and the
  result holds a view of that stable copy (`intern_exception_text`), never the source buffer.
- Parallel workers never borrow string handles raw: the "all primitive" short-circuit excludes
  the str kind, and a typed primitive/string input is read through a boxed snapshot (stage-3
  raw reads pending); an object-kind input lends its slots (`parallel_worker_slot::typed_input`)
  to both forms.
- Neither form demotes an object-kind source: exclusivity comes from the ONE element proof
  (`prove_elements`, parallel_transform.cpp) - the declared class (`declared_class_exclusive`)
  plus `script_array::live_elem_refs`, which counts direct element references and every
  reference minted through an element (`reference_holder::elem_root`, pinned by
  `resolve_ref_lvalue`), else the per-element normalizing walk. A new reference factory that
  can reach an element's subtree must pin the root array the same way, or the fork's
  reference-free proof is unsound. `parallel_for` narrows the walk to the members the body
  names; `parallel_transform` reaches every element WHOLE because its by-value parameter
  binding clones the entire element on the worker (a shallow string copy bumps the node's
  count), so a shared string anywhere below an element must detach at the barrier. A
  shared_ptr-tagged instance with a live alias (holder or instance count above one) is
  rejected, never cloned apart: the clone would diverge from the serial loop and the alias
  defeats the reference-free premise. Worker-side instance copies register on the shared
  class definition under `engine::parallel_instance_registry_lock()` (region-time only).

## 9. Inline-cache entries are keyed by site, never by symbol (2026-09-04)

`env_lookup_cache` entries (`chunk.hpp`) carry a class pointer, a field slot and a serial but
NO symbol. The this-field IC (`this_field_ic_probe` / `this_field_ic_arm`, the store twin) is
therefore sound only at a cache slot that exactly one symbol ever resolves through: an op's
own `ip * 3` (+1 for its second operand) when that op resolves that symbol and nothing else
arms the same index. A fused store's `ip * 3` is armed for the STORE TARGET; its operand
resolvers must not probe a read IC at that index for a different symbol - the entry answers
the target's cell and the operand reads the wrong field. The value resolvers survive because
they re-stamp class and slot on every miss; a pointer resolver that trusts the entry does not.
Adding an IC probe to a resolver requires either an exclusively owned slot (the string-builtin
receiver gate reads the receiver LOAD's own slot, recorded at compile time as
`call_site::receiver_cache_slot`) or a symbol check on the entry. Pinned by
`Strong Types :: bare_field_store_then_read_in_loops_battery` (the Foundry field-write shape,
which the whole suite otherwise missed).

## 10. Lambda capture sources resolve in the runtime's lookup order (2026-09-04)

The SOURCE a captured name binds to is decided by ONE kernel, `detail/capture_ladder.hpp`
(`resolve_capture_source`), used verbatim by `visit_lambda_expr` and `exec_closure`: the
enclosing frame's slot (locals and parameters, incl. the parser's `chained_outer_slots`),
then the member scope when active (own env scopes below the floor, then fields / static
fields / a hybrid's C++ property; a method name shadows and is not a cell), then the
environment chain from the creating env. A same-named global never wins over a slot or a
member. The capture KIND is applied by the caller afterwards ([&]/[&x] cell-share the
source, [=]/[x]/bare [] snapshot it); an explicit [x]/[&x] takes variables only, so a
member keeps the `Cannot capture undefined variable` text with or without a global. The
capture env parents the GLOBAL env, so "already captured" tests must read its OWN storage
(`get_own_value_ptr`), never `contains` - a `contains` walk reaches the global and the slot
is silently skipped (the bug class that let `[&]` box a global instead of the local).
Pinned by `Lambda Captures :: *_over_same_named_global` and the method-shadowing rows.

## 11. The string write gate (2026-09-04)

String copies share ONE `strong_ptr<script_string>` node (`clone()` and
`clone_for_assignment` are shallow for strings by design - O(1) copies), so an in-place
write through a shared node would reach every holder: `string b = a; b.to_upper()` used
to uppercase `a`, and `for (auto kv : m) kv.first.to_upper()` rewrote the MAP KEY under
the ordering invariant. Value semantics are restored at exactly one place:
`script_value::unchecked_as_string_ref()` (value.hpp) is the ONLY mutable string
accessor, and it unshares first - a node with `use_count() > 1` is replaced by a private
copy (`unshare_string_node`, memory_cap charged deferred) before the reference is handed
out. Bound (`cpp_bound`) strings return the live C++ member (write-through) and skip the
gate. Rules:

- Every in-place string write goes through the gate: the 17 mutators in the shared
  registry (`k_string_mutator_method_names`, `detail/builtin_methods.hpp` - ONE list feeds
  the registry's id set and index table), the `+=` in-place sites in both backends, and
  the host `as_string_ref()` / `std::string&` door. Never read the node handle mutably by
  another route.
- `self` at a mutator call MUST be the storage the script named, never a transient copy:
  under the gate a copy's write is lost, not aliased. Identifier receivers resolve to
  their cell on each backend's fast path (the vm drops its LOADed receiver copy and the
  interpreter its pre-args snapshot before the call - a live copy only makes the gate copy
  the node); every other receiver shape binds through the shared kernel
  `detail/string_mutator_receiver.hpp` (used verbatim by `interpreter::member_read_on` and
  `vm_backend::exec_call_method`'s builtin-direct branch): a popped reference (subscript
  read, ref alias) derefs to its storage, a member-final receiver (`obj.s`, `this.s`,
  `kv.first`) resolves through `resolve_ref_lvalue` exactly like `f(obj.s)` binds a
  `string&` parameter, and a chain (`t.trim().to_upper()`) redirects to its root when the
  root's node IS the node the inner call handed back (identifier roots ride
  `call_site::chain_root_symbol/slot` in the vm, `string_receiver_cell` in the
  interpreter; ref-bindable roots ride the kernel). Anything else is a temporary and its
  mutation stays there (`s.substr(1).to_upper()`, a by-value method result, a computed
  property, an escaped bound method).
- Scratch-backed receivers (typed `array<string>` elements, PROPERTY refs such as
  `kv.first`) must `commit_scratch()` after the mutator, on every path that passes the
  deref'd scratch as self (identifier fast paths included) - the unshare lands in the
  scratch, and only the commit stores the fresh node back.
- Map keys ride this, never a copy (Dev ruling 2026-09-04): the range-for pair's `first`
  stays the key's SHARED handle (`args.push_back(it->first)` / `it->first.clone()` in both
  backends, zero per-iteration work), `keys()` / `to_array()` / predicate arguments hand
  out shared handles too, and the first write through any of them detaches. The static
  checker is the front door: a range-for over a statically known map marks its pair
  variable (`var_entry::map_pair`) and every mutation spelling on `kv.first` it can see
  (string mutator, store, compound, `++`/`--`, `auto& k = kv.first`, a `T&` argument) is
  `cannot mutate 'kv.first': map keys are read-only` under strict; a `var` map or a
  `var`-sourced copy is the runtime's (`Map Tests :: map_key_read_only_static_battery`).
- The parity oracle cannot catch a missed gate or a transient self (both backends would
  agree); the batteries can: `Map Tests :: map_key_immutable_battery`, `Strong Types ::
  string_value_semantics_battery / string_mutator_receiver_lvalue_battery /
  string_mutator_chain_battery`, `review_regression :: string_cow_*`.

## 12. A compound assignment reads its target after its right side (2026-09-11)

`x op= e` evaluates `e`, then reads `x`, then stores, on both backends (the right operand before the left, as
C++17 sequences every assignment operator); `x = x op e` and the plain binary keep their left-to-right reads.
So with `mutate(int& v) { v = 20; return 2; }`, `x + mutate(x)` and `x = x + mutate(x)` are 5 and `x += mutate(x)`
is 22. The rule for an implementation: never carry a C++ reference to a slot's current representation across an
evaluation that can run user code, because a by-reference argument boxes the variable into a cell in place and
the saved reference then names the slot's reference wrapper; re-resolve the binding and read the payload at the
point the language's order puts the read (the interpreter's identifier compound path does this after the right
side; the VM's compound store reads the slot after the right side by construction). Suite `Compound Assignment
Order` pins it.

## 13. A call argument takes its value when it is evaluated (2026-09-27)

Arguments evaluate left to right after the callee, and a by-value argument's value is fixed at that point: a later
argument (or a conversion the binding runs) that writes the variable, element or object it read does not change what
the callee receives; a reference parameter binds the lvalue and sees the write. An evaluated argument can still share
storage (an element read is a reference, a heavy value is a handle to a shared node, a host global is a cpp-bound
box), so the value is taken explicitly, and only where a later argument may run user code:

- The parse-time stamp `call_expr::arg_captures` / `new_expr::arg_captures` (`transient_read_marker::stamp_arg_captures`,
  recomputed by the VM compiler for cloned nodes) marks every argument ahead of the last one that is not
  `callout_free<true>` (a `math::` intrinsic over callout-free operands counts as free). Kind `ref` (a call or operator
  result) acts only on a reference; kind `full` (a read of storage) also on a heavy value or a cpp-bound box.
- Right after evaluating a marked argument, each backend captures it through the ONE kernel `detail/arg_capture.hpp`,
  classified by how the callee resolved ahead of the arguments binds that position: a copy parameter dereferences,
  copies a heavy payload and decodes a bound primitive; a `shared_ptr`/`weak_ptr` parameter and an opaque host callable
  only dereference (a host `T&` or `bound_array<T>&` keeps working on the object); a reference parameter is untouched
  (Tier 1 re-resolves from the expression, Tier 2 shares the evaluated reference). Interpreter: `capture_call_argument`
  in every call path (math intrinsic, string builtin, direct method, generic) and both `new` loops; VM: `op_capture_arg`
  with the callee's form (pending register, stack callee, method receiver, `new` class, builtin).
- A binding conversion that may run user code (`binding_may_run_code`) captures the later arguments first, in both
  binders; a coroutine captures its arguments in `coroutine_handle::set_function`, since it binds them at the first
  resume.

A new call form must stamp its arguments and capture on both backends; an operand held across a later operand's
evaluation must leave the interpreter's value stack (statements clear it at their boundaries). Suite `Call Argument
Capture` pins it.
