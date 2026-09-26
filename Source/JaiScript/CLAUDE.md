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

Debug has no LTCG, so it **links in ~5s incremental** (Release links in ~165s due to `/GL`+`/LTCG` — measured on the MSI; the link grows with the test exe), and MSVC's
`0xDDDDDDDD` freed-memory fill surfaces dangling-reference / use-after-free bugs that Release
can mask. (`JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES` is an inert CMake option: nothing in the runtime
is guarded by it any more.) **Iterate in Debug; do a final Release pass for the shipped config.**

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
- **`operator==`, `operator<=>` and `map_key_hash` must stay consistent**: `<=>` is a strict weak ordering
  (the map's iteration and serialization order) and `map_key_hash` hashes `<=>`-equivalent keys alike (the map's
  lookup, `core/script_map.hpp`). Float comparison uses `std::strong_order` (total order incl. NaN); complex types
  order and hash by held-pointer identity, never by `&storage_`.
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
  Debug/Release difference. The boundary recurses: `array<array<int>>` and `map<string, array<int>>`
  type their inner nodes too, and a float into an int element runs the scalar slot's checked
  kernel (`array<int> x = [1e100]` rejects like `int x = 1e100`; `detail/numeric_conversion.hpp`). On the C++ side `as_array()`/`checked_as_array()` are the
  `array<var>` view (a typed node returns one error), readers use `script_array::for_each`,
  `bound_array<int64_t>&` mutates the buffer in place, `std::vector<script_int>` extracts
  unboxed. Typed map STORAGE is the open campaign (`docs/typed_array_design.md` stage 6,
  `docs/PERF_PASS_2026-09-02.md`).
- **Class-typed arrays admit subclasses** (Dev ruling 2026-09-02): `array<Base>` holds `Base`
  or any subclass (exact id first, parent walk on a name mismatch); each slot keeps its own
  class tag; `array<Derived>` converts to `array<Base>` by value; unrelated classes raise.
  The element boundary judges the instance behind a `var` (a var-held `Base` stores; a rejection
  names the payload's type, never `any`). **Handle-typed elements take null** (Dev ruling
  2026-09-08): `array<shared_ptr<T>>`, `array<weak_ptr<T>>` and `map<K, shared_ptr<T>>` values
  hold null like a C++ vector of handles, and a `shared_ptr<T>` element takes a live instance of
  `T` or a subclass the way a `shared_ptr<T>` variable does (the slot owns the handle); by-value
  class and primitive elements stay non-nullable (`Cannot store null in array<Foo>`). A
  `shared_ptr<T>` type spells itself in full (`array<shared_ptr<Foo>>`); its id is that spelling
  interned, distinct from `Foo`'s. **Element conversions** (Dev ruling 2026-09-09): int and float
  convert both ways (the checked float-to-int kernel), bool and int convert both ways, everything
  else needs the exact type (`array<string> s = [1]` raises: `to_string(1)` or `cast<string>(1)`
  spells it), map keys never coerce.
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
  `to_int()`/`to_float()` objects, a `function` slot takes a callable or null (a std::function's rule), exactly ONE converting ctor (no chaining), the array/map
  element boundary on every path; each site owns its rejection text.
- **`++`/`--` work on every lvalue** (elements, map entries, members, `this.x`, nested): a
  non-identifier target desugars at parse time to `+= 1` / `-= 1`; postfix yields the old value.
- **An assignment expression's value is the destination's stored value** (Dev ruling 2026-09-08,
  C++ lvalue semantics): `auto r = (c.n = 3.9)` is int 3 for a typed local, a bare or explicit
  field, a static, an accessor (read back through `_get_x`), an array element or a map value,
  plain or compound; postfix keeps the old stored value. Both backends read the destination back
  only when the result is consumed (vm `next_op_discards_result` / `compound_flag_result_needed`,
  interpreter `expression_result_needed_`), so statement-position stores cost nothing extra. The
  top-level implicit result of an expression statement is never read back (a host convenience: no
  read-back copy, no extra getter call): a typed local or a plain static store hands the host the
  stored value because it converts before producing anything, an explicit field, accessor, element
  or map store hands it the right-hand side. A setter-only member (no field, no getter in the
  chain) yields its right-hand side (`member_reads_back`).
- **By-value returns copy, own locals move** (C++ [stmt.return]; `docs/script_extensible.md`
  6.5): `return e;` from a by-value function yields a value aliasing nothing the callee could
  name - a temporary moves, the callee's own local/parameter moves out of its dying slot
  (NRVO, no extra destructor), a global/field/element/captured read or a reference result
  deep-copies through the store kernel (`shared_ptr` handles share, scalars/strings are copies
  already), so `f().push(4)` never grows the callee's storage; `-> T&` returns alias and the
  top-level `return` is unchanged. Kernel `detail/return_conv.hpp`, one site per backend.
- **Aggregate copies are C++ copies** (Dev ruling 2026-09-05): copying a class instance, array or map
  copies value members/elements and SHARES `shared_ptr`/`weak_ptr` ones (`script_value::clone_element`
  is the member-copy rule; `clone()` on a bare handle stays the explicit pointee clone for hosts). A
  `weak_ptr<T>` field/static takes a shared handle like the declaration form.
- **`==` is memberwise on heavy values** (Dev ruling 2026-09-05; `detail/structural_equality.hpp`, one
  kernel for both backends): arrays elementwise, maps entrywise under the map's key ordering, class
  instance VALUES fieldwise (same class only; a user `operator==` takes precedence at every level),
  early out on size/class mismatch and the first unequal member; `shared_ptr` handles compare by
  pointer like `std::shared_ptr`; `same_object(x, y)` (engine-core builtin) is identity (refs resolve,
  handles unwrap, primitives/strings by value). Ordering stays off containers/instances. The C++-level
  `script_value::operator==`/`<=>` are unchanged (map-key identity ordering).
- **Unsafe parallel mode trusts the script's data, never the runtime's structures**
  (`engine::allow_unsafe_parallel(true)`, `docs/parallel_design.md`): the admission walk still
  warms every name and pins every element method it can, the element proof still detaches shared
  strings, and the worker walls (unpinned method calls, operator dispatch, bound-method thunks,
  accessor reads and stores) raise their verdicts in every mode; only the script-semantic
  verdicts and the element proof's verdict go lenient. Runner: `--unsafe-parallel`.
- **Parallel bodies see the enclosing function's locals** (`docs/parallel_design.md` §13): a body's
  free name that is a local of the frame its statement runs in is provisioned from that frame's
  slot (`parallel_for_stmt::frame_captures`, recorded by the parser), never from a global of the
  same name; a pinned element method's captures resolve in its class's scope (the global
  environment) and live in a separate per-worker method environment. `.jaibite` version 16.
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
- **Constructor exits** (Dev ruling 2026-09-26): a bare `return;` ends a constructor body early and the call
  yields the instance; `return expr;` in a constructor's own body is a parse error (a lambda or nested function
  inside it returns freely); a `throw` propagates, the assignment target keeps its value and the half-built
  instance runs no destructor.
- Coroutines: `coroutine`/`yield`, drivable by range-`for`; `coroutine` prefixes every function
  spelling (`coroutine name(...) [-> T]`, `coroutine function name(...) [-> T]`, `coroutine T name(...)`),
  free, method or static method, and produces the same declaration node.
- stdlib: `print`, `to_string`, `type_of`, `to_json`/`from_json` (full-precision floats via
  `std::to_chars`).
- **Parse-time type proofs** (`docs/static_checking.md` "Proofs"): the static checker's walk runs
  in every mode and stamps same-type/field proofs on AST nodes; both backends consume them behind
  total runtime guards (`detail/type_proofs.hpp`), so `engine::type_proofs(false)` is
  observationally identical (gates: `proof_differential_gate`, `jaiscript_fuzz --no-proofs`).
- **Typed scalar instruction set** (`docs/typed_regions_design.md`): the VM lowers a fused binary,
  compare-jump or scalar move whose operands the checker proved int/int or float/float (and whose
  operands are frame slots, temps or literals) to one `op_t_*` instruction with the slots in the
  instruction word; each op guards one tag per operand and falls back to the instruction it replaced
  (`chunk::typed_sites`), so texts and post-states are the general op's. Lowering follows the
  type-proofs flag (proofs off = typed ops off). A new opcode that a body may contain must be added
  to both env safe-lists in `vm_compiler.cpp` and, if it names a slot, to the debugger's locals walk.
- **Closed typed loops run as cell blocks** (`docs/typed_regions_design.md`, last section): a loop whose
  head is a typed compare-jump and whose body is only typed arithmetic, stamped scalar `T_LOAD`s and
  typed-lane index stores gets a cell-form copy; `op_cells_back` guards every slot, resolves the
  containers once, unboxes into the call record's raw area and runs the copy as one block; a miss flushes
  and re-runs the original instruction on the general form, so the interpreter stays the oracle. Controls:
  suites `Closed Typed Loops` and `Cell Loops`. The operation set, the region's graph, the verifier, the listing
  and the liveness are the module `vm/region_ir.hpp` (the compiler builds a `region_ir` and consults it). The
  listing tool: `engine::disassembly_sink` (every chunk the VM compiles, as compiled; off by default), the runner's
  `--disasm=<file>` (readable listings) and `--manifest=<file>` (`vm/chunk_manifest.hpp`, the identity gate of a
  compiler change: every field, exact constant bits).
- **Escape hatches** (`docs/static_checking.md` "Escape hatches"): `extern [type|function] name;`
  declares a name defined outside the program (checker: a declared global, typed -> checked and
  proven; both backends: the unit SHARES the visible global instead of declaring its own, and creates
  the shared global as a typed null placeholder when no unit has declared it, which the first declaring
  unit then adopts in place, Dev ruling 2026-09-12;
  a function extern declares nothing); `[[warn_mode]]` / `[[strict_mode]]`
  set the checker's mode for a unit (first declaration), a block (first statement) or one
  statement (nested function/lambda bodies keep the enclosing mode); innermost wins, the engine
  mode is the default, off ignores them, the runtime never changes. `.jaibite` version 11.
- **Declaration spellings the parser keeps for the checker**: `function f = expr;` / `function f;` (top level, body, field)
  is an auto slot at runtime (the same as a function-typed parameter) that carries `variable_decl::function_spelling`,
  so the strict checker can hold the slot to callables and null without the runtime changing; `ClassName& name = expr;` and
  `ClassName& name;` at statement level are reference declarations (the same lookahead that reads `ClassName& name(` as a
  reference-return function; C++: whatever can be a declaration is one). `.jaibite` version 15.
- **A namespace is a scope** (`docs/namespaces.md`): a namespace variable is one cell in the namespace's own
  environment (parented on the global one; `detail/namespace_scope.hpp`), so `ns::x = v`, `op=` and `++` write it under
  its declared type's assignment table, a namespace function reads and writes it by its bare name (its environment is
  the namespace's scope over the globals, never the caller's), an initializer sees the members declared before it and
  a reopened namespace redeclares in place; the variable declaration runs through each backend's ordinary declaration
  executor (the interpreter's `visit_variable_decl`, the VM's `exec_decl_var` as a one-declaration program) with the
  environment swapped to the scope. Host-registered entries (`script_namespace_data::variables`: `string::to_lower`,
  `jai::filesystem`) stay the host's values, read-only from script (`Cannot assign to 'name': it is read-only`).
- **A literal include inside a function body is spliced at parse time** (Dev ruling 2026-09-12; `docs/global_shadowing.md`):
  the included text becomes the function's own (its parameters and locals visible, its declarations the function's;
  the parse cache stamps the spliced files; no jaibite sibling for such a unit; `.jaibite` version 18, so a sibling
  written before the splice re-parses); a computed `include(p)` stays a
  runtime include that sees the environment only. Functions without includes pay nothing.
- **Program-scope redeclarations shadow by unit** (Dev ruling 2026-09-12; `docs/global_shadowing.md`): each
  executed source is a unit (a file by path, a literal by content), numbered at first execution; a variable
  declared again by ANOTHER unit gets its own cell; code resolves a name to its own unit's cell, else to the
  newest cell declared no later than its compile time (a function: the run that minted it; top-level code: its
  current run, so a re-run reader string reads as of now), so code compiled before a redeclaration keeps its
  binding; the SAME unit (a reload, a re-run hook string) writes in place, and
  functions/classes always replace in place (hot reload). Inside a unit, `{}` blocks scope as in C++ (a block
  declaration shadows for the block and vanishes after; a callee sees the outer binding). An include is TEXTUAL:
  the file runs at the include site, in the current scope and under the including unit (a later `var x` in the
  includer writes the included x in place; `{ include ... }` scopes it); an import is its own unit and a top-level
  declaration only (inside a function, a block, a class or a namespace it is a parse error: an import runs once and takes
  no scope; Dev ruling 2026-09-14). The host reads and
  assigns the newest cell. Zero cost
  on the VM's armed lookup paths (`string_symbolizer::shadow_count`, `environment::define_variable`), and zero
  per-call cost until the first shadow: the running unit is derived from the executing frame and the ambient
  stamp is written only while a name has versions (row 123.7).
- **Catch variable = block-scoped local** (`docs/EXCEPTION_DESIGN.md` addendum): `catch (e)`
  binds `e` as a frame-slot local of the catch body (scope env at top level) - C++ `catch`
  semantics: shadows for the block only, invisible to callees and after the block, captured
  like any local, survives yields; the exception pair + in-catch marker are parked per
  coroutine. `.jaibite` version 12.
- **Files and locations** (guide ch. 10 "Where files are"): ONE resolution ladder for `import`, `include`,
  the checker's closure gather and the `resolve_path(name)` builtin: absolute as given; relative against the
  ASKING file's folder, then the include paths in order, then the working directory (canonical spelling is
  the cache key). Engine-core location builtins on every engine: `current_script_dir/path` (the executing
  file, a function because imports change it), `root_script_dir/path` (the outermost `execute_file`),
  `executable_dir` (alias `bin_dir`), `app_data_dir/app_cache_dir(name)` (platform defaults,
  `engine::app_directory_resolvers` for hosts), `working_dir`, `parent_dir`, `file_name`. `parent_dir`
  weakly canonicalizes its result (not purely lexical); `file_name` is lexical. Core source loading,
  location queries and disk caches do touch the filesystem. General `read_file`/`write_file`/`list_dir`/
  `writable_dir` are host functions (the runner has them). `.jaibite` siblings mark their own file (v14)
  so a moved folder keeps its names.
- **Filesystem privilege**: enabled by default. C++ `eng->disable(jai::filesystem)`, `enable(...)`,
  `is_enabled(...)`; `jai::filesystem` aliases scoped `jai::privilege::filesystem`, the only built-in
  privilege implemented. Script `reflect::is_enabled(requirements...)` is query-only: at least one
  argument, builtin integer flags and/or permission-name strings, ALL enabled. Unknown names return
  false without interning/registering; invalid bits/types raise. All core path queries (even lexical
  `file_name`), `include`/`import`,
  `execute_file`, explicit `jaibite_load`/`jaibite::save` require it. With it disabled, automatic disk
  cache I/O/metadata and checker dependency discovery are skipped; source strings and in-memory jaibite
  bytes remain usable. Filename labels grant no disk access. Deep refusals use `checked_result` with
  the shared, catchable text `Insufficient privilege: filesystem.`; unhandled errors leave execution.
  Change privileges only while idle or on the engine execution thread (including C++ callbacks):
  subsequent gated operations see the change, work already started completes. Active parallel regions
  reject changes until workers join. No concurrent cross-thread mutation, cancellation, atomics or
  locks; the mask is plain engine-owned state. Optional final requirements on
  `add_function(name, callable, jai::filesystem)`, `add_variadic_function` and `dynamic_binder::method`
  are checked inside the
  callable, preserving checks through aliases/reflection. The runner gates file/directory/archive/
  download/audio-file start operations this way. Other native bindings must check or declare their
  requirements; exposed streams and shared C++ state remain host-controlled. Separate engines isolate
  script environments, not native objects or the process. Do not claim network/audio privileges, an OS
  sandbox, or a general virtual-filesystem policy from this API. On the POSIX curl-executable fallback
  only, `Download.received()` also requires filesystem because it polls the output file; WinHTTP/libcurl
  progress queries use memory and remain available.
- **Custom permissions**: `eng->register_privilege(name)` returns nothing and registers a string name,
  disabled on first registration and idempotent thereafter (grant unchanged). `"filesystem"` aliases
  the built-in without changing its grant. Use one string or an `initializer_list<string_view>` for
  `enable`, `disable`, `is_enabled`, checked `require_privilege` and optional callable requirements:
  `add_function(name, callable, {"filesystem", "app.edit"})` requires ALL. Original builtin flag APIs
  stay available. Names are reusable across engines; each engine's registration/grants are independent.
  Unknown queries return false without interning; grant/require/binding operations reject unknown names,
  never auto-register. Names live only in the engine interner; binding resolves requirements once to
  engine-local slot IDs, checked without name lookup or allocation at invocation. Registration/binding
  may allocate. The shared checked kernel reports the first missing builtin, then custom permissions in
  registration order: `Insufficient privilege: {name}.` No new locks/atomics/globals; registration and
  grant changes obey the same execution-thread/idle-only, no-active-region ownership rule. A custom name
  gates only callables/host code that require it; naming `"network"` does not install a network gate.
- **`utf8_string`** (Dev ruling 2026-09-25, guide ch. 9): its own storage kind (`TYPEID_UTF8_STRING`, `script_utf8` node:
  bytes + character caches) with the string library in characters (`detail/utf8_methods.hpp` wraps every string method;
  size/at/substr/find/insert/erase/pads/reverse/split("")/`u[i]`/range-for count characters, results keep the kind).
  Operators unwrap it to its string at the head of every general entry and rewrap a string result (`detail/utf8_operands.hpp`,
  before the char promotion); `==`/`<=>` compare bytes, a map key keeps its kind. Caches (`detail/utf8_text.hpp`: ASCII flag,
  count, two remembered positions, breadcrumbs every 64 characters) live only on utf8_string, clear on every write
  (`unchecked_as_utf8_ref` is the copy-on-write gate), and are written only while no parallel region runs. `utf8_string` is
  an identifier the parser reads as a type (like `double`); `.jaibite` version 19.
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
