# JaiDOOM remaining general operations at row 102

The warmed row 102 census attributes every `LOAD`, `INDEX`, `BINARY`, `INDEX_FUSED`, `BINARY_FUSED` and `MATH` instruction to a script function. `draw_flat_col` accounts for 87.13% of general binary operations and 67.14% of math calls. It is the largest concentrated target after the texture-column work.

This is an instruction-count attribution, not native CPU attribution. Counts do not establish which operation consumes the most uninstrumented time.

## Per-function attribution

Counts below are executions per measured tic, over the 3984-tic replay. The raw JSON retains integer counts and every function. `BINARY` and `BINARY_FUSED` are separate opcode families; `INDEX` and `INDEX_FUSED` are also separate.

| Function | LOAD | INDEX | BINARY | INDEX_FUSED | BINARY_FUSED | MATH |
|---|---:|---:|---:|---:|---:|---:|
| draw_flat_col | 11,105.2 | 4,442.1 | 13,326.3 | 0.0 | 4,442.1 | 6,663.1 |
| draw_tex_col | 5,578.4 | 5,578.4 | 0.0 | 0.0 | 5,578.4 | 0.0 |
| draw_seg | 6,935.0 | 903.2 | 322.5 | 1,029.0 | 2,106.5 | 1,701.3 |
| draw_patch | 2,039.4 | 2,010.0 | 0.0 | 29.3 | 4,312.4 | 0.0 |
| vid_row_half | 39.0 | 0.0 | 0.0 | 5,579.6 | 39.0 | 0.0 |
| trace_block_dz | 1,530.3 | 1,221.3 | 300.8 | 1,215.3 | 469.0 | 0.0 |
| light_band | 349.9 | 0.0 | 699.8 | 0.0 | 699.8 | 349.9 |
| All other functions | 3,026.8 | 364.4 | 645.8 | 1,206.9 | 2,950.1 | 1,210.1 |
| **Total** | 30,604.1 | 14,519.4 | 15,295.2 | 9,060.0 | 20,597.3 | 9,924.4 |

All six per-IP sums equal the main engine's opcode totals exactly, including replay setup. The complete replay counts are 121,926,872 LOAD; 57,845,481 INDEX; 60,936,005 BINARY; 36,095,212 INDEX_FUSED; 82,059,628 BINARY_FUSED; and 39,538,853 MATH. They match Fable's independently captured row 102 totals.

## What the LOAD total means

Of 121,926,872 LOAD instructions, 108,096,075 have a frame-slot operand and 13,830,797 have no direct slot. The latter require the name-resolution path. A slot-capable instruction can still fall back to the environment at runtime; the static shape is not a measured cache-hit rate.

The largest named slot loads include `palcm` (31,073,159), `flat`, `bandbase`, `wx` and `wy` (8,848,636 each), and `tp` (8,008,008). The no-direct-slot group includes outer arrays in nested index expressions, such as the four `VX_X`/`VX_Y` accesses in `trace_block_dz` (1,198,425 each). It cannot all be labeled scalar global traffic.

Conversely, accesses to `VW`, `CENTERY` and `VIEW_*` inside fused binary and index operations do not emit a standalone LOAD. A general environment-read optimization must count those operands separately. Neither 121.9 million nor 13.8 million is a count of all global reads.

## Next structural measurements

- `draw_tex_col` has one LOAD, one BINARY_FUSED and one INDEX per texture sample, 22,224,523 of each. Fable's row 103 specifically targets that triple; the row 102 census establishes its pre-change scope.
- `draw_flat_col` has three `math::itrunc` results per sample feeding stack-based operations. Across its loop it executes 53,091,816 BINARY and 26,545,908 MATH instructions. A measured lowering that carries these known integer results into scalar operands is a more concentrated next experiment than a claim based on all LOADs.
- `vid_row_half` accounts for 22,228,989 INDEX_FUSED operations returning palette strings. It should be assessed as string/palette traffic, with existing string interning and reuse preserved. Its instruction count alone gives no allocation count.

These are candidates for guarded lowering under the existing syntax. They do not justify changing `var` semantics, conversion rules or evaluation order.

The separate [LOAD cost controls](LOAD_COST_2026-09-10.md) measure stable environment reads and explain the limits of extrapolating their cost.

## Capture and verification

Astra built the exact `4bd53021231649906f5f021a96fe1dd42eb92655` library with `/DJAISCRIPT_VM_PROFILE` in `CMAKE_CXX_FLAGS`. An isolated copy of the runner host exports the already collected `profile_chunks_` instruction counts and main-engine opcode totals after replay. No runtime-owned source changed. The host reads copied census metadata and does not dereference possibly expired chunk pointers.

The invocation is `jaiscript.exe --no-debug --no-pause --backend=vm --benchmark-main 1 p10_timedemo.jai -- 120x78`. The runner completes a full warm-up, resets the main VM counters, then runs one measured `main()` replay. Warm-up and measured state hashes both equal `1746772898`, and the process exits zero. Worker-engine dumps are outside the explicitly delimited main-engine export and are not added to it.

An initial attempt supplied `-DJAISCRIPT_VM_PROFILE=ON` as a CMake option. The project does not consume that option, so the attempt produced no profile data. It was rejected and retained under `uninstrumented-attempt-*`; the corrected capture verifies the compiler define on every translation unit.

Native ETW attribution was not captured after the earlier canceled Windows elevation. No new elevation attempt was made during this work. Instrumented cycle charges and this capture's elapsed time are not published as native CPU shares or performance results.

## Evidence

All paths are relative to `Source/JaiScript/out/astra/row102-audit-20260909/`:

- `census/capture.json`, `census/doom.out`, `census/doom.err`: command, warm/measured hashes and complete capture.
- `census/attribution.json`, `census/ip-counts.json`, `census/load-sites.json`: complete counts, function shares and LOAD operand shapes.
- `parse-census.py`: delimiter parsing, total reconciliation and hash checks.
- `census/source-inputs.json`, `census/host-export-overlay.txt`, `build-census.ps1`, `census/release/compile_commands.json`: source, host-only export and build provenance.
- `load-gauge/`: separate uninstrumented LOAD/POP controls, instruction-shape proof and both-backend correctness checks. No load-cost timing result is established in this report.
