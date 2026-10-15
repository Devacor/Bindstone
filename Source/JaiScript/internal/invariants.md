# Load-bearing invariants

Things that are easy to break silently. Each has a source anchor; if you're about to violate
one, read the anchor first. Background: `thin_value_spec.md` / `thin_value_rebaseline.md`.

## 1. script_value layout gates

`value.hpp` static_asserts `sizeof(script_value) == 32`, `alignof == 8` and nothrow move.
`script_value::storage` is a hand-written tagged union (8-byte payload plus index byte, 16 bytes),
with asserts pinning its size and nothrow move (the noexcept move contract depends on it). Layout is
8B `type_info_ptr` + 8B `engine*` + 16B storage. The `engine_` and
`type_info_` members are **the language**, not fat: zero-statics/multi-engine and the
var/auto/int type ladder live in them — never evict either to buy payload bytes. A new storage
alternative must be pointer-sized and nothrow-move or the asserts fire. The math vectors are one
alternative, a copy-on-write `script_vec_node` handle: every relocation shape that carried a 12-byte
inline payload measured 1-6% whole-program (`internal/math_types_design.md` 4.1).

## 2. strong_ptr single-pointer rules

`strong_ptr.hpp`: the handle is ONE object pointer; the control block is *derived* at a negative
offset (`cb_from_object`, via `cb_storage_offset = offsetof(control_block<T>, storage)`
— never hardcode the offset: over-aligned T and the Debug-only magic member both move it).
Sound only because `make_strong` and `detail::adopt_pooled` are the only producers of a non-null
handle: the raw-`T*` ctor is deleted and the cross-type converting ctors are deleted by design
(a Derived→Base conversion would adjust the pointer off the storage head AND slice the
static-type destroy). `adopt_pooled` exists solely for the per-engine block pools
(`detail::holder_block_pool` in value.cpp, for `reference_holder`, `object_holder` and `script_vec_node`): pooled blocks derive `control_block<T>`
without moving `storage`'s offset (static_assert-gated) and reset counts + Debug magic on reuse,
so the derivation invariant is preserved. `ptr_` MUST always be exactly `control_block<T>::storage`'s head.
Enforced: negative `is_constructible` static_asserts (strong_ptr_tests.cpp),
`sizeof == sizeof(void*)` gates (strong_ptr.hpp), and a Debug canary
`cb_magic_live = 0x5B0CB10C` asserted at every derivation (`cb_from_object`) and scrambled to
`0x0DEADCB0` on dealloc — a canary assert means a dead/foreign block, i.e. someone minted a
handle outside make_strong or used it after free.

## 2b. Frame slot pointers survive stack growth via the ONE chokepoint

Flat stack: vm frame slots live in value-stack WINDOWS
(`frame::window_base/window_live`, slot k at `stack_[window_base + k]`), and the stack is a
flat vector that reallocates on growth. The VM caches raw pointers into slots across
dispatch iterations (`counted_for_state.var/end_ptr/step_ptr`, `exec_cfor_prep` — end/step
may point INSIDE a slot's int payload). Sound only because ALL growth funnels through
`value_stack::grow_push` (vm_backend.hpp), which calls `rebase_window_pointers` — a
byte-offset rebase of exactly that enumerated pointer set — before the push. THE RULE:
never cache a raw pointer into stack/window storage across anything that can push, unless
you add it to `rebase_window_pointers` in the same commit; references never qualify
(owner-pinned holders, §3). Windows are created FULL (null placeholders up to `local_count`
behind `window_live`), so operand temps always start above the whole window. A fiber frame
is a window frame over the fiber's own value stack (`vm_coroutine_state::stack_storage`).
Resuming swaps it whole with the VM's stack (`value_stack::swap_payload`), so window indices
stay valid without rebasing. A raw-entered frame (`frame::raw`) reserves its window and builds
it (`build_raw_window` / `materialize_raw_window`) before the general form touches it. The
interpreter's `call_stack_` frames reserve the full `local_count` up front and never
reallocate mid-frame (a violation shows as silent wrong loop counts in Release).

## 2c. A released value, or a host call, may run script that re-enters the VM

Dropping the last handle to an instance runs its destructor synchronously (a script destructor, or a
host C++ destructor that calls a stored script callback), and a host method may call script back
before it returns. That script pushes onto the same execution stacks and can move the value stack's
buffer. The rules that keep this sound:

- A handle's destructor (`strong_ptr`, `weaker_ptr`) reads its pointer once, nulls it and releases
  through a local, so a value whose release re-enters is never touched again by its own destructor.
- `value_stack::pop_back` / `truncate` destroy a slot in place while `count` still covers it (a
  re-entry pushes above it, never over it) and re-read the buffer by index after every release.
  `truncate` destroys top down, so a popped frame's locals die last declared first and its parameters
  last (C++ order; the interpreter's `call_frame::locals` destroys the same way); a scope exit's `op_kill_slot` and
  the interpreter's `kill_scope_slot` move the value out of its slot before it dies, so a destructor that re-enters
  finds the slot already null. Which slots a scope exit kills is decided when the vm compiles the body
  (`slot_may_own_destructor`): a destructor a hot reload adds to a class reaches the block exits of functions compiled
  after it, and frame teardown destroys the rest at the function's end.
- `storage` assignment puts the new payload in place first and releases the old one last, so a
  destructor that stores to the slot being assigned keeps its store (C++ `shared_ptr` order) and one
  that moves the buffer finds the slot already written.
- A VM container never destroys an element while it still holds it: `iter_states_`, `try_records_`
  and `pending_callees_` shrink through `truncate_detached` / `pop_back_detached` (the tail moves into
  a local first; a noinline cold path, so the hot frame pop keeps its inlining).
  `builtin_arg_pool_` is a deque: a nested builtin call's vector never moves an outer one.
- A host lane (`class_definition::native_lane`) never hands the host a reference into the value
  stack: a `const script_value&` parameter moves its argument out of the slot into the lane's
  frame (`dynamic_binder::lane_held`), and a method returning its receiver copies it before the call.
  The receiver slot owns a handle, so a callback that drops the receiver's last variable does not
  end the call's object.

A destructor that runs while a throw unwinds gets its own exception state (`execute_callable`
parks the unwinding flag, exception, value and trace around it). Pinned by Spec Conformance
`reentrant_destructors_and_operand_order` and Reentrant Callbacks (the ASan build shows the
heap-use-after-free when broken).

## 3. Reference cells

A reference never holds a raw address. Eight owner-pinned holder modes exist
(`reference_holder`, value.hpp; long-form: internal/reference_model.md): CELL (the holder
OWNS the value in inline storage — Lua upvalue), ELEMENT (container+index re-resolve;
typed-buffer elements read through scratch), FIELD (owner+id re-resolve), MAP ENTRY (pinned
map + key re-resolve), CPP-MEMBER (receiver + C++ object pinned; the inline storage is a
cpp_bound box aimed at the member), PROPERTY (receiver pinned; getter into the
inline scratch per touch, setter on assign-through), STRING-ELEMENT (`string_base` plus
index) and COMPONENT (`component_base` plus a vector component index). Rules:

- Holder blocks come from the per-engine pool (`script_value::acquire_reference_holder`,
  value.cpp — one pop + placement-new per mint, memory_cap-charged for cells). The pool is
  single-threaded: parallel workers DO mint cell references inside their bodies, so acquire
  falls back to plain self-deleting `make_strong` blocks while a region is active (blocks
  self-describe their release via `dealloc_fn`, so both kinds coexist). A block released on
  a foreign thread is deleted there and counted (`foreign`), so an orphaned pool frees itself
  once drained. Cells are minted
  ONLY by `make_cell_reference`; the boxed value is never itself a reference.
- Subscript reads whose consumer provably discards identity skip the holder mint entirely
  (`detail/transient_read.hpp`, parse-time classifier consumed by BOTH backends; runtime
  gate `has_custom_binary_ops_`). Any NEW lvalue-consuming syntax must keep its operand
  positions unmarked there — see the checklist comment at the classifier. Escape-marked
  declarations (`parser.cpp` ref_escape_marker) box at decl/bind; unmarked storage
  boxes ON DEMAND at the first ref bind through the ONE box-in-place + share kernel
  pair (`vm_backend::share_env_ref` / interpreter `share_boxed_env_storage`; ref-param
  binds ride it via `bind_site_reference` on both backends). The vm kernel must demote active
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
  (vm `bind_parameters(..., site, caller_frame, caller_code, ...)`; interpreter
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

Cpp-bound values report raw storage index 14, not their semantic
shadow index. Any NEW binary/unary/compound handler that dispatches on `raw_storage_index()`
MUST normalize bound operands first via `bound_decoded_temp()` (value.hpp) — copy the
prologue from `detail::binary_arithmetic` (detail/binary_handlers.hpp) or `vm_backend::handle_add`.
The failure mode is SILENT: index 14 falls into an erroring `type_mismatch`
default, or worse a wrong-constant default (`same_as` → false). Raw-index *fast-path guards*
(`idx == TYPEID_INT`) are safe as-is — they exclude bound values and fall through.

A compound assignment or `++`/`--` on a bound variable writes through to the C++ variable
(`assign_through`). On a bound value held as an array element, map value, member or field, it
stores a detached plain result in that slot. A non-const by-ref extraction (`as_int_ref` family)
aliases only on an exact width and signedness match; otherwise it throws `jai::runtime_error`.

## 5. VM run_dispatch MSVC Debug frame ceiling

`run_dispatch` (vm_backend.cpp) executes calls in-loop, so its native frame sits on the
recursion path (capped by `engine::max_call_depth`) in Debug. Handlers return `op_status`;
`VM_TRY_OP` / `VM_TRY_OP_SHARED` (identical) add no `checked_result` temp to `run_dispatch`'s
frame. Each case in the main switch still grows that frame, so the ops outside the main set
share one case that calls the grouped `exec_extended` dispatch. Follow that pattern for new opcodes — do
NOT add fresh cases to the main switch for them. Canary:
`recursion_depth_error` (vm_backend_tests.cpp) must fail *catchably*; if the Debug frame
grows too big, that test instead kills the exe with a native stack overflow (0xC00000FD,
surfaces in the shell as exit code 127) mid-suite.

## 6. Backend parity is byte-identical, including error text

Interpreter and VM must produce identical results AND identical error codes/messages; the suite
runs green on both (`--backend=vm`). Preferred mechanism: shared kernels in `detail/` used
verbatim by both backends — `detail/ref_lvalue.hpp` ("shared VERBATIM ... parity by
construction") is the model, `detail/builtin_methods.hpp` the older one. Legacy twins remain:
`is_truthy` (interpreter.hpp vs vm_backend.cpp) and the VM's `handle_*` operators, which mirror
`detail/binary_handlers.hpp` (the interpreter's `handle_*` forward to that kernel) — if you touch one
twin, touch the other in the same commit. New shared logic goes in `detail/`; don't grow the
twin population.

## 7. Benchmark methodology

The Foundry benchmark harness (Release BENCHMARKS config) reports **integer µs/iteration with
±50% run-to-run variance** (the `x64-Release BENCHMARKS` preset, `JAISCRIPT_ENABLE_BENCHMARKS=ON`;
results print under `--verbose`). Compare min-of-3 runs,
and never claim a sub-µs win from it — anything under ~1 µs needs a dedicated ns micro-bench.

## 8. Typed array representation follows the declared type

- `array<int>` / `array<float>` / `array<bool>` / `array<char>` / `array<string>` are raw
  buffers by construction (`script_array::kind_t` i64/f64/b8/c8/str; u8 and i8 share the
  byte buffer with b8/c8, and v2/iv2/v3/iv3/v4/rgba are component-word buffers at the
  vector's stride) and `array<UserType>`
  is a buffer of `obj_slot{handle, tag}` (kind obj, the per-slot
  tag reproduces the boxed value on read); `array<var>` (and a literal with no spelled or
  inferable element type) is the ONLY `script_value`-vector form.
  A uniform `auto` literal infers its element type. There is no flag and no Debug/Release
  difference; converting between forms is an explicit assignment, nothing demotes silently.
- The tag is the node kind: `as_array()` / `checked_as_array()` are the `array<var>` view and
  return ONE consistent error on a typed node (`typed_array_vector_view_message(kind)`, naming the element type). Readers use
  `script_array::for_each` / `get`; writers use `set` / `push`; `bound_array<T>&` mutates the
  buffer in place and binds only element types that fit the kind.
- A raw fast lane tests the KIND it was written for (`kind() == i64 || kind() == f64`), never
  `is_typed()` — byte and string kinds are typed too and must take the enforcing `get`/`set`
  path (fuzz seed 129 stored an int into a string buffer through an `is_typed()` lane).
- A `checked_result` message is a NON-OWNING view for literals: dynamic text (a caught
  exception's `what()`, a built string) rides an error's `{0}` as a symbol from
  `engine::park_error_text` (`string_symbolizer::park`); the result never views the source buffer.
- Parallel workers never borrow string handles raw: the "all primitive" short-circuit excludes
  the str kind, and a typed primitive/string input is read through a boxed snapshot; an object-kind input lends its slots (`parallel_worker_slot::typed_input`)
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

## 9. Inline-cache entries are keyed by site, never by symbol

`env_lookup_cache` entries (`chunk.hpp`) carry a class pointer, a field slot and a serial but
NO symbol. The this-field IC (`this_field_ic_probe` / `this_field_ic_arm`, the store twin) is
therefore sound only at a cache slot that exactly one symbol ever resolves through: an op's
own `ip * 3` (+1 for its second operand) when that op resolves that symbol and nothing else
arms the same index. A fused store's `ip * 3` is armed for the STORE TARGET; its operand
resolvers must not probe a read IC at that index for a different symbol - the entry answers
the target's cell and the operand reads the wrong field. The value resolvers survive because
they re-stamp class and slot on every miss; a pointer resolver that trusts the entry does not.
Adding an IC probe to a resolver requires either an exclusively owned slot or a symbol check on
the entry (the string-builtin receiver cell resolves through `call_site::receiver_slot` /
`receiver_symbol` with no IC probe). Pinned by
`Strong Types :: bare_field_store_then_read_in_loops_battery` (the Foundry field-write shape,
which the whole suite otherwise missed).

## 10. Lambda capture sources resolve in the runtime's lookup order

The SOURCE a captured name binds to is decided by ONE kernel, `detail/capture_ladder.hpp`
(`resolve_capture_source`), used verbatim by `visit_lambda_expr` and `exec_closure`: the
enclosing frame's slot (locals and parameters, incl. the parser's `chained_outer_slots`),
then the member scope when active (own env scopes below the floor, then fields / static
fields / a hybrid's C++ property; a method name shadows and is not a cell), then the
environment chain from the creating env. A same-named global never wins over a slot or a
member. The capture KIND is applied by the caller afterwards ([&]/[&x] cell-share the
source, [=]/[x]/bare [] snapshot it); an explicit [x]/[&x] takes variables only, so a
member keeps the `Cannot capture undefined variable` text with or without a global. A
program-scope source (the ladder's `global` flag) is never captured by a default ([=], bare
[], [&]) and `[&g]` leaves it unlisted, so the call reads the global live; only `[g]` and an
init-capture `[x = e]` copy at creation (the init expression is evaluated by the creating
code and handed to the closure op, never resolved through the ladder). The
capture env parents the GLOBAL env, so "already captured" tests must read its OWN storage
(`get_own_value_ptr`), never `contains` - a `contains` walk reaches the global and the slot
is silently skipped (the bug class that let `[&]` box a global instead of the local).
Pinned by `Lambda Captures :: *_over_same_named_global` and the method-shadowing rows.

## 11. The string write gate

String copies share ONE `strong_ptr<script_string>` node (`clone()` and
`clone_for_assignment` are shallow for strings by design - O(1) copies), so an in-place
write through a shared node would reach every holder: `string b = a; b.to_upper()` would
uppercase `a`, and `for (auto kv : m) kv.first.to_upper()` would rewrite the MAP KEY under
the ordering invariant. Value semantics hold at exactly one place per string kind:
`script_value::unchecked_as_string_ref()` (value.hpp) is the only mutable accessor for a byte
string and `unchecked_as_utf8_ref()` for a utf8_string (`unchecked_text_ref` routes through
them); each unshares first - a node anyone else holds is replaced by a private
copy (`unshare_string_node`, memory_cap charged deferred) before the reference is handed
out. Bound (`cpp_bound`) strings return the live C++ member (write-through) and skip the
gate. Rules:

- Every in-place string write goes through the gate: the 18 mutators in the shared
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
- Map keys ride this, never a copy: the range-for pair's `first`
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

## 12. A compound assignment reads its target after its right side

`x op= e` evaluates `e`, then reads `x`, then stores, on both backends (the right operand before the left, as
C++17 sequences every assignment operator); `x = x op e` and the plain binary keep their left-to-right reads.
Every target follows the same order: `obj.f op= e`, `this.f op= e`, `C::s op= e`, `ns::x op= e`,
`a[j] op= e` and `a[j] = e` evaluate `e` first, then the receiver, container and index, then the read (a user
getter included), the arithmetic and the store, so an error in `e` comes before any error the target raises.
The VM emits the value first and the executor rotates the operands into its own order
(`compound_flag_value_first`); a literal right side keeps the target-first emission.
A local read stays before the target: evaluating a receiver, index or getter can write that local, even though reading it
cannot itself raise or write.
So with `mutate(int& v) { v = 20; return 2; }`, `x + mutate(x)` and `x = x + mutate(x)` are 5 and `x += mutate(x)`
is 22. The rule for an implementation: never carry a C++ reference to a slot's current representation across an
evaluation that can run user code, because a by-reference argument boxes the variable into a cell in place and
the saved reference then names the slot's reference wrapper; re-resolve the binding and read the payload at the
point the language's order puts the read (the interpreter's identifier compound path does this after the right
side; the VM's compound store reads the slot after the right side by construction). Suite `Compound Assignment
Order` pins it.

## 13. A call argument takes its value, or binds its reference, when it is evaluated

Arguments evaluate left to right after the callee, and a by-value argument's value is fixed at that point: a later
argument (or a conversion the binding runs) that writes the variable, element or object it read does not change what
the callee receives; a reference parameter sees the write. A reference parameter binds its argument at that point too
(C++): in `put(o.x, o = other)` the reference names the `x` of the object `o` held when `o.x`
was evaluated, `put(a[i], i++)` names `a[0]`, and a later argument that reassigns a receiver or replaces a nested one
moves nothing. An evaluated argument can still share storage (an element read is a reference, a heavy value is a
handle to a shared node, a host global is a cpp-bound box), so the value is taken explicitly, and only where a later
argument may run user code:

- The parse-time stamp `call_expr::arg_captures` / `new_expr::arg_captures` (`transient_read_marker::stamp_arg_captures`,
  recomputed by the VM compiler for cloned nodes) marks every argument ahead of the last one that is not
  `callout_free<true>` (a `math::` intrinsic over callout-free operands counts as free). Kind `ref` (a call or operator
  result) acts only on a reference; kind `full` (a read of storage) also on a heavy value or a cpp-bound box.
- Right after evaluating a marked argument, each backend captures it through the ONE kernel `detail/arg_capture.hpp`,
  classified by how the callee resolved ahead of the arguments binds that position: a copy parameter dereferences,
  copies a heavy payload and decodes a bound primitive; a `shared_ptr`/`weak_ptr` parameter and an opaque host callable
  only dereference (a host `T&` or `bound_array<T>&` keeps working on the object). A place argument (a member, subscript
  or component lvalue whose receiver or index evaluation decides; the stamp's `arg_capture_lvalue` bit,
  `transient_read_marker::place_lvalue_argument`) that a reference parameter takes mints its reference there, through the
  same site resolution the binding uses (interpreter `bind_site_reference`, VM `bind_place_reference`, the node riding the
  capture instruction above `capture_place_shift`); the reference rides beside the evaluated value
  (`hold_minted_reference`, `argument_capture_storage::minted`) and the binding takes it as it is
  (`early_bound_reference`: the parameter binding in both binders, a host `T&` through `host_reference_argument`). An
  unstamped argument binds at the call, which is the same place: nothing runs between its evaluation and the
  binding. On the VM a probed call's field argument a frame-slot local names (`o.left`) compiles to `op_place_arg`: when the
  parked callee is one script function whose parameter is a `T&`, the reference is minted in place of the member read (the
  same plain-field step, `detail::plain_field_reference`), else the read runs as `op_get_member`; a place argument that
  evaluated to a reference is bound as it is (`bind_parameters`, `bind_plain_references`). Interpreter: `capture_call_argument` in every call path (math intrinsic, string builtin, direct method,
  generic) and both `new` loops; VM: `op_capture_arg` with the callee's form (pending register, stack callee, method
  receiver, `new` class, builtin).
- A binding conversion that may run user code (`binding_may_run_code`) captures the later arguments first, and binds
  the places of the later reference parameters (`bind_later_places`), in both binders; a coroutine captures its
  arguments in `coroutine_handle::set_function`, since it binds them at the first `next()`. A graft refuses an element
  reference argument a later argument may move.
- A VM graft stages an enclosing call's callee and earlier arguments at their original evaluation points. The ordinary
  capture instruction takes any required value snapshot, and the call retains its original argument-source metadata for
  reference binding. Its captured argument prefix stays in owned compiler slots and moves into the final call on both the graft and guard-fallback paths, so surrounding expression operands cannot interleave with it.
  A private aggregate binding is not evidence that its contents cannot be written through a later argument.
- An overload set with mixed parameter modes retains the evaluated value and original source until selection. A value
  parameter takes the snapshot, a shared handle takes the evaluated identity, and a reference binds the original lvalue.
  An all-reference/all-share set needs no deep snapshot. Static script dispatchers carry their callable metadata through
  aliases; host callables retain their existing opaque binding contract. Constructors and new expressions carry the same
  argument-source metadata as ordinary calls.

A call resolves its callee before its arguments (C++17 order). On the VM, `op_probe_callee` resolves an identifier
callee into the per-fiber pending-callee stack (`pending_callees_`) and `op_call_from_scratch` calls it; `op_probe_member`
raises a member or computed callee's lookup error at the same point (emitted when an argument is non-trivial), leaving
the stack untouched. Pinned by the `callee_first_*` tests in vm_backend_tests.cpp.

A new call form must stamp its arguments and capture on both backends; an operand held across a later operand's
evaluation must leave the interpreter's value stack (statements clear it at their boundaries). Suite `Call Argument
Capture` pins it.

## 14. An auto declaration takes its initializer's static type, once

`auto x = e` is the type the checker gives `e`, never the kind the value holds when the line runs: a `var` or
unknown static type makes `x` a `var`, any other type keeps `auto` (the runtime locks to the value, which has
that type). The checker writes the decision into the declaration (`variable_decl::type` becomes the var marker,
`decl_fast_flags` recomputed; `auto_spelled` keeps the spelling for re-checks and `.jaibite`, which saves the slot
as `auto` and types it again at load). The rules for an implementation:

- Only a proof pass decides (stamping or clearing, never a diagnostics walk or a per-call instance-variable
  check), and both proof modes see the same program (`program_global` answers in either), so proofs on and off
  decide alike.
- The first proof pass of every unit runs in every mode (`script_cache_entry::autos_typed`, `jaibite::autos_typed_`),
  before either backend compiles or runs the unit; `auto_typed` then freezes the decision, so a later re-stamp
  (a surface epoch move, a deferred class defined by an import mid-run) never re-types a declaration under a
  running unit or a compiled chunk.

Suite `Spec Conformance` (`auto_static_type`) pins it, both backends, proofs on and off, every check mode.

## 15. Collection transforms preserve observable copy behavior

Dot array append/concat/filter/map/slice mutate; array:: forms copy. Dot map keeps
input constraints; namespace map takes declared callback result constraints. Filters
preserve array T or map K,V. Dynamic slots retain current typed container constraints
through dynamic type twins while permitting the variable itself to be rebound.
Selected outputs use clone_element: class values copy and explicit handles share.
Never deep-copy read-only inputs or overwritten merge entries preemptively. In-place
selection moves retained storage, so nocopy objects remain usable through references.
Any snapshot held across a user callback is shallow and pins its backing node.

Public ordering and private map-key ordering are distinct contracts. Public arrays
and vectors order lexicographically; map entries compare keys by private map-key
semantics and values by public ordering. Public numeric <=> groups NaNs last and
signed zeros equivalent; ordinary numeric relational/equality operators stay IEEE.
Private map-key comparison and hashing must remain consistent, including expired
weak owners. Comparison errors and original script throws survive container ordering.
Suite Spec Conformance's api_consistency groups pin these contracts on both backends
with proofs on and off.
