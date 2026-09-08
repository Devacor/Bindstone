# Script opcode profiles and accounting audit, 2026-09-08

Corrected captures and accounting controls: [2026-09-08 refresh](SCRIPT_PROFILE_REFRESH.md).

Three captures of each workload identify stable rendering hotspots. The initial
per-function profiler does not yet provide disjoint script self time: coroutine
reentry is charged twice, and host/coroutine entry accounting is incomplete.
The tables below rank recorded opcode charges. They are not percentages of
uninstrumented CPU time or predicted optimization gains.

Runtime source: `4922f220`, including the typed scalar opcode set. The only
capture-driver overlays reset GLOOM before the named frame and reset the
standalone runner before its repeated `main()`. Release/MSVC build with
`/DJAISCRIPT_VM_PROFILE`, `/O2 /Ob2 /Oi /Ot /GL /Zi /MT`; one P core
(CPU 0 on the Core Ultra 9 285HX). No debugger was attached.

GLOOM runs 600 warm-up ticks, reboots in the same engine, then executes the
600-tick reference with `--profile-reset-tick 0`. The state-hash and summary
script calls after the loop also appear in the dump. JaiDOOM executes one full
warm-up replay and profiles the second `main()`, including its level/demo
setup. The game-loop timing reported by the script remains a narrower interval.

## Recorded opcode-charge rankings

Each share is the median of three captures. Ranges describe those captures,
not a padding experiment or confidence interval. Operation counts repeat
exactly across all three runs. Parent VM reports are selected separately from
JaiDOOM's startup worker reports.

### GLOOM, 600 measured frames

| Function | File | Median share | Observed share range | Operations/window |
|---|---|---:|---|---:|
| gloom_row_quad | render.jai | 28.12% | 28.03% to 28.20% | 26,697,768 |
| render_view_impl | render.jai | 22.12% | 22.06% to 22.14% | 22,696,508 |
| gloom_ray_chunk_step | pure.jai | 12.07% | 12.00% to 12.10% | 9,705,432 |
| draw_billboard | render.jai | 10.33% | 10.29% to 10.55% | 11,044,774 |
| gather_input | game.jai | 3.75% | 3.72% to 3.78% | 6,000 |
| update | particles.jai | 3.32% | 3.28% to 3.33% | 1,025,976 |
| gloom_particle_step | pure.jai | 2.34% | 2.32% to 2.34% | 1,134,270 |
| accumulate_hash | game.jai | 1.87% | 1.87% to 1.90% | 1,423,926 |
| tick | enemies.jai | 1.71% | 1.70% to 1.72% | 46,305 |
| tile_at | maps.jai | 1.36% | 1.36% to 1.37% | 1,098,384 |
| build_strip | render.jai | 1.27% | 1.27% to 1.28% | 592,676 |
| hud_panel_rows | hud.jai | 1.26% | 1.25% to 1.42% | 341,416 |

### JaiDOOM, 3984 measured tics

| Function | File | Median share | Observed share range | Operations/window |
|---|---|---:|---|---:|
| draw_tex_col | render.jai | 19.54% | 19.53% to 19.56% | 298,114,747 |
| draw_flat_col | render.jai | 18.17% | 18.17% to 18.22% | 321,659,513 |
| draw_seg | render.jai | 14.59% | 14.59% to 14.60% | 190,510,743 |
| vid_row_half | vid.jai | 10.40% | 10.39% to 10.44% | 181,043,703 |
| draw_patch | hud.jai | 7.67% | 7.67% to 7.67% | 122,293,601 |
| draw_sprite | render.jai | 3.54% | 3.52% to 3.55% | 33,041,740 |
| pix_clear | vid.jai | 3.28% | 3.28% to 3.29% | 74,596,416 |
| trace_block_dz | things.jai | 2.73% | 2.72% to 2.75% | 59,906,471 |
| monster_tic | things.jai | 2.46% | 2.44% to 2.46% | 103,055 |
| draw_things | render.jai | 1.71% | 1.70% to 1.71% | 23,769,242 |
| draw_psprite | render.jai | 1.68% | 1.68% to 1.69% | 25,987,312 |
| light_band | render.jai | 1.30% | 1.30% to 1.31% | 15,339,109 |


## First common optimization target

JaiDOOM's `draw_tex_col` hot line is:

```jai
pix[y * VW + x] = palcm[pcm + tp[base + iv]];
```

Its `draw_flat_col` combines a strided pixel store with nested palette and
texture reads. GLOOM's `render_view_impl` reads `strip[tacc >> 11]`,
stores pixels and advances the row; `draw_billboard` performs
`pix[y * vw + x] = lut[code]`. These are the same broad numeric-buffer pattern
in both games.

A useful experiment is a proven or guarded numeric loop that keeps index
arithmetic, reads and stores unboxed through the region. Buffer identity,
aliasing, operation/error order and callback/debugger boundaries still apply.
GLOOM's separate row-assembly hotspot includes repeated pixel comparisons,
`parts.push(" ")` and string joining; it warrants its own measured increment.

## Accounting controls

### Coroutine reentry is charged to parent and child

`control-direct.jai` and `control-coroutine.jai` execute the same
100000-iteration integer loop. The direct function reports 200004 child
operations and 99.95% of recorded charges; its four-operation parent has 0.03%.

The resumed coroutine still reports 200004 child operations, now at 49.93%.
Its seven-operation parent also receives 50.05%. Both rows record approximately
the same cycle charge, and the total approximately doubles. The caller's
`resume()` opcode contains nested VM execution that is also timed in the child.
Thus the printed “callee bodies excluded” description does not hold for this path.

### Host and coroutine entry/inclusive columns are incomplete

The reset control invokes a helper 64 times per `main()`. After warm-up,
the report retains exactly 64 helper entries, proving the reset scope. Its
host-entered `main` nevertheless has nonzero work with entries/inclusive equal
to zero. GLOOM's `gloom_frame` behaves the same way; the internal
`run_tick` correctly reports 600 entries. The coroutine child also has zero
entry/inclusive columns despite executing its loop.

Some reported opcode charges exceed their function's inclusive frame time.
Return machinery and frame timing use different endpoints. These columns should
not be treated as nested wall/CPU intervals.

### Worker reports and auxiliary counters have different scope

Worker backends print the same “main engine” heading. JaiDOOM level setup runs a
parallel transform, and the parent reset does not clear its worker counters.
The analyzer selects the unique parent report containing `draw_tex_col` and
excludes the worker reports from its table.

Reset clears function/IP and opcode totals; it does not clear every auxiliary
environment, scope or call-path counter. Such counters may include warm-up.
They are not used in the measured-window tables above.

## Verification and evidence

All three GLOOM captures match frame `dfd969ed5a336dca` and state
`1503537018`. Both replays in each JaiDOOM capture match `1746772898`.
The one-frame GLOOM control retains its first named frame; the helper-call
control retains only the measured replay. Both ordinary Debug suites passed
116 suites / 2749 tests, and both 300-tick GLOOM hashes remain
`7fecc09815bb64e0` / `3580805725`.

Raw root: `Source/JaiScript/out/astra/script-profile-20260908/`.

- `source-inputs.json`, `tree/`, `configure.log`, `build.log`
  and `release/`: fixed inputs, flags, executables and symbols.
- `captures.json`, `gloom-1` through `gloom-3` and
  `timedemo-1` through `timedemo-3` `.log/.err` files: hashes and
  original profiler dumps.
- `parsed-profiles.json`, `summary.json` and `analyze.py`:
  selected parent reports, repeated counts, shares and hot source lines.
- `scope-check.jai/.err`, `gloom-scope-check.err`,
  `control-direct.jai/.err` and `control-coroutine.jai/.err`:
  reset and accounting counterexamples.
- `ACCOUNTING_LIMITS.md`: detailed findings sent to Fable.
- `debug-interpreter.log`, `debug-vm.log` and `debug-gloom-300.log`:
  ordinary Debug gates for the capture-driver changes.

Verified by running: hashes, repeatable operation counts, reset boundaries and
the accounting counterexamples. Inferred from source shapes: the shared numeric
grid/column-loop opportunity. Untrusted as performance results: all instrumented
elapsed times, the initial profiler's purported disjoint self percentages and
its incomplete inclusive/entry fields. Use the matched comparison snapshot for
ordinary timings and padding bands.
