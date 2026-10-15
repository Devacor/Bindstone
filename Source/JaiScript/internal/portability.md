# Portability status

Scope: the library proper — `include/jaiscript/` + `source/implementation/`. The `examples/`
programs guard their console code (`<windows.h>`, `<conio.h>`, VT mode) with `#ifdef _WIN32`;
the fuzz driver splits `_popen`/`popen` (`source/tests/fuzz/fuzz_main.cpp`).

## 1. The library's OS/compiler-specific surface

Everything outside the sites below is standard C++20 (`std::filesystem`/iostreams for file IO, no
path-separator assumptions). Every site is guarded and carries a portable branch.

- **`detail/integer_ops.hpp` — `mul_ovf`**: three-way split.
  `__builtin_mul_overflow` (GCC/Clang), `_mul128` (MSVC, behind `<intrin.h>`), and a
  generic division-check fallback. The generic fallback is correct: `a == 0` short-circuit, both
  `±1 × INT64_MIN` overflow cases handled before the `out / a` probe, and that probe can never hit
  the `INT64_MIN / -1` hardware trap (the only `out == INT64_MIN, a == -1` shape is caught by the
  explicit case above it).
- **`<intrin.h>` containment**: `integer_ops.hpp` reaches engine.cpp, interpreter.cpp,
  parser.cpp and vm_backend.cpp directly, and interpreter.cpp, interpreter_dispatch.cpp and
  vm_backend.cpp through `detail/scalar_ops.hpp` (included by `detail/binary_handlers.hpp`); no
  public header includes those two. The other `<intrin.h>` includes sit in .cpp files behind
  diagnostic build flags (`JAISCRIPT_VM_COVERAGE`, `JAISCRIPT_VM_PROFILE`,
  `JAISCRIPT_VM_COARSE_PROFILE`). `detail/thread_pool.hpp` includes `<immintrin.h>` on x86 only,
  for `_mm_pause`.
- **OS calls in `engine.cpp`**: file stat for the `execute_file` fast lane
  (`GetFileAttributesExW` / `stat`), the process id in jaibite temporary names, environment
  lookups (`_dupenv_s` / `getenv`), the per-platform app data and cache directories, and
  `engine::executable_path()` (Windows, macOS, `/proc/self/exe`).
- **`debug/connector.cpp`**: Winsock vs BSD sockets for the DAP connector.
- **`stdlib/time.hpp`**: `localtime_s` vs `localtime_r`.
- **Compiler attributes**: `__declspec(noinline)` vs `__attribute__((noinline))` in
  `vm/vm_backend.hpp` and the cold-path macros (`JAI_MATH_COLD`, `JAI_VM_COLD_PATH`);
  `vm_coverage.cpp` (a diagnostic build) uses `dbghelp` on MSVC.
- **`native_stack_low()`** (`source/implementation/environment.cpp`): the `windows.h`
  dependency stays `.cpp`-local, `WIN32_LEAN_AND_MEAN`+`NOMINMAX` guarded. The Windows
  implementation queries the current thread's stack bounds and checks a 256 KiB headroom
  margin. Guarded recursive execution paths raise the catchable `max_recursion_depth`
  text, `Maximum recursion depth exceeded - possible infinite recursion`; `.jaibite`
  reads and writes instead raise `jaibite: AST nesting too deep` or
  `jaibite: type nesting too deep`. The reader/writer also retain their existing
  1000-level traversal limit, independently of headroom. Non-Windows builds return
  `false` and have **no native-stack headroom measurement**. Their numeric call-depth
  and traversal limits still apply, but do not establish safety for every native
  recursion path or host stack size. A POSIX implementation needs platform-specific
  stack-bound discovery (there is no pure-std way to read stack bounds).
- **`JAI_FORCEINLINE`/`JAI_NOINLINE`** (`jaiscript_fwd.hpp`) and **`static_type_name.hpp`**
  (`__FUNCSIG__` vs `__PRETTY_FUNCTION__`) carry three-way splits. Unknown-compiler behavior:
  plain `inline`, and an empty type-name signature (degrades binder diagnostics, not
  correctness). `jaiscript_fwd.hpp` also refuses an MSVC build without exception unwinding.

Call-depth caps are engine-owned: interpreter 256 and VM 250000 by default.
`eng->max_call_depth(jai::backend_type::interpreter, depth)` and the equivalent VM
setter accept `[1, INT_MAX]` between executions; the one-argument overload reads
the configured cap. Setters reject an executing engine or active parallel region,
and parallel workers snapshot both settings at region entry. Raising either cap does
not disable Windows headroom checks. VM self tail calls that reuse a frame run in
constant space without increasing call depth; the execution budget still applies.

The parser's `MAX_PARSE_DEPTH = 250` is a weighted recursion counter, not an AST-depth
bound or a complete native-stack guarantee. Parentheses consume extra weight but
disappear from the AST, left-associative expressions grow AST depth iteratively,
and recursive assignment and type parsing are not covered by that counter.

## 2. Layout / representation assumptions

- **`strong_ptr` control-block derivation** (`strong_ptr.hpp` `cb_storage_offset`,
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
  built/split with byte shifts (`fixed_u32/64`, varint, zigzag), and `f64`
  round-trips through `uint64_t` via `memcpy` (no type-punning). The real
  format requirements are: little-endian *on disk* (by definition, either host order works),
  IEEE-754 `double`, 64-bit `script_int`. A big-endian host would interoperate correctly.
- **No other punning**: `script_value::storage` is a tagged union read only through its active
  alternative; its `memcpy`s relocate whole payload words, and a math value reaches a host type by
  `memcpy` of its component words into the trivially-copyable `math_adapter<T>::native_type`.
  cpp-bound reads decode through `unchecked_as_int()/unchecked_as_float()` over pointers to
  genuinely-typed host variables.

## 3. Compilers

The library builds with MSVC, GCC and Clang. `.github/workflows/ci.yml` builds and tests
windows-msvc, linux-gcc (g++-14) and linux-clang, each in Debug and Release, through
`tools/ci/gates.sh`. libc++ ships no `std::from_chars` for `double`, so
`serialization/json_archive.hpp` keys `from_chars_double` on `__cpp_lib_to_chars` and falls back
to `strtod` (locale decimal point swapped in, ERANGE → `result_out_of_range`).
GCC/Clang warn `-Winvalid-offsetof` at the one `strong_ptr` site (§2); the offset is correct.

## 4. JAISCRIPT_WRAP_ON_OVERFLOW

The wrap policy is one compile definition. Suites in review_regression,
release_review_regression, spec_conformance, strong_types and vm_backend key off `engine::throw_on_overflow()` and
assert two's-complement wraparound under it. CI builds only the default checked policy, so the
wrap configuration is verified by a local Debug build with `JAISCRIPT_WRAP_ON_OVERFLOW=1` and the
full suite on both backends.

## 5. What is guaranteed vs Windows-only

Guaranteed (standard C++20, built by CI on three compilers):
- Everything in `include/jaiscript/` and `source/implementation/`, with the guarded sites of §1.
- jaibite files written on any conforming host load on any other (§2).

Windows-only (all deliberate, all guarded):
- The `native_stack_low()` headroom guard's actual measurement (§1).
- `examples/` (crawler, demoreel, gloom): VT-mode console setup, `_kbhit`/`_getch`.
- The MSVC-specific fast paths (`_mul128`) — equivalents exist for GCC/Clang.
