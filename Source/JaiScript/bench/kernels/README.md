# JaiScript kernel suite

The sixteen interpreter kernels of daScript's [dasProfile](https://github.com/borisbat/dasProfile) in JaiScript, each
in two forms:

| folder | what |
| --- | --- |
| `jai/<kernel>_typed.jai` | idiomatic typed JaiScript: `int`/`float`, `array<float>`, classes with typed fields |
| `jai/<kernel>_dyn.jai` | the same algorithm with `var` everywhere (untyped parameters, fields, containers) |
| `host/jai_kernel_host.cpp` | a runner-equivalent engine that also registers the C++ `add_one(int) -> int` the interop kernel calls |

Every program prints `"<board name>", <seconds per run>, <runs>` and a `checksum` line. A sample runs the kernel as
many times as fit a 0.5 s budget and reports the per-run time (dasProfile's protocol, timed with a `time::stopwatch`).

```
build\bin\jaiscript --no-debug --no-pause --backend=vm bench/kernels/jai/dict_typed.jai
bench\kernels\host\build.cmd    -> bench\kernels\bin\jai_kernel_host.exe
bench\kernels\bin\jai_kernel_host.exe --backend=vm bench/kernels/jai/interop_typed.jai
```

`build.cmd` links the host against `build\lib\jaiscript.lib` (the package's Release build, `docs/BUILD.md`);
`JAI_BUILD` names another build directory.
