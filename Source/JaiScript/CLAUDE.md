# JaiScript Development Guide

High-performance, C++-like embedded scripting language with hot-reload. Snake_case naming
throughout. Used by the MutedVision (MV) engine / Bindstone via a header API + a static lib.

**Key traits:** natural property access (`obj.property`), implicit `self` in methods,
automatic lambda captures, `!= null` null checks, hot-reload of script classes, zero static
state (every `script_value` carries an engine reference → multi-engine + thread safe).

---

## Build & Test

CMake + Ninja, opened as a folder in Visual Studio (VS 18). Build dirs live under
`out/build/<config>/`; the test exe is `bin/jaiscript_tests.exe`.

### Fast iteration: use the **Debug** build

Debug has no LTCG, so it **links in ~5s incremental** (Release links in ~165s due to `/GL`+`/LTCG` — measured on the MSI; the link grows with the test exe), and it
defines `JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES` (catches env/closure cycles) plus MSVC's
`0xDDDDDDDD` freed-memory fill (surfaces dangling-reference / use-after-free bugs that Release
can mask). **Iterate in Debug; do a final Release pass for the shipped config.**

Build the test target (`--target jaiscript_tests`). (The redundant standalone test exes —
`test_runner`, `test_simple_static`, `test_string_ctor` — were removed from the CMake build.)

```bash
# Configure Debug once (Ninja) if out/build/x64-Debug/CMakeCache.txt is missing:
powershell.exe -Command "& cmd /c '\"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 && cd /d C:\git\Bindstone\Source\JaiScript && cmake -S . -B out/build/x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl 2>&1'"

# Build the test target (Debug)
powershell.exe -Command "& cmd /c '\"...\VsDevCmd.bat\" -arch=x64 && cd /d C:\git\Bindstone\Source\JaiScript && cmake --build out/build/x64-Debug --target jaiscript_tests 2>&1'"

# Build (Release)  — swap x64-Debug -> "x64-Release BENCHMARKS" (the configured Release dir)
# Run all tests
C:\git\Bindstone\Source\JaiScript\out\build\x64-Debug\bin\jaiscript_tests.exe
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
precise AND (suite must match AND test must match). (Fixed; the old bare-pattern form
ANDed suite+test and silently skipped a matched suite's tests.)

### Building against Bindstone / MutedVision (CLI works!)

Bindstone **does** build from the command line — pass `/p:SolutionDir=` (the `.vcxproj`
include paths use `$(SolutionDir)`, the only reason a naive build fails). Use this to verify
JaiScript header changes against MV without the IDE:

```bash
powershell.exe -Command "& cmd /c '\"...\VsDevCmd.bat\" -arch=x64 && cd /d C:\git\Bindstone && msbuild \"VSProjects\MutedVision\MutedVision_Windows\MutedVision_Windows.vcxproj\" /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=C:\git\Bindstone\ /m 2>&1'"
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
  vm/        opcode, chunk, vm_compiler, vm_backend, disassembler (bytecode backend)
source/implementation/  lexer.cpp parser.cpp interpreter.cpp(+_dispatch) engine.cpp value.cpp vm/ ...
source/tests/           Foundry suites (language/ semantics/ containers/ stdlib/ vm/ ...)
squirrel/               NOT built — benchmark comparison source only
```

**Pipeline:** `lexer` → `parser` (recursive descent, builds AST) → one of TWO swappable
`execution_backend`s: the tree-walking `interpreter` (default) or the bytecode `vm`
(`jai::vm`, environment-backed frames, full Foundry parity). Select per engine with
`engine->set_backend(backend_type::vm)` BEFORE the first execute (forbidden after), or run the
whole test suite with `jaiscript_tests.exe --backend=vm`. Script callables execute through
`execution_backend::execute_callable` (thunks capture engine* + `script_callable` payload — never
a backend pointer). `interpreter.cpp` is ~11k lines; arithmetic & comparison are inlined in
`visit_binary` fast paths AND in `interpreter_dispatch.cpp` handlers; the vm's `op_binary` shape
operands mirror the same semantics (all three must stay in sync — see integer overflow below).
Builtin container/string methods live in ONE registry both backends share
(`detail/builtin_methods.hpp`, `init_builtin_method_registries`).

**`script_value`** (`core/value.hpp`): type-erased `std::variant` (null/int64/double/string/
char/bool/array/map/object/function/reference/shared_ptr/weak_ptr). Heavy types are wrapped in
non-atomic `strong_ptr` for O(1) copies. C++-bound values use `cpp_bound_ptr_` +
`cpp_bound_type_size_` (low 7 bits = byte size, bit `0x80` = unsigned) — decode via
`unchecked_as_int()/unchecked_as_float()`, NOT raw casts. References hold a `reference_holder`
(four owner-pinned modes: cell / container+index / instance+field_id / map+key — see
`docs/reference_model.md`); `deref()` resolves them. `clone()` = deep copy, copy = shallow.
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
`object_holder::keep_alive` anchor — don't remove it. A getter-only `.property()` (`nullptr`
setter, or a member whose type is not copy-assignable) registers a REJECTING setter: every
store spelling raises `Cannot assign to 'x': it is read-only` on both backends and the strict
checker reports it at load (`docs/diagnostics.md` family B).

```cpp
engine engine;
script_value r = engine.execute("2 + 2");           int v = r.as<int>();   // or r.as_int()
engine.add_function("square", [](script_int n){ return n*n; });
engine.add_variadic_function("print", [](const std::vector<script_value>& a){ /*...*/ });
engine.add_global("PI", script_value(3.14159, &engine));
engine.add_global_ref("hp", hpFloat);               // binds a live C++ variable (cpp_bound)
engine.execution_budget(1.0/60);   // wall-clock seconds per execute()/resume(); overrun raises a
                                   // catchable runtime error. 0 = unlimited. DEFAULT: 1.0 second.
                                   // Checked at loop back-edges + call entry (clock sampled every
                                   // 1024 ticks — no measurable hot-path cost).
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
- **Array representation follows the declared type** (Dev ruling 2026-09-02): `array<int>`
  / `array<float>` are raw int64/double buffers, always (bool/char/string buffers and
  `array<UserType>` object-slot buffers follow the same rule); `array<var>` (and literals without
  a spelled element type) is the only `script_value`-vector form; converting between them
  is an explicit assignment (`array<var> x = ints;`), nothing demotes silently, no
  Debug/Release difference. On the C++ side `as_array()`/`checked_as_array()` are the
  `array<var>` view (a typed node returns one error), readers use `script_array::for_each`,
  `bound_array<int64_t>&` mutates the buffer in place, `std::vector<script_int>` extracts
  unboxed. Typed map STORAGE is the open campaign (`docs/typed_array_design.md` stage 6,
  `docs/PERF_PASS_2026-09-02.md`).
- **Class-typed arrays admit subclasses** (Dev ruling 2026-09-02): `array<Base>` holds `Base`
  or any subclass (exact id first, parent walk on a name mismatch); each slot keeps its own
  class tag; `array<Derived>` converts to `array<Base>` by value; unrelated classes raise.
- **Map key types** (Dev ruling 2026-09-02): a direct store checks the declared key type
  (`map<string,int> m; m[5] = 1` raises; keys never coerce); `map<var,V>` takes any key; an
  `auto` key/value slot infers from a uniform literal (`map<auto,int> m = {5: 1}` is
  `map<int,int>`), stays dynamic for an empty literal, and a mixed literal raises.
- **`cast<T>(e)` / `cast<T&>(e)` / `cast<shared_ptr<T>>(e)`**: the explicit typed-boundary
  spelling. `cast<T>(e)` yields what `T tmp = e;` would hold (class targets copy by value
  when e's class is T or derives from T; primitives take the assignment conversion table);
  `cast<T&>(e)` is a reference to the same object (member reads and stores go through -
  `cast<T&>(h.p).t = 5`, the strict-mode fix for a subtype member on a base-typed receiver);
  `cast<shared_ptr<T>>(e)` shares the instance as a `shared_ptr<T>`. A class mismatch raises
  the catchable `Cannot cast 'X' to 'T'`; null passes the value/handle forms and fails the
  reference form. Char converts only through the cast (`cast<int>('a') == 97`,
  `cast<char>(97) == 'a'` range-checked 0..255, `cast<string>(c)`, `cast<char>("a")` for a
  one-character string; `char c = 65` still raises). Shared kernel `detail/cast_kernel.hpp`
  (`.jaibite` version 9 added the node).
- **One conversion table for every typed slot** (`detail/typed_slot.hpp` +
  `class_definition.hpp` `convert_for_typed_slot_core`): declaration, `=`, by-value parameter,
  typed return and field store all run the assignment table (`docs/static_checking.md` rule
  table) - `string` slots take anything, `bool` is truthiness, `int`/`float` take bool and
  `to_int()`/`to_float()` objects, exactly ONE converting ctor (no chaining), the array/map
  element boundary on every path; each site owns its rejection text.
- **`++`/`--` work on every lvalue** (elements, map entries, members, `this.x`, nested): a
  non-identifier target desugars at parse time to `+= 1` / `-= 1`; postfix yields the old value.
- **By-value returns copy, own locals move** (C++ [stmt.return]; `docs/script_extensible.md`
  6.5): `return e;` from a by-value function yields a value aliasing nothing the callee could
  name - a temporary moves, the callee's own local/parameter moves out of its dying slot
  (NRVO, no extra destructor), a global/field/element/captured read or a reference result
  deep-copies through the store kernel (`shared_ptr` handles share, scalars/strings are copies
  already), so `f().push(4)` never grows the callee's storage; `-> T&` returns alias and the
  top-level `return` is unchanged. Kernel `detail/return_conv.hpp`, one site per backend.
- **Read-only by-value parameters bind by handle** (`docs/parallel_design.md` §15): a by-value
  class/array/map parameter whose callee provably never mutates, aliases, compares, returns or
  passes it whole, reads only primitive/string terminals through it, and (with everything it
  calls) writes nothing but its own locals, binds the argument itself instead of a clone -
  unobservable by construction, `nocopy` still raises, methods/coroutines excluded in v1.
  Proof = the parallel admission walk in `proof_mode` (`prove_readonly_parameters`), cached per
  function against `engine::definition_epoch()`; kill switch `engine::parameter_alias(false)`,
  gates `parameter_alias_differential_gate` + `jaiscript_fuzz --no-param-alias`.
- **User accessors** `_get_x` / `_set_x`: member access `expr.x` (any receiver, `this.x`
  included) reads through the nearest USER `_get_x` in the class chain and stores through the
  nearest USER `_set_x` (compound assigns and `++`/`--` read then write through them); the
  synthesized field accessors never shadow a user one in either direction of the chain, and a
  child `_get_x` over a base field needs no `override`. A BARE `x` inside the chain's methods
  is the backing field (accessor bodies do not recurse). Such a field is off every member fast
  lane (vm member IC, field proofs — `class_definition::user_accessor`), is a non-lvalue to
  references, and adding/removing an accessor on hot reload takes effect for existing instances.
- Functions & lambdas with captures (`[=]`/`[&]`, auto-capture), variadics, default args,
  by-reference params.
- Script classes: inheritance with `super()`, ctor overloading + `: this(...)` delegation,
  field-init in **declaration order**, static members/methods, hot reload with instance migration.
- Coroutines: `coroutine`/`yield`, drivable by range-`for`; `coroutine` prefixes every function
  spelling (`coroutine name(...) [-> T]`, `coroutine function name(...) [-> T]`, `coroutine T name(...)`),
  free, method or static method, and produces the same declaration node.
- stdlib: `print`, `to_string`, `type_of`, `to_json`/`from_json` (full-precision floats via
  `std::to_chars`).
- **Parse-time type proofs** (`docs/static_checking.md` "Proofs"): the static checker's walk runs
  in every mode and stamps same-type/field proofs on AST nodes; both backends consume them behind
  total runtime guards (`detail/type_proofs.hpp`), so `engine::type_proofs(false)` is
  observationally identical (gates: `proof_differential_gate`, `jaiscript_fuzz --no-proofs`).
- **Escape hatches** (`docs/static_checking.md` "Escape hatches"): `extern [type|function] name;`
  declares a name defined outside the program (checker: a declared global, typed -> checked and
  proven; both backends: a no-op, the name resolves dynamically); `[[warn_mode]]` / `[[strict_mode]]`
  set the checker's mode for a unit (first declaration), a block (first statement) or one
  statement (nested function/lambda bodies keep the enclosing mode); innermost wins, the engine
  mode is the default, off ignores them, the runtime never changes. `.jaibite` version 11.
- **Catch variable = block-scoped local** (`docs/EXCEPTION_DESIGN.md` addendum): `catch (e)`
  binds `e` as a frame-slot local of the catch body (scope env at top level) - C++ `catch`
  semantics: shadows for the block only, invisible to callees and after the block, captured
  like any local, survives yields; the exception pair + in-catch marker are parked per
  coroutine. `.jaibite` version 12.
- **Runtime diagnostics** (`docs/diagnostics.md`): one catalog of every runtime error text, byte-identical
  on both backends. Null receiver / "no member" / arity+overload / typed-boundary texts name the
  action, the slot and the concrete type (`Cannot call 'push' on null ('v8' is null)`, `Vec2 has no
  member 'x'`, `'fib' expects 1 argument, got 2`, `Cannot pass string to parameter 'n' of 'fib'
  (declared int)`); kernels `null_member_diagnostic.hpp`, `arity_diagnostic.hpp`,
  `class_definition.hpp` (`typed_slot_rejection`, `overload_miss`). Error paths never intern a new
  string while the symbolizer is frozen (parallel workers get the slot-less shape of the same text).

```jaiscript
class Cat { int a = 0; Cat(int v) { a = v; } }
class Tiger : Cat { Tiger() : super(5) {} void roar() { print("ROAR " + to_string(a)); } }
auto t = Tiger(); t.roar();

for (auto& x : nums) { x *= 2; }                 // reference range-for mutates in place
for (auto kv : scores) { print(kv.first + ": " + to_string(kv.second)); }
```

---

## Status

Production-ready: complete lexer/parser + TWO backends at full Foundry parity (tree-walking
interpreter + bytecode VM), script classes, hot reload, C++ integration, JSON, coroutines
(interpreter: continuation replay; vm: fibers). The old `LEGACY_VM/` attempt was deleted;
`include/jaiscript/vm/` is the real one. Run the whole suite on the VM with
`jaiscript_tests.exe --backend=vm` (both backends must stay green).

A deep audit fixed: loop-`throw` infinite hang, range-`for` slot allocation & OOB on
shrink, `switch` continue/break leakage, map-read auto-insert, `<=>`/NaN map-key ordering,
int64 `==` precision, cpp-bound float/int reads, float→int UB, integer-overflow policy,
JSON float precision + recursion depth, and a dangling-temporary in C++ method chaining.
Regression tests live in `source/tests/language/review_regression_tests.cpp`.

**Current status:** both backends at full parity — 1443 tests Debug / 1474 Release
BENCHMARKS, green on interpreter AND vm. VM-perf branch highlights: in-loop VM call frames
(script calls no longer recurse natively), lvalue reference arguments (`f(obj.field)`,
`f(arr[i])`) via the shared kernel `detail/ref_lvalue.hpp` (verbatim in both backends), and
the thin-value fold — `script_value` is now 32 bytes with permanent static_assert gates.
The invariants that make all this hold are in `docs/invariants.md`; read it before touching
value layout, strong_ptr, raw-index dispatch, or the vm run loop. Parse-time type proofs are
consumed by both backends (proofs on/off byte-identical; strong-types + hot-reload batteries
and the fuzz differential gate pin it).

**Workflow:** edit → build Debug (`--target jaiscript_tests`) → run suite → fix → final Release
pass. Reproduce a bug as a foundry test first, confirm it's red, then fix to green.
