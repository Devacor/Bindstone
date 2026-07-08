# Portability status (2026-07 audit)

Scope: the library proper — `include/jaiscript/` + `source/implementation/`. The `examples/`
programs are Windows console demos (guarded `#ifdef _WIN32` + `<conio.h>`/`SetConsoleMode`) and
are exempt by design; same for the fuzz driver's `_popen`/`popen` split
(`source/tests/fuzz/fuzz_main.cpp:182-195`, which already carries a POSIX branch).

## 1. The library's entire OS/compiler-specific surface

Three sites. Everything else is standard C++20 (`std::filesystem`/iostreams for file IO — no
path-separator assumptions, no Win32 calls, no MSVC pragmas).

- **`detail/integer_ops.hpp` — `mul_ovf`** (:58-76): three-way split.
  `__builtin_mul_overflow` (GCC/Clang), `_mul128` (MSVC, the only `<intrin.h>` user), and a
  generic division-check fallback. The GCC/Clang path **compiles and is exercised** (clang 18
  sweep, §3). The generic fallback is correct: `a == 0` short-circuit, both `±1 × INT64_MIN`
  overflow cases handled before the `out / a` probe, and that probe can never hit the
  `INT64_MIN / -1` hardware trap (the only `out == INT64_MIN, a == -1` shape is caught by the
  explicit case above it).
- **`<intrin.h>` containment holds**: `integer_ops.hpp` is included by exactly five impl
  files (engine, interpreter, interpreter_dispatch, parser, vm_backend .cpp) and by no header.
- **`native_stack_low()`** (`environment.cpp:8-36`): the ONLY `windows.h` include in the
  library, `.cpp`-local, `WIN32_LEAN_AND_MEAN`+`NOMINMAX` guarded. Non-Windows builds compile
  the `#else` branch and return `false` — safe, but it means the stack-headroom guard (which
  converts an imminent native stack overflow into the catchable `max_recursion_depth` error)
  is **Windows-only**; elsewhere the sole protection is `JAI_MAX_CALL_DEPTH`. A POSIX port
  should add a `pthread_attr_getstack` branch (there is no pure-std way to read stack bounds).
- **`JAI_FORCEINLINE`** (`detail/interpreter.hpp:29-37`) and **`static_type_name.hpp`**
  (:17-23, `__FUNCSIG__` vs `__PRETTY_FUNCTION__`) both carry correct three-way splits.
  Unknown-compiler behavior: plain `inline`, and an empty type-name signature (degrades
  binder diagnostics, not correctness).

## 2. Layout / representation assumptions

- **`strong_ptr` control-block derivation** (`strong_ptr.hpp:93`,
  `offsetof(control_block<T>, storage)`): `control_block<T>` is NOT standard-layout (data
  members in both base and derived class), so this `offsetof` is *conditionally-supported*
  ([support.types.layout]/1). MSVC, GCC, and Clang all support it for this shape; Clang/GCC
  emit `-Winvalid-offsetof` (one site, warned once per instantiating TU — warning, not error,
  and the computed offset is correct). If a compiler ever genuinely rejects it, the
  standard-blessed rework is
  aggregation instead of inheritance: make `control_block<T>` standard-layout as
  `{ control_block_base base; alignas(T) unsigned char storage[sizeof(T)]; }` and pass
  `&cb->base` as the erased handle — same layout, unconditional `offsetof`. Do not silence
  the warning without reading invariants.md §2 first.
- **jaibite format** (`detail/ast_serializer.hpp`): the header comment says "little-endian,
  x64 only", but the code is **host-endian-independent** — every multi-byte primitive is
  built/split with byte shifts (`fixed_u32/64` :67-72/:536-547, varint, zigzag), and `f64`
  round-trips through `uint64_t` via `memcpy` (:81-84, :561-565; no type-punning). The real
  format requirements are: little-endian *on disk* (by definition, either host order works),
  IEEE-754 `double`, 64-bit `script_int`. A big-endian host would interoperate correctly.
- **No other punning**: `value.hpp` has zero `memcpy`/`union`/`bit_cast` reinterpretation;
  cpp-bound reads decode through `unchecked_as_int()/unchecked_as_float()` over pointers to
  genuinely-typed host variables.

## 3. Second-compiler smoke — clang 18 + libc++ (2026-07)

No clang-cl ships with this VS 2026 install, and the VS 2026 MSVC STL hard-rejects
clang < 20 (`yvals_core.h` STL1000), so the clang-cl/MS-STL combination is untestable here.
Instead all 18 library TUs were compiled with the NDK's **clang 18.0.3 + libc++,
`--target=x86_64-linux-android33`, `-std=c++20 -Wall -Wextra -fsyntax-only`** — a stronger
probe than clang-cl: different STL, and no `_WIN32`/`_MSC_VER`, so it exercises the
`__builtin_mul_overflow` path and the `native_stack_low` fallback for real.

Result: after one fix (below), **16 of 18 TUs compile with 0 errors**; the remaining two hit
a single structural issue. Findings, ranked:

1. **FIXED — `std::from_chars(const char*, const char*, double&)` does not exist on libc++**
   (`serialization/json_archive.hpp`, two call sites in `parse_number_flat`). libc++ ships
   integer-only `from_chars` and — being incomplete — leaves `__cpp_lib_to_chars` undefined,
   which is the feature-test now keyed on: a `from_chars_double` shim falls back to `strtod`
   (locale decimal-point swapped in, ERANGE → `result_out_of_range`, full-consumption `ptr`
   preserved). This un-broke engine.cpp, interpreter.cpp, and value.cpp under clang.
   `stdlib/json.hpp:329` (`std::stod`) is unaffected — it compiles everywhere, though it
   inherits the host locale like any `stod` (pre-existing on MSVC too).
2. **OPEN — include cycle leaves `class_definition` incomplete in `engine_impl.hpp`**
   (environment.cpp, vm_compiler.cpp — the two TUs whose first effective include is
   `class_definition.hpp`). Cycle: `class_definition.hpp:6` → `engine.hpp` → (tail, :853)
   `engine_impl.hpp` → `#include "class_definition.hpp"` is a guard no-op, so the
   *non-dependent* member accesses `class_def->get_name()/create_instance()/get_type_id()`
   (`engine_impl.hpp:57,69,73`) name members of an incomplete class. MSVC's lenient template
   parsing accepts this; two-phase-lookup compilers (GCC/Clang) reject it. Fix belongs in the
   `class_definition.hpp`/`engine.hpp` layering (forward-decls instead of the engine.hpp
   include, or hoist the class_definition-dependent templates out of engine_impl.hpp) — not
   applied here because class_definition.hpp is under active surgery on this branch.

Warning classes (none load-bearing):

| class | unique sites | where |
|---|---|---|
| `-Wunused-parameter` | many | binder/interpreter internals (style) |
| `-Winvalid-offsetof` | 1 | strong_ptr.hpp:93 — expected, see §2 |
| `-Wdelete-non-abstract-non-virtual-dtor` | 1 | `jai::engine` has a virtual `execute` (engine.hpp:103) but a non-virtual dtor (:76). Harmless today (nothing derives from engine); becomes UB the day a consumer subclasses engine and deletes through `engine*`. Either virtualize the dtor or de-virtualize `execute` (no overriders exist). |
| `-Wpessimizing-move` | 6 | ref_lvalue.hpp:551,:557; interpreter.cpp:4723,:5220,:5536,:6024 — `return std::move(x)` / `T v = std::move(f())` shapes that *block* copy elision (mild perf smell, relevant on this branch) |
| `-Wreorder-ctor` | 1 | environment.hpp:70 (member init order; benign) |
| `-Wunused-variable`(+set) | 7 | interpreter.cpp:3357,:4016,:6215; parser.cpp:370,:658,:1407,:3032 |
| `-Wmissing-field-initializers` | 1 | script_class.hpp:244 (value-init is the intent) |

`detail/thread_pool.hpp` also compiles clean under the same configuration.

## 4. JAISCRIPT_WRAP_ON_OVERFLOW — first real build

The wrap policy had never been configured before this audit. Built Debug/Ninja with
`/DJAISCRIPT_WRAP_ON_OVERFLOW=1` and ran the full Foundry suite: **1593 tests green on both
backends** (81 suites; interpreter and vm). The wrap-mode test branches are real, not
decorative — ~25 sites across review_regression / release_review_regression / strong_types /
vm_backend suites key off `engine::throw_on_overflow()` and assert two's-complement wraparound
results under this config. The identical tree also passed 1593 × 2 under the default checked
policy in both Debug and Release, so both policies are verified against the same suite.

**CI recommendation**: the wrap config is one compile definition away from silently rotting
again. Add two cheap gates to whatever CI materializes: (1) Debug + `JAISCRIPT_WRAP_ON_OVERFLOW`,
full suite, both backends; (2) any clang build (even `-fsyntax-only`) to keep the
GCC/Clang-only code paths compiling.

## 5. What is guaranteed vs Windows-only

Guaranteed (standard C++20, verified by §3):
- Everything in `include/jaiscript/` — including `detail/thread_pool.hpp`
  (std::thread/mutex/condition_variable only) — and all of `source/implementation/`,
  modulo §3 item 2 (the one remaining include-order defect; MSVC-only until fixed).
- jaibite files written on any conforming host load on any other (§2).

Windows-only (all deliberate, all guarded):
- The `native_stack_low()` headroom guard's actual measurement (§1).
- `examples/` (crawler, demoreel, gloom): VT-mode console setup, `_kbhit`/`_getch`.
- The MSVC-specific fast paths (`_mul128`) — equivalents exist for GCC/Clang.
