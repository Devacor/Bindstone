# JaiScript Test Suite Cleanup Analysis

## Summary
- **Total Files**: ~192 test files
- **Foundry Test Suites**: 45 files ✅ **KEEP ALL**
- **Legacy/Manual Tests**: ~147 files ⚠️ **REVIEW FOR DELETION**

---

## ✅ FOUNDRY TEST SUITES (45 files - KEEP ALL)

These are properly structured test suites using the Foundry framework with `FOUNDRY_REGISTER`.

### Core/Semantics Tests (21 files)
- `semantics/auto_container_conversion_tests.cpp`
- `semantics/container_conversion_tests.cpp`
- `semantics/conversion_tests.cpp`
- `semantics/enhanced_conversion_tests.cpp`
- `semantics/lexer_tests.cpp`
- `semantics/map_conversion_tests.cpp`
- `semantics/parser_tests.cpp`
- `semantics/script_container_tests.cpp`
- `semantics/test_bound_debug.cpp`
- `semantics/test_conversion_isolation.cpp`
- `semantics/test_cpp_bound.cpp`
- `semantics/test_deep_copy.cpp`
- `semantics/test_deep_copy_comprehensive.cpp`
- `semantics/test_global_persistence.cpp`
- `semantics/test_object_deep_copy.cpp`
- `semantics/type_conversion_tests.cpp`
- `semantics/type_info_tests.cpp`
- `semantics/vector_conversion_tests.cpp`
- `semantics/weak_ptr_tests.cpp`
- `semantics/engine_tests.hpp`
- `semantics/value_semantics_tests.hpp`

### Language Feature Tests (18 files)
- `language/control_flow_tests.cpp`
- `language/exception_handling_tests.cpp`
- `language/function_tests.cpp`
- `language/hot_reload_tests.cpp` ⭐ **Currently debugging**
- `language/include_import_tests.cpp`
- `language/override_edge_cases.cpp`
- `language/override_tests.cpp`
- `language/parser_debug_test.cpp`
- `language/range_based_for_tests.cpp`
- `language/script_class_tests.cpp`
- `language/static_field_tests.cpp` ⭐ **Currently debugging**
- `language/static_method_tests.cpp` ⭐ **Currently debugging**
- `language/switch_statement_tests.cpp`
- `language/test_cat_class_syntax.cpp`
- `language/vm_tests.cpp`
- `language/assignment_tests.hpp`
- `language/member_access_tests.hpp`
- `language/operator_tests.hpp`

### Container Tests (2 files)
- `containers/array_tests.cpp`
- `containers/map_tests.cpp`

### Integration Tests (2 files)
- `integration/test_constructor_debug.cpp`
- `integration/class_builder_tests.hpp`

### Performance Tests (1 file)
- `performance/test_native_classes.cpp`

### VM Tests (1 file)
- `vm/vm_function_debug.cpp`

---

## ⚠️ LEGACY/MANUAL TESTS (~147 files - TO DELETE)

These are standalone test executables, debug files, and duplicates.

### Categories:

#### 🔴 DELETE - Debug/Scratch Tests (~60 files)
Quick one-off tests created for debugging specific issues:
- `general/test_debug_*.cpp` (7 files - conversion, map, method issue)
- `general/test_minimal*.cpp` (4 files - minimal reproductions)
- `general/test_trace_*.cpp` (2 files - debugging traces)
- `general/test_segfault_debug.cpp`
- `general/test_method_*_debug.cpp` (multiple debug variants)
- `general/test_simple_*.cpp` (10+ files - simple reproductions)
- `general/test_*_minimal.cpp` (multiple minimal tests)
- All `test_switch_*` files (13 files - duplicated by switch_statement_tests.cpp Foundry suite)
- All `test_fallthrough_*` files (6 files - old switch debugging)
- All `test_pair_*` files (6 files - likely covered by container tests)
- All `test_ref_*` files (3 files - reference debugging)

#### 🟡 EVALUATE - Potential Feature Coverage Gaps
These might test features not covered by Foundry suites:

**Performance/Benchmarks:**
- `general/benchmark_vs_chaiscript.cpp`
- `general/compile_time_benchmark.cpp`
- `performance/comprehensive_benchmark.cpp`
- `performance/script_class_perf_test.cpp`
- `performance/script_class_vs_chaiscript_perf.cpp`
- `performance/quick_perf_test.cpp`
- `performance/native_script_class_perf.cpp`

**Standalone Features:**
- `general/test_cpp_static_methods.cpp` - C++ static method binding
- `general/test_destructor*.cpp` (3 files) - Destructor tests
- `general/test_super_semantic.cpp` - Super keyword semantics
- `language/test_constructor_syntax_comprehensive.cpp` - Constructor syntax
- `language/test_function_return_types.cpp` - Return type checking
- `language/test_parser_constructor_syntax.cpp` - Parser for constructors
- `language/test_template_parameters.cpp` - Template parameters
- `containers/test_map_constructor_syntax.cpp` - Map constructor syntax

**Integration/Shared Ptr:**
- `integration/test_shared_ptr_conversion.cpp`
- `integration/test_shared_ptr_debug.cpp`
- `integration/class_builder_tests.cpp` (has .hpp Foundry version)

**VM Tests:**
- `vm/test_vm_method_calls.cpp`
- `vm/test_vm_references.cpp`
- `vm/test_vm_value_semantics.cpp`
- `vm/vm_direct_function_test.cpp`

#### 🟢 DELETE - Demo/Example Files
- `general/run_class_demo.cpp`
- `general/simple_class_demo.cpp`
- `general/simple_working_class_test.cpp`
- `general/example_isolated_test.hpp`
- `general/test_class_builder_example.cpp`
- `general/test_concept.cpp`

#### 🟢 DELETE - Duplicate .cpp files (have .hpp Foundry versions)
Some folders have both .cpp and .hpp - the .hpp files are the Foundry suites:
- `containers/array_tests.cpp` (duplicate of .hpp)
- `containers/map_tests.cpp` (duplicate of .hpp)
- `language/assignment_tests.cpp` (duplicate of .hpp)
- `language/control_flow_tests.cpp` (already counted in Foundry)
- `language/function_tests.cpp` (already counted in Foundry)
- `language/member_access_tests.cpp` (duplicate of .hpp)
- `language/operator_tests.cpp` (duplicate of .hpp)
- `language/vm_tests.cpp` (already counted in Foundry)
- `semantics/engine_tests.cpp` (duplicate of .hpp)
- `semantics/lexer_tests.cpp` (already counted in Foundry)
- `semantics/parser_tests.cpp` (already counted in Foundry)
- `semantics/type_conversion_tests.cpp` (already counted in Foundry)
- `semantics/value_semantics_tests.cpp` (duplicate of .hpp)

#### 🔵 SPECIAL - Test Runner
- `general/run_tests.cpp` - Will become `tests/main_test_runner.cpp`

---

## 🎯 MIGRATION CANDIDATES

Before deleting, consider if these unique features should be migrated into Foundry suites:

### High Priority - Missing Coverage:
1. **Destructor support** - `test_destructor*.cpp` (3 files)
   - Should add to language/script_class_tests.cpp or create language/destructor_tests.cpp

2. **Super keyword** - `test_super_semantic.cpp`
   - Should add to language/override_tests.cpp or language/script_class_tests.cpp

3. **C++ static method binding** - `test_cpp_static_methods.cpp`
   - Should add to integration/class_builder_tests.hpp

4. **Constructor syntax edge cases** - `test_constructor_syntax_comprehensive.cpp`
   - Should add to language/script_class_tests.cpp

5. **Template parameters** - `test_template_parameters.cpp`
   - Should add to language/function_tests.cpp or create new suite

### Medium Priority - Performance/Benchmarks:
- Keep ONE consolidated benchmark suite
- Suggested: Create `performance/benchmarks.cpp` Foundry suite with comparison tests
- Delete all individual benchmark files

### Low Priority - VM Tests:
- If VM backend is still active, migrate to `vm/` Foundry suites
- If VM backend is deprecated, delete all

---

## 📋 RECOMMENDED ACTION PLAN

1. **Review Migration Candidates** - Check if features are already tested
2. **Migrate missing features** - Add tests to appropriate Foundry suites
3. **Delete all legacy files** - Clean sweep of non-Foundry tests
4. **Create main_test_runner.cpp** - Move run_tests.cpp to root as main_test_runner.cpp
5. **Verify all tests still pass** - Run full Foundry suite

---

## 🚨 ANSWERS TO KEY QUESTIONS

1. **Destructors**: ✅ TESTED - Found in `language/script_class_tests.cpp`
2. **Super keyword**: ✅ TESTED - Found in `language/script_class_tests.cpp` and `override_edge_cases.cpp`
3. **VM Backend**: ❓ UNKNOWN - Need to check if VM is active
4. **Benchmarks**: ❓ USER DECISION - Keep performance comparisons?
5. **C++ static methods**: ❓ UNKNOWN - Need to verify coverage

## 💡 FINAL RECOMMENDATION

**SAFE TO DELETE (~140 files):**
- All `general/test_*` debug/scratch files
- All demo files
- All duplicate .cpp files that have .hpp Foundry versions

**KEEP FOR REVIEW (~7 files):**
- Performance benchmarks (decide if you want them)
- VM standalone tests (if VM is still active)
- Any unique integration tests not covered by Foundry suites

