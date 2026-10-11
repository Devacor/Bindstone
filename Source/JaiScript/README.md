# JaiScript

JaiScript is an embedded scripting language for C++ engines: C++-flavored syntax, script
classes with hot reload, coroutines, a three-rung type ladder (`int` / `auto` / `var`) with a
static checker that is strict by default, value semantics with references by declaration, and
two interchangeable backends — a tree-walking interpreter and a bytecode VM — kept at
byte-for-byte parity by 2,400+ tests and a differential fuzzer. It has no dependencies and
builds as a static library plus headers.

The documentation — a 14-chapter language guide, a 14-chapter embedding guide, the examples
and a reference — is a static site in [`docs/site/`](docs/site/). Open
`docs/site/index.html` in a browser; a clone is all you need.

## A taste

```jaiscript
class Creature {
    string name;
    int hp = 10;

    Creature(string n, int h) { name = n; hp = h; }

    void hurt(int amount) { hp -= amount; }
    bool alive() { return hp > 0; }
}

coroutine int countdown(int from) {
    for (int i = from; i > 0; i--) { yield i; }
}

auto squad = [Creature("scout", 3), Creature("brute", 9)];
for (auto& c : squad) { c.hurt(4); }              // auto& mutates the elements in place
for (auto c : squad) { print("{} hp={} alive={}", c.name, c.hp, c.alive()); }

for (auto n : countdown(3)) { print("countdown {}", n); }   // a coroutine drives a range-for

var loose = 1;
loose = "now a string";                           // var is the opt-out from typing
print("{} {}", type_of(loose), cast<int>('a'));   // char converts only through a cast

try { int x = loose; print("{}", x); } catch (e) { print("caught: {}", e); }
```

```
scout hp=-1 alive=false
brute hp=5 alive=true
countdown 3
countdown 2
countdown 1
string 97
caught: Cannot initialize 'x' (declared int) with string
```

That last line is the type ladder in one breath. `int x = "text"` never reaches the runtime —
the checker rejects it before a statement executes. `int x = loose` *does* reach the runtime,
because the value came through `var` and nothing could know its type ahead of time, so it
raises where `try`/`catch` can see it.

## Embedding it

An engine is an object. There is no global interpreter state anywhere: every value carries its
engine, so two engines on two threads need no locks.

```cpp
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <cmath>

struct Vec2 {
    double x = 0, y = 0;
    Vec2(double x, double y) : x(x), y(y) {}
    double length() const { return std::sqrt(x * x + y * y); }
};

auto e = jai::engine::make();          // always a shared_ptr; the constructor is private
jai::stdlib::register_all(e);          // formatted print, to_string, math, JSON, containers

e->add_function("sqrt", [](jai::script_float x) { return std::sqrt(x); });

jai::dynamic_binder<Vec2>(*e, "Vec2")
    .constructor<jai::script_float, jai::script_float>()
    .property("x", &Vec2::x)
    .property("y", &Vec2::y)
    .method("length", &Vec2::length)
    .build();

double len = e->execute("Vec2(3, 4).length()").as_float();   // 5.0
```

Two knobs are worth knowing on day one:

```cpp
e->set_backend(jai::backend_type::interpreter);   // tree-walking interpreter instead of the
                                                  // VM; must come before the first execute
e->static_checking(jai::check_mode::warn);        // collect diagnostics instead of gating on
                                                  // them; can change at any time
```

The embedding guide covers the rest: binding inheritance and operators, `shared_ptr` handles,
hot reload, precompiled `.jaibite` scripts, execution budgets, signals, and the debugger.

## What's in the box

**Types.** `int`, `float`, `string`, `char`, `bool`, arrays and maps (typed or dynamic),
`shared_ptr<T>` handles, `weak_ptr<T>`, and `T&` references. The ladder is `int` (enforced),
`auto` (inferred, then enforced), `var` (dynamic, forever).

**The checker.** Strict by default: typed-to-typed mistakes and undeclared identifiers are
load errors, thrown as `jai::static_check_error` before anything runs; only a value sourced
from `var` can still fail at run time. A program's imports are checked as one closure, so a
name declared in one file is known in every other whatever the import order. `extern`
declares names the host provides, and `[[warn_mode]]` / `[[strict_mode]]` relax or tighten a
unit, a block, or a single statement.

**Value semantics.** Assignment copies, like C++. You get aliasing when you ask for it — `T&`
references, `shared_ptr<T>` handles — and `cast<T>` / `cast<T&>` / `cast<shared_ptr<T>>` are
the explicit spellings at a typed boundary.

**Script classes.** Inheritance with `super()`, constructor overloading and `: this(...)`
delegation, static members (`C::s`), operator overloading, user `_get_x` / `_set_x`
accessors, and hot reload that migrates live instances field by field.

**Coroutines.** `coroutine` functions *and* methods, `yield`, drivable from a range-`for` or
resumed by the host a frame at a time.

**Everything else you reach for.** Lambdas with automatic captures, default and variadic
arguments, template strings, `include` / `import` with a `.jaibite` disk cache, namespaces,
enums, destructuring, `parallel_transform`, JSON and binary serialization, a `reflect::` API,
a DAP debugger with a VS Code extension, an execution budget, a memory cap, and checked
integer overflow.

## Build

You need a C++20 compiler and CMake 3.20+; Ninja is recommended. Verified — the full suite on
both backends — with MSVC 19.51 (Visual Studio 18, toolset 14.51), Clang 20 and GCC 16; the
CI workflow targets MSVC, GCC 14 and Clang on Ubuntu 24.04. Older compilers may work and are
untested.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target jaiscript_tests jaiscript_runner
```

Binaries land in `build/bin/`. With the Visual Studio generator, drop `-G Ninja` and pass
`--config Release` to the build step; the binaries land in `build/bin/` too.

Debug links in seconds; Release links with LTCG and takes minutes. Iterate in Debug, then do a
Release pass.

| Option | Default | What it does |
|---|---|---|
| `JAISCRIPT_BUILD_TESTS` | top-level | The Foundry test suite (`jaiscript_tests`) and the fuzzer (`jaiscript_fuzz`) |
| `JAISCRIPT_BUILD_RUNNER` | top-level | The standalone runner / REPL (`jaiscript`) |
| `JAISCRIPT_BUILD_LSP` | top-level | The language server (`jaiscript_lsp`) |
| `JAISCRIPT_BUILD_EXAMPLES` | top-level | `jai_gloom`, `jai_crawler`, `jai_demoreel` |
| `JAISCRIPT_INSTALL` | top-level | Install rules and the `find_package(JaiScript)` package |
| `JAISCRIPT_ENABLE_DEBUGGER` | ON | The DAP-over-TCP debug listener; turn it off for shipping builds |
| `JAISCRIPT_ENABLE_BENCHMARKS` | OFF | Performance suites inside `jaiscript_tests` |
| `JAISCRIPT_NATIVE_ARCH` | OFF | `-march=native`; faster, non-portable binaries |
| `JAISCRIPT_ENABLE_LTO` | OFF | `-flto` for GCC/Clang Release builds |
| `JAISCRIPT_LTCG` | top-level | `/GL` + `/LTCG` for MSVC Release builds (OFF as a subproject, so a consumer's Release link stays incremental) |

"top-level" means ON when JaiScript is the top-level CMake project and OFF when another project
pulls it in with `add_subdirectory`; [`docs/BUILD.md`](docs/BUILD.md) has every option and both
consumption routes (`add_subdirectory` and `find_package(JaiScript)`).

Nothing third-party ships in this repository. The cross-language comparison benchmarks compile
only if you supply those sources yourself — see [`docs/BUILD.md`](docs/BUILD.md). Integer
arithmetic raises a catchable error on overflow by default; define `JAISCRIPT_WRAP_ON_OVERFLOW`
for the whole build to opt into silent two's-complement wrap instead.

## Tests

```bash
build/bin/jaiscript_tests                 # tree-walking interpreter
build/bin/jaiscript_tests --backend=vm    # bytecode VM
```

Both must be green. That is the project's central rule, and the reason two backends can be
maintained at all. As of September 2026 the suite is 111 suites / 2,497 tests in the
benchmarks configuration, green on both backends.

Filter with `jaiscript_tests "Coroutine"` (additive: matching suites *or* matching test names)
or `jaiscript_tests "Suite.Test"` (the strict AND form). `jaiscript_fuzz --iterations 1000`
runs the differential fuzzer, which generates programs and fails if the two backends disagree
on output or on error text.

## The runner

`jaiscript` is this language's `python`: a file runner, a REPL, and a live-coding watcher.

```bash
jaiscript hello.jai            # run a file and exit
jaiscript                      # REPL — globals, functions and classes accumulate
jaiscript --watch hello.jai    # re-run on every save
```

It defaults to the VM backend and strict checking; `--backend=interp`, `--check=warn|off` and
`--budget=N` change that. Details in [`tools/jaiscript/README.md`](tools/jaiscript/README.md).

## Examples

Three complete programs live in [`examples/`](examples). Each has a C++ host that is a thin
console shell — a VT terminal, key input, a clock — and *all* of the actual program in script.

- **GLOOM** (`examples/gloom`) — a real-time raycast shooter: four maps, keycards, secrets,
  enemy brains as coroutines, particles through `parallel_transform`. It has also been ported
  to Squirrel, Lua, ChaiScript and Python for a five-way comparison.
- **GRUBWELL** (`examples/crawler`) — a step-based first-person dungeon crawler, and the hot
  reload showpiece: run it with `--dev`, edit a script, press `R`, and the dungeon changes
  under you.
- **demoreel** (`examples/demoreel`) — thirteen truecolor demoscene scenes running on *two*
  live engines at once; TAB swaps the backend mid-scene and the picture does not change.

Each has a `.jai` shim you can drop onto the runner, and each ships a self-check: run it with
`--smoke` and it plays itself headlessly, printing a deterministic state hash. The interpreter
and the VM have to produce the same one.

A fourth, larger demo — a from-scratch DOOM reimplementation in roughly 8,000 lines of script,
with 16-player netplay and demo playback — is described in the documentation but is not
distributed here, because its content derives from assets that cannot be redistributed.

## Documentation

- [`docs/site/index.html`](docs/site/index.html) — the documentation site: language guide,
  embedding guide, examples, reference.
- [`docs/README.md`](docs/README.md) — the index of the specifications behind the site.
- [`docs/grammar.md`](docs/grammar.md) — the full grammar and operator precedence.
- [`docs/static_checking.md`](docs/static_checking.md) — the checker's contract: modes, the
  conversion table, escape hatches.
- [`docs/diagnostics.md`](docs/diagnostics.md) — every error text, byte-identical on both backends.
- [`tools/vscode-jaiscript/`](tools/vscode-jaiscript/) — syntax highlighting and the debugger
  client for VS Code.
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — how to build, test, and land a change.

Bugs and questions: [github.com/Devacor/JaiScript/issues](https://github.com/Devacor/JaiScript/issues).

## Status

JaiScript 0.1.0 — first public release (September 2026). The language, both backends, the
embedding API and the `.jaibite` format are complete and covered by 2,400+ tests
run on both backends and a differential fuzzer. Pre-1.0 means the C++ API may still change
between minor versions; [`docs/site/stability.html`](docs/site/stability.html) lists what is
settled and what may change before 1.0.

## License

MIT — see [LICENSE](LICENSE).

## Credits

JaiScript was designed and created by Devacor (mike@m2tm.net) in honor of his love, Jai.
