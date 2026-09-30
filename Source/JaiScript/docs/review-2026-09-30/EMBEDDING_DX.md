# Embedding DX: an outsider builds a host, 2026-09-30

Baseline: master at 1c3e7974. The host is `examples/embed_quickstart/`, a standalone CMake project that
consumes JaiScript through `add_subdirectory`. It was written using only the public material: the site
(`docs/site/`: embedding chapters, guide, reference), `README.md`, `CONTRIBUTING.md`, `docs/BUILD.md` (linked
from the README) and the headers under `include/jaiscript/`. No file under `source/` was read.

The host covers: an engine, a script file run with `execute_file`, script functions called from C++ and C++
functions called from script, a `dynamic_binder` class set (constructor overloads, methods, properties, `operator+`,
a base class with virtual dispatch, a registered-class member bound by reference), a signal member with a script
receiver, JSON and binary saves through the archives (including a `shared_ptr<Base>` holding a derived object and a
script-side `to_json` snapshot), import-based hot reload with instance migration and a deliberately broken edit, the
execution budget, the filesystem privilege and a custom privilege, every error surface caught in C++, and a DAP
debugger attach. It exits 0 when all 27 checks hold.

## Executed gates

| Gate | Result |
|---|---|
| MSVC 19.51, Ninja, Debug, interpreter / VM | all checks pass on both backends |
| MSVC 19.51, Ninja, Release, interpreter / VM | all checks pass on both backends |
| Clean build, library + host, Debug | 17 s |
| One-file edit of the host (`main.cpp`, ~340 lines) | Debug 12 s, Release 66 s |
| DAP attach, raw client over TCP | initialize, attach, setBreakpoints, stopped at game.jai:12, stackTrace, scopes, variables (`frame = 1`), continue, disconnect |
| `verify_site.py --no-samples` after the doc fixes | passes |

Interpreter and VM output is identical except one stack-trace line (friction 10).

## What worked

- **The API matches the docs.** Once CMake was right, the host compiled on the first attempt. Every signature the
  chapters show (`add_function`, `add_global`, `make_object`, the binder chain, `signal`/`signal_emitter`,
  `to_json(*eng, v)`, `enable`/`disable`/`register_privilege`, `debug::listen`) is the one in the headers.
- **Binding is pleasant.** Operators by token (`.method("+", &Vec2::operator+)`), `base_class<Actor>()` with virtual
  `describe()` dispatch, a `Vec2` member bound by reference (`m.pos = Vec2(1, 2) + spawn_offset()` writes the C++
  member), and a plain `jai::signal` member exposed with one `.property` line all behave as the binder chapter says.
- **Signals cross cleanly.** A C++ `hurt()` emit runs a script lambda receiver; a receiver that divides by zero
  reaches `set_script_error_handler` with a stack instead of throwing into the emit site.
- **Import hot reload is the standout.** Overwriting `live/rules.jai` mid-run is picked up by a later frame within the
  0.2 s window; a live `Tuning` instance keeps `crit`, gains `bonus` from its initializer; an uninitialized
  `int hits_dealt;` keeps counting. A broken edit (`return "three"` from an `int` function) raises a catchable
  compiler-style listing naming `live\rules.jai:2:23` with a caret, and the program keeps the rules it had.
- **Safety rails do what they say.** A `while (true)` inside `try/catch` is stopped by the budget and the script's
  catch never runs; the message carries its own fix-it. A sandbox engine with `disable(jai::filesystem)` refuses
  `import`; a `"game.cheats"` requirement on a host function is enforced until `enable("game.cheats")`.
- **Error listings are first-class.** Undeclared names fail the load with file:line:col, the source line and a caret;
  parse errors carry a location; runtime errors come with `format_stack_trace()`.
- **The debugger is real.** A bare TCP client drives the DAP loop from attach to continue and sees the stopped
  frame's locals (stepping was not exercised).

## Friction, ranked

Each item names the doc or API change that removes it.

1. **The documented `add_subdirectory` route does not compile.** `source/CMakeLists.txt` spells the library's include
   path as `$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>`; as a subproject `CMAKE_SOURCE_DIR` is the consumer's
   directory, so the library's own precompiled header fails with `C1083: Cannot open include file:
   'jaiscript/core/engine.hpp'`. `docs/BUILD.md` says only the consumer needs `target_include_directories`, which does
   not help the library target. The BUILD.md snippet also leaves `JAISCRIPT_BUILD_LSP` on (the option is in neither the
   README table nor BUILD.md), so `tools/lsp` builds in the consumer and fails the same way. Workaround used:
   `target_include_directories(jaiscript PUBLIC <JaiScript>/include)` after `add_subdirectory`.
   *Fix:* `${PROJECT_SOURCE_DIR}/include` (one line); add `JAISCRIPT_BUILD_LSP` to the README table and the BUILD.md
   snippet; default every tool/test/example option OFF when JaiScript is not the top-level project
   (`PROJECT_IS_TOP_LEVEL`); link "Consuming it from another project" from the Embedding Quickstart. There is no
   `install()`/`find_package` route (the `$<INSTALL_INTERFACE:include>` has no install rules behind it); either add an
   export set or say add_subdirectory/FetchContent is the only route.

2. **MSVC consumers need undocumented flags.** A TU that includes `core/engine.hpp` and `stdlib/stdlib.hpp` fails in
   Debug with `C1128: number of sections exceeded object file format limit: compile with /bigobj`. The library passes
   `/bigobj` PRIVATE; the in-tree hosts (`examples/crawler`, `tools/jaiscript`) add `/bigobj /permissive-` and the
   static runtime by hand. The forced
   `/MT` is documented in BUILD.md only. The library build also prints its own warnings into the consumer log (six
   C4834 in `vm_backend.cpp`, C4996 `getenv` in `engine.cpp`).
   *Fix:* make `/bigobj` an INTERFACE (PUBLIC) option of the target; honor a parent's `CMAKE_MSVC_RUNTIME_LIBRARY`
   instead of forcing `/MT`; clear the library's own warnings.

3. **Calling a script function from C++ is undocumented.** No chapter shows it; Safety Rails mentions `as_function()`
   in passing. From the headers: `eng->get_variable("tick").as_function()(std::vector<script_value>{...})` returns a
   `checked_result<script_value>`, and `eng->format_error(r)` renders a failure. `convert_script_function<Sig>`
   (`engine_impl.hpp`) makes a typed `std::function` but swallows errors into the error handler and returns `R{}`.
   Whether a plain host call with no script running needs `engine::external_call_guard` is unclear from the Signals
   chapter's wording ("outside a normal call"); the host uses it defensively.
   *Fix:* a Quickstart section "Calling script from C++" (lookup, arguments with `make_value`, the checked result, the
   guard rule stated exactly). Consider a throwing convenience, `eng->call("tick", 1)` or
   `eng->function<int(int64_t)>("tick")`, beside the non-throwing one.

4. **Polymorphic saves of a binder-bound class need an undocumented registrar tag.** The Serialization chapter says
   polymorphism "rides the binding you already wrote" and that `.base_class<B>()` lets a `shared_ptr<Base>` holding a
   Derived write `$type`. With `dynamic_binder<Monster>(...).base_class<Actor>()`, `to_json` throws `Cannot serialize
   unregistered polymorphic type 'class Monster' through pointer-to-base 'class Actor'. Register it with
   jai::registrar<T, Context>("Name") or derive from jai::property_owner<T, ...>.` What works is
   `jai::registrar<Monster, void, jai::bases<Actor>>("Monster", configure)` plus `jai::bind_registrar(eng)`; the
   `bases<>` tag appears only in a `registrar.hpp` comment. That header's usage comment also advertises a
   `jai::registrar<Player, "Player">` form the template does not have.
   *Fix:* either make `.base_class<B>()` register the serialization name and relation, or correct the chapter and
   document `registrar<T, Ctx, bases<B...>>`; name `bases<>` in the error text; drop the NTTP comment.

5. **Breakpoints silently miss when `execute_file` gets a non-canonical path, and report `verified: true`.** The
   Debugging chapter says `execute_file` stamps `std::filesystem::canonical()`. Given
   `D:/.../embed_quickstart/scripts\game.jai` (a forward-slash define joined with `fs::path`), traces show that exact
   mixed spelling, `setBreakpoints` answers `verified: true`, and the script never stops. Passing
   `fs::canonical(...)` makes the same breakpoint stop at once.
   *Fix:* canonicalize in `execute_file` as documented (or normalize both sides in the matcher); answer `verified:
   false` for a path no loaded unit carries.

6. **`debug::listen` on a busy port fails silently.** A second process calling `jai::debug::listen(*eng)` while
   52472 is taken gets a connector whose `port()` is 0: no exception, no log line. The chapter says "the port is
   whatever your host logs at startup" and that the runner probes upward; `listen` does neither. Bindstone's own client
   listens on 52472 by default, so a host embedded next to it collides.
   *Fix:* probe upward like the runner, or throw; document `port() == 0` as the failure signal until then.

7. **Release iteration for a consumer costs a full LTCG link.** The library compiles with `/GL` (top-level
   `add_compile_options`), so every consumer Release link is a whole-program link: 66 s for a one-line edit of a 340-line host (Debug: 12 s). There is no MSVC
   switch (`JAISCRIPT_ENABLE_LTO` is GCC/Clang only).
   *Fix:* a `JAISCRIPT_MSVC_LTCG` option, OFF by default when not top-level (the Bindstone fast-iteration pattern).

8. **The hot-reload docs contradict each other.** The embedding Hot Reload chapter says an import's "mtime check
   happens when the import statement executes" and "unchanged files cost one mtime check", and never mentions
   `import_recheck_interval`, the call form `import(...)`, groups or `jai::force`. Guide chapter 10 describes the
   windowed passes correctly, then its intro calls import "never an expression, never a value" (the reference table
   repeats it) and its last section says an unchanged file "costs a single mtime comparison". Neither chapter shows
   the host side of a reload loop (C++ calling a per-frame script function, `engine::import_recheck_interval(0)` for
   tests).
   *Fix:* one snapshot of the import rules in the embedding chapter with a C++ loop; strike the per-reach mtime
   sentences and the "never an expression" row.

9. **Reading script state back into C++ is a guessing game.** `get_variable` is not in the docs. `as_array()` on an
   `array<string>` global throws `array<int>/array<float> hold raw buffers: the script_value vector view exists only
   for array<var> ...`, naming int/float for a string array (the fix-it `as<std::vector<T>>` is right). The Quickstart
   covers scalars only; `execute_file` is missing from the reference's Engine API table.
   *Fix:* a Quickstart section "Reading script state from C++" (`get_variable`, `as<std::vector<T>>`,
   `as<std::shared_ptr<T>>`, typed vs `array<var>`); generalize the raw-buffer text to name the actual element type;
   add `execute_file`, `get_variable`, `has_function` to the reference table.

10. **Error texts need a second pass for hosts.** `what()` appends the category, so logs read `Insufficient privilege:
    filesystem.: Insufficient privilege` and `Script execution budget exceeded - raise engine::execution_budget or break
    up the work: Script execution budget exceeded`; there is no documented accessor for the bare message. An anonymous
    unit is `script:1:9` in a check listing and `<script>:1:10` in a parse error. The outer frame of a script function
    the host called reads `at <script> (<script>:1)` on the interpreter and `at <script> (<script>:0)` on the VM, a
    byte-parity divergence and a misleading frame (no script was running).
    *Fix:* a `message()` without the category (or drop the suffix when it repeats the text); one spelling for the
    anonymous unit; a `<host>` frame spelled identically on both backends.

## Smaller items

- The Quickstart snippets carry no `#include` lines (guide ch. 1 and the tour do: `core/engine.hpp`,
  `stdlib/stdlib.hpp`). The umbrella `jaiscript/jaiscript.hpp` exists and is never mentioned.
- `jai::static_check_error` is declared in `detail/type_checker.hpp`, a detail header, although hosts catch it.
- `engine.hpp` interleaves the public API with internals (`parallel_compile_mutex`, `execution_limits()`,
  `math_alias_rows`, `note_host_entry`, `count_hot_reload_field_migration`); `add_functionWithArity` is the one
  camelCase name. Its comment on `make_object(shared_ptr)` ("doesn't support property access on C++ objects") is
  contradicted by the binder chapter and by this host.
- `execute_file` writes `game.jaibite` next to the source. The chapter documents the cache; the Quickstart should
  say "add `*.jaibite` to .gitignore or call `jaibite_cache(false)`".
- The JSON pretty-printer puts an array's first element on the bracket line (`[10,\n    20,`).
- The DAP stack frame for code inside `tick()` is named `script`, not `tick`.
- An uncaught exception in a host `main` exits with code 3 and loses buffered output; the Quickstart's error section
  would benefit from a two-line `try`/`catch (const std::exception&)` around the whole host.

## Doc typos corrected alongside

Separate commits, each unambiguous against the headers or the site itself:

- README and docs/README: the guides are 14 chapters each, not 13.
- Debugging chapter and `tools/vscode-jaiscript/README.md`: the class is `jai::debug::debug_connector`; a paused script
  is exempt from the execution budget (the text said "the 1 s cap").
- Reference: `jai::to_json(*eng, v)` / `jai::to_binary(*eng, v)` (the engine argument was missing); the `.jaibite`
  format is version 26 (`k_jaibite_version`), not 20.

## Time per step

Agent wall clock (an agent reads and builds far faster than a person; the human column is a judgment, not a
measurement).

| Step | Agent | Human estimate |
|---|---|---|
| Read README, Quickstart, binder, safety rails, hot reload, serialization, signals, debugging, coroutines, modules, reference | 5 min | 1.5-2 h |
| CMake consumption (three configure/build rounds, friction 1-2) | 7 min | 30-90 min; the first blocker has no doc answer |
| First full host run, three runtime surprises (container read, exit-3 abort, polymorphic save) | 5 min | 45 min |
| Debugger attach (DAP client, canonical path, busy port) | 7 min | 30-60 min with VS Code |
| VM backend, broken-reload path, Release pass | 3 min | 20 min |
| Doc fixes, site verifier, commits | 4 min | - |

## Rating

**6.5 / 10 for an outsider today.** The runtime and the C++ API are strong (8.5): signatures match the docs, the
binder is concise, errors are excellent, hot reload with check-then-apply is the best moment of the whole exercise,
and the debugger works over a plain socket. The documentation is broad and mostly
accurate but has holes exactly where a host starts (calling script from C++, reading results back, the reload loop
from the host side) and several contradictions (6.5). Build integration is the weak point (3.5): the documented
recipe fails in the first ten minutes with an error the docs do not explain, and MSVC consumers must copy flags from
the in-tree examples. Fixing friction 1-3 (a one-line CMake change, an INTERFACE `/bigobj`, and one Quickstart
section) would move the outsider experience to about 8.5.
