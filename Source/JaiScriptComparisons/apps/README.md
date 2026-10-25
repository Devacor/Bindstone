# Benchmark apps

Four general programs, each written once per language (JaiScript, Lua 5.4, Squirrel 3.2, CPython 3) with the same
algorithm, workload and deterministic data, so whole-program speed compares across ordinary application shapes rather
than kernels:

| app | what it exercises |
| --- | --- |
| `richards/` | Martin Richards' OS scheduler simulation after Octane's `richards.js`: polymorphic `run` over four task classes, linked packet queues, object fields. Each run checks Octane's queue and hold counts (2322, 928). |
| `deltablue/` | the DeltaBlue incremental constraint solver after Octane's `deltablue.js`: a deep class hierarchy, virtual dispatch, identity comparisons, growing and shrinking lists. Each run checks the chain test and the four projection checks. |
| `bytecode/` | a stack-based bytecode VM in the script: an opcode dispatch loop over a flat integer program (recursive fib, an arithmetic loop, an array sieve) with an array operand stack, a call stack and an array heap. `assemble.py` writes the program every port embeds. |
| `spatial/` | 100,000 entities (sized past the caches on purpose) on a 256 x 256 grid: a cell to bucket map rebuilt every step, 3 x 3-cell neighbour queries that steer each entity from its crowd, bouncing integer position updates. |

Every program prints `result <hash>` (a 32-bit hash of its results, folded as `(h * 31 + v) & 0xFFFFFFFF`) and
`time_ms <ms>` (its own clock around the workload, so start-up and compilation stay out). All arithmetic is integer and
stays inside 64 bits, the random data comes from one LCG (`state = (state * 1103515245 + 12345) & 0x7FFFFFFF`) every
port implements, and every division and modulo has non-negative operands, so all five runs of an app (JaiScript on both
backends included) print the same hash.

## Idiom

Each port is that language's ordinary code, not a tuned one: classes with fields and methods in JaiScript, Squirrel and
Python, metatables in Lua; Python and Lua dispatch the bytecode VM with an `if` chain, JaiScript and Squirrel with
`switch`. Two differences follow from the languages:

- JaiScript objects are values, so the shared object graphs (Richards, DeltaBlue) are `shared_ptr` handles built with
  `new`, a method passes itself on as `shared_from_this()`, and the spatial entities are an `array<Entity>` reached
  through `Entity&`. Reference counting does not collect cycles, so a Richards run clears the scheduler's task links and
  a DeltaBlue test clears its variables' constraint lists when it ends; the other languages leave that to their
  collectors.
- A JaiScript constructor dispatches virtual calls to its own class, as C++ does, so DeltaBlue's constraints call
  `add_constraint()` at the creation site instead of at the end of the base constructor.

The JaiScript ports run under strict checking.

## Building and running

The runner needs the JaiScript runner (`jaiscript`, whole-program Release) and two stand-alone interpreters built from
source by this project, `apps_lua` (Lua 5.4's `lua.c`) and `apps_sq` (Squirrel 3.2's `sq.c`, double floats), behind
`APPS_PEERS`. Their roots default to `third_party/lua` and `third_party/squirrel`; `APPS_LUA_ROOT` and
`APPS_SQUIRREL_ROOT` point elsewhere:

```
cmake -S Source/JaiScriptComparisons -B Source/JaiScriptComparisons/out/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DAPPS_PEERS=ON
cmake --build Source/JaiScriptComparisons/out/release --target jaiscript_runner apps_lua apps_sq
python Source/JaiScriptComparisons/apps/run.py [--rounds 5] [--apps richards,deltablue,bytecode,spatial]
       [--impls jai_vm,jai_interp,lua,squirrel,python] [--bin DIR] [--python EXE] [--no-lock]
```

CPython is the interpreter running `run.py` unless `--python` names another. Each sample is one process; samples are
interleaved round robin across implementations, and a cell is the median of `--rounds` samples with its spread (half
the sample range as a share of the median). A sample whose hash differs from the app's first implementation, or that
exits non-zero, fails the run. The whole run holds the machine-wide timing lock (`JaiScript/tools/ci/timing_lock.sh`)
unless `--no-lock`. The report lands in `out/apps/RESULTS_<date>.md`.
