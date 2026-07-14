# JaiDOOM profiling handoff

You are profiling JaiDOOM — a complete DOOM implemented in pure JaiScript — using a recorded
real-gameplay demo as the workload. Everything you need is in this folder; C++ changes are
out of scope (script + analysis only, unless the coordinator says otherwise).

## The artifacts

| what | where |
|---|---|
| The tape (Dev's real playthrough, 3984 tics of E1M1, skill 2) | `demo.json` (this folder) |
| The uncapped replay harness | `scripts/p10_timedemo.jai` |
| Fast runner (measure on THIS one) | `../../out/build/x64-Release BENCHMARKS/bin/jaiscript.exe` |
| Instrumented runner (opcode attribution; timings NOT representative) | `../../out/build/x64-Profile/bin/jaiscript.exe` |
| The perf ledger (append findings here) | `PERF_BASELINE.md` |
| Gate roster (must stay green after any change) | `scripts/p1..p9*.jai`, `jaidoom.jai -- selftest/flowtest`, `../../tools/jaiscript/net_smoke.jai` |

## Commands

```
cd scripts
R="../../out/build/x64-Release BENCHMARKS/bin/jaiscript.exe"
"$R" --no-debug --no-pause p10_timedemo.jai                      # full pipeline @120x78
"$R" --no-debug --no-pause p10_timedemo.jai -- 200x140           # big-terminal config
"$R" --no-debug --no-pause p10_timedemo.jai -- simonly           # playsim isolated
"$R" --no-debug --no-pause --backend=interp p10_timedemo.jai     # interpreter comparison
```

Reports ms/tic with sim/render/blit split, realtime multiple, worst tic, and a STATE_HASH.

## First numbers (coordinator's runs at head, quiet box — your starting band)

- 120x78 full pipeline: **18.30 ms/tic** (sim 0.96 / render 15.30 / blit 2.03), worst 36 ms,
  1.6x realtime, hash `2710547389` (identical across two runs).
- **HASH REFERENCE UPDATED 2026-07-13: the tape's STATE_HASH is now `1746772898`** (verified
  identical across two vm runs AND the interp backend). The old `2710547389` died when map
  decorations landed (things.jai DECOR table): decor mobjs join the pool the hash folds, and
  each spawn consumes a math::random draw, shifting the sim stream. The tape replays the same
  3984 tics deterministically — regression rule 1 applies against the NEW hash.
- **THE OPENING QUESTION:** the synthetic bench (`p7_bench.jai`, start-room autopilot) reads
  render ~5.4 ms/frame at the same resolution — the real tape renders ~3x heavier. Where does
  real-gameplay render time actually go (which draw stage, which scenes)? Long sightlines?
  sprite counts? masked walls? flats? Attribute it, don't guess.

## The rules

1. **Determinism is the regression gate.** The tape's STATE_HASH must be bit-identical:
   across repeated runs, across backends, and ACROSS ANY OPTIMIZATION YOU MAKE. Hash change =
   you broke the sim; revert. (Presentation-only changes can't move it — it folds sim state
   only.) Run twice after every change.
2. **Never rebuild anything while a `jaiscript` process is running** (`Get-Process jaiscript`
   first) — and you shouldn't need to rebuild at all.
3. **First run after editing any script reparses + rewrites `.jaibite`** — discard run 1 or
   pre-warm. Bands are 3 runs minimum; a contended box shows ALL stages elevated
   proportionally — discard those runs, don't average them in.
4. **Attribution before optimization.** The Profile-build runner prints `[vm-profile]` opcode
   self-time histograms and consumer tables on exit — run the tape under it (short it with a
   truncated tics array if 70s is too slow there) to see WHICH opcodes/paths dominate. For
   script-level attribution, add temporary clock_ms stage counters inside render stages
   (draw_seg wall loop vs flats vs sprites vs masked) — remove them after measuring.
5. Known context: worst-tic spikes ≈ lazy sprite composites (first sight of each monster
   type); frame strings ~68KB at 120x78; PERF_BASELINE.md's ledger lists the unpulled levers
   in priority order (parallel blit wins at ≥160 cols and auto-enables; renderer columns are
   the untouched big-size lever; sprite inner-loop int-ify; plane-fill dedup).
6. Deliverables: append a "timedemo" section to PERF_BASELINE.md — bands for all four
   command configs, the render-attribution answer to the opening question, ranked
   recommendations (measured expected win per lever, not vibes), and the tape hash you
   verified against. If you implement any script-level optimization: full gate roster green
   + identical hash + before/after band, or it reverts.
