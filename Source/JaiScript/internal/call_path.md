# The call path (VM)

How a script call enters and leaves its frame inside the VM's dispatch loop, what each step guards, and what the
measurements say. The interpreter is unaffected; the bytecode is unaffected (the manifests of every program are unchanged).

## A frame switch dispatches its target

`run_dispatch` holds the running frame as a pointer. A call that pushes a frame (every call instruction, a constructor, a
cell block's call, a stdlib alias's real call) and a return that lands in its caller (the fall-off, `return`, the fused
returns) rebind it and dispatch the target's instruction through the replicated dispatch (`JAI_SWITCH_FRAME`): the callee's
first instruction, or the caller's instruction after the landing. The loop head never runs between the two frames, and each
switch site is its own indirect branch. An armed debugger takes the loop head (`JAI_DISPATCH_AT` checks the hook), so every
stop, step and breakpoint sees the frames it saw before; profiled and coverage builds take the loop head too.

## Lean frames

A lean frame is a record frame on an untouched environment: the callee's compiled body runs on its definition's environment
(a method) or the caller's (a plain function), its arguments are slots `0..argc-1` where they stand, and nothing is saved or
restored around it. There is one push per callee kind, `push_lean_method_frame` (the receiver moves into the frame's `this`)
and `push_plain_frame`, and one pop, `pop_lean_frame`. The return doors (`return_with_result`'s identity return,
`fall_off_script_frame`) run the pop themselves; `pop_script_frame_core` runs it for every other exit (unwinding, a frame
whose slot was boxed). A profiled build pops every frame through the general pop, which accounts it.

## The call instructions enter lean frames themselves

| instruction | lane | what it answers |
|---|---|---|
| `op_call_method` | `enter_lean_method_call` | the receiver's class against the site's method rows: the monomorphic row (statically resolved), then the polymorphic rows |
| `op_call_from_scratch`, `op_call_typed` | `enter_lean_sibling_call` | a parked sibling method: its dispatcher's memoized overload on a copy of the frame's receiver |
| `op_probe_callee` followed by a call with no arguments | `enter_probed_call` | the probe's own answer (the frame receiver's sibling method, or the site-cached script function), entered at once |

Each lane answers what the instruction's general path answers first, under the same guards, and pushes the same record.
A lane that declines has changed nothing, and the general path runs: it answers the same rows again, takes the shapes a lean
frame does not (a body that needs a frame environment, an argument that converts, a reference receiver from a host thunk,
a coroutine), and raises a spent budget or a depth limit exactly as before.

A probe and a call with no arguments between them have nothing that runs in between (internal/call_site_identity.md,
"Eliding the probe"), so `enter_probed_call` never writes the pending-callee register: the caller is parked on the call
instruction, as the pair leaves it, and the callee's return lands after the call. The fusion stays off while a debugger is
armed, in profiled and coverage builds, on parallel workers and for a tail-flagged call to a function (the splice is the call's
own).

## What guards a lane

| guard | why |
|---|---|
| receiver an instance (no coroutine handle) | the rows key on a script class |
| class identity and method epoch, per row | a reload, an added method or a field-set change bumps the epoch |
| field shadow (the row's class memo, else `has_field`) | a field named like the method takes the call |
| the site's own conditions, memoized in `call_site::mic_lean` | an identifier receiver whose name a string builtin may take keeps the variable-first order; `same_as` and `super` calls keep theirs |
| no native methods, no host class with script bases | the host lane answers first on the general path |
| the dispatcher's compiled body is the declaration's (`body_cache_key`) | a reload that replaced the body recompiles on the general path |
| the definition environment is the running one | nothing to save or restore |
| return conversion classified, depth gate, record pool, arguments bind in place, budget tick | the general push's own preconditions |
| not a parallel worker | workers keep their pins and admission rules; the serial path pays one flag test |

The declaration stays pinned for the frame (`pin_method_decl`), so a reload during the call keeps the running body alive and
the next call refetches through the epoch.

A lean bind whose every parameter takes one storage kind as it is (`int`, `float`, `bool`, `char`, `string`) carries that
signature packed beside its verdict (`lean_kinds_of`: the count and one storage index per parameter, up to seven): the arguments
bind in place when each one's storage index is its parameter's, one compare per argument against the packed word; any other
argument takes the per-parameter rows, which decide exactly as before. The method push moves the receiver into the record's
pooled `this` directly.

## Measured

Release LTCG, interleaved, pinned to one P core, against the base at an equal-length path (base2/base is the noise row).
A throwaway prototype of the same pieces set the bound first.

| program | prototype / base | build / base | base2 / base |
|---|---|---|---|
| Richards | 0.925 | 0.928 | 1.001 |
| DeltaBlue | 0.884 | 0.896 | 0.999 |
| bytecode | | 0.972 | 1.002 |
| method-call kernel (`c.bump(); c.peer();`) | 0.857 | 0.888 | 1.001 |
| fib_recursive_dyn | | 0.920 | 0.999 |
| tree_dyn, sort, queen, string2float | | 0.95 to 0.97 | 1.00 |

The games (GLOOM, OSSUARY, JaiDOOM, Gator Quest, crawler, demoreel) move within their noise with every hash held: they run in
the cell tier, where calls are few. On Richards a call and return goes from about 27% of the run to about 18%.

- A lane inlined into `run_dispatch` made every program slower (the method-call kernel 1.30x): the dispatch loop's register
  allocation is the hot path's, so a lane stays an out-of-line call.
- A lane from `op_call_from_scratch` straight into `push_plain_frame` for a parked script function measured within the noise
  (fib 0.975 to 0.978 against noise up to 1.026); the plain push is already one layer below the call.
- Writing what every entry of one body writes alike once per record measured nothing before this work
  (internal/apps_gap.md); the cost was in the layers between the instruction and the push, and in the loop head between frames.
  Skipping those writes outright, as a bound, saved under 0.5 ns of a 33 ns method call.

The kind signature and the direct receiver move, Release LTCG, pinned, near-empty callees (`switch (x) { default: return x; }`,
which no graft takes): `l.m(i)` 34.3 to 32.2 ns, `this.m(i)` 33.9 to 32.4, a bare sibling `m(i)` 38.8 to 38.5, a free function
`k(i)` 32.3 to 31.7; Richards, DeltaBlue and bytecode within their noise. What measured and was left out:

- A return lane for lean frames (the identity return's pop and landing ahead of `return_with_result`, from `RETURN`,
  `RETURN_IDENT` and `RETURN_BINARY`) saved about 1 ns per call, and made Richards 2.8% and DeltaBlue 1.2% slower in LTCG builds.
- The lane from `op_call_from_scratch` into `push_plain_frame` again, with a lane for the probe's cached answers ahead of
  `exec_probe_callee`: the free call 2 ns faster, DeltaBlue 1.8% and bytecode 1.0% slower.
- Power-of-two try, loop and counted-for records, so the pushes and pops size their stacks with a shift: nothing.
