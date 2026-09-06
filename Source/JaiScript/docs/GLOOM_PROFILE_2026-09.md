# GLOOM CPU profile: 2026-09-06

Identified element reads account for **23.93% of serial CPU samples** and
**29.41% with eight workers**. Script-value construction/destruction accounts
for **13.99%** and **16.96%**. The union of element reads, writes, array push
and value lifetime is 48.02% serial and 49.83% with eight workers. These
categories overlap; their individual percentages must not be added.

Push now executes through `exec_push_fused`. Its full category is
9.17% serial and 11.55% with eight workers, while method-call handling is
3.88% and 4.33%. The older CALL_METHOD ranking no longer represents push's
current execution path.

## Build and workload

- Release library and `jai_gloom.exe`: `out/astra/profile`.
  Runtime includes `baf46227`'s fused-push change. Capture preparation:
  `a74a15e3`; corrected process handling: `fc0accce`.
- MSVC 19.51.36252 x64, C++20,
  `/O2 /Ob2 /Oi /Ot /GL /Zi /DNDEBUG /MT`,
  `/LTCG /OPT:REF /OPT:ICF /DEBUG:FULL /INCREMENTAL:NO`.
  `GLOOM_PROFILE_CAPTURE` adds two measured-loop ETW markers.
  Opcode timing instrumentation is disabled.
- Core Ultra 9 285HX, 24 physical/logical CPUs. Serial affinity: CPU 0.
  Eight-worker affinity: CPUs 0, 1, 10, 11, 12, 13, 22, 23.
- Five processes per configuration. Each runs 600 warm-up ticks, reboots game
  state in the same runtime, then runs 600 measured ticks. VM backend,
  seed 666, 100x40, quad pixels.
- All ten processes exit 0 and match frame `dfd969ed5a336dca` and
  state `1503537018`. The marked binary also passes the 300-tick serial
  and eight-worker gate: `7fecc09815bb64e0` / `3580805725`.
- Captured executable SHA256:
  `c7ef13d99303ceb6eb8b8e64d813c35365b51b73896a5ec6b299d1d48db5f4c6`.

## Measurement method and integrity

WPR captures CPU samples at a **1000 us period**. The analysis selects each
manifest PID between its measured-loop events from provider
`744b3f90-7d83-4b7b-a861-40637623c592`. Loading, warm-up, reboot and
summaries are excluded. Marker spans agree with reported loop times within
0.1 ms.

The selected data contains **6,933 serial samples** and **9,946 eight-worker
samples**. Observed CPU IDs exactly match affinity. Both ETL headers report
**zero lost events and zero lost buffers**. Serial has no missing stacks;
eight workers has three, 0.03% of its samples.

Xperf exports physical stacks and sampled instruction addresses. DbgHelp
resolves address-specific inline chains and source lines from the matching
private PDB. All 3,916 queried JaiScript RVAs resolve; 1,307 have inline frames.
This avoids mistaking xperf's appended list of inlinees for an executed inline
chain. Microsoft public symbols resolve ntdll and kernel32, whose image
timestamps and sizes match the trace. Unresolved leaf samples are kernel or
driver addresses: **1.07% serial, 2.11% eight workers**.

Exclusive time credits the innermost resolved function at the instruction
pointer. Inclusive time credits each function on the expanded stack once per
sample, including recursive appearances only once. Identical undecorated names
are aggregated. Outer wrappers can have nearly 100% inclusive time and little
exclusive work.

Category attribution stops at the innermost VM dispatch activation. Thus an
entry call around an entire frame does not turn the whole frame into call
overhead. The rules include identified handlers and their helper work:

- Reads: indexed and fused-index read handlers, typed-element materialization,
  and parallel borrowed subscript reads.
- Writes: index store, assignment and compound-index handlers.
- Push: `exec_push_fused`, the array push kernel, and the identified
  push slice in `exec_call_method`.
- Function calls: callee probes, call handlers, frame push/pop, binding and
  shared returns. Native calls using these same handlers are included.
  Shared returns cannot be uniquely assigned to functions versus methods.
- Methods: member probes, method-call handlers, method frame entry and
  overload/cache handling. Method contexts are excluded from the function row.
- Boxing/lifetime: `script_value` and storage construction/destruction,
  including storage destruction and descendant allocation/refcount work.
  Assignments outside those lifetime routines are not included.

Generic fused resolvers can contain additional semantic work outside these
buckets. The figures quantify these explicit attribution rules, not every
language-level operation. Raw expanded stacks preserve every decision.

## Requested category breakdown

| Category | Serial CPU % | Serial per-run range | Eight-worker CPU % | Eight-worker per-run range |
|---|---:|---|---:|---|
| Element reads | 23.93 | 23.50 to 24.46 | 29.41 | 28.54 to 30.33 |
| Element writes | 7.28 | 6.24 to 7.88 | 5.39 | 4.77 to 6.21 |
| Array push | 9.17 | 8.44 to 9.98 | 11.55 | 10.88 to 12.18 |
| Function-call machinery and shared returns | 5.01 | 4.39 to 5.42 | 7.27 | 6.49 to 7.94 |
| Method-call machinery | 3.88 | 3.60 to 4.48 | 4.33 | 3.86 to 5.07 |
| script_value construction/destruction | 13.99 | 12.53 to 15.57 | 16.96 | 15.45 to 18.35 |

Ranges are the minimum and maximum shares across five runs, not confidence
intervals. Boxing overlaps container and call categories.

## Comparison with the recorded profile

The reference is the 200-tick GLOOM opcode ranking under
`Baseline at 85f0a1af` in
[PERF_PASS_2026-09-02.md](PERF_PASS_2026-09-02.md).
It measures instrumented opcode cycles. This profile measures Release CPU
samples over 600 warmed ticks and includes shared-host frame hashing.
Percentages have different denominators and cannot be subtracted as per-op
performance deltas.

| Recorded observation | Fresh observation | What moved |
|---|---|---|
| CALL_METHOD: 16.0%; 94% of its calls were array.push | Dedicated push category: 9.17% / 11.55%; method handling: 3.88% / 4.33% | Push moved into a separate fused execution path. The old method bucket is no longer its proxy. |
| INDEX_FUSED_DECL alone: 16.0% | Identified read paths: 23.93% / 29.41%; index_fused_read remains a leading exclusive function | Reads remain a major target. This broader category does not establish an 8-point regression. |
| INDEX_STORE_FUSED: 6.4% | Identified writes: 7.28% / 5.39% | Writes remain material; workload and measurement differences affect the shares. |
| STORE: 5.2%, dominated by the background-grid copy | exec_store: 3.95% / 4.14% inclusive, zero exclusive samples; script_value::clone: 2.48% / 2.41% inclusive | The old store instruction's self-time is no longer a useful standalone priority. Helper/copy work remains. |
| CALL_FROM_SCRATCH: 3.6%; RETURN: 2.75% | Call probes, handlers and shared returns: 5.01% / 7.27% | Call machinery remains smaller than read and lifetime traffic in this workload. |
| Boxing spread through opcode timings | Construction/destruction: 13.99% / 16.96% | Sampling exposes the cross-cutting lifetime cost directly. |

`run_dispatch` has 13.54% serial exclusive CPU and 10.69% with eight
workers. This includes operations implemented directly in the loop, not only
instruction dispatch.

Eight workers produce **1.435 times the CPU samples** for the same workload.
This is CPU work summed across threads, not wall time. Exclusive Windows heap
allocation/free paths rise from **4.70% to 8.80%**. In the eight-worker run,
`parallel_borrow_subscript_read` is **5.16% inclusive**; **1.50% of total
CPU** is exclusive heap work beneath it. The source constructs worker-owned
strings from borrowed bytes. `RtlSleepConditionVariableSRW` also accounts
for **2.66% exclusive CPU**, which measures executing code, not time asleep.

The evidence supports prioritizing typed element/scalar flow and value
lifetime traffic, then worker-local materialization and its allocation cost.
Any optimization must preserve the existing ownership, aliasing and reload
contracts. This work makes no language or contract decision.

## Serial: top 30 inclusive functions

| Rank | Function | Inclusive CPU % | Exclusive CPU % |
|---:|---|---:|---:|
| 1 | <code>ntdll.dll!RtlUserThreadStart</code> | 98.99 | 0.00 |
| 2 | <code>main</code> | 98.99 | 0.00 |
| 3 | <code>invoke_main</code> | 98.99 | 0.00 |
| 4 | <code>kernel32.dll!BaseThreadInitThunk</code> | 98.99 | 0.00 |
| 5 | <code>gloom::run_gloom</code> | 98.99 | 0.00 |
| 6 | <code>__scrt_common_main_seh</code> | 98.99 | 0.00 |
| 7 | <code>gloom::&#x27;anonymous namespace&#x27;::run_smoke</code> | 98.99 | 0.00 |
| 8 | <code>&#x27;anonymous namespace&#x27;::jai_session::frame</code> | 98.38 | 0.00 |
| 9 | <code>&#x27;anonymous namespace&#x27;::call_script</code> | 98.37 | 0.00 |
| 10 | <code>std::invoke</code> | 98.36 | 0.00 |
| 11 | <code>vm::execute_callable</code> | 98.36 | 0.00 |
| 12 | <code>script_callable_thunk::operator()</code> | 98.36 | 0.00 |
| 13 | <code>vm::run</code> | 98.36 | 0.00 |
| 14 | <code>std::_Func_class&lt;checked_result&lt;script_value&gt;,std::vector&lt;script_value,std::allocator&lt;script_value&gt;... [c64a3141]</code> | 98.36 | 0.00 |
| 15 | <code>vm::call_script_function</code> | 98.36 | 0.00 |
| 16 | <code>vm::run_dispatch</code> | 98.36 | 13.54 |
| 17 | <code>std::_Func_impl_no_alloc&lt;script_callable_thunk,checked_result&lt;script_value&gt;,std::vector&lt;script_valu... [97113bc8]</code> | 98.36 | 0.00 |
| 18 | <code>vm::exec_index_fused_decl</code> | 20.29 | 0.59 |
| 19 | <code>vm::index_fused_read&lt;0,&#x27;vm::exec_index_fused_decl&#x27;::&#x27;5&#x27;::decl_lane_sink &amp;&gt;</code> | 19.54 | 4.08 |
| 20 | <code>vm::fused_cmp_operand</code> | 10.72 | 6.04 |
| 21 | <code>vm::fused_ident_value</code> | 9.94 | 3.10 |
| 22 | <code>vm::exec_push_fused</code> | 8.86 | 0.72 |
| 23 | <code>script_value::script_value</code> | 7.64 | 2.52 |
| 24 | <code>vm::exec_index_store_fused</code> | 7.28 | 2.54 |
| 25 | <code>vm::exec_call_method</code> | 7.10 | 0.09 |
| 26 | <code>vm::exec_fused_cmp_jump</code> | 6.56 | 1.75 |
| 27 | <code>script_value::deref</code> | 6.35 | 3.04 |
| 28 | <code>script_value::storage::~storage</code> | 5.86 | 4.31 |
| 29 | <code>vm::store_popped_value</code> | 4.93 | 0.68 |
| 30 | <code>vm::binary_fused_compute&lt;&#x27;vm::run_dispatch&#x27;::&#x27;125&#x27;::temp_lane_sink &amp;&gt;</code> | 4.50 | 0.87 |

## Serial: top 30 exclusive functions

| Rank | Function | Exclusive CPU % | Inclusive CPU % |
|---:|---|---:|---:|
| 1 | <code>vm::run_dispatch</code> | 13.54 | 98.36 |
| 2 | <code>vm::fused_cmp_operand</code> | 6.04 | 10.72 |
| 3 | <code>script_value::storage::~storage</code> | 4.31 | 5.86 |
| 4 | <code>vm::index_fused_read&lt;0,&#x27;vm::exec_index_fused_decl&#x27;::&#x27;5&#x27;::decl_lane_sink &amp;&gt;</code> | 4.08 | 19.54 |
| 5 | <code>vm::fused_ident_value</code> | 3.10 | 9.94 |
| 6 | <code>script_value::deref</code> | 3.04 | 6.35 |
| 7 | <code>script_value::storage::storage</code> | 2.91 | 2.91 |
| 8 | <code>ntdll.dll!RtlpLowFragHeapAllocFromContext</code> | 2.84 | 2.93 |
| 9 | <code>__security_check_cookie</code> | 2.81 | 2.81 |
| 10 | <code>vm::exec_index_store_fused</code> | 2.54 | 7.28 |
| 11 | <code>script_value::script_value</code> | 2.52 | 7.64 |
| 12 | <code>vm::frame_slot</code> | 2.22 | 2.39 |
| 13 | <code>vm::exec_fused_cmp_jump</code> | 1.75 | 6.56 |
| 14 | <code>vm::is_numeric_binary_op</code> | 1.69 | 1.69 |
| 15 | <code>script_value::storage::destroy</code> | 1.66 | 1.70 |
| 16 | <code>ntdll.dll!RtlFreeHeap</code> | 1.63 | 1.64 |
| 17 | <code>vm::exec_binary_fused_decl</code> | 1.13 | 4.49 |
| 18 | <code>script_value::is_int</code> | 1.11 | 1.49 |
| 19 | <code>script_value::operator=</code> | 1.00 | 2.29 |
| 20 | <code>vm::exec_compound_fused</code> | 1.00 | 1.93 |
| 21 | <code>script_array::size</code> | 0.91 | 1.23 |
| 22 | <code>vm::binary_fused_compute&lt;&#x27;vm::run_dispatch&#x27;::&#x27;125&#x27;::temp_lane_sink &amp;&gt;</code> | 0.87 | 4.50 |
| 23 | <code>vm::binary_fused_compute&lt;&#x27;vm::index_fused_read&lt;0,&#x27;vm::exec_index_fused_decl&#x27;::&#x27;1&#x27;::&lt;lambda_1&gt; &gt;&#x27;::&#x27;... [02a6fb19]</code> | 0.87 | 4.28 |
| 24 | <code>std::vector&lt;vm::vm_instruction,std::allocator&lt;vm::vm_instruction&gt; &gt;::operator[]</code> | 0.81 | 0.81 |
| 25 | <code>&#x27;vm::exec_index_fused_decl&#x27;::&#x27;5&#x27;::decl_lane_sink::raw_int</code> | 0.78 | 1.04 |
| 26 | <code>vm::exec_decl_var</code> | 0.74 | 2.38 |
| 27 | <code>vm::exec_push_fused</code> | 0.72 | 8.86 |
| 28 | <code>script_value::current_type</code> | 0.72 | 0.72 |
| 29 | <code>vm::store_popped_value</code> | 0.68 | 4.93 |
| 30 | <code>std::vector&lt;script_value,std::allocator&lt;script_value&gt; &gt;::_Emplace_back_with_unused_capacity</code> | 0.65 | 0.68 |

## Eight workers: top 30 inclusive functions

| Rank | Function | Inclusive CPU % | Exclusive CPU % |
|---:|---|---:|---:|
| 1 | <code>ntdll.dll!RtlUserThreadStart</code> | 99.54 | 0.00 |
| 2 | <code>kernel32.dll!BaseThreadInitThunk</code> | 99.54 | 0.00 |
| 3 | <code>std::invoke</code> | 99.10 | 0.00 |
| 4 | <code>vm::execute_callable</code> | 96.51 | 0.02 |
| 5 | <code>vm::call_script_function</code> | 96.50 | 0.05 |
| 6 | <code>vm::run</code> | 96.15 | 0.00 |
| 7 | <code>vm::run_dispatch</code> | 96.14 | 10.69 |
| 8 | <code>main</code> | 56.49 | 0.00 |
| 9 | <code>invoke_main</code> | 56.49 | 0.00 |
| 10 | <code>gloom::run_gloom</code> | 56.49 | 0.00 |
| 11 | <code>gloom::&#x27;anonymous namespace&#x27;::run_smoke</code> | 56.49 | 0.00 |
| 12 | <code>__scrt_common_main_seh</code> | 56.49 | 0.00 |
| 13 | <code>&#x27;anonymous namespace&#x27;::jai_session::frame</code> | 56.11 | 0.00 |
| 14 | <code>&#x27;anonymous namespace&#x27;::call_script</code> | 56.05 | 0.00 |
| 15 | <code>script_callable_thunk::operator()</code> | 56.04 | 0.00 |
| 16 | <code>std::_Func_class&lt;checked_result&lt;script_value&gt;,std::vector&lt;script_value,std::allocator&lt;script_value&gt;... [c64a3141]</code> | 56.04 | 0.00 |
| 17 | <code>std::_Func_impl_no_alloc&lt;script_callable_thunk,checked_result&lt;script_value&gt;,std::vector&lt;script_valu... [97113bc8]</code> | 56.04 | 0.00 |
| 18 | <code>detail::&#x27;anonymous namespace&#x27;::run_worker_chunk</code> | 52.67 | 0.02 |
| 19 | <code>thread_pool::{ctor}::__l15::&lt;lambda_1&gt;::operator()</code> | 43.05 | 0.00 |
| 20 | <code>std::thread::_Invoke&lt;std::tuple&lt;&#x27;thread_pool::thread_pool&#x27;::&#x27;15&#x27;::&lt;lambda_1&gt; &gt;,0&gt;</code> | 43.05 | 0.00 |
| 21 | <code>thread_pool::run_worker</code> | 43.05 | 0.00 |
| 22 | <code>thread_start&lt;unsigned int (__cdecl*)(void *),1&gt;</code> | 43.05 | 0.00 |
| 23 | <code>std::_Func_class&lt;void&gt;::operator()</code> | 40.52 | 0.00 |
| 24 | <code>vm::exec_index_fused_decl</code> | 23.42 | 0.87 |
| 25 | <code>vm::index_fused_read&lt;0,&#x27;vm::exec_index_fused_decl&#x27;::&#x27;5&#x27;::decl_lane_sink &amp;&gt;</code> | 22.27 | 2.68 |
| 26 | <code>detail::&#x27;anonymous namespace&#x27;::run_region</code> | 16.17 | 0.03 |
| 27 | <code>vm::exec_push_fused</code> | 11.30 | 0.49 |
| 28 | <code>script_value::script_value</code> | 10.32 | 3.31 |
| 29 | <code>vm::exec_parallel_for</code> | 9.42 | 0.00 |
| 30 | <code>detail::run_parallel_for</code> | 9.41 | 0.00 |

## Eight workers: top 30 exclusive functions

| Rank | Function | Exclusive CPU % | Inclusive CPU % |
|---:|---|---:|---:|
| 1 | <code>vm::run_dispatch</code> | 10.69 | 96.14 |
| 2 | <code>ntdll.dll!RtlpLowFragHeapAllocFromContext</code> | 5.38 | 6.36 |
| 3 | <code>vm::fused_cmp_operand</code> | 4.61 | 7.64 |
| 4 | <code>script_value::storage::~storage</code> | 4.31 | 6.05 |
| 5 | <code>script_value::script_value</code> | 3.31 | 10.32 |
| 6 | <code>script_value::storage::storage</code> | 3.16 | 3.19 |
| 7 | <code>ntdll.dll!RtlFreeHeap</code> | 3.06 | 3.48 |
| 8 | <code>vm::index_fused_read&lt;0,&#x27;vm::exec_index_fused_decl&#x27;::&#x27;5&#x27;::decl_lane_sink &amp;&gt;</code> | 2.68 | 22.27 |
| 9 | <code>ntdll.dll!RtlSleepConditionVariableSRW</code> | 2.66 | 3.28 |
| 10 | <code>__security_check_cookie</code> | 2.56 | 2.56 |
| 11 | <code>vm::fused_ident_value</code> | 2.23 | 6.74 |
| 12 | <code>script_value::storage::destroy</code> | 1.96 | 2.01 |
| 13 | <code>script_value::deref</code> | 1.85 | 3.72 |
| 14 | <code>vm::exec_index_store_fused</code> | 1.76 | 5.39 |
| 15 | <code>vm::exec_fused_cmp_jump</code> | 1.60 | 5.01 |
| 16 | <code>detail::parallel_borrow_subscript_read</code> | 1.49 | 5.16 |
| 17 | <code>vm::is_numeric_binary_op</code> | 1.35 | 1.35 |
| 18 | <code>vm::frame_slot</code> | 1.29 | 1.45 |
| 19 | <code>std::vector&lt;script_value,std::allocator&lt;script_value&gt; &gt;::_Emplace_back_with_unused_capacity</code> | 1.20 | 1.26 |
| 20 | <code>script_value::operator=</code> | 1.04 | 2.93 |
| 21 | <code>vm::env_lookup_cached</code> | 0.97 | 1.41 |
| 22 | <code>script_value::is_int</code> | 0.92 | 1.36 |
| 23 | <code>vm::value_stack::push_back</code> | 0.91 | 3.15 |
| 24 | <code>vm::exec_index_fused_decl</code> | 0.87 | 23.42 |
| 25 | <code>script_array::get</code> | 0.83 | 1.33 |
| 26 | <code>vm::exec_binary_fused_decl</code> | 0.75 | 3.48 |
| 27 | <code>vm::value_stack::dtor_elidable</code> | 0.75 | 0.76 |
| 28 | <code>vm::exec_index</code> | 0.74 | 8.63 |
| 29 | <code>memcpy</code> | 0.70 | 0.70 |
| 30 | <code>vm::exec_decl_var</code> | 0.69 | 1.90 |

Long display names use a stable eight-character ID. Full names are retained
in `symbol-catalog.json` and the unabridged CSV tables.

## Captured intervals

| Mode | Run | PID | Measured interval (trace us) | Samples | Captured wall ms |
|---|---:|---:|---|---:|---:|
| serial | 1 | 38136 | 1629834 to 3015231 | 1383 | 1385.397 |
| serial | 2 | 43188 | 4482854 to 5871668 | 1388 | 1388.814 |
| serial | 3 | 42724 | 7338455 to 8737117 | 1394 | 1398.662 |
| serial | 4 | 44116 | 10199544 to 11588691 | 1387 | 1389.147 |
| serial | 5 | 37388 | 13047651 to 14433221 | 1381 | 1385.570 |
| workers8 | 1 | 48316 | 1451469 to 2662287 | 1991 | 1210.818 |
| workers8 | 2 | 20856 | 3967264 to 5184642 | 1976 | 1217.378 |
| workers8 | 3 | 27696 | 6484633 to 7708216 | 1995 | 1223.583 |
| workers8 | 4 | 34132 | 9009524 to 10223674 | 1971 | 1214.150 |
| workers8 | 5 | 47916 | 11510071 to 12722398 | 2013 | 1212.327 |

These wall times identify sampled spans, not new benchmark results. They are
not used to credit a speedup over the Lua comparison. The comparison and
profile executables differ, including fused push and profiling markers.

## Verification, evidence and limits

**Verified by running:** Release builds; all ten captured exits and hashes;
marker spans; affinity; loss counters; address-specific inline resolution;
matching executable/PDB and OS image identities; final full Debug suites on
both backends, **116 suites and 2740 tests each**; documentation site checks
with `--no-samples`.

**Inferred from source:** category meanings, push routing and the ownership
reason for worker string materialization. The hotspots do not demonstrate
the speedup of an unimplemented optimization.

**Limits:** small differences inside the per-run ranges are not credited.
Generic resolver work is not fully assignable to semantic categories.
Kernel/driver leaf symbols remain unresolved for the shares above. Xperf
reported system-rundown metadata-format warnings for unrelated PIDs; selected
sample/marker records parsed and ETL loss counters are zero. Other processes'
samples are excluded, but their scheduling/cache effects are not removed.
No whole-machine isolation or zero-background capture is claimed.

The earlier comparison's working-tree source audit matches its VM backend
and opcode header to `b79fbc60`. Its compiler-file hash did not match the
tested commit blobs. Comparison binaries and hashes are preserved; the
working-tree manifest is not proof of every compiled input. This report uses
the captured executable and matching PDB for attribution, not a commit label.
The LTCG PDB source-file records did not supply source checksums.

Raw paths below are relative to `Source/JaiScript/out/profile-2026-09`:

- `20260906-171437-serial.etl`, `20260906-171437-workers8.etl`,
  `20260906-171437-captures.json`, `20260906-171437-binary.txt`,
  and ten matching stdout/stderr logs.
- `serial-markers.csv`, `workers8-markers.csv`, `serial-dump.csv`,
  `workers8-dump.csv` and `*-samples.json`.
- `resolved-symbols.jsonl`, `ntdll-symbols.jsonl`,
  `kernel32-symbols.jsonl`, `os-image-verification.json`,
  `symbol-catalog.json`.
- `serial-inclusive.csv`, `serial-exclusive.csv`,
  `workers8-inclusive.csv`, `workers8-exclusive.csv`.
- `expanded-summary.json`, `analysis-audit.json`,
  `trace-integrity.log` and `*-frequency.txt`.
- `parse_samples.py`, `resolve_symbols.cpp`, `expand_samples.py`,
  `audit_analysis.py` and `make_profile_report.py`: analysis sources.
- `final-debug-interpreter.log`, `final-debug-vm.log`,
  `marker-gate-serial.log`, `marker-gate-workers8.log`,
  `final-site-check.log` and `final-gloom-debug.log`.
- `comparison-source-provenance.json` and the PDB source-file queries.

Example exports, with the matching PDB search path and an ignored symcache:

```text
xperf -i capture.etl -o markers.csv -a dumper -provider "{744b3f90-7d83-4b7b-a861-40637623c592}" -add_rawdata
xperf -i capture.etl -symbols -o samples.csv -a dumper -stacktimeshifting
```

Astra's changes are limited to comparison/capture tooling and reports.
Protected runtime files, reference scripts, the JaiScript adapter, shared
build directories and git worktrees were not edited by this task.
