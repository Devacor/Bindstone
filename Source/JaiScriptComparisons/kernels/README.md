# Cross-language kernel suite

The sixteen interpreter kernels of daScript's [dasProfile](https://github.com/borisbat/dasProfile),
each in five forms, so JaiScript can be compared tier by tier against PUC Lua, LuaJIT and AngelScript:

| folder | what |
| --- | --- |
| `JaiScript/bench/kernels/jai/<kernel>_typed.jai` | idiomatic typed JaiScript: `int`/`float`, `array<float>`, classes with typed fields |
| `JaiScript/bench/kernels/jai/<kernel>_dyn.jai` | the same algorithm with `var` everywhere (untyped parameters, fields, containers) |
| `lua/<kernel>.lua` | dasProfile's Lua script, unchanged in its timed code; only a `checksum` line was added |
| `angelscript/<kernel>.as` | idiomatic AngelScript 2.38: `int`/`uint`/`double` locals, `array<T>`, script classes reached through handles (`@`); `profile.as` is the budget loop, `lua_number.as` Lua's float formatting |
| `host/` | `lua_kernel_host.c` (PUC Lua + dasProfile's `profile_native`), `addOne.c` (LuaJIT FFI target); the JaiScript host (`jai_kernel_host.cpp`, runner-equivalent engine + `add_one`) is `JaiScript/bench/kernels/host` |

Every program prints `"<board name>", <seconds per run>, <runs>` and a `checksum` line; the runner
checks the checksums across implementations.

## Protocol

dasProfile's: a sample is one process that runs the kernel as many times as fit a 0.5 s budget (one
call calibrates, each batch regrows the count 1.2x the projection, at most 100x) and reports the
per-run time of the batch that met the budget. A cell is the median of five samples. Samples are
interleaved round robin across implementations, and the shared timing lock is held per kernel.
JaiScript times each batch with a `time::stopwatch` (the engine's steady clock, in ms over a >= 500 ms
batch); Lua with a QueryPerformanceCounter clock (`profile_native.clock`), LuaJIT with the same through FFI, and
AngelScript with the same registered by its host (`double clock()`).

## Algorithms

The math is identical everywhere (checksums agree); the data representation is each language's
idiom, as in dasProfile itself. The Lua column is dasProfile's Lua (so its DAS/Lua ratios carry
over); the JaiScript ports follow the das reference's structure, and the AngelScript ports the typed JaiScript ones:

- AngelScript computes in `double`: its host builds the scriptmath add-on with `AS_USE_FLOAT=0` (the add-on
  otherwise registers only the `float` overloads) and registers `exp`, which the add-on lacks. Arrays are filled after
  `reserve()`, since `insertLast` grows the buffer by exactly one element.
- mandelbrot: Lua uses complex tables with metatables; JaiScript and AngelScript use two float locals (das: `float2`).
  The iteration rule is Lua's repeat-until (step first, stop at |z| > 2 or 256 steps).
- particles, n-bodies: Lua uses tables; JaiScript uses classes with typed (or `var`) fields in an
  `array<Class>` reached through `Class&` / `var&` (das: structs with `float3`); AngelScript holds `array<Class@>`.
- sha256: Lua's script wraps bit ops in variadic helpers (dasProfile's own choice, kept for the
  normalization); LuaJIT runs `sha256_luajit.lua` (the `bit` library), changed from 1024 to 32
  hashes per run so all columns do the same work. JaiScript uses native int64 ops with 32-bit masks, AngelScript
  `uint` words (32-bit, wrapping, logical `>>`).
- tree: the treap's paired returns are out-parameters (`shared_ptr<Node>&`), as in the das port; node
  priorities come from each runtime's own RNG (the checksum is independent of them). AngelScript has no RNG, so its
  priorities come from a 32-bit LCG in script, and its root lives in a `Tree` object as in Lua (a handle cannot be
  passed `&inout`).
- fib loop: int32 wraparound as in das; JaiScript ints are checked int64, so the loop masks
  (`& 0xFFFFFFFF`, one extra op per iteration). AngelScript's `int` is 32-bit and wraps as das does. LuaJIT's doubles
  overflow to inf (checksum `nan`).
- sort: the AngelScript array add-on sorts in quadratic time (`sort(callback)` is a selection sort, `sortAsc`/`sortDesc`
  an insertion sort), so the AngelScript port sorts with a quicksort in script that calls the comparator through a
  function handle, as Lua's `table.sort` calls `cmp`.
- float2string: LuaJIT's `tostring` differs from Lua 5.5's `%.14g` on a few values (checksum 351128).
  AngelScript's stdlib formats floats fixed-point only (`formatFloat`; `+` on a string prints 6 significant digits), so
  `lua_number.as` produces `%.14g` from `formatFloat` and trims the trailing zeros; string2float builds its inputs with
  it.
- interop: JaiScript calls a registered C++ `add_one(int) -> int`, so it runs under
  `bin/jai_kernel_host.exe` instead of the runner; Lua calls `profile_native.addOne`, LuaJIT an FFI
  `addOne` from `addOne.dll`, AngelScript the `int add_one(int)` that `angelscript_run` registers.

## Building and running

```
set LUA_SRC=<lua-5.5.1 source root>        (lua.org/ftp/lua-5.5.1.tar.gz)
set LUAJIT_SRC=<LuaJIT v2.1 checkout>      (github.com/LuaJIT/LuaJIT, branch v2.1)
host\build_peers.cmd                       -> bin\lua_kernel_host.exe, luajit.exe, addOne.dll
..\..\JaiScript\bench\kernels\host\build.cmd -> JaiScript\bench\kernels\bin\jai_kernel_host.exe
cmake -S .. -B ..\out\release -G Ninja -DCMAKE_BUILD_TYPE=Release -DANGELSCRIPT_PEERS=ON
cmake --build ..\out\release --target angelscript_run   -> ..\out\release\bin\angelscript_run.exe
python run.py [--runs 5] [--kernels a,b] [--impls jai_vm_typed,jai_vm_dyn,jai_interp_typed,jai_interp_dyn,lua,luajit_joff,luajit,angelscript]
```

The JaiScript host links JaiScript's `build\lib\jaiscript.lib`, and `run.py` uses JaiScript's `build\bin\jaiscript.exe`
(the package's build layout, `docs/BUILD.md`): build the library and `jaiscript_runner` in Release
first. `JAI_BUILD` names another build directory for both (an Open Folder build's
`out\build\<config>`). `AS_RUN` names another `angelscript_run.exe`. Set `TIMING_LOCK` to a lock
directory to share the machine; `--no-lock` skips it. The report lands in `RESULTS_<date>.md`.

The "DAS interp est." columns scale this machine's Lua time by dasProfile's published DAS
interpreter / Lua 5.5.1 ratio (Linux: Ryzen 7 PRO 8700GE; M1 Max). That is a cross-machine
normalization and an estimate, not a measurement.
