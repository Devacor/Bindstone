# JaiDOOM Lua comparison verification, 2026-09-17

The Lua 5.4.8 port conforms to the frozen JaiDOOM workload. No performance result is reported: the paired runner could not obtain its required quiet window.

## Snapshot

- JaiScript runtime/source revision: 9e078bd65a62cf1bfab1deeef408dad62c44b546.
- Lua 5.4.8, int64 integers and double numbers; the unchanged pinned source archive has SHA256 4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae.
- The 17 original modules, demo and WAD hashes are in SOURCE_INPUTS.json. WAD data is not committed.
- Workload: 3984 recorded E1M1 tics, 120x78, serial simulation/render/HUD/blit, sound off.
- Compiler: MSVC 19.51.36252, x64, CMake/Ninja. Release C/C++ flags /O2 /DNDEBUG /Zi; the Lua host adds /Ob2 /Oi /Ot /GL, with /LTCG /OPT:REF /OPT:ICF at link. Static CRT. Full commands and source/binary identities are in the raw build manifests.
- Normal and padding Lua executables were built. The latter adds 40 retained volatile statements to its unused help path.

## Verified by running

| Gate | Result |
|---|---|
| Full timedemo state |1746772898 on all cold/warm replays |
| Per-tic simulation and frame identity |7968/7968 records agree for JaiScript VM, interpreter, Lua normal and Lua padded |
| Cold frame-stream FNV-1a64 |0ad5f98ab443f0bb |
| Warm frame-stream FNV-1a64 |c2502bc18d9f42c2 |
| Last frame |79443 bytes on both replays |
| Lua P4 smoke |ALL GREEN |
| Lua P7 benchmark/feel checks |All 14 checks green |
| Original P4/P7 |Green on both JaiScript backends |
| Full Debug language suites |3160 tests passed per backend |
| GLOOM 300 ticks, seed 666 |Frame 7fecc09815bb64e0, state 3580805725 on both backends, workers 0/8, normal/padded |
| Comparison-runner regression tests |22 passed |

The frame certificate covers all 7968 per-tic state/frame records per configuration, not just the final simulation hash. Its source and executable hashes remained unchanged during capture.

## Measurement status

Requested protocol: one detected P core, complete in-process warm-up, three rounds of Jai normal / Lua normal / Jai padding / Lua padding, repeated twice per round. That gives six accepted samples per runtime/layout and a padding band for each configuration.

The machine reports 24 logical CPU sets, with 8 in the fastest efficiency class. The selected P-core IDs are 0, 1, 10, 11, 12, 13, 22, 23; serial runs select the first one.

The attempt failed at the initial quiet check after ten minutes. After Fable's link completed, measured background activity remained approximately 1.8-2.8 core equivalents against the 0.20-core limit. Brave, two Roblox Studio processes, Spotify and desktop services were present. No application was stopped. No workload process or accepted timing sample was produced.

Conformance logs contain elapsed-time diagnostics, including trace work, under a loaded machine. Those numbers are not performance points. There is no measured padding band or trustworthy JaiScript/Lua ratio from this attempt.

## Raw evidence

All paths below are under Source/JaiScript/out/astra/jaidoom-lua-20260916/:

- full-warm-conformance/comparison.json, inputs.json, four traces and logs.
- lua-numeric-loops/comparison.json and p4_smoke.log / p7_bench.log.
- debug-gates/results.json and complete suite logs.
- game-gates/results.json and GLOOM/P4/P7 logs.
- frozen/source-inputs.json, source.zip and the frozen workload tree.
- build-logs/ and integrated-release/build-inputs.json.
- timing-20260917/metadata.json, runner.py and outcome.json.

No language-runtime source was changed. The port changes examples/jaidoom/ports, comparison integration and documentation.

## Retry on an idle machine

From the repository root, using the existing verified frozen build:

    python Source/JaiScript/examples/gloom/ports/run_comparison.py --build Source/JaiScript/out/astra/jaidoom-lua-20260916/integrated-release --jai-workload-root Source/JaiScript/out/astra/jaidoom-lua-20260916/frozen/tree/Source/JaiScript --engines jai-lua --workloads timedemo --rounds 3 --timedemo-conformance Source/JaiScript/out/astra/jaidoom-lua-20260916/full-warm-conformance --output Source/JaiScript/out/astra/jaidoom-lua-20260916/timing-quiet-01

Use a fresh output directory. Reserve the shared timing window first. The runner verifies the frame certificate and all current inputs before measuring, and retains rejected samples rather than turning them into data points.
