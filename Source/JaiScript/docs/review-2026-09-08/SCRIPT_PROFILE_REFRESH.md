# Script profile refresh: 2026-09-08

Runtime source `c85ea0dd5fce58b620c0dee9464567168bd73ebb`. This contains
typed scalar operations, typed loads, both nested-dispatch accounting repairs,
and the native-property getter replay correction. The only source overlay is
the separately gated growing-getter regression test; tests are disabled in the
profiling build.

These tables report instrumented TSC opcode self charge and active frame spans.
They are diagnostic attribution, with three-capture ranges. They are not native
CPU samples or uninstrumented performance deltas; no padding control applies
to these instrumentation counters. The paired performance report carries the
normal/padding measurements separately.

## Scope and controls

MSVC 19.51.36252 Release, C++20, `/O2 /Ob2 /Oi /Ot /GL /Zi /MT`,
`/LTCG /DEBUG:FULL` and `/DJAISCRIPT_VM_PROFILE`. CPU affinity is P CPU 0
on the Core Ultra 9 285HX. No debugger is attached.

Each GLOOM capture warms 600 ticks, resets the same runtime, then profiles the
600-tick reference, seed 666, 100x40 quad output. The reset occurs before
measured frame zero. Each JaiDOOM capture warms a complete replay and resets
profiling before the second main(). Its profile includes level/demo setup in
that main call; its reported ms/tic timer excludes that setup.

Three captures per workload pass the required hashes. GLOOM run_tick has exactly
600 entries. Every reported opcode count is identical across the three captures
of each workload. Reported function identities are unambiguous and repeat in all
three captures. Parent VM reports are selected explicitly; startup-worker
reports are excluded.

Controls pass: exactly 64 measured helper entries, host-entered frame counts,
single and deeper coroutine/native reentry, retained independently measured
native self work, and exclusion of a 200 ms pause from a suspended coroutine's
inclusive span. Coroutine entries count resume activations. Inclusive spans
count nested activations again, including recursion. Call/return bookkeeping is
charged at opcode boundaries. For in-loop calls, the inclusive timer stops at
the start of frame cleanup; opcode self charge includes that cleanup. Some self
totals therefore exceed the corresponding inclusive totals. The columns use
different endpoints and should be read with that convention.

The runtime prints the 30 functions ranked by self charge. Inclusive columns
below belong to those same functions; the output does not provide a complete
ranking by inclusive time. Auxiliary scope, producer and binding counters retain
warmup and are excluded from measured-window counts here.

## GLOOM, 600 measured ticks

| Function | File | Self Mcycles | Self share (median; range) | Inclusive Mcycles | Opcodes | Entries |
|---|---|---:|---|---:|---:|---:|
| `gloom_row_quad` | render.jai | 1170.8 | 29.20%; 29.15% to 29.38% | 1153.5 | 26697768 | 20128 |
| `render_view_impl` | render.jai | 939.3 | 23.46%; 23.42% to 23.49% | 3276.6 | 22696508 | 592 |
| `gloom_ray_chunk_step` | pure.jai | 522.0 | 13.03%; 12.86% to 13.06% | 522.5 | 9705432 | 8880 |
| `draw_billboard` | render.jai | 446.3 | 11.14%; 11.13% to 11.18% | 466.6 | 11044774 | 4017 |
| `update` | particles.jai | 141.4 | 3.53%; 3.52% to 3.54% | 238.1 | 1025976 | 591 |
| `gloom_particle_step` | pure.jai | 97.1 | 2.42%; 2.41% to 2.43% | 104.6 | 1134270 | 170208 |
| `accumulate_hash` | game.jai | 78.5 | 1.96%; 1.96% to 1.97% | 85.8 | 1423926 | 600 |
| `tile_at` | maps.jai | 58.3 | 1.46%; 1.45% to 1.46% | 57.7 | 1098384 | 39228 |
| `build_strip` | render.jai | 54.6 | 1.36%; 1.36% to 1.38% | 83.1 | 592676 | 1388 |
| `hud_panel_rows` | hud.jai | 52.9 | 1.32%; 1.32% to 1.32% | 80.1 | 341416 | 592 |
| `collect_billboard` | render.jai | 48.7 | 1.22%; 1.21% to 1.22% | 71.9 | 338412 | 9472 |
| `los_clear` | combat.jai | 39.0 | 0.97%; 0.96% to 0.98% | 88.2 | 409313 | 1862 |
| `bfs_field` | game.jai | 36.8 | 0.92%; 0.89% to 0.93% | 36.6 | 437797 | 11 |
| `tex_tone` | render.jai | 28.5 | 0.71%; 0.71% to 0.71% | 28.1 | 603550 | 44416 |
| `pad_to` | util.jai | 21.8 | 0.54%; 0.54% to 0.54% | 22.5 | 178528 | 12752 |
| `pilot_brain` | game.jai | 21.4 | 0.53%; 0.53% to 0.53% | 156.6 | 171279 | 600 |
| `rows_quad` | render.jai | 19.3 | 0.48%; 0.48% to 0.49% | 1190.1 | 145040 | 592 |
| `sprite_lut` | render.jai | 18.2 | 0.45%; 0.45% to 0.46% | 20.3 | 58471 | 4017 |
| `brain_grunt` | enemies.jai | 15.7 | 0.39%; 0.39% to 0.39% | 63.5 | 81856 | 3045 |
| `spot_free` | sim.jai | 14.0 | 0.35%; 0.35% to 0.35% | 33.1 | 105636 | 3858 |
| `Billboard` | render.jai | 14.1 | 0.35%; 0.35% to 0.35% | 15.9 | 52221 | 4017 |
| `run_tick` | game.jai | 11.8 | 0.30%; 0.29% to 0.30% | 617.5 | 49011 | 600 |
| `tile_solid` | maps.jai | 11.2 | 0.28%; 0.28% to 0.28% | 10.3 | 76938 | 38469 |
| `enemy_art` | enemies.jai | 11.1 | 0.28%; 0.28% to 0.28% | 10.6 | 142615 | 4144 |
| `hud_status_row` | hud.jai | 9.6 | 0.24%; 0.24% to 0.24% | 10.9 | 52638 | 592 |
| `render_play` | hud.jai | 8.5 | 0.21%; 0.21% to 0.22% | 3377.8 | 24272 | 592 |
| `tick` | enemies.jai | 8.0 | 0.20%; 0.20% to 0.31% | 71.3 | 46305 | 4137 |
| `pilot_input` | game.jai | 8.1 | 0.20%; 0.20% to 0.20% | 12.9 | 7800 | 600 |
| `build_render_tables` | render.jai | 7.5 | 0.19%; 0.18% to 0.19% | 13.6 | 143319 | 1 |
| `mix32` | util.jai | 7.3 | 0.18%; 0.18% to 0.18% | 7.2 | 64800 | 7200 |

## JaiDOOM, one measured main replay

| Function | File | Self Mcycles | Self share (median; range) | Inclusive Mcycles | Opcodes | Entries |
|---|---|---:|---|---:|---:|---:|
| `draw_tex_col` | render.jai | 17653.3 | 20.62%; 20.59% to 20.63% | 17690.5 | 298114747 | 800447 |
| `draw_flat_col` | render.jai | 15844.3 | 18.52%; 18.51% to 18.53% | 15890.4 | 321659513 | 994706 |
| `draw_seg` | render.jai | 12842.1 | 15.01%; 14.95% to 15.01% | 47818.8 | 190510743 | 773210 |
| `vid_row_half` | vid.jai | 8897.4 | 10.39%; 10.38% to 10.45% | 8882.3 | 181043703 | 155376 |
| `draw_patch` | hud.jai | 6771.1 | 7.91%; 7.89% to 7.91% | 6860.0 | 122293601 | 116893 |
| `draw_sprite` | render.jai | 3069.6 | 3.59%; 3.58% to 3.59% | 5767.9 | 33041740 | 158673 |
| `pix_clear` | vid.jai | 2891.5 | 3.38%; 3.37% to 3.38% | 2891.7 | 74596416 | 3984 |
| `trace_block_dz` | things.jai | 2352.9 | 2.75%; 2.74% to 2.75% | 2353.2 | 59906471 | 2523 |
| `draw_things` | render.jai | 1477.8 | 1.72%; 1.72% to 1.74% | 8409.2 | 23769242 | 3984 |
| `draw_psprite` | render.jai | 1463.2 | 1.71%; 1.71% to 1.71% | 1462.6 | 25987312 | 4068 |
| `light_band` | render.jai | 1114.7 | 1.30%; 1.30% to 1.31% | 1112.7 | 15339109 | 1393988 |
| `draw_masked` | render.jai | 1092.4 | 1.27%; 1.27% to 1.31% | 1121.0 | 19339269 | 3457 |
| `sector_at` | render.jai | 1041.8 | 1.22%; 1.21% to 1.22% | 1744.8 | 11050620 | 87912 |
| `bsp_node` | render.jai | 981.4 | 1.15%; 1.15% to 1.15% | 492659.9 | 7047677 | 528844 |
| `sprite_lookup` | things.jai | 973.6 | 1.14%; 1.13% to 1.14% | 1004.9 | 3220310 | 162810 |
| `point_side` | render.jai | 923.0 | 1.08%; 1.07% to 1.08% | 906.4 | 8641735 | 1019317 |
| `bsp_subsector` | render.jai | 898.2 | 1.05%; 1.05% to 1.05% | 48703.0 | 5918310 | 255810 |
| `mobj_pos_ok` | things.jai | 544.5 | 0.64%; 0.64% to 0.64% | 544.8 | 13818970 | 977 |
| `bar_patch` | hud.jai | 531.2 | 0.62%; 0.62% to 0.63% | 7424.9 | 3387270 | 112909 |
| `sprite_prepare` | things.jai | 524.4 | 0.61%; 0.61% to 0.62% | 569.7 | 4644470 | 469596 |
| `lump_num` | wad.jai | 414.3 | 0.48%; 0.48% to 0.49% | 426.7 | 1144679 | 190715 |
| `draw_sky_col` | render.jai | 368.4 | 0.43%; 0.43% to 0.43% | 366.4 | 6220117 | 48907 |
| `blit_half` | vid.jai | 350.4 | 0.41%; 0.40% to 0.41% | 9250.5 | 1111536 | 3984 |
| `bar_num` | hud.jai | 303.4 | 0.35%; 0.35% to 0.35% | 2127.8 | 1825368 | 35856 |
| `render_view` | render.jai | 247.9 | 0.29%; 0.29% to 0.29% | 61496.5 | 4553712 | 3984 |
| `things_tic` | things.jai | 236.4 | 0.28%; 0.28% to 0.28% | 3289.0 | 5985114 | 3984 |
| `tex_warm_map` | render.jai | 223.8 | 0.26%; 0.26% to 0.26% | 224.4 | 14100 | 1 |
| `pos_ok_scan` | game.jai | 197.3 | 0.23%; 0.23% to 0.23% | 284.1 | 3234163 | 4798 |
| `pickup_touch` | game.jai | 161.5 | 0.19%; 0.19% to 0.19% | 162.5 | 2630013 | 3984 |
| `main` | p10_timedemo.jai | 82.7 | 0.10%; 0.10% to 0.10% | 85593.1 | 235815 | 1 |

## Opcode attribution and count units

| Workload | Opcode | Self share (median; range) | Count | Count / tick or tic |
|---|---|---|---:|---:|
| gloom | `T_LOAD` | 15.84%; 15.79% to 15.88% | 11583674 | 19306.12 |
| gloom | `INDEX_STORE_FUSED` | 6.79%; 6.76% to 6.85% | 4895397 | 8158.99 |
| gloom | `DECL_VAR` | 6.33%; 6.30% to 6.41% | 1954025 | 3256.71 |
| gloom | `T_ADD_II` | 6.01%; 6.00% to 6.06% | 7828334 | 13047.22 |
| gloom | `T_JEQ_II` | 4.72%; 4.65% to 4.74% | 7657328 | 12762.21 |
| gloom | `CALL_FROM_SCRATCH` | 4.65%; 4.65% to 4.67% | 417902 | 696.50 |
| gloom | `CALL_METHOD` | 3.89%; 3.87% to 4.01% | 201609 | 336.01 |
| gloom | `LOAD` | 3.78%; 3.76% to 3.79% | 3593089 | 5988.48 |
| gloom | `T_MUL_II` | 3.24%; 3.24% to 3.26% | 4152424 | 6920.71 |
| gloom | `T_PUSH` | 2.98%; 2.94% to 3.05% | 1710109 | 2850.18 |
| timedemo | `INDEX_STORE_FUSED` | 8.36%; 8.35% to 8.39% | 90792140 | 22789.19 |
| timedemo | `INDEX_FUSED` | 6.69%; 6.68% to 6.71% | 72927188 | 18305.02 |
| timedemo | `LOAD` | 6.55%; 6.52% to 6.55% | 144798615 | 36345.03 |
| timedemo | `BINARY_FUSED_STORE` | 5.76%; 5.76% to 5.76% | 24005225 | 6025.41 |
| timedemo | `BINARY` | 5.70%; 5.69% to 5.70% | 83646742 | 20995.67 |
| timedemo | `DECL_VAR` | 5.52%; 5.52% to 5.55% | 43567604 | 10935.64 |
| timedemo | `BINARY_FUSED_DECL` | 5.19%; 5.19% to 5.19% | 59486824 | 14931.43 |
| timedemo | `CALL_FROM_SCRATCH` | 4.96%; 4.96% to 4.99% | 7871337 | 1975.74 |
| timedemo | `INDEX` | 3.80%; 3.79% to 3.80% | 58325602 | 14639.96 |
| timedemo | `T_LOAD` | 3.79%; 3.79% to 3.79% | 49641337 | 12460.17 |

Counts are divided by 600 for GLOOM and 3984 for JaiDOOM. JaiDOOM's numerator
also includes measured-main setup, as described above. The earlier estimate of
about 667k GET_MEMBER-to-DECL_VAR pairs is a whole GLOOM run estimate, about 1.1k
per frame. The auxiliary producer counter is cumulative and cannot provide an
exact reset-window pair count.

## Hot source locations

GLOOM sources are under `examples/gloom/scripts/`; JaiDOOM sources are under
`examples/jaidoom/scripts/`. Line numbers refer to the frozen c85ea0dd workload.

| Workload / function | Source lines | Shape |
|---|---|---|
| gloom / `gloom_row_quad` | render.jai: 532, 534, 577, 529, 528, 531 | Four pixel comparisons; blank-fragment push; row-fragment join |
| gloom / `render_view_impl` | render.jai: 367, 370, 366, 365, 369, 368 | Strip read; two pixel stores; texture/pixel/loop increments |
| gloom / `gloom_ray_chunk_step` | pure.jai: 50, 51, 47, 46, 61, 53 | Packed grid read; shift/mask unpack; ray-step arithmetic |
| gloom / `draw_billboard` | render.jai: 728, 727, 726, 725, 699, 698 | Sprite texel read; palette lookup and pixel store |
| gloom / `update` | particles.jai: 64, 63, 66 | Serial by-reference particle iteration and step calls |
| timedemo / `draw_tex_col` | render.jai: 200, 201, 198, 197, 199, 195 | Texture/palette reads and pixel store; vertical texture increment |
| timedemo / `draw_flat_col` | render.jai: 221, 218, 216, 217, 213, 215 | Flat/palette reads and pixel store; coordinate and light-band arithmetic |
| timedemo / `draw_seg` | render.jai: 375, 398, 407, 374, 378, 377 | Light-band calculation and calls to flat/texture column painters |
| timedemo / `vid_row_half` | vid.jai: 207, 208, 209, 205, 206, 211 | Foreground/background palette lookup and half-block row append |
| timedemo / `draw_patch` | hud.jai: 52, 53, 51, 50, 47, 48 | Patch texel read, clipping and palette/pixel write |

GLOOM's largest self rows remain the quad-row builder, wall painter, ray step and
billboard painter. JaiDOOM remains dominated by texture/flat column rendering
and segment traversal. The first common target is the numeric grid/column loop:
index arithmetic, buffer loads, palette lookup and pixel stores. Keeping proven
values unboxed across that sequence can remove work from both games without a
syntax change. Aliasing, getter/operator effects, overflow, bounds and evaluation
order remain part of the contract. Check whether a reference can invoke a getter
before evaluating it, and route those references through the ordinary read path.
The growing-getter regression shows why speculative evaluation followed by replay
can change a bounds error into a value.

The row builder's string-array pushes and joins are a separate GLOOM target.
The generic typed element-store descriptor experiment was measured by Fable and
reverted as flat; another descriptor-only change needs evidence before priority
over the raw numeric work.

## Comparison with the earlier attribution

The earlier `4922f220` capture double-charged coroutine bodies to their resume
callers. The corrected build also contains additional typed operations. Changes
in percentage between those captures combine accounting and implementation
changes; they do not measure speed.

The rendering order is stable. GLOOM's four leading rows now have median shares
of 29.20%, 23.46%, 13.03% and 11.14%, with ranges in the table. JaiDOOM's leading
column rows have 20.62% and 18.52%. Coroutine-resuming caller charges are no
longer evidence of duplicated child work. Whole-program gains are established
by the separate uninstrumented, padded comparisons.

## Evidence

Raw root: `Source/JaiScript/out/astra/getter-fixed-20260908/`.

- `captures.json` and `gloom-1..3` / `timedemo-1..3` logs and stderr.
- `parsed-profiles.json`, `summary.json`, `opcode-profiles.json`,
  `opcode-summary.json`, `profile-audit.json` and
  `unmatched-profile-rows.json`; `analyze.py` and `analyze-opcodes.py` reproduce
  the tables, with CSV exports for further analysis.
- `verification-audit.json`: full Debug gates, 64 getter probes and the
  accounting controls; `native-*.log/.err` preserve the independent TSC bracket.
- `source-inputs.json`, `regression-final-overlay.json` and
  `profile/`: source identities, test overlay and profiling build.
- Earlier failed accounting controls remain in
  `../script-profile-20260908/` and `../profile-fixed-20260908/`.
- Getter replay counterexamples remain in `../getter-boundary-20260908/`.

Verified by running: scopes, controls, hashes, repeated opcode counts and tables.
Inferred from the source shapes: the proposed numeric-region target. No speed
gain is inferred from instrumentation time, and no worker self-time attribution
is claimed by these parent-VM tables.
