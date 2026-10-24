# Reads borrow

A read only looks through a value. `node->left->value` in C++ is two pointer loads and no count traffic; `node.left.value` in
JaiScript is held to the same: every inner link of a read chain is borrowed (`const&`) where it lives, and only the final value
the program asked for is copied into its destination. A link that could run user code (a getter, an operator, a host property
with an accessor) reads as before, with the protective copy that keeps its receiver alive.

## Where the copies were (step 0)

Measured on a `/Z7` Release build (no LTCG) with a stack-walking sampler (main thread, ~1 kHz, leaf plus first caller outside
`script_value`'s own members). `copy_handle` and `destroy_handle` are `JAI_NOINLINE`, so every handle copy and release in the
process lands in one of them, wherever it was inlined from. Shares are of all samples.

| workload | samples | copy+destroy | attributed to |
|---|---|---|---|
| JaiDOOM timedemo (120x78) | 7200 | 0.69% | stack pops 0.29, temp stores 0.08, fused operand temps 0.07, env loads 0.07, ref declarations 0.06, the rest under 0.02 each |
| Gator Quest smoke (2000 ticks, 2 bots) | 2478 | 0.28% | stack pops 0.08, member_access_value 0.04, temp stores 0.04, ref declarations 0.04 |
| OSSUARY (scene 15, 1 worker) | 1741 | 0.17% | stack pops 0.11, reference binding 0.06 |
| tree kernel (typed) | 536 | 8.21% | stack pops 2.61 and GET_MEMBER pushes 2.43 (by-value arguments `orig.right`, which the callee keeps), temp stores 1.12, env loads 1.12, reference write-through 0.56 |
| chain_reads kernel | 414 | 2.42% | GET_MEMBER push and pop of the intermediate (0.97 + 0.97), INDEX pop 0.48 |

The three games run almost entirely in the cell tier (`cells_run_frame` 40-49% exclusive), which already reads fields in place:
there the count traffic is under 0.7% and none of it is a member read. The copies live in the opcode tier, in dynamic (`var`)
code: `tree` and the chain kernel.

The chain kernel showed the larger cost was not the count at all. `w.entities[k].value` minted a heap `reference_holder` for
`w.entities[k]` (`make_element_reference` + the holder pool, about 15% of the kernel) because the transient-read marker never
marked the object of a member read; and each intermediate link of `a.b.c` was a GET_MEMBER that pushed a handle copy for the next
GET_MEMBER to pop (`exec_get_member` 21% exclusive, most of it the generic path for the stack form).

## What changed

1. **The transient marker looks through member reads** (`detail/transient_read.hpp`, both backends). The object of a member read
   used as a value (anything but a bind, a write or a call through it) is consumed transiently, so `grid[y][x].cost`,
   `a[i].pos.x` and `w.entities[k].value` read the element shallowly and mint no reference. The walker gained a `place` flag:
   lvalue positions (store targets, call arguments and callees, unary operands, range-for subjects, reference declarations and
   returns, casts) keep minting; value positions (operands, assigned values, `var`/`auto` initializers, by-value returns,
   literal elements, `throw`) do not.
2. **One borrowed field read, `site_field`** (`vm_backend.cpp`). The proof, the member_ic row (filled on first sight) and the host
   lane, returning the field cell in place. `exec_get_member`, `exec_member_temp` and `fused_member_container` had three copies
   of this block; they now share the one.
3. **`op_get_member_path`**: a chain of two links or more (`a.b.c`, `grid[y][x].cost`, `w.items[i].pos.x`, the receiver of
   `a.b.c.f()`) compiles to one instruction over a `member_path_proto`: the root (a frame slot, or a value the compiler pushed),
   then each link inward. Member links borrow through `site_field` at their own member_ic row (`ip + k * code.size()` for the
   link k steps in); subscript links whose index is a frame slot or a constant borrow an untyped array's element in range.
   The outer link is `exec_get_member`'s read, which copies the final value. `exec_member_temp`'s nested form borrows its prefix
   the same way.

## Fallback

A link the walk cannot serve (a getter or a user operator, a host property, null, a miss, a map key, a typed array, an index out
of range or not an int, a registered global operator, a worker's element read) hands the rest of the chain to the general reads
over an owned copy of its receiver, exactly as the LOAD, INDEX and GET_MEMBER sequence read it: `member_access_value` for a member
(still trying the link's own row first), `exec_index` under the subscript's flags for an element. Results, errors and their texts
are those of the general reads. Nothing runs between a borrow and its use; where user code runs (the ladder), it runs over the
owned copy.

`elem_member_operand` shapes (`a[i].x` in a loop with an identifier base) keep their alias-temp lowering, which the cell
translation closes.

## Temporaries

A chain whose outer link is a member read, with two links or more, over a temporary (a call's result, a construction, a literal,
an operator's value: anything but a name) holds the temporary until the outer read completes, as C++ keeps a temporary until its
full-expression ends: `make().mid.peek.v` runs `peek` while `make()`'s object lives, and the object dies after `peek`'s result
(the final receiver dies first, then the temporary). `detail::held_chain_root` names the temporary for both backends. The member
path holds its own root; a chain the path does not start from the temporary (a computed or typed index, `?.`, more than eight
links) compiles the temporary with a copy under it (`op_dup`), and `op_deref_top` with a = 2 drops that copy after the outer read. The
interpreter's outer link keeps the temporary its innermost link evaluated (`chain_root_hold_`). A chain over a name holds
nothing: the variable owns the value.

A getter's or a method's receiver dies when the call returns, once nothing else owns it: the interpreter's pooled method
environment releases its receiver at the call's end, as the vm's frame does.

## The `cached_global_env_ || parallel_worker_` gate

It is not a safety requirement. `cached_global_env_` is null on the vm-reference backend (which validates without proofs or
caches) and in a worker (which keeps its proofs and reads its own member_ic rows). The expression is named `proofs_on()`; the IC
half of the old gate was already `member_ic_rows`, which returns null on the vm-reference backend and the worker's own rows in a
worker.

## Measured

Release (LTCG), interleaved, 7 rounds against the base at an equal-length path: `chain_reads_dyn` 187.9 to 94.2 us (-50%; the
marker alone -26%); JaiDOOM, Gator Quest, OSSUARY and GLOOM within their noise floors, hashes held. Kernels with no bytecode
change move by up to 5% either way between builds of the same source (code layout); the manifests confirm which programs changed.

## Not changed

By-value arguments (`merge(lower.right, ...)`) copy because the callee keeps the value. Reference parameters bind a reference.
A method receiver is copied because the method runs user code. Range-for binds and const parameters showed no measurable traffic.
The cell tier (`cells_run_frame`) was not touched.
