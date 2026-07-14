# JaiDOOM performance baseline — 2026-07-12

The pre-showcase yardstick (Dev ruling: baseline BEFORE the language-showcase pass; every
coroutine/class/parallel swap gets measured against this band).

## Environment
- Machine: MSI 285HX ("the MSI"), Windows 11 Pro
- Runner: `out/build/x64-Release BENCHMARKS/bin/jaiscript.exe` (VM backend, char-promotion
  rounds 1+2 in), scripts warm-jaibite
- Bench: `scripts/p7_bench.jai` — 300-frame autopilot play session, E1M1, 120x78 subpixels +
  status bar, 6 monsters awake and firing, doors/movement active, HUD+messages drawn

## Numbers (3 runs)
| run | ms/frame | sim | render | blit | worst frame |
|-----|----------|-----|--------|------|-------------|
| 1 | 9.66 | 3.55 | 5.31 | 0.81 | 39 |
| 2 | 9.60 | 3.51 | 5.28 | 0.80 | 39 |
| 3 | 9.81 | 3.62 | 5.41 | 0.79 | 39 |

**Band: 9.60–9.81 ms/frame vs the 28.5 ms (35fps) budget — 2.9x headroom.**

## Notes
- Worst-frame ~39 ms spikes are one-time lazy texture/sprite composites (`tex_prepare`/
  `sprite_prepare` on first sight), not steady-state; a map-start warm-up pass would flatten
  them if we ever care.
- `clock_ms` is integer-ms, so the sim/render/blit split is aggregate-accurate only.
- Scaling: a 200x140 terminal roughly triples pixel cost → ~25-28 ms/frame, borderline. The
  unpulled levers, in order: parallel_transform blit rows (GLOOM-proven ~2x on that stage),
  plane-fill dedup, sprite inner-loop int-ify, WallChunk-style parallel columns.
- Gate roster at baseline (all GREEN, run by hand 2026-07-12): p1_verify, jaidoom selftest,
  p4_smoke, p5_view, p6_specials, p6_hud, jaidoom flowtest, p7_bench.
- Blit-row body shape matters: GLOOM's parts.push + join row build cost +0.5 ms/frame over the
  `+=` row build at 120 cols (measured during the parallel-blit swap) — the shared serial/parallel
  body keeps `+=` on locals. p7_bench grew knobs: `-- 200x140` (size), `-- serial|parallel` (blit).

## Showcase-pass ledger (append measurements after each swap)
| change | ms/frame band | verdict |
|--------|---------------|---------|
| (baseline) | 9.60–9.81 | — |
| coroutine brains+movers | 9.60–9.86 | NEUTRAL (sim 3.51–3.57 vs 3.51–3.62 baseline; +0.05 top-of-band is render/blit noise) |
| mover+weapon classes | 9.55–9.76 | NEUTRAL (sim 3.51–3.58 vs 3.51–3.62 baseline; two contended runs 10.51/14.32 discarded — all three stages incl. untouched blit elevated proportionally = box noise) |
| parallel blit @120x78 | serial 9.66–9.73 / parallel 9.90–10.18 | NEGATIVE at play size (blit stage 0.79–0.85 serial vs 1.02–1.05 parallel — 39 rows can't amortize the region barrier; same-day serial control 9.75–10.03, box warm) → default BLIT_WORKERS=0, parallel path kept behind the knob |
| parallel blit @200x140 | serial 19.44–20.12 / parallel 19.59–19.73 | STAGE WIN, total wash (blit 2.13–2.28 → 1.84–1.92, −15%; render ~14 dominates at this size — the crossover is real but the lever is the renderer, not the blit) |
| parallel texture warm @map start | 9.73–10.05, worst 38–43 → 20 | WIN on load + spikes: E1M1's 33 wall textures composite in 137–144 ms serial → 79–86 ms parallel (~1.7x, `tex_composite` pure body); lazy first-sight composite spikes moved into load, bench worst frame halved (residual 20 ms = lazy sprite composites, untouched) |
| RE-MEASURE after the VM region-overhead kill (2026-07-12 eve, other session's per-slot snapshot cache) | serial 9.66–9.80 unchanged | blit STAGE now wins at BOTH sizes: 120x78 blit 0.85→0.58 (−33%, was a LOSS pre-fix) but whole-frame still favors serial (blit is 8% of frame; parallel runs carried elevated sim/render ~+1ms — worker-pool presence or box noise, unresolved); 200x140 blit 2.60→1.10 (−57%, was −15%) and total ~20.7 vs serial controls 21.0–23.2 → parallel blit is now a REAL win at big sizes. Default stays BLIT_WORKERS=0 at play size; flip it for 200x140+ terminals. Render (~15 of 21 ms) remains the big-size lever. |
| ROUND 2 complete (rockets/powerups/lights/gravity/positional audio/cheats/save-load/ARMS + demos/doom2/nightmare) | 10.07–10.66 (sim 3.73–3.95, render 5.49–5.78, blit 0.85–0.93, worst 20–23) | IN BAND vs the 9.6–10.7 pre-round yardstick, top-of-band honest: sim carries the round's real new per-tic work (lights_tic over the light-effect list, player gravity/air arcs, positional-audio tier/pan picks on every sound, cheat-buffer key path, plus round-2-final's per-tic DEMO_REC branch and nightmare corpse-respawn scan — the last two are one predicted branch each when off). Track-4 adds are pay-when-used: demo recording only while taping, respawn scan only touches state-6 mobjs under NIGHTMARE, the HUD face's RNG became a cheaper local LCG (demo determinism fix). Worst-frame halved vs baseline (20–23 vs 39, the texture warm from round 1). |

## Timedemo (recorded real-gameplay tape, uncapped) — 2026-07-13

- Workload: `demo.json` (Dev's playthrough, 3984 tics E1M1 skill 2) via `p10_timedemo.jai`,
  runner rebuilt at head. STATE_HASH `2710547389` — bit-identical on every run below
  (15+ runs incl. both backends, both sizes, and every attribution variant).
- Context: the coordinator's opening band (18.30 ms/tic, render 15.30) predates five engine
  increments landed the same night (this-field IC `2ae137e9`, slice-window method calls
  `6491e91c`, ast_pin defer `cd05ab18`, escape-mark cut `745297bd`, method return-conv memo
  `c5c5894c`). The tape gated all five at once: hash unchanged.

### Bands (3 runs each, warm jaibite, post-edit run discarded)
| config | ms/tic | sim | render | blit | worst | realtime |
|--------|--------|-----|--------|------|-------|----------|
| 120x78 full | 7.41–8.08 | 0.41–0.44 | 6.00–6.57 | 0.98–1.07 | 18–19 | 3.5–3.9x |
| 200x140 full | 17.73–20.14 | 0.44–0.52 | 14.58–16.52 | 2.71–3.09 | 39–40 | 1.4–1.6x |
| simonly | 0.36 (dead flat x3) | 0.36 | — | — | 3–4 | 79x |
| interp 120x78 | 23.42–24.45 | 1.14–1.21 | 18.55–19.36 | 3.69–3.86 | 56–63 | 1.2x |

vs the coordinator's 18.30 band: **2.4x faster at the same scripts** — the delta is the five
engine increments (real gameplay code is method/field/bare-ident-arg dense, exactly what
they attack). vm-over-interp on real gameplay: 3.16x.

### The opening question, answered
The "real tape renders 3x heavier than p7_bench" gap (15.3 vs 5.4) was mostly ENGINE
OVERHEAD, not scene complexity: at current head the synthetic reads 4.45–4.57 and the tape
6.00–6.14 — the gap is now **1.35x**. Real scenes are method/field/arg-denser per pixel than
the start-room autopilot, so the old escape-boxing + vector-path method dispatch + env-walk
field reads taxed them ~3x harder; those taxes are deleted. The residual render, attributed
by measurement (temporary stage clocks + per-kind column/row counters, then per-stage
ablation runs; all instrumentation removed after, hash held throughout):

| stage | ms/tic | share | how measured |
|-------|--------|-------|--------------|
| wall texture pixels (`draw_tex_col` loops) | 1.75 | 28% | ablation delta (pixel loop skipped) |
| flat pixels (`draw_flat_col` loops) | 1.39 | 22% | ablation delta |
| BSP traversal + seg/clip/projection math | 1.37 | 22% | bsp stage minus both ablations |
| weapon/HUD/frame-string (outside render_view) | ~0.9 | 15% | render split minus stage sum |
| frame setup (pix_clear + clip-array rebuild) | 0.46 | 7% | stage clock |
| things (sprites + masked) | 0.40 | 6% | stage clock |

Composition per tic: 198 wall cols / 5,704 wall rows; 239 flat cols / 2,094 flat rows; 12
sky cols; sprites ~6 visible typical, masked rare on this tape. Flats cost ~3.2x more per
pixel-row than walls (per-pixel float div + 2 muls + 2 itruncs vs a fixed-point stepper) —
long sightlines multiply wall ROWS, open rooms multiply flat COST.

### Ranked recommendations (measured ceilings, not vibes)
1. **Flat inner loop int-ify** (ceiling 1.39 ms/tic, realistic ~0.7–1.0): `z = dz/dy` and the
   band depend only on the screen ROW — hoist per-row z/band out of the per-column work, or
   run fixed-point wx/wy steppers like the wall vfx/dvfx pattern. Pure script, hash-gated.
2. **BSP/seg overhead** (ceiling 1.37): engine-shaped heat (INDEX/field/call machinery inside
   `bsp_node`/`draw_seg` bodies) — rides the register-file wave; confirm composition with the
   WPR pass below before scripting anything here.
3. **Wall loop residual** (ceiling 1.75, realistic ~0.3–0.5): already fixed-point; remaining
   per-row cost is two indexings (`tp[base+iv]`, `palcm[...]`) — typed-array conversion for
   TEX_PIX/PALCM/PIX if any are still heterogeneous (the GLOOM rec->class lesson).
4. **Setup rebuild** (ceiling 0.46, realistic ~0.2): CLIPTOP/CLIPBOT/COLDONE/WALLZ push-rebuild
   every frame (480 pushes at 120 cols) — keep the arrays and index-write in place, exactly as
   CLIPS_N already does.
5. **200x140 renderer columns** (the big-size lever, unchanged): render is 14.6–16.5 of ~19
   ms/tic there; parallel blit already flips on at ≥160 cols.

### Native attribution (next pass — plain exe under WPR, no embedded instrumentation)
```
wpr -start CPU -filemode
cd examples/jaidoom/scripts
"...x64-Release BENCHMARKS/bin/jaiscript.exe" --no-debug --no-pause p10_timedemo.jai
wpr -stop jaidoom_timedemo.etl
```
~30 s capture at 120x78; the tape is deterministic so stacks line up run-to-run. Targets:
confirm the bsp/seg engine share and rank INDEX vs field vs call machinery inside it.
