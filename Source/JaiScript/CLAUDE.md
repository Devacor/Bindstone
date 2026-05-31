# JaiScript Development Guide

High-performance, C++-like embedded scripting language with hot-reload. Snake_case naming
throughout. Used by the MutedVision (MV) engine / Bindstone via a header API + a static lib.

**Key traits:** natural property access (`obj.property`), implicit `self` in methods,
automatic lambda captures, `!= null` null checks, hot-reload of script classes, zero static
state (every `script_value` carries an engine reference → multi-engine + thread safe).

---

## Build & Test

CMake + Ninja, opened as a folder in Visual Studio 2022. Build dirs live under
`out/build/<config>/`; the test exe is `bin/jaiscript_tests.exe`.

### Fast iteration: use the **Debug** build

Debug has no LTCG, so it **links in ~1s** (Release links in ~115s due to `/GL`+`/LTCG`), and it
defines `JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES` (catches env/closure cycles) plus MSVC's
`0xDDDDDDDD` freed-memory fill (surfaces dangling-reference / use-after-free bugs that Release
can mask). **Iterate in Debug; do a final Release pass for the shipped config.**

Build only the test target (`--target jaiscript_tests`) to skip the redundant standalone test
exes (`test_runner`, `test_simple_static`, …) which relink the whole lib and rebuild every time.

```bash
# Configure Debug once (Ninja) if out/build/x64-Debug/CMakeCache.txt is missing:
powershell.exe -Command "& cmd /c '\"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 && cd /d d:\git\Bindstone\Source\JaiScript && cmake -S . -B out/build/x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl 2>&1'"

# Build the test target (Debug)
powershell.exe -Command "& cmd /c '\"...\VsDevCmd.bat\" -arch=x64 && cd /d d:\git\Bindstone\Source\JaiScript && cmake --build out/build/x64-Debug --target jaiscript_tests 2>&1'"

# Build (Release)  — swap x64-Debug -> x64-Release
# Run all tests
d:\git\Bindstone\Source\JaiScript\out\build\x64-Debug\bin\jaiscript_tests.exe
```

(Substitute the full `VsDevCmd.bat` path; `vswhere.exe not recognized` on the first line is
harmless.) Adding a **new** test `.cpp` requires a CMake reconfigure (the test list is a
`file(GLOB_RECURSE)` evaluated at configure time) — re-run the configure command above.

### Test filtering

```
jaiscript_tests.exe "Script Class"            # bare pattern (ADDITIVE): runs every suite whose
                                              #   NAME matches (all its tests) OR any test whose
                                              #   name matches — never subtracts a matched suite
jaiscript_tests.exe "Suite.Test"              # dot = explicit AND: suite filter . test filter
jaiscript_tests.exe --gtest_filter="Property.*"   # also: --jaitest_filter=, --filter=
jaiscript_tests.exe --verbose                 # per-test names + timings (use to find a hang)
```
NOTE: a bare pattern is **additive** — `"Array Tests"` runs that whole suite, and `"push"` runs
every test named `*push*` across all suites. Use the dotted `"Suite.Test"` form when you want the
precise AND (suite must match AND test must match). (Fixed 2026-05; the old bare-pattern form
ANDed suite+test and silently skipped a matched suite's tests.)

### Building against Bindstone / MutedVision (CLI works!)

Bindstone **does** build from the command line — pass `/p:SolutionDir=` (the `.vcxproj`
include paths use `$(SolutionDir)`, the only reason a naive build fails). Use this to verify
JaiScript header changes against MV without the IDE:

```bash
powershell.exe -Command "& cmd /c '\"...\VsDevCmd.bat\" -arch=x64 && cd /d d:\git\Bindstone && msbuild \"VSProjects\MutedVision\MutedVision_Windows\MutedVision_Windows.vcxproj\" /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=d:\git\Bindstone\ /m 2>&1'"
```

### Benchmarks

`x64-Release BENCHMARKS` config (`-DJAISCRIPT_ENABLE_BENCHMARKS=ON`) adds perf suites
(core ops, ChaiScript/Squirrel comparisons, loops, containers). **Caveat:** the harness reports
**integer µs/iteration with ±50% run-to-run variance** — it cannot resolve sub-µs changes, so
don't trust it for micro-optimizations (use a dedicated ns micro-bench for those).

---

## Architecture

```
include/jaiscript/      Headers
  core/      value, engine, dynamic_binder, conversion_registry, class_definition, ...
  detail/    lexer, parser, interpreter, ast, string_symbolizer, integer_ops
  serialization/ archive, json_archive, binary_archive, polymorphic   (Cereal -> JaiScript migration)
  signals/   signal, signal_impl   (C++ signal system)
  properties/ observable_property, property_manager
  stdlib/    print/to_string/type_of/to_json/from_json, math, containers
  testing/   foundry.hpp (test framework)
source/implementation/  lexer.cpp parser.cpp interpreter.cpp(+_dispatch) engine.cpp value.cpp ...
source/tests/           Foundry suites (language/ semantics/ containers/ stdlib/ ...)
LEGACY_VM/, squirrel/   NOT built — ignore (the bytecode VM is unfinished/legacy)
```

**Pipeline:** `lexer` → `parser` (recursive descent, builds AST) → `interpreter`
(tree-walker — the only backend). `interpreter.cpp` is ~11k lines; arithmetic & comparison are
inlined in `visit_binary` fast paths AND in `interpreter_dispatch.cpp` handlers (both must stay
in sync — see integer overflow below).

**`script_value`** (`core/value.hpp`): type-erased `std::variant` (null/int64/double/string/
char/bool/array/map/object/function/reference/shared_ptr/weak_ptr). Heavy types are wrapped in
non-atomic `strong_ptr` for O(1) copies. C++-bound values use `cpp_bound_ptr_` +
`cpp_bound_type_size_` (low 7 bits = byte size, bit `0x80` = unsigned) — decode via
`unchecked_as_int()/unchecked_as_float()`, NOT raw casts. References hold a `reference_holder`
(`target` ptr + weak `sourceEnv`); `deref()` resolves them. `clone()` = deep copy, copy = shallow.
Every `script_value` needs an engine ref: `script_value(value, eng)` or
`script_value(std::monostate{}, eng)` for null.

**Engine / environment** (`engine.cpp`): `global_environment_`, persistent reused `interpreter`,
scope chain with O(1) slot-based locals (parser assigns `slot_index`) falling back to a symbol
map. `string_symbolizer` interns names → IDs (engine-bound, non-deterministic — NEVER share or
serialize across engines).

---

## Integer-overflow policy (compile-time)

Defined in `detail/integer_ops.hpp` (kept OUT of the widely-included `interpreter.hpp` so its
`<intrin.h>` doesn't bloat consumers). Selected at build time, applied via `if constexpr` so the
unused policy's code vanishes (zero runtime cost for the other mode):

- **`JAISCRIPT_CHECKED_OVERFLOW` (default — "safe by default")**: overflowing `+ - * unary-` and
  `INT64_MIN/-1` division raise a catchable runtime error. Detection is cheap
  (`_mul128`/`__builtin_mul_overflow`, not division).
- **`JAISCRIPT_WRAP_ON_OVERFLOW`**: silent two's-complement wraparound (max speed).

Either way arithmetic is **never undefined behavior** (compute in unsigned). All integer ops
route through `jai::ints::try_add/try_sub/try_mul/try_div/mod` — use these, never raw `a+b` on
`script_int`. `engine::throw_on_overflow()` reports the build's policy (useful in tests).

Parser recursion is capped at `MAX_PARSE_DEPTH = 250` (`detail/parser.hpp`) and JSON text parsing
at `JAI_MAX_JSON_PARSE_DEPTH = 128` (`json_archive.hpp`) — both low enough that hitting the limit
throws *before* the native stack overflows (1024 was too deep for a 1 MB Debug stack).

---

## Compile-time notes (consumers care about this)

Measured on MV (Release, `/MP`): per-TU cost is **~99% front-end header parsing, ~0% codegen**;
JaiScript is **~10%** of a typical MV TU (the rest is Cereal/ChaiScript/SDL, being migrated out).
Under `/MP` the clean-build wall-clock is set by the slowest TU + link, so per-header trims and
PCH additions barely move it — the big win is the Cereal/ChaiScript removal.

Header-hygiene rules that DO matter (keep them this way):
- **`signals/signal.hpp` does NOT include `signal_impl.hpp`** (which pulls the whole
  engine+value system). Consumers that fire *script* receivers must include `signal_impl.hpp`
  explicitly (`dynamic_binder.hpp` and `signal_serialization.hpp` already do). This cut signal
  consumers from 186→127 includes.
- **`checked_result.hpp` does NOT include `<format>`** (it's on the universal `value.hpp` path).
  Error messages use plain `{0}`/`{1}` substitution.
- Keep `<intrin.h>`, heavy std headers, and impl headers out of widely-included core headers.

---

## dynamic_binder (C++ integration)

```cpp
dynamic_binder<MyClass>(engine, "MyClass")
    .constructor<script_int, script_float>()
    .method("method_name", &MyClass::method_name)
    .method("chain", [](MyClass& self) -> MyClass& { return self; })  // returns *this for chaining
    .property("prop", &MyClass::prop)
    .base_class<BaseClass>()      // enables inheritance + automatic upcast on shared_ptr<Base>
    .build();
```
Zero-copy const& bindings, operator overloading, ctor overloading. A method returning `T&`
into its receiver (the chaining idiom) is kept alive across temporaries via the
`object_holder::keep_alive` anchor — don't remove it.

```cpp
engine engine;
script_value r = engine.execute("2 + 2");           int v = r.as<int>();   // or r.as_int()
engine.add_function("square", [](script_int n){ return n*n; });
engine.add_variadic_function("print", [](const std::vector<script_value>& a){ /*...*/ });
engine.add_global("PI", script_value(3.14159, &engine));
engine.add_global_ref("hp", hpFloat);               // binds a live C++ variable (cpp_bound)
```

---

## Foundry test framework

```cpp
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {
class my_tests : public suite {
public:
    my_tests() : suite("My Suite") {}
    void forge_tests() override {
        test("two_plus_two", [this]() {
            auto e = engine::make();                 // returns shared_ptr<engine>
            check_eq((int64_t)4, e->execute("2 + 2").as_int());   // check_eq(expected, actual)
        });
    }
};
}
// GOTCHA: FOUNDRY_REGISTER is at global scope, so it needs a using-alias first:
using my_tests = jai::foundry::tests::my_tests;
FOUNDRY_REGISTER(my_tests)
```

Assertions: `check`, `check_eq(expected,actual)`, `check_ne`, `check_near(exp,act,tol)`,
`check_throws([]{...})`, `check_true/false`, `check_null/not_null`, `check_gt/lt/ge/le`.
Tests auto-register and run via `main_test_runner.cpp`. Add tests to an EXISTING suite file
when possible (no reconfigure); a new file needs a CMake reconfigure.

When a parse error is recoverable the parser *synchronizes* and `execute()` may return null
(NOT throw) — so "deeply nested input doesn't crash" is tested by "execute returns and the
engine stays usable", not by `check_throws`.

---

## Code guidelines (CRITICAL)

- **No non-const `static` / `thread_local` storage** — it pollutes state across engines/tests.
  Use instance members or engine-bound storage (`parameter_storage` pattern). `static const`
  (non-pointer) is fine.
- **Every `script_value` needs an engine ref** (`script_value(v, eng)` / `(std::monostate{}, eng)`).
- **`operator==` and `operator<=>` must stay consistent** and form a strict weak ordering —
  `script_value` is used as `std::map` key. Float comparison uses `std::strong_order` (total
  order incl. NaN); complex types order by held-pointer identity, never by `&storage_`.
- **Never read a `script_value` after moving from it** (the move ctor nulls the source).
- Match surrounding style (tabs in `interpreter.cpp`/`parser.cpp`).

---

## Language features (all implemented)

- Operators: `+ - * / % ++ -- <=> < > <= >= == != && || ! | ^ & << >> ?:` and compound assigns.
- Control flow: `if/else`, `while`, C-style + range-based `for`, `break/continue`,
  `try/catch/throw`, `switch/case/default` (**break-by-default**; explicit `fallthrough;` to
  fall through). `throw`/`break`/`continue` correctly unwind out of loops.
- Types: `int64`, `double`, `string`, `char`, `bool`, arrays `[1,2,3]`, maps `{"k": v}`,
  `weak_ptr`, references, functions. Automatic conversions.
- Functions & lambdas with captures (`[=]`/`[&]`, auto-capture), variadics, default args,
  by-reference params.
- Script classes: inheritance with `super()`, ctor overloading + `: this(...)` delegation,
  field-init in **declaration order**, static members/methods, hot reload with instance migration.
- Coroutines: `coroutine`/`yield`, drivable by range-`for`.
- stdlib: `print`, `to_string`, `type_of`, `to_json`/`from_json` (full-precision floats via
  `std::to_chars`).

```jaiscript
class Cat { int a = 0; Cat(int v) { a = v; } }
class Tiger : Cat { Tiger() : super(5) {} void roar() { print("ROAR " + to_string(a)); } }
auto t = Tiger(); t.roar();

for (auto& x : nums) { x *= 2; }                 // reference range-for mutates in place
for (auto kv : scores) { print(kv.first + ": " + to_string(kv.second)); }
```

---

## Status

Production-ready: complete lexer/parser/interpreter, script classes, hot reload, C++ integration,
JSON, coroutines. **Exception handling and overflow checks work in the interpreter** (the
bytecode VM under `LEGACY_VM/` is unfinished and not built).

A 2026-05 deep audit fixed: loop-`throw` infinite hang, range-`for` slot allocation & OOB on
shrink, `switch` continue/break leakage, map-read auto-insert, `<=>`/NaN map-key ordering,
int64 `==` precision, cpp-bound float/int reads, float→int UB, integer-overflow policy,
JSON float precision + recursion depth, and a dangling-temporary in C++ method chaining.
Regression tests live in `source/tests/language/review_regression_tests.cpp`.

**Workflow:** edit → build Debug (`--target jaiscript_tests`) → run suite → fix → final Release
pass. Reproduce a bug as a foundry test first, confirm it's red, then fix to green.
