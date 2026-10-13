# OSSUARY comparison runner

Build the baseline and candidate Release trees with DEMOREEL_PADDING_CONTROLS=ON. Enable DEMOREEL_PORT_LUA and DEMOREEL_PORT_SQUIRREL on the baseline tree for the port controls. The CMake roots for those dependencies are explicit cache options.

Use run_ossuary_comparison.py with --baseline BUILD --candidate BUILD --scenes SHAPED --natural-scenes NATURAL --lua-scenes LUA --squirrel-scenes SQUIRREL --output PATH. All output must be under Source/JaiScript/out/astra. Make NATURAL from a copy of the scene directory with skull_natural.jai replacing skull.jai. The original scripts remain unchanged.

The runner measures the zero-based scene 15 at 100x40, t=3.0 and seed 20260705. Default workload: five measured frames, preceded by five warm-up frames in the same runtime and then a reboot/jump to the same scene state. --frames N and --warmup N can increase the interval. --perf --scene also works directly on each executable; without --scene the existing whole-reel per-scene behavior is retained.

JaiScript baseline/candidate, shaped/natural, serial/six workers, Lua 5.4.8 and Squirrel 3.2 are interleaved for at least three rounds. Each runtime/layout gets six accepted samples. Serial processes use one detected P core; the six-worker configurations use six. Only one workload process runs at a time.

An interpreter run establishes the frame-stream reference. Both layouts of every configuration must match it before timing. A nonconforming port is reported without a timing point; a JaiScript mismatch stops the comparison. Every timed run rechecks the hash. Executable and script hashes are checked again at completion.

The normal and padded executables differ by 40 retained volatile statements in their cold help function. Report normal and padded medians, sample ranges and the paired padding band together. The default background CPU ceiling is 0.20 core equivalents. A user-authorized alternative must be supplied explicitly with --max-background-cores and identified in the report. The runner records actual background activity and observed competing build/test/benchmark processes.

Raw output contains metadata.json, runs.json, summary.json and every process log. Input changes, hash failures, nonzero exits, or incomplete rounds cannot produce a successful timing summary.
