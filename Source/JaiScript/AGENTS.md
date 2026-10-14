# AGENTS.md — JaiScript

JaiScript is an embedded, C++-like scripting language with hot reload: a static library plus headers, snake_case
throughout. Every script runs on one of two backends held to identical behavior: the bytecode VM (the default) and the
tree-walking interpreter (the oracle that keeps the VM honest). Same results, same printed output, same error text,
byte for byte.

`CONTRIBUTING.md` is the human workflow and `docs/BUILD.md` the build reference; this file is the short version plus
the rules a change must not break.

## The language contract is the site

`docs/site/` (guide, embedding guide, reference, stability) is the specification. Before changing behavior, read the
page that states the rule; change the page in the same change. When code and site disagree, one of them is a bug.
Error texts are catalogued in the reference's "Run-time errors" table and must match it.

## Build

```bash
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build       -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-debug --target jaiscript_tests
```

On Windows run these from a Visual Studio developer prompt (`VsDevCmd.bat -arch=x64`). Iterate in Debug: no LTCG, so
links take seconds, and MSVC's freed-memory fill turns a dangling reference into a crash. Finish with a Release pass;
optimizer-dependent bugs show up only there. Other targets: `jaiscript` (library), `jaiscript_runner` (binary
`jaiscript`), `jaiscript_fuzz`, `jaiscript_lsp`, `jai_gloom`, `jai_crawler`, `jai_demoreel`.

## Test

```bash
build-debug/bin/jaiscript_tests                 # interpreter
build-debug/bin/jaiscript_tests --backend=vm    # VM
jaiscript_tests "Script Class"     # bare pattern is ADDITIVE: matching suites (all their tests) OR matching test names
jaiscript_tests "Suite.Test"       # dot form is the strict AND
jaiscript_tests --verbose          # per-test names and timings (finds a hang)
```

A new test `.cpp` needs a CMake reconfigure (the list is a configure-time `file(GLOB_RECURSE)`); a test added to an
existing file needs nothing. Reproduce a bug as a failing test first, watch it go red, then fix it.

```cpp
#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
namespace jai::foundry::tests {
class my_tests : public suite {
public:
	my_tests() : suite("My Suite") {}
	void forge_tests() override {
		test("two_plus_two", [this]() {
			auto e = make_engine();                               // honors --backend=
			check_eq((int64_t)4, e->execute("2 + 2").as_int());  // check_eq(expected, actual)
		});
	}
};
}
using my_tests = jai::foundry::tests::my_tests;   // FOUNDRY_REGISTER is at global scope: alias first
FOUNDRY_REGISTER(my_tests)
```

`make_engine()` applies the runner's backend and sets `import_recheck_interval(0)`; `engine::make()` gives the default
(VM) engine in both suites and keeps the 0.2 s import window. A recoverable parse error makes `execute()` return null
rather than throw, so "deep nesting does not crash" is tested as "execute returns and the engine still works".

## Gates a change must pass

- `jaiscript_tests` and `jaiscript_tests --backend=vm`, green in Debug **and** Release.
- The differential fuzzer: `jaiscript_fuzz --isolate` reports no new divergence (triage one as a suppression in
  `source/tests/fuzz/fuzz_harness.hpp`, naming the backend at fault; fixing it removes the entry).
- The example hash gates and the language-server tests, exactly as CI runs them:
  `BIN=build/bin bash tools/ci/gates.sh all` (or name gates: `tests-vm gloom crawler demoreel gatorquest lsp`).
- A compiler change: dump `jaiscript --manifest=<file> <script>` before and after over the affected programs; the
  manifests (every chunk field, exact constant bits) change only where the change intends.
- A change to what the AST serializer writes bumps `k_jaibite_version` (`include/jaiscript/detail/ast_serializer.hpp`).
- Docs: `python docs/site_tools/verify_site.py --runner <bin>/jaiscript` (links, structure, leakage, retired error texts,
  and every sample run on both backends under strict). A sample shows its real output.

## Code rules

- No non-const `static` or `thread_local` storage. State that outlives a call belongs to an engine; one static cache
  breaks multiple engines on multiple threads. `static const` non-pointer data is fine.
- Every `script_value` carries its engine: `script_value(v, eng)`, `script_value(std::monostate{}, eng)` for null.
- `operator==`, `operator<=>` and `map_key_hash` stay consistent: `<=>` is a strict weak ordering (map iteration and
  serialization order) and equivalent keys hash alike. Float keys order totally (a NaN key is found again; `0.0` and
  `-0.0` are one key); complex values order and hash by held-pointer identity.
- Never read a `script_value` after moving from it (the move constructor nulls the source).
- Integer arithmetic goes through `jai::ints::try_add/try_sub/try_mul/try_div/mod`, never raw `a + b` on `script_int`:
  the overflow policy (checked by default, `JAISCRIPT_WRAP_ON_OVERFLOW` wraps) lives there, and arithmetic is never UB.
- Error text is built only on the failing path (a cold, non-inlined helper), never eagerly on a hot path, with bytes
  identical to the catalogued text.
- Code marked `KEEP BYTE-PARALLEL` exists in both the interpreter and the VM: change both sides together. A rule both
  backends need lives once, in a shared kernel under `include/jaiscript/detail/`; writing the same rule twice is the
  signal to add a kernel.
- Comments state a non-obvious *why* in one line: no narration, no history.
- Match the file: tabs in `interpreter.cpp` and `parser.cpp`, otherwise follow the surrounding code.

### Header hygiene (consumers pay for every include)

- Binding instantiations, not header parsing, dominate consumer compile time: keep binder thunks and container
  conversions out of headers that do not need them (schema-driven primitive property binders live out of line in
  `schema_binding.cpp`).
- `signals/signal_decl.hpp` does not include `signal_impl.hpp`; hot-path headers include `signal_decl.hpp`, and `.cpp`
  files that fire script receivers include `signal.hpp` (or `signal_impl.hpp`).
- `checked_result.hpp` does not include `<format>`; error messages use plain `{0}`/`{1}` substitution.
- `detail/integer_ops.hpp` (and its `<intrin.h>`) stays out of `interpreter.hpp`. Keep `<intrin.h>`, heavy standard
  headers and implementation headers out of widely included core headers.
- Parse recursion (`MAX_PARSE_DEPTH`) and JSON nesting (`JAI_MAX_JSON_PARSE_DEPTH`) stay low enough to throw before the
  native stack overflows in a Debug build.

## Design principles

- C++ semantics are the contract: lifetimes, copies, references, evaluation order, conversions. The exception is a
  static-typing artifact such as slicing; there the dynamic choice wins (a subclass stored into a base slot keeps its
  class).
- Both backends stay at parity. A change that satisfies one backend is not done; the interpreter is never dropped to
  simplify the VM.
- A simplification lands only when it is behavior- and error-text-identical, and no slower.
- Uplift the general path rather than special-casing a shape: a fast path that only one spelling reaches is suspect,
  and operator overloads and dynamic values must keep working on every path.
- Move work to compile time and cut accidental copies before anything else.
- JaiScript is a package; host applications and the examples are consumers. Consumer-shaped code in the library is
  suspect.

## Architecture

```
include/jaiscript/  core/ (value, engine, dynamic_binder, class_definition)  detail/ (lexer, parser, AST, shared kernels)
                    vm/ (opcodes, compiler, backend, disassembler)  stdlib/  signals/  properties/  serialization/  testing/
source/implementation/   library .cpp files (one TU each)       source/tests/   Foundry suites, fuzz/
tools/   runner, lsp, vscode-jaiscript, ci/gates.sh             examples/   GLOOM, GRUBWELL, demoreel, Gator Quest
docs/    BUILD.md, site/ (the specification), site_tools/
```

Pipeline: lexer → parser (recursive descent, AST) → the static checker (strict by default; it also stamps type proofs
both backends consume behind runtime guards) → one `execution_backend`: `jai::vm` or the interpreter, chosen per engine
with `set_backend` before the first execute. Builtin container and string methods live in one registry both backends
share. `script_value` is a 32-byte type-erased value with `strong_ptr`-wrapped heavy types; `clone()` is a deep copy,
copying is shallow. Interned symbol IDs are engine-bound: never share or serialize them across engines.

## Implementation notes

A new opcode joins both env safe-lists in `vm_compiler.cpp` and, if it names a slot, the debugger's locals walk.
<!-- internal-only -->
Design notes live in `internal/`. Read before touching: `internal/invariants.md` (value layout, `strong_ptr`, raw-index
dispatch, the VM run loop), `internal/typed_regions_design.md` (the VM's typed regions and opcodes),
`internal/type_proofs_design.md` (proofs).
<!-- /internal-only -->
The code and the site are the reference for everything else.

## Commits

One-line commit messages: a title, no body, no trailers.
