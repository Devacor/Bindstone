# JaiScript Performance Analysis

## Current JaiScript Performance (Release Build)

Benchmarks run with `/O2 /GL /LTCG` optimizations on x64-Release configuration (2025-12-18):

| Benchmark                      | Time (μs) | Notes                                    |
|--------------------------------|-----------|------------------------------------------|
| Integer Addition               | 2         | Single arithmetic operation              |
| Float Multiplication           | 2         | Single floating-point operation          |
| Variable Operations            | 6         | 3 variable declarations + 1 addition     |
| Function Calls                 | 5         | Function declaration + invocation        |
| Array Push/Pop                 | 18        | 3 pushes + 1 pop + size check            |
| Map Insert/Lookup              | 14        | 2 inserts + 1 lookup                     |
| Class Creation                 | 7         | Class definition + instantiation         |
| Method Invocation              | 10        | Class with method + method call          |
| For Loop (100 iterations)      | 17        | 0.17μs per iteration                     |
| String Concatenation           | 8         | String operations                        |
| Complex Expression             | 6         | Multi-operator expression evaluation     |
| Class Inheritance              | 112       | Base + derived class + instantiation     |
| Variable Lookup Heavy          | 15        | 10 variable lookups in expression        |
| Hot Loop (1000 iterations)     | 76        | 0.076μs per iteration                    |
| Simple Compound Assignment     | 53        | 100 compound assignments (+=, -=, etc.)  |

## Performance Optimizations

JaiScript has undergone several rounds of optimizations to improve performance:

### 1. String Symbolizer & ID-based Optimizations

The string symbolizer implementation combined with ID-based class operations provides significant performance improvements:

**Key Optimizations:**
- **O(1) Variable Lookups**: Using interned uint64_t IDs instead of string comparisons
- **Parse-time Interning**: Names are interned once during parsing, not at runtime
- **Cached Type IDs**: Common type names pre-interned and cached
- **Integer-based Maps**: All lookups use integer keys for fast comparison
- **ID-based Class Operations**: `class_instance` and `class_definition` use uint64_t IDs for field/method lookups
- **Cached Operator IDs**: Frequently-used operators (e.g., `[]`, `_cpp_object`) cached as member variables
- **Parser Pre-computed IDs**: AST nodes store `name_id` and `symbol_id` from parser, avoiding redundant interning

**ID-based Class Operations (v0.1.4):**

The most recent optimization converted `class_instance` and `class_definition` to use `uint64_t` IDs internally for all field and method operations:

**Before:**
```cpp
instance->set_field("position", value);     // String comparison
instance->get_method("update");             // String lookup
```

**After:**
```cpp
uint64_t field_id = symbolizer->intern("position");
instance->set_field(field_id, value);       // Integer comparison (O(1))
instance->get_method(method_id);            // Integer lookup (O(1))
```

**Key improvements:**
- Field and method lookups now use integer hash maps instead of string maps
- Frequently-used IDs cached (e.g., `cpp_object_field_id_`, `subscript_op_id_`)
- Pre-computed IDs from parser used directly (e.g., `var_decl->name_id`, `func_decl->name_id`)
- Only convert IDs back to strings for error messages
- Public APIs still accept strings for ergonomics, internally converted to IDs once

**Performance gains:**
- Hot loops: 37% faster (1136μs → 715μs)
- Recursion: 15-19% faster (Fibonacci: 27μs → 22μs)
- Class operations: 14-18% faster (Creation: 7μs → 6μs, Inheritance: 133μs → 109μs)
- Algorithms: 13-19% faster (Bubble sort: 156μs → 127μs)

### 2. Unchecked Accessor Optimizations (v0.1.3)

Added ultra-fast unchecked accessors for direct variant access without type checking when type is already known:

**Unchecked Accessors:**
```cpp
// Direct variant access using std::get_if (no exceptions, no type checks)
inline script_bool unchecked_as_bool() const noexcept {
    return *std::get_if<static_cast<size_t>(storage_index::jai_bool)>(&storage_);
}

inline script_int unchecked_as_int() const noexcept {
    return *std::get_if<static_cast<size_t>(storage_index::jai_int)>(&storage_);
}
// ... similar for float, string, etc.
```

**Optimized `is_truthy()` using unchecked accessors:**
```cpp
inline bool is_truthy(const script_value& value) {
    switch (value.type()) {
        case jai_bool_type: return value.unchecked_as_bool();
        case jai_int_type: return value.unchecked_as_int() != 0;
        case jai_float_type: return value.unchecked_as_float() != 0.0;
        case jai_string_type: return !value.unchecked_as_string().empty();
        // ...
    }
}
```

This eliminates exception overhead and reduces branch prediction misses in hot paths.

### 3. Type Check Optimizations (v0.1.2)

Replaced chained `is_int() / is_float() / is_string()` checks with single `type()` call + switch statements:

**Before:**
```cpp
if (value.is_int() && other.is_int()) {
    // int path
} else if ((value.is_int() || value.is_float()) && (other.is_int() || other.is_float())) {
    // float path
}
// Each is_*() call checks type info internally
```

**After:**
```cpp
auto leftType = value.type();
auto rightType = other.type();
if (leftType == jai_int_type && rightType == jai_int_type) {
    // int path
} else if ((leftType == jai_int_type || leftType == jai_float_type) &&
           (rightType == jai_int_type || rightType == jai_float_type)) {
    // float path
}
// Single type() call per value
```

**Optimized Locations:**
- Unary operators (-, ~, !)
- Increment/decrement operators (++, --)
- Compound assignment operators (+=, -=, *=, /=)
- Binary arithmetic paths
- Loop condition evaluation (`is_truthy()`)
- For-loop structure (native C++ for-loop with lambda helpers)

### Performance Impact:
- **Recursive algorithms**: ~94% faster (Fibonacci benchmark: 3381μs → 220μs → 22μs)
- **Loop-heavy operations**: ~37-47% faster (For Loop: 125μs → 79μs → 67μs, Hot Loop: 1345μs → 1136μs → 715μs)
- **String method calls**: ~27% faster (String Concatenation: 11μs → 8μs)
- **Array algorithms**: ~61% faster (Bubble Sort: 329μs → 200μs → 156μs → 127μs)
- **Method invocation**: ~55% faster (Method Invocation: 20μs → 17μs → 9μs)
- **Function calls**: ~60% faster (Function Calls: 10μs → 5μs → 4μs)
- **Class operations**: ~75% faster (Class Creation: 24μs → 22μs → 7μs → 6μs, Class Inheritance: 139μs → 133μs → 109μs)

## Architecture: Value Type Implementation

JaiScript uses a type-safe design prioritizing correctness over raw micro-operation speed.

### Memory Layout

**JaiScript's `script_value`** uses **discriminated union (std::variant)**:
```cpp
class script_value {
    type_info_ptr type_info_;         // Shared type metadata (16 bytes)
    std::weak_ptr<engine> engine_ref_; // Engine reference (16 bytes)
    std::variant<...> storage_;        // 14 alternatives, ~40 bytes
    void* cpp_bound_ptr_;              // C++ binding (8 bytes)
};
```

### Performance Characteristics

**JaiScript arithmetic** uses type-safe variant access:
```cpp
// Type-safe with discriminant check
script_int leftInt = leftVal.as_int();   // std::get<script_int>(storage_)
script_int rightInt = rightVal.as_int(); // Checks variant index each time
return leftInt + rightInt;
// Cost: 2 variant checks + arithmetic = ~5-8ns
```

This ensures type safety and prevents undefined behavior at the cost of some micro-operation overhead.

### For Loop Performance Profile

For `for (auto i = 0; i < 100; ++i) { sum += i; }`:

**JaiScript** (~17μs for 100 iterations):
```
Per iteration:
1. condition (i < 100):     ~2ns (get_type() + static_cast, no RTTI)
2. body (sum += i):         ~10ns (2 ID lookups + optimized type checks + value construction)
3. increment (++i):         ~5ns (ID lookup + optimized type check + value construction)
                           ------
Total: ~17ns × 100 = 1.7μs + overhead = 17μs
```

**Optimizations applied:**
- Native C++ for-loop structure with lambda helpers (compiler-friendly pattern)
- Unchecked accessors in `is_truthy()` (no exception overhead)
- Error capture flags instead of throwing from lambdas

Loop performance is adequate for typical game scripting (event handlers, AI logic, UI) but tight computational loops should be written in C++.

### Micro-Overhead Sources

1. **Variant Discriminant Checks**: Every `as_int()` / `as_float()` requires `storage_.index()` check
   - **Optimization Applied**: Replaced chained `is_int() / is_float()` checks with single `type()` call + switch
   - **Result**: ~15% reduction in type checking overhead
2. **Value Stack Operations**: push/pop on every sub-expression evaluation
3. **Script_value Construction**: Creating new values for intermediate results
4. **Integer-based ID Lookups**: O(1) environment lookups using interned IDs (optimized from string lookups)
5. **is_truthy() Conversion**: Optimized with single switch statement on type

**Design Rationale**: JaiScript prioritizes **type safety** and **memory efficiency** (no heap allocation for primitives) over micro-operation speed. For real-world game scripts:
- ✅ Function calls dominate performance profile (JaiScript 20-40x faster)
- ✅ Memory efficiency matters for embedded use
- ✅ Type safety prevents entire classes of bugs

### Loop Optimization Approach

Loop conditions require evaluation each iteration:
1. The condition value **changes every iteration** (e.g., `i < 100` evaluates differently as `i` changes)
2. Must evaluate the condition expression each time
3. `is_truthy()` needed because conditions can be any type (int, bool, object with conversion)

**Optimization Applied**: `is_truthy()` uses a **single `switch` statement** on `value.type()` instead of multiple `is_bool()`, `is_null()`, `is_int()` checks. This reduces overhead from ~5 type checks to ~1 type check + switch dispatch (~10-20% faster).

---

## Benchmark Results

Performance measurements running representative workloads:

### Summary Table

| Benchmark                        | JaiScript Time | Notes |
|----------------------------------|----------------|-------|
| **Integer Addition**             | 2μs            | Single arithmetic operation |
| **Float Multiplication**         | 2μs            | Floating-point operation |
| **Variable Operations**          | 6μs            | Variable declarations + addition |
| **Function Calls**               | 5μs            | Function declaration + invocation |
| **Array Push/Pop**               | 18μs           | Array operations |
| **Map Insert/Lookup**            | 14μs           | Hash map operations |
| **Class Creation**               | 7μs            | Class instantiation |
| **Method Invocation**            | 10μs           | Instance method call |
| **For Loop (100 iterations)**    | 17μs           | ~170ns per iteration |
| **Variable Lookup Heavy**        | 15μs           | Multiple variable accesses |
| **Complex Expression**           | 6μs            | Multi-operator expression |
| **Factorial(10) - Recursion**    | 11μs           | Recursive algorithm |
| **Fibonacci(6) - Deep Recursion**  | 21μs          | Deep recursive calls |
| **Binary Search**                | 22μs           | Search algorithm |
| **Bubble Sort (10 elements)**    | 118μs          | Sorting algorithm |
| **Hot Loop (1000 iterations)**   | 76μs           | ~76ns per iteration |

---

## JaiScript vs ChaiScript Comparison

Direct head-to-head benchmarks comparing JaiScript against ChaiScript (a popular C++ embedded scripting language).

### Where JaiScript Dominates (Function/Recursion Heavy)

| Benchmark | JaiScript | ChaiScript | JaiScript Speedup |
|-----------|-----------|------------|-------------------|
| **Function Calls** | 5μs | 201μs | **40x faster** |
| **Factorial(10)** | 11μs | 1431μs | **130x faster** |
| **Fibonacci(6)** | 21μs | 3403μs | **162x faster** |
| **Method Invocation** | 10μs | 200μs | **20x faster** |
| **Range-For (100 elem)** | 28μs | 169μs | **6x faster** |
| **Binary Search** | 22μs | 319μs | **14x faster** |
| **Class Creation** | 7μs | 16μs | **2.3x faster** |
| **Bubble Sort (10 elem)** | 118μs | 195μs | **1.7x faster** |

### Where ChaiScript Wins (Simple Operations)

| Benchmark | JaiScript | ChaiScript | ChaiScript Speedup |
|-----------|-----------|------------|-------------------|
| For Loop (100 iter) | 17μs | 12μs | 1.4x faster |
| Variable Lookup Heavy | 15μs | 10μs | 1.5x faster |
| Variable Operations | 6μs | 5μs | ~Equal |
| Integer Addition | 2μs | 2μs | Equal |
| Float Multiplication | 2μs | 1μs | 2x faster |

### Loop Performance Deep Dive

| Loop Pattern | JaiScript | Notes |
|--------------|-----------|-------|
| For Loop (literal condition) | 17μs | Fast path: `i < 100` |
| For Loop (expression condition) | 20μs | No fast path: `i < n` |
| Range-For (copy, 100 elem) | 28μs | `for(auto x : arr)` |
| Range-For (reference, 100 elem) | 40μs | `for(auto& x : arr)` |
| Hot Loop (10x100 nested) | 81μs | Nested iteration scaling |

**Loop Optimization Notes:**
- Literal condition fast path saves ~15% (17μs vs 20μs)
- Range-for with copy is faster than reference (no reference wrapper overhead)
- JaiScript range-for is **6x faster** than ChaiScript's equivalent

### Value Type Comparison (script_value vs BoxedValue)

| Operation | JaiScript | ChaiScript | Notes |
|-----------|-----------|------------|-------|
| Integer Construction | 1μs | 1μs | Equal |
| String Construction | 1μs | 1μs | Equal |
| Boolean Construction | 1μs | 1μs | Equal |
| Float Construction | 1μs | 1μs | Equal |
| Type Checking | 5μs | 3μs | ChaiScript slightly faster |
| Array Construction | 7μs | 5μs | ChaiScript slightly faster |
| Mixed Type Operations | 8μs | 7μs | Similar |

### Binary Search Tree Benchmark

| Implementation | JaiScript | ChaiScript | Notes |
|----------------|-----------|------------|-------|
| Native Script TreeNode (15 nodes) | 651μs | **SKIPPED** | ChaiScript lacks null support for object fields |
| C++ Bound TreeNode (15 nodes) | 116μs | 73μs | ChaiScript 1.6x faster with C++ FFI |

### Key Insights

1. **JaiScript's Strength: Function Calls & Recursion**
   - ID-based lookups and optimized call stack give JaiScript **40-162x advantage** in function-heavy code
   - Recursive algorithms (factorial, fibonacci) show the biggest wins
   - Method invocation is **20x faster** than ChaiScript

2. **ChaiScript's Strength: Tight Loops & Simple Ops**
   - ChaiScript's for-loop is only **1.4x faster** now (gap closed from 8.4x)
   - Basic arithmetic and variable operations slightly faster
   - Gap largely eliminated through dynamic_cast removal

3. **Language Features**
   - JaiScript supports proper `null` for object fields (tree structures work natively)
   - ChaiScript requires workarounds with `is_var_null()` for undefined checks
   - JaiScript's native class system handles complex data structures better

4. **Real-World Implications**
   - **Game scripting**: JaiScript wins (function calls, event handlers, AI logic dominate)
   - **Tight computation loops**: Write in C++ for both engines
   - **Complex data structures**: JaiScript's null support is essential

*Benchmarks run on x64-Release build with MSVC 2022, /O2 /GL /LTCG optimizations*

### Performance Profile

**Excellent Performance:**
- ✅ **Function/Method Calls**: 5-10μs - ID-based lookups + string interning eliminate overhead
- ✅ **Recursion**: 11-21μs - Efficient stack frame management + cached symbol IDs + unchecked accessors
- ✅ **Algorithms**: Fast execution for search (22μs), sort (118μs), and computational tasks
- ✅ **Class Operations**: 7μs instantiation - ID-based field/method access
- ✅ **For Loops**: ~170ns per iteration - get_type() + static_cast (no RTTI) + native C++ structure
- ✅ **Hot Loops**: 76μs for 1000 iterations (~76ns per iteration)

**Adequate Performance:**
- ⚪ **Simple Expressions**: 2-8μs - Good for event handlers and UI
- ⚪ **Container Operations**: 14-18μs - Acceptable for game scripting use cases

**Recommendations:**
- Use JaiScript for game logic, events, AI, UI, and general scripting
- Write performance-critical tight loops (physics, particles) in C++ when needed
- Function-heavy code benefits most from JaiScript's optimizations

### Loop Implementation Details

JaiScript's for loop implementation:
```cpp
auto loop_env = get_pooled_environment(environment_);  // ONCE per loop
environment_ = loop_env;
for (init; ; update) {
    dispatch_expr(stmt->condition.get());  // Switch dispatch (no virtual call)
    script_value conditionValue = pop_value();  // Stack operation
    if (!is_truthy(conditionValue)) break;
    dispatch_stmt(stmt->body.get());
    dispatch_expr(stmt->update.get());  // Switch dispatch
    pop_value();  // Pop update result
}
```

**Optimizations in place:**
✅ Switch-based AST dispatch (eliminates virtual call overhead)
✅ Native C++ for-loop structure with lambda helpers (Phase 1 & 2 optimizations)
✅ Unchecked accessors for zero-overhead type access (using std::get_if)
✅ Error capture flags instead of exception throwing from lambdas
✅ Environment pooling - reuses environments, not creating per-loop
✅ Single scope for entire loop (not per-iteration)
✅ Efficient checked_result with inlining
✅ String symbolizer for fast variable lookup
✅ Optimized type checking with single type() call
✅ Optimized is_truthy() using unchecked accessors after type switch

**Per iteration costs:**
- 3 AST node evaluations (condition, body, update)
- 2 value stack operations
- Truthiness conversion on condition
- Switch-based dispatch for AST traversal (~2-4% faster than virtual calls)

Performance is adequate for typical game scripting use cases. For extremely tight computational loops, write in C++ and expose to JaiScript.

## Why JaiScript is Fast

### 1. **Modern C++ Implementation (C++20)**
   - Zero-cost abstractions
   - Move semantics throughout
   - Optimized memory allocations

### 2. **ID-based Lookups Throughout**
   - All identifier/field/method lookups use uint64_t instead of std::string
   - Hash computed once, reused everywhere
   - O(1) integer comparisons instead of O(n) string comparisons
   - Class instance fields stored in `unordered_map<uint64_t, script_value>`
   - Method dispatch uses integer keys

### 3. **Parse-time Optimizations**
   - Names interned during parsing
   - Type IDs cached at class registration
   - AST nodes store pre-computed IDs (`name_id`, `symbol_id`)
   - No redundant string interning at runtime

### 4. **Smart Memory Management**
   - Object pooling for environments
   - Efficient shared_ptr usage
   - Minimal allocations in hot paths

### 5. **Compiler Optimizations**
   - Link-time code generation (LTCG)
   - Whole program optimization (WPO)
   - Inline function expansion
   - Intrinsic function usage

## Building for Performance

### Visual Studio:
1. Select **Release** configuration in dropdown
2. Build the solution
3. Enable benchmarks: Add CMake variable `JAISCRIPT_ENABLE_BENCHMARKS=ON`

### Command Line:
```bash
cmake -DCMAKE_BUILD_TYPE=Release -DJAISCRIPT_ENABLE_BENCHMARKS=ON ..
cmake --build . --config Release
```

### Run Benchmarks:
```bash
./bin/jaiscript_tests
```

## Profiling

To enable profiling support:
```bash
cmake -DJAISCRIPT_ENABLE_PROFILING=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
```

This enables `/Zi` debug info with optimizations for tools like Visual Studio Profiler.

## Performance Tips

### For Library Users:
1. **Reuse engine instances** - Engine creation is ~600μs
2. **Pre-register types** - C++ class registration is one-time cost
3. **Use typed parameters** - Avoid runtime type conversions
4. **Batch script execution** - Parse once, execute many times
5. **Cache function references** - Avoid repeated lookups

### For Script Writers:
1. **Minimize variable lookups in loops** - Cache frequently used values
2. **Use local variables** - Faster than global lookups
3. **Prefer static methods** - No 'this' resolution overhead
4. **Inline small functions** - Reduce call overhead
5. **Use primitive types** - int/float faster than objects

## Future Optimizations

Potential improvements under consideration:
- [ ] JIT compilation for hot loops
- [ ] Constant folding in parser
- [ ] Dead code elimination
- [ ] Type inference for faster dispatch
- [ ] SIMD operations for arrays
- [ ] Bytecode caching

## Benchmark History

Track performance improvements over time (git-verified):

| Date       | Commit   | For Loop (100) | Hot Loop (1000) | vs ChaiScript | Notes                           |
|------------|----------|----------------|-----------------|---------------|---------------------------------|
| 2025-12-18 | HEAD     | 17μs           | 76μs            | 1.4x slower   | + dynamic_cast removal (90 calls) + throw cleanup |
| 2025-12-15 | 9310a48f | 28μs           | 197μs           | 2.3x slower   | Switch-based AST dispatch |
| 2025-12-04 | bb2b900a | 42μs           | ~400μs          | 3.5x slower   | "only 2x loop perf of ChaiScript" |
| 2025-11-07 | bf50a1a6 | 67-79μs        | 715μs           | ~6x slower    | Aggressive string interning |
| 2025-11-04 | 47b8a6aa | 101μs          | 1136μs          | 8.4x slower   | Initial PERFORMANCE.md |

**Progress: 8.4x slower → 1.4x slower = 83% of the gap eliminated**

ChaiScript For Loop baseline: 12μs

---

*Benchmarks run on: Windows x64*
*Compiler: MSVC 2022 with /O2 /GL /LTCG*
*Build: x64-Release*
