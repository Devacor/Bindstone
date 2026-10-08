# Simplification census: dead code, accidental complexity, consumer-shaped code

This census lists what the implementation can lose without losing behavior: code nothing reaches, layers that only
forward, shims for finished migrations, knobs nothing sets, and code shaped around one consumer. It complements
`SIMPLIFICATION_AUDIT.md`, which covers rules implemented more than once across paths (its X1-X19 dead-code rows are
not repeated here), and `fast_path_census.md`, which covers VM fast lanes and their general twins.

Paths are relative to `Source/JaiScript/`. Anchors refer to master `4e80bdba1`.

## Status

| Part | State |
|---|---|
| Static evidence (include graph, caller scans, macro census, dependency direction) | **complete** |
| Source coverage (llvm-cov over the suites, games, fuzz, CLI, LSP, DAP) | **incomplete**: the coverage tree is configured but not built (the shared timing lock was not free during the census window) |
| `/OPT:REF` removal map | **not run** |

Every row below is backed by a caller scan across `include`, `source`, `tools`, `examples`, `bench`, and the consumer
trees (`Source/MV`, `Source/Game`, `Source/Workbench`, `Source/Editor`, `Source/SolutionSpecific`). A row says
"0 callers" only when a full-text search found none in any of them. The coverage pass adds the untested-live list for
function bodies and the dead branches inside live functions; until it runs, that list holds only what static reading
shows.

**Coverage recipe** (to finish the census): configure `out/build/x64-Coverage` with Clang 22 (VS LLVM),
`-DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS_DEBUG=-O1 -g0" "-DCMAKE_CXX_FLAGS=-fprofile-instr-generate -fcoverage-mapping"
-DCMAKE_EXE_LINKER_FLAGS=-fprofile-instr-generate`, lld-link; build `jaiscript_tests jaiscript_fuzz jaiscript_runner
jaiscript_lsp jai_gloom jai_demoreel jai_crawler`; run the suite on both backends, the `tools/ci/gates.sh` game
commands, the JaiDOOM `p10_timedemo.jai` (vm, and interp `simonly`), a fuzz sample over every band, the runner's flags
(`--help --version --selftest --check=* --disasm --manifest --coverage --benchmark-main --miss-every`, REPL over stdin,
`net_smoke.jai`) and `tools/lsp/tests/lsp_test.py --only fixtures|gatorquest`, each with its own `LLVM_PROFILE_FILE`;
merge with `llvm-profdata merge -sparse` and report with `llvm-cov report/export` over all binaries, ignoring
`tests|examples|External`. Each step fits an 18-minute lock hold.

## Bars

- A **safe deletion** removes code with no callers, no includers, or a branch whose condition cannot hold. Compiled code
  that runs does not change, so it owes the suites on both backends, the game hashes and manifest identity, and no
  timing beyond a spot check.
- A **refactor** replaces a layer with the simpler shape named in its row. It owes the full gate set and, where it
  touches a hot path, the perf A/B.
- Documented public API stays unless Dev says otherwise, even with no in-repo use.
- The property and signal tooling (`property_owner`, `JAI_PROPERTY`, `JAI_SIGNAL_PROPERTY`, observable properties,
  signal binders), the binder helpers, registrars and math adapters are embedding features. Code our own harnesses do
  not exercise is listed for tests, never for deletion.
- Duplication between the interpreter and the VM is deliberate (the interpreter is the oracle) and is not counted.

## 1. Safe deletions

Ranked by lines × confidence ÷ risk. "Lines" counts deletable lines (estimates marked ~).

| # | Item | Location | Lines | Evidence | Confidence | Risk |
|---|---|---|---:|---|---|---|
| S1 | Tracked never-built tests | `source/tests/integration/test_binary_serialization.cpp.disabled`, `test_jaiscript_binary_serialization.cpp.disabled`, `test_map_copy_behavior.cpp.disabled` | 1,074 | Outside every glob (`.disabled`); the first includes Cereal and a header that no longer exists | high | none |
| S2 | `source/implementation.zip` | tracked, 428 KB | (binary) | A July snapshot of 24 implementation files; nothing references it | high | none |
| S3 | Root scratch files | `test_cat_only.jai`, `test_cat_tiger.jai`, `test_cat_tiger2.jai`, `test_point_pattern.jai`, `test_class_type_check.cpp`, `test_fallthrough_debug.cpp`, `test_json_output.cpp`, `test_operator_parse.cpp` | 250 | No build file or script names them; `test_point_pattern.jai` calls the removed global `sqrt`. Two docs count them as "non-production" sites (`docs/thin_value_spec.md`, `thin_value_rebaseline.md`); those counts update | high | none |
| S4 | Stale root build scripts | `build_benchmarks.bat`, `build_release.bat`, `build_tests.bat` | 11 | Hard-coded VS 2022 paths and old build dirs; every repo hit for `build_tests.bat` is `Source/MV/Tests`'s own | high | none |
| S5 | Stale root docs | `MIGRATION_PLAN.md` (890, calls itself historical; every link points at `docs/MIGRATION_PLAN.md`), `STRING_TO_DO.md` (116, describes the pre-registry string methods) | 1,006 | Unreferenced | high | none |
| S6 | `test_string_static_method` target + file | `source/tests/test_string_static_method.cpp`, `source/tests/CMakeLists.txt:269-282` | 88 | Does not compile: calls `foundry::make_engine()` without foundry, unlinked runner, and uses `new` (a foreign-keyword parse error). It breaks the default `cmake --build` (`docs/review-2026-09-30/EMBEDDING_DX.md:239`) | high | none |
| S7 | `bst_profiler` target + file | `source/tests/CMakeLists.txt:213-235`, `source/tests/performance/bst_profiler.cpp`, the FILTER at :76 | 159 | Built only under BENCHMARKS; calls unqualified `make_engine()` that does not resolve | high | none |
| S8 | `performance/Makefile` | `source/tests/performance/Makefile`; doc line `source/tests/README.md:112` | 88 | Names files and dirs that do not exist | high | none |
| S9 | Commented-out `JsonParser` | `include/jaiscript/stdlib/json.hpp:265-481` | 217 | A `/* ... */` block marked deprecated | high | none |
| S10 | Orphan `core/builtin_methods.hpp` | whole file | 213 | Nothing includes it (`interpreter.hpp`'s include resolves to `detail/`); a mutable static singleton superseded by `detail/builtin_methods.hpp` | high | none (installed headers: an outside includer would break) |
| S11 | Write-only C++ type-converter pipeline | `dynamic_binder.hpp:1663-1684`, `engine::register_type_converter[_impl]` (`engine.hpp:975-990`, `engine.cpp:3364`), `engine::convert_to_value` (`engine.cpp:3372`), `conversion_registry::cpp_type_converters_`, `get_cpp_type_converter` (`conversion_registry.cpp:729`), `convert_cpp_type_from_void` (:734) | ~70 | Every bound type registers a converter; no reader is ever called. Also one fewer lambda instantiation per bound type (binding instantiations dominate compile time) | high | none (compile time improves) |
| S12 | Conversion-registry dead layer | `conversion_registry.hpp:112,116,119,158-168,195`; `conversion_registry_templates.hpp:75-118`; `conversion_registry_impl.hpp:100-104` | ~50 | `register_vector_conversion` / `register_map_conversion` have 0 callers; their helpers recurse into themselves or call a declared-never-defined function; `register_standard_conversions` is never defined; the `[[deprecated]]` engine-less overload only throws | high | none |
| S13 | Dead subscript-resolver hook | `engine.cpp:3414-3430`, `execution_backend.hpp:219`, `vm_backend.hpp:312-313,1050`, `interpreter.hpp:1409-1410,1438-1439`, `interpreter_backend.hpp:101-103` | ~35 | Installed and stored by both backends, never invoked. Keep the `symbolize("[]")` call if symbol order must stay byte-identical | high | none |
| S14 | Orphan `core/bound_cpp_vector_registration.hpp` | whole file | 69 | Never included; duplicates the registration inside `dynamic_binder.hpp` | high | none |
| S15 | Unused value members | `value.hpp`: `unchecked_as_vec2/vec3/vec4/ivec2/ivec3/color` (2469-2474), `is_unique_reference` (2635), `set_weak_ptr` / `set_shared_ptr` / `get_shared_ptr_value` (2670-2697), `clear_engine` (2453), `has_type_tag` (377), `is_weak_ptr_type` (641), `has_reference_semantics` (646), `cpp_member_ref` (2079), `is_parallel_borrow` (269) | ~60 | Definition is the only occurrence in every tree | high | none |
| S16 | `make_registered_object` | `core/engine_make_object.hpp` (48), declaration `value.hpp:368-370`, an unused include in `source/tests/semantics/script_container_tests.cpp:6` | 52 | Nothing calls it | high | none |
| S17 | `cereal_migration.hpp` | `include/jaiscript/serialization/cereal_migration.hpp` | 50 | Nothing includes it; its body is behind `#ifdef CEREAL_NVP`. Bindstone holds no Cereal header or name (only history comments in `SolutionSpecific/BindstoneClient/main.cpp`) | high | none |
| S18 | Unused class-definition / registry / archive / interpreter members | `class_definition.hpp`: `validate_method_args` 340, `is_vm_class` 402, `get_static_method_names` 1092, `has_property_setter` 1573, `make_script_class_definition` 2468; `class_registry.hpp:76` `make_shared_script_class`; `engine.hpp:1410` `extractParameterTypes(+Impl)`; `string_symbolizer.hpp:207` `intern_many`; `archive_impl.hpp` `write_custom` 748, `has_preread_properties` 1338; `binary_archive.hpp:719` `get_object_property_count`; `interpreter.hpp` `make_namespace_object` 365, `has_return_value` 394, `get_environment_shared` 601, `clear_continuations` 174 | ~120 | Definition is the only occurrence (word-boundary scan) | high | none |
| S19 | `function_binder.hpp` dead helpers | `detail::call_with_tuple` 733, `values_to_tuple` 739, `call_with_converted_args` 745, `FunctionBinder::process_argument` 1040 | ~40 | 0 callers; duplicate the live call path | high | none |
| S20 | `engine::register_type_conversion` (both overloads) | `engine.hpp:892-921`, `engine.cpp:3268-3271` | ~35 | 0 callers; the template overload builds an engine-less `script_value` (against the contract). Undocumented | high | none |
| S21 | `JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES` | `CMakeLists.txt:38`, `source/CMakeLists.txt:76-79`, `source/tests/CMakeLists.txt:91`, `CMakeSettings.json:38,77`; `environment::validate_parent_chain` (empty, `environment.hpp:327`) and its 7 calls (`environment.hpp:54,63,74,313`, `environment.cpp:862,890,916`) | ~25 | The option is never read, the define guards nothing, the function is empty. `CLAUDE.md`'s note about it goes too | high | none |
| S22 | Write-only type-name registry | `typeNameRegistry` (`engine.cpp:621`), `register_type_name_impl`, `get_registered_type_name` (`engine.cpp:3347-3362`, `engine.hpp:936`) | ~25 | The reader has 0 callers | high | none |
| S23 | `dynamic_binder::script_value_to_cpp` | `dynamic_binder.hpp:1528-1553` | ~25 | 0 callers; a second conversion ladder beside `value_converter` | high | none |
| S24 | `checked_result` twins | `static_message()` ×3 (rename its 4 call sites, `engine.cpp:4569,4575`, `interpreter.cpp:3937`, one test, to `message()`), `propagate() &&` ×3 (0 callers), the wrong "Legacy compatibility" labels (`checked_result.hpp:166,287,369`) | ~25 | `message()` is the dominant spelling | high | low (universal header) |
| S25 | `JAI_ISOLATED_TEST` and `REGISTER_FOUNDRY_TESTS` | `foundry.hpp:411-430`, `auto_register.hpp:60-64` | ~25 | Nothing defines the macro; only `.disabled` files use the registration macro | high | none |
| S26 | `binding_traits.hpp` leftovers | `type_name<>` + 4 specializations (126-151), `should_reject_binding` (83, 97) | ~35 | 0 uses | high | none |
| S27 | `execution_backend::push_scope` / `pop_scope` virtuals | `execution_backend.hpp:200-206`, `interpreter_backend.hpp:65-71`, `vm_backend::push_scope` (`vm_backend.cpp:727`) | ~20 | Never called through the interface; both backends override the only defaults that use them; the VM's `push_scope` has no caller | high | none |
| S28 | Unused aliases | `typed_property_owner` / `has_typed_property_owner_v` (`property_manager.hpp:509-514`; the feature stays); `script_class_instance` (`script_class.hpp:262`, 7 renames); `script_value::storage_type()` (`value.hpp:444`, 6 renames) | ~15 | Aliases kept "during migration"; the migration is done | high | low |
| S29 | Stale includes | `interpreter.cpp:42 <sstream>`, `:52 <fstream>`; `vm_backend.cpp:94 <fstream>`, `:95 <sstream>`; `vm_compiler.cpp:17 <array>`; `parser.cpp:12 <iostream>`, `:14 <cctype>`; `value.cpp:9 <sstream>`; headers: `detail/interpreter.hpp:49 <deque>`, `:50 <iostream>` (also drops an iostream static initializer from every including TU), `class_definition.hpp:19 <cstdio>`, `:20 <cstdlib>` | 14 | No symbol of the header appears in the file. Header removals may expose TUs that leaned on them transitively: fix those with their own includes | high (impl), medium-high (headers) | low |
| S30 | `registrar.hpp` dead macros + fictional usage | `JAI_HAS_CONCEPTS`, `JAI_HAS_NTTP_STRING` (`registrar.hpp:17-30`; C++20 is required, so `fixed_string.hpp` either goes or is included plainly), the `jai::registrar<Player, "Player">` comment (:178-190), which describes a form that does not exist (`EMBEDDING_DX.md:93-96` asked for its removal) | ~25 | Nothing tests the macros | high | none |
| S31 | Other dead macros and knobs | `JAI_MAX_CALL_DEPTH` branch (`execution_limits.hpp:21-27`; also the stale "10000" in `source/tests/CMakeLists.txt:179,190`, `vm_backend_tests.cpp:453`); `JAI_TEMPLATE` (`archive.hpp:62`); the redundant `JAI_FORCEINLINE` copy (`interpreter.hpp:13-22`) | ~20 | Defined or tested nowhere | high | none |
| S32 | `engine::setHasCustomNumericOps` | `engine.cpp:3526-3535`, `engine.hpp:764`; its one caller `engine.hpp:724` calls `set_has_custom_numeric_operators` | ~12 | Same body as the public snake_case setter | high | none |
| S33 | Declared, never defined | `environment::get_this_field_ptr` (`environment.hpp:385`) | 1 | — | high | none |
| S34 | `source/implementation/types.cpp` | the file, `JAISCRIPT_SOURCES` line 23, `jaiscript.cpp` line 7 | 8 | Empty translation unit; MutedVision's wildcard follows | high | none |
| S35 | Unreferenced scratch scripts | `source/tests/scripts/test_array_map_methods.jai`, `test_static_errors.jai`, `test_weak_ptr_minimal.jai` | 139 | No test, CMake or doc names them | medium-high | none |
| S36 | Stale comments | `engine.cpp:3537` ("Removed duplicate..."), `interpreter_backend::get_interpreter`'s "backwards compatibility" (it is used by `parallel_transform.cpp`), `script_value::type()`'s "Legacy alias" (it is the dominant spelling) | 3 | — | high | none |

**Totals.** About 1,050 lines of compiled code and headers (S9-S34), about 1,950 lines of never-built tests and
scratch (S1, S3, S6-S8, S35), about 1,000 lines of stale root docs (S5), and the 428 KB zip. None changes code that
runs, except S29's header includes (compile only) and S11/S13, which delete registrations nothing reads.

**Landing order.** One batch for the files no one builds (S1-S8, S35: no gates beyond a configure and build), one for the
headers and members (S9-S34: suites on both backends, manifest identity, MutedVision lib build since public headers move;
`vm_backend.*` rows S13 and S27 rebase around the VM agents' landings as standalone commits).

## 2. Accidental-complexity refactors

| # | Item | Today | Simpler shape | Lines | Confidence | Risk |
|---|---|---|---|---:|---|---|
| R1 | `ast_visitor` (`detail/ast.hpp:121-171`) | 45 pure virtuals with one implementation (`interpreter`); dispatch is a switch on `node_type`, and nothing uses the base polymorphically (no `ast_visitor*`/`&`, no `accept`) | Delete the base and drop `override`; `visit_*` calls inside the interpreter stop being virtual | ~50 | medium-high | interpreter codegen changes: a perf spot check (expected neutral or better) |
| R2 | `interpreter_backend` (`detail/interpreter_backend.hpp`, 194 lines) | A pure adapter: every override forwards to `interpreter_->` (its comment says it exists to keep the interpreter unchanged) | `interpreter` derives from `execution_backend` directly; the few renames (`execution_budget`, `execute_textual`, `running_unit`, which only returns `current_unit()`) become the interface names | ~180 | medium | `interpreter` is held by `shared_ptr` with `enable_shared_from_this`; `parallel_transform` reaches through `get_interpreter()` |
| R3 | `ref_element_constraint_compatible` (`ref_lvalue.hpp:618`) | Calls `container_element_compatible` with the same arguments; 2 callers | Call the kernel | 3 | high | none |
| R4 | `JAI_SP_FORCEINLINE` / `JAI_SP_NOINLINE` (`strong_ptr.hpp:22-31`) | Duplicate `JAI_FORCEINLINE` / `JAI_NOINLINE` (`jaiscript_fwd.hpp`) | One spelling | ~20 | medium | include order (check that `strong_ptr.hpp` sees `jaiscript_fwd.hpp`) |
| R5 | `effect_summary.hpp` (`detail/`, 164 lines) | No production includer; only two `vm_backend_tests.cpp` tests (~95 lines) check it agrees with the production transient-read classifier. It reads as a staged unification (row B2/B3 of `SIMPLIFICATION_AUDIT.md`) that was not adopted | Either adopt it as that row's effect module, or delete it with its two tests | 164 + 95 | medium | **needs Dev** (it belongs to audit row 10's plan) |
| R6 | `polymorphic.hpp:322-324` duck-typed `initialize()` | The default-constructible polymorphic load calls `ptr->initialize()` whenever the type has one. It is MV's Component convention (`component.h:256`), undocumented, untested, inconsistent (the `load_and_construct` path and the plain `shared_ptr` path do not call it), and a host type that happens to have `initialize()` gets it called on load | Delete it; hosts use the documented `property_owner::post_load(any_archive_reader&)` | 3 | high that it is unshaped; medium that no Bindstone type depends on it (MV Components construct through `load_and_construct`, which calls `initialize()` itself) | low after a Bindstone `-verifyassets` run |
| R7 | `_cpp_object` as a named overflow field | The C++ backing of a hybrid instance lives in a reserved field name with collision guards (`class_definition.hpp:115-119`, `type_checker.cpp:1448,3216,3489`) | A dedicated slot on `class_instance`; the reserved name and its guards go | ~30 net | medium | serialization format (`$script`/hybrid archives) must stay readable: judgment, needs a design pass |
| R8 | Path canonicalizers (`engine.cpp:2971`, `engine.cpp:4477` `location_spelling`, `interpreter.cpp:2420`, `debug/controller.cpp:17`) | Four helpers with different fallbacks | **Not proposed:** merging changes the spelling of paths that do not exist (diagnostic texts) | — | — | — |

## 3. Documented or public API with no in-repo use (needs Dev)

| # | Item | Location | Lines | Uses | Docs |
|---|---|---|---:|---|---|
| D1 | `set_backend(std::unique_ptr<execution_backend>)` / `backend_type::custom` and the non-pure defaults that exist only for a third backend (`language_less` / `array_transform` texts, `execution_backend.hpp:131-140`; "Coroutines are not supported by this backend", `coroutine.cpp`) | `engine.hpp:77,943` | ~40 | no third backend anywhere | `docs/site/embedding/03-backends.html:47`. Stale "vm until phase 5" comments go either way |
| D2 | `stdlib::register_math_functions` | `include/jaiscript/stdlib/math.hpp` (body is empty: math is engine-core), `stdlib/stdlib.hpp:7,22,42-43`, `docs/site_tools/check_api_catalog.py:37` | 7 | `register_all` calls it | not documented by name; `CLAUDE.md` names `register_math()` as the idempotent host entry |
| D3 | `engine::get_weak_engine` / `get_shared_engine` | `engine.hpp:176-184` | 8 | 0 | not documented; wrap `weak_from_this` / `shared_from_this` |
| D4 | `core/static_binder_impl.hpp`, `core/dynamic_binder_serialization.hpp` | headers | — | tests and docs only | documented (`02-dynamic-binder.html:481`); **keep** |
| D5 | Unused binder macros `JAI_BIND_TEMPLATE_VARIANTS_*`, `JAI_TEMPLATE_BINDER_2`, `JAI_STATIC_BINDER_AUTO` | `template_binder.hpp`, `static_binder*.hpp` | — | 0 | binder toolkit: **keep**, list under tests (U5) |
| D6 | `JAISCRIPT_ENABLE_PROFILING` | `CMakeLists.txt:36`, `source/CMakeLists.txt:99`, `source/tests/CMakeLists.txt:285` | ~15 | adds `/Zi /Oi /PROFILE` only | convenience; keep or drop |
| D7 | `static_method_tests` target | `source/tests/CMakeLists.txt:251-267` | 17 | builds two suites already in `jaiscript_tests`, for the VS dropdown | `CLAUDE.md` records that its siblings were removed |
| D8 | `bench/bench_json.cpp` | includes Cereal's bundled rapidjson; no CMake builds it | file | 0 | delete, or vendor rapidjson |

## 4. Untested but live (for tests, not deletion)

Static reading only until the coverage pass runs; the VM files' figures are in `spec_conformance.md` ("Execution
census": `vm_backend.cpp` 85.2% of lines, `vm_compiler.cpp` 96.8%).

| # | Feature | Gap | Test to add |
|---|---|---|---|
| U1 | `registrar` with a context (`registrar<T, Context, bases<...>>`, `bind_registrar<Context>`) | No test calls `bind_registrar`; the context form runs only in Bindstone (`Game/Script/script.cpp:131`, ~30 registrar sites) | A neutral context struct (`struct host_ctx { int marker; }`): the configure lambda receives it; replay into two engines |
| U2 | Property and signal tooling paths our harnesses skip | Exact list waits on coverage | Tests with a neutral (non-MV) host type, plus doc examples where missing |
| U3 | `JAI_SIGNAL_PROPERTY`'s emitter naming | Hard-codes the `name##Signal` emitter (`signals/signal_property.hpp:64`) | Covered with the generalization in E2 |
| U4 | `thread_pool` | Tested inside `strong_ptr_tests.cpp:404-612` | Move to its own suite file |
| U5 | Template / static binder macros (D5) | 0 uses | One test per macro form, neutral types |
| U6 | `factory_deserialization_tests.cpp:106,172,225` set `CPP_OBJECT_FIELD` by hand | Tests an internal field instead of a public API | Rewrite through `extend` / the hybrid API (or R7's slot) |
| U7 | VM paths already listed by the audit (X16-X19: boxed chain tail, opaque `exec_call` tail, methods as values, constructor catch arms) | see `SIMPLIFICATION_AUDIT.md` | as listed there |

## 5. Consumer-shaped code (e)

JaiScript is a package; Bindstone and MutedVision consume it. **Dependency direction: clean.** No file in the package
includes an MV, Bindstone, Game or Workbench header or path, and no CMake file names one (verified by grep at
`4e80bdba1`). What leaks is shaping inside general features, wording, and three planning documents.

| # | Candidate | Verdict | Evidence | Action |
|---|---|---|---|---|
| E1 | Hybrid `_cpp_object` / `script_extensible` / `extend(obj, Class)` | (1) general embedding feature | Neutral mixin and API (`core/script_extensible.hpp`, `engine.hpp:1012-1058`); Bindstone uses it only from script | The lead example of `docs/site/embedding/13-script-extensible.html` and `docs/script_extensible.md:26,32,135,286-287,329` copies Bindstone's creature generator (`Voidling : Creature`, Bindstone arena timings and gates): replace with a neutral one (a plugin host or GUI widget); stop documenting the private `_cpp_object` field; see R7 |
| E2 | `property_owner`, `JAI_PROPERTY`, `JAI_SIGNAL_PROPERTY`, observable properties | (1) keep (Dev ruling) | Tests and docs use neutral types | (3) for one detail: `JAI_SIGNAL_PROPERTY` bakes in MV's camelCase `name##Signal` emitter. Add `JAI_SIGNAL_PROPERTY_AS(sig, name, emitter)` and keep the two-argument form as the default. Comment fixes in `properties/macros.hpp:6,24`, `property_serialization.hpp:1008` |
| E3 | Polymorphic serialization registry | (1) keep | General, tested | (3): delete the duck-typed `initialize()` (R6). Process-wide registries (`polymorphic_registry::instance()`, `type_name_registry`, `registrar_registry`) are the one undocumented exception to the no-mutable-statics rule (keyed by C++ type): document it; the registry also writes to stderr (`polymorphic.hpp:69,75`) |
| E4 | Cereal-migration remnants | done | S1, S17, D8; doc wording ("Cereal-shaped") is positioning, fine | delete per section 1 |
| E5 | Signal binding (`bind_signal_type`, text receivers) | (1) keep | Generic over the signature, neutral docs (`10-signals-properties.html`) | comment at `signal.hpp:7` names MV files |
| E6 | `registrar` with a services context | (1) keep: `Context` is a template parameter | — | U1 tests; neutral doc example in place of `registrar<Building, Services>` (`02-dynamic-binder.html:174-176`); S30 |
| E7 | Math adapter macros | (1) keep | Fully parameterized; neutral tests and docs | comment at `math/adapter.hpp:12-14` |
| E8 | `static_type_name` | (1) keep | Standard nameof technique with self-tests | comment examples (`MV::Foo`, `MV::Scene::Node`) |
| E9 | `thread_pool` | (1) keep (internal) | Used only by `parallel_transform`; MV keeps its own pool | drop the provenance comment (`thread_pool.hpp:6-7`); U4 |
| E10 | Easing family `mix/unmix/mix_in/.../unmix_out_in` (`math_intrinsics.hpp:36-40`) | (1) general, MV naming | Documented in `guide/15-math.html`; no Bindstone script calls it | relabel the "Bindstone-style" comment; conventional easing aliases are Dev's call |
| E11 | Runner NetChannel / NetServer, `Download` | (1) general runner features | 4-byte big-endian framing is common; JaiDOOM, Gator Quest and `net_smoke.jai` use them | comment-only fixes (F list) |
| E12 | Root planning documents `BINDSTONE_ADOPT_JAISCRIPT.md`, `BINDSTONE_COMPLETION_PLAN.md`, `MIGRATION_PLAN.md` | (2) Bindstone's | consumer plans inside the package | move to Bindstone's side (S5 deletes the duplicate `MIGRATION_PLAN.md`) |

No whole component needs to move to Bindstone's side: every candidate is a general embedding feature or package-internal.
The MV-specific parts are one hook (R6), one naming convention (E2), examples and comments.

## 6. Consumer mentions to rewrite as neutral examples (f)

Code comments and doc examples that name `MV::`, Bindstone, MutedVision or Workbench:

- Headers: `core/registrar.hpp:183,187,188` (`MV::Services`); `detail/static_type_name.hpp:29,42,53,62`;
  `detail/thread_pool.hpp:6-7`; `math/adapter.hpp:12-14`; `properties.hpp:12,15`; `detail/math_intrinsics.hpp:36,191,198`;
  `detail/integer_ops.hpp:23`; `detail/interpreter.hpp:28`; `serialization/polymorphic.hpp:39` (Component /
  `componentOwner` / Node); `signals/signal.hpp:7`; `properties/macros.hpp:6,24` ("matches MV_PROPERTY");
  `properties/README.md:3,343-381` (a "Migration from MV::Property" section); MV vocabulary in
  `dynamic_binder.hpp:740,1397`, `engine_impl.hpp:65`, `function_binder.hpp:284`, `type_info.hpp:519`,
  `property_serialization.hpp:1008`.
- Runner: `tools/jaiscript/fetch.hpp:4`; `tools/jaiscript/main.cpp:247,635-637,866,1239,1264`.
- Tests (comments): `integration/dynamic_binder_tests.hpp:476,507,1434`; `integration/reentrant_callback_tests.cpp:47,344`;
  `language/exception_handling_tests.cpp:483`; `language/prelaunch_regression_tests.cpp:264`;
  `properties/container_property_tests.cpp:284`; `serialization/factory_deserialization_tests.cpp:14`;
  `serialization/weak_ptr_backref_tests.cpp:2`; `signals/test_signal_auto_binding.cpp:121`; `stdlib/math_tests.cpp:200`.
- Docs site: `embedding/13-script-extensible.html` (E1), `embedding/02-dynamic-binder.html:174-176` (E6), and the
  "engine's established pair" wording for the signal property.
- Design docs under `docs/` (about 24 files) cite Bindstone measurements and gates; they are dated records, and the
  user-facing site is what must read standalone.

## 7. Live diagnostics that look dead (keep)

| Item | Why it stays |
|---|---|
| `JAISCRIPT_VM_COARSE_PROFILE` (`vm_backend.hpp:67,276`, `vm_backend.cpp` 7, 8677-8755, 19345, 19388) | No CMake option and no build cache sets it, but it is the instrument behind the time shares of `docs/review-2026-10-01/COVERAGE_CENSUS.md` and `typed_regions_design.md:293`. A CMake option would make it discoverable |
| `JAISCRIPT_VM_PROFILE`, `JAISCRIPT_DIAG_XTHREAD_RC`, `JAISCRIPT_DIAG_ATOMIC_RC` | Set through `CMAKE_CXX_FLAGS` in the `x64-Profile`, `x64-RelProfiling`, `x64-Debug-XTHREAD`, `x64-Debug-ATOMRC` build dirs; CMake options would make them discoverable |
| `JAISCRIPT_VM_COVERAGE`, `JAISCRIPT_MISS_INJECTION`, `JAISCRIPT_ENABLE_DEBUGGER`, `JAISCRIPT_EXPERIMENT_VM_NO_GS` | CMake options with users |
| `JAISCRIPT_WRAP_ON_OVERFLOW`, `JAI_SIGNALS_NO_THREADSAFE`, `JAI_MAX_SERIALIZATION_DEPTH`, `JAI_MAX_JSON_PARSE_DEPTH`, `JAI_ALLOW_MISLEADING_REFERENCE_BINDING` | Documented policy switches and escape hatches |
| `JAI_VMPROF_FUNCTION` env var (`vm_backend.cpp:277`), the demoreel `DEMOREEL_*` env vars | Profiling and example-host switches |
| `prepared_callback`, `debug::transport`, `check_observer`, `receiver_owner::handle_base` | Header decoupling, a documented extension point, two implementations, type erasure |
| Every public runtime engine knob (`type_proofs`, `aggregate_lowering`, `ir_passes`, `parameter_alias`, `local_alias`, `memory_cap`, ...) | Each has at least one caller in tests, tools or examples |
| `signal::shared_receiver_type` and `receiver<T>::shared_type` | Both spellings used (~23 Bindstone uses) and documented |
