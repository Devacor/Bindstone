# Cross-language kernel suite

The sixteen interpreter kernels of daScript's [dasProfile](https://github.com/borisbat/dasProfile),
each in four forms, so JaiScript can be compared tier by tier against PUC Lua and LuaJIT:

| folder | what |
| --- | --- |
| `jai/<kernel>_typed.jai` | idiomatic typed JaiScript: `int`/`float`, `array<float>`, classes with typed fields |
| `jai/<kernel>_dyn.jai` | the same algorithm with `var` everywhere (untyped parameters, fields, containers) |
| `lua/<kernel>.lua` | dasProfile's Lua script, unchanged in its timed code; only a `checksum` line was added |
| `host/` | `lua_kernel_host.c` (PUC Lua + dasProfile's `profile_native`), `addOne.c` (LuaJIT FFI target), `jai_kernel_host.cpp` (runner-equivalent engine + `add_one`) |

Every program prints `"<board name>", <seconds per run>, <runs>` and a `checksum` line; the runner
checks the checksums across implementations.

## Protocol

dasProfile's: a sample is one process that runs the kernel as many times as fit a 0.5 s budget (one
call calibrates, each batch regrows the count 1.2x the projection, at most 100x) and reports the
per-run time of the batch that met the budget. A cell is the median of five samples. Samples are
interleaved round robin across implementations, and the shared timing lock is held per kernel.
JaiScript times with the runner's `clock_ms()` (1 ms resolution over a >= 500 ms batch); Lua with a
QueryPerformanceCounter clock (`profile_native.clock`), LuaJIT with the same through FFI.

## Algorithms

The math is identical everywhere (checksums agree); the data representation is each language's
idiom, as in dasProfile itself. The Lua column is dasProfile's Lua (so its DAS/Lua ratios carry
over); the JaiScript ports follow the das reference's structure:

- mandelbrot: Lua uses complex tables with metatables; JaiScript uses two float locals (das: `float2`).
  The iteration rule is Lua's repeat-until (step first, stop at |z| > 2 or 256 steps).
- particles, n-bodies: Lua uses tables; JaiScript uses classes with typed (or `var`) fields in an
  `array<Class>` reached through `Class&` / `var&` (das: structs with `float3`).
- sha256: Lua's script wraps bit ops in variadic helpers (dasProfile's own choice, kept for the
  normalization); LuaJIT runs `sha256_luajit.lua` (the `bit` library), changed from 1024 to 32
  hashes per run so all columns do the same work. JaiScript uses native int64 ops with 32-bit masks.
- tree: the treap's paired returns are out-parameters (`shared_ptr<Node>&`), as in the das port; node
  priorities come from each runtime's own RNG (the checksum is independent of them).
- fib loop: int32 wraparound as in das; JaiScript ints are checked int64, so the loop masks
  (`& 0xFFFFFFFF`, one extra op per iteration). LuaJIT's doubles overflow to inf (checksum `nan`).
- float2string: LuaJIT's `tostring` differs from Lua 5.5's `%.14g` on a few values (checksum 351128).
- interop: JaiScript calls a registered C++ `add_one(int) -> int`, so it runs under
  `bin/jai_kernel_host.exe` instead of the runner; Lua calls `profile_native.addOne`, LuaJIT an FFI
  `addOne` from `addOne.dll`.

## Building and running

```
set LUA_SRC=<lua-5.5.1 source root>        (lua.org/ftp/lua-5.5.1.tar.gz)
set LUAJIT_SRC=<LuaJIT v2.1 checkout>      (github.com/LuaJIT/LuaJIT, branch v2.1)
host\build.cmd                             -> bin\lua_kernel_host.exe, luajit.exe, addOne.dll, jai_kernel_host.exe
python run.py [--runs 5] [--kernels a,b] [--impls jai_vm_typed,jai_vm_dyn,jai_interp_typed,jai_interp_dyn,lua,luajit_joff,luajit]
```

`build.cmd` links the host against `out\build\x64-Release BENCHMARKS\lib\jaiscript.lib`, and `run.py`
uses that folder's `bin\jaiscript.exe`: configure it with `-DCMAKE_BUILD_TYPE=Release
-DJAISCRIPT_ENABLE_BENCHMARKS=ON` and build `jaiscript_runner` first. Set `TIMING_LOCK` to a lock
directory to share the machine; `--no-lock` skips it. The report lands in `RESULTS_<date>.md`.

The "DAS interp est." columns scale this machine's Lua time by dasProfile's published DAS
interpreter / Lua 5.5.1 ratio (Linux: Ryzen 7 PRO 8700GE; M1 Max). That is a cross-machine
normalization and an estimate, not a measurement.
