# Structural technique controls, 2026-09-09

These controls measure small execution mechanisms on frozen JaiScript 68aeae4d,
Lua 5.4.8, LuaJIT 2.1 JIT with JIT disabled, and a C++ switch interpreter
implementing only these primitive operations. They are not GLOOM timings or whole-program speed ceilings.

## Results

One pinned P core. Values are nanoseconds per loop iteration. Each process
warms 65,536 iterations, then averages eight invocations of 4,000,000 iterations.
Three ABAB rounds yield six accepted processes per layout and configuration.
Every invocation checks its result. The padding band is the observed range of
paired padded/normal deltas, not a confidence interval.

| Gauge | Runtime | Normal ns/iteration | Padding ns/iteration | Padding band |
|---|---|---:|---:|---:|
| Arithmetic/compare | JaiScript VM | 22.121 | 22.923 | +2.10% to +4.45% |
| Arithmetic/compare | Lua 5.4.8 | 7.040 | 7.015 | -1.09% to +0.60% |
| Arithmetic/compare | LuaJIT 2.1 JIT, disabled | 4.142 | 4.166 | -0.35% to +2.04% |
| Arithmetic/compare | C++ switch control | 3.934 | 3.915 | -2.21% to +0.84% |
| Array expression | JaiScript VM | 66.086 | 67.871 | +2.32% to +2.98% |
| Array expression | Lua 5.4.8 | 13.570 | 13.557 | -5.73% to +0.62% |
| Array expression | LuaJIT 2.1 JIT, disabled | 14.879 | 15.637 | -8.11% to +25.98% |
| Array expression | C++ switch control | 9.083 | 9.099 | -13.68% to +17.22% |
| Array read into typed local | JaiScript VM | 43.492 | 43.901 | -1.11% to +1.46% |
| Array read into typed local | Lua 5.4.8 | 13.575 | 13.639 | -1.43% to +6.93% |
| Array read into typed local | LuaJIT 2.1 JIT, disabled | 16.126 | 15.882 | -18.43% to +9.41% |
| Array read into typed local | C++ switch control | 9.715 | 9.129 | -13.97% to +15.52% |
| Script-call loop | JaiScript VM | 88.840 | 88.160 | -1.30% to -0.49% |
| Script-call loop | Lua 5.4.8 | 16.450 | 16.011 | -9.65% to +12.30% |
| Script-call loop | LuaJIT 2.1 JIT, disabled | 7.304 | 7.249 | -3.63% to +0.90% |
| Script-call loop | C++ switch control | 3.696 | 3.702 | -0.29% to +1.15% |

## Instruction counts and code generation

Counts below cover one 4,000,000-iteration invocation. VM counts come from a
separate VMPROF build with counters reset after warm-up; Lua counts come from
per-state count hooks installed only for the measured invocation. The hooked
and instrumented times are excluded from the timing table.

| Gauge | JaiScript recorded opcodes | Lua 5.4 instructions | LuaJIT JIT-disabled instructions |
|---|---:|---:|---:|
| Arithmetic/compare | 24,000,003 | 24,000,004 | 28,000,004 |
| Array expression | 32,032,776 | 36,016,394 | 40,016,393 |
| Array read into typed local | 36,032,776 | 36,016,394 | 40,016,393 |
| Script-call loop | 40,000,003 | 40,000,004 | 44,000,004 |

The arithmetic and call loops have essentially the same recorded instruction
counts in JaiScript and Lua 5.4. Their normal/padded elapsed ratios are
3.14/3.27 for arithmetic and 5.40/5.51 for the call loop. These compare complete
patterns, including operand handling and callee computation, not isolated
dispatch or call-frame costs.

The original array expression is `acc = acc + data[idx]`. On JaiScript it uses
general fused binary/store opcodes, including their runtime fast paths. The second form reads
`int value = data[idx]` before adding it to `acc`; it records 4,000,000 T_LOAD
executions. That form is 34.19% faster normally and 35.32% faster with padding,
despite executing more VM instructions. Lua 5.4 records the same instruction
count for both forms. The store RHS `i % 17` still uses a general producer in
JaiScript, so the declaration form is a typed-read control, not an all-typed loop.

This is evidence for improving compiler coverage of typed expressions. It is
not a recommendation to require extra source declarations or to change var.
Fable identifies the missing path as a typed element read landing in a compiler
temporary. Its GLOOM coverage is limited; JaiDOOM needs its own census.

## Controls and limitations

- The common arithmetic stays in an exact integer range below 2^53. Lua 5.4
  receives integer arguments; LuaJIT JIT is explicitly disabled and asserted off.
  No bitwise-library call is substituted for a native bitwise instruction.
- The C++ control reads a 16-byte instruction stream from disk. Inspection of
  the linked executable found the runSwitch body, its instruction fetch, indirect
  dispatch, back edges and integer division. The loop was not folded away. This
  control omits objects, dynamic types, engine ownership, general error handling
  and a complete language call convention. Its timings cannot be shipping claims.
- Array setup is inside each invocation. The C++ control initializes a native
  array directly; the languages initialize their own arrays/tables. Counts and
  timings include each language's setup and call overhead.
- Large bands remain in the LuaJIT JIT-disabled and C++ array controls, and in
  Lua 5.4's call control. Small differences involving those rows are not reliable.
  The two C++ array controls execute identical instruction streams; their
  differing medians demonstrate the unresolved variation.
- Ratios on these patterns do not predict the saving available on GLOOM. They
  support measuring value handling, compiler coverage and call machinery.
- The background allowance is 1.0 core, matching the campaign. CPU accounting
  is quantized; small controls remain more sensitive to screening and layout
  than whole-program runs. Both layouts and all excluded attempts are retained.

## Verification and evidence

Verified by running: 32 small-input checks across four runtimes, two layouts
and four patterns, plus four JaiScript interpreter-oracle checks; 225 timing processes, all exact
results, with 192 accepted and 33 excluded; independent arithmetic result
formula checks; instruction-count captures; Lua bytecode listings and linked
C++ disassembly. The audit checks accepted order, screening, medians, bands
and unchanged executable/script/source hashes.

Read/source-audited: the declaration-only T_LOAD lowering, source equivalence
of the two array forms, C++ control limitations and emitted instruction paths.
No native CPU-time attribution is inferred from these data.

Raw paths relative to Source/JaiScript:

- out/astra/structural-gauges-20260909/timings/{audit,metadata,runs,summary}.json
- out/astra/structural-gauges-20260909/timings/*.log
- out/astra/structural-gauges-20260909/{gauge.cpp,gauges.jai,gauges.lua,*.ops,CMakeLists.txt}
- out/astra/structural-gauges-20260909/{run.py,verify.py,count-instructions.py}
- out/astra/structural-gauges-20260909/instruction-counts/{counts.json,*.out,*.err}
- out/astra/structural-gauges-20260909/{lua54-bytecode.txt,luajit-jit-off-bytecode.txt}
- out/astra/structural-gauges-20260909/{native-dispatch-identity.json,native-dispatch-assembly.txt}
- out/astra/structural-gauges-20260909/{build,build-profile}/compile_commands.json

The runner copy is preserved at completion. The timing driver records binary,
script and C++ source hashes before timing and checks them again at completion.
