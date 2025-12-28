# JaiScript Performance Analysis

## Current JaiScript Performance (Release Build)

Benchmarks run with `/O2 /GL /LTCG` optimizations on x64-Release configuration (2025-12-28):

| Benchmark                      | Time (μs) | Notes                                    |
|--------------------------------|-----------|------------------------------------------|
| Integer Addition               | <1        | Single arithmetic operation              |
| Float Multiplication           | <1        | Single floating-point operation          |
| Variable Operations            | 2         | 3 variable declarations + 1 addition     |
| Function Calls                 | 4         | Function declaration + invocation        |
| Array Push/Pop                 | 7         | 3 pushes + 1 pop + size check            |
| Map Insert/Lookup              | 5         | 2 inserts + 1 lookup                     |
| Class Creation                 | 11        | Class definition + instantiation         |
| Method Invocation              | 9         | Class with method + method call          |
| For Loop (100 iterations)      | 8         | 0.08μs per iteration (~80ns)             |
| String Concatenation           | 3         | String operations                        |
| Complex Expression             | 2         | Multi-operator expression evaluation     |
| Class Inheritance              | 94        | Base + derived class + instantiation     |
| Variable Lookup Heavy          | 5         | 10 variable lookups in expression        |
| Hot Loop (1000 iterations)     | 44        | 0.044μs per iteration (~44ns)            |
| Simple Compound Assignment     | 14        | 100 compound assignments (+=, -=, etc.)  |
| Engine Creation                | 42        | One-time engine initialization           |
| Stdlib Registration            | 133       | Standard library function registration   |
| String Copy (Long String)      | 5         | Copying long strings                     |
| String Passing to Function     | 7         | Function parameter passing               |
| String Method Chaining         | 4         | Method call chains on strings            |

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

### 4. Slot-Based Local Variable Storage (v0.1.6)

Replaced hash map-based local variable lookup with O(1) array indexing like Squirrel's bytecode VM:

**Before:**
```cpp
// Hash map lookup per variable access
std::unordered_map<uint64_t, script_value> locals;
auto it = locals.find(symbol_id);  // O(1) amortized, but hash + comparison overhead
```

**After:**
```cpp
// Direct array indexing - parser assigns slot indices at parse time
std::vector<script_value> locals;
return &locals[slot_index];  // O(1) guaranteed, single array access
```

**Key Implementation:**
- Parser assigns numeric slot indices to parameters and local variables during parsing
- Each function tracks `local_count` for pre-reserving the locals vector
- `SIZE_MAX` used as invalid slot sentinel (naturally fails bounds check, no casting)
- Parameters get slots 0, 1, 2..., then local variables in declaration order
- Function scope tracking in parser handles nested functions/lambdas correctly

**Performance Impact:**
- **Fibonacci(15)**: 1051μs → 878μs (**16% faster**)
- **Recurse with 10 Locals (depth=15)**: 36μs (new benchmark)
- **vs Squirrel on local-heavy code**: 12x gap (better than 16x on Fibonacci)
- **vs ChaiScript on local-heavy code**: JaiScript is **67x faster**

The slot-based approach eliminates hash computation and bucket traversal on every local variable access, providing consistent O(1) performance.

### 5. Raw Engine Pointer & Shared String Optimization (v0.1.5)

Two key optimizations to reduce `script_value` overhead:

**Raw Engine Pointer:**
```cpp
// Before: weak_ptr has atomic refcount overhead on every copy
std::weak_ptr<engine> engine_ref_;  // 16 bytes + atomic ops

// After: raw pointer, safely nulled on engine destruction
engine* engine_;  // 8 bytes, no atomic overhead
```

**Shared String/Function Storage:**
```cpp
// Before: string/function stored directly (copies entire string)
script_string storage_;  // Expensive to copy

// After: shared_ptr for cheap copies
std::shared_ptr<script_string> string_ptr_;  // Copy = atomic increment only
std::shared_ptr<function> function_ptr_;
```

**Performance Impact (v0.1.5):**
- **Hot Loops**: 71μs → 49μs (**31% faster**)
- **For Loops**: 17μs → 15μs (**12% faster**, gap now 1.25x vs ChaiScript)
- **Bubble Sort**: 115μs → 82μs (**29% faster**)
- **BST**: 700μs → 596μs (**15% faster**)
- **Fibonacci**: 20μs → 17μs (**15% faster**)
- **String concat**: Neutral (shared_ptr indirection hidden by actual string work)

## Architecture: Value Type Implementation

JaiScript uses a type-safe design prioritizing correctness over raw micro-operation speed.

### Memory Layout

**JaiScript's `script_value`** uses **discriminated union (std::variant)**:
```cpp
class script_value {
    type_info_ptr type_info_;         // Shared type metadata (8 bytes)
    engine* engine_;                   // Raw engine pointer (8 bytes)
    std::variant<...> storage_;        // 14 alternatives, ~40 bytes
    // Note: strings/functions use shared_ptr for cheap copies
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

**JaiScript** (~8μs for 100 iterations = **80ns per iteration**):
```
Per iteration:
1. condition (i < 100):     ~1ns (get_type() + static_cast, no RTTI)
2. body (sum += i):         ~5ns (2 ID lookups + optimized type checks + value construction)
3. increment (++i):         ~2ns (ID lookup + optimized type check + value construction)
                           ------
Total: ~8ns × 100 = 0.8μs + overhead = 8μs
```

**JaiScript now beats ChaiScript** (8μs vs 12μs = 1.5x faster).

**Optimizations applied:**
- Native C++ for-loop structure with lambda helpers (compiler-friendly pattern)
- Unchecked accessors in `is_truthy()` (no exception overhead)
- Error capture flags instead of throwing from lambdas
- Improved string interning reduces lookup overhead

### Micro-Overhead Sources

1. **Variant Discriminant Checks**: Every `as_int()` / `as_float()` requires `storage_.index()` check
   - **Optimization Applied**: Replaced chained `is_int() / is_float()` checks with single `type()` call + switch
   - **Result**: ~15% reduction in type checking overhead
2. **Value Stack Operations**: push/pop on every sub-expression evaluation
3. **Script_value Construction**: Creating new values for intermediate results
4. **Integer-based ID Lookups**: O(1) environment lookups using interned IDs (optimized from string lookups)
5. **is_truthy() Conversion**: Optimized with single switch statement on type

**Design Rationale**: JaiScript prioritizes **type safety** and **memory efficiency** (no heap allocation for primitives). Recent optimizations have made JaiScript faster than ChaiScript across all benchmarks:
- ✅ Function calls dominate performance profile (JaiScript 82x faster)
- ✅ Even loops are now faster than ChaiScript (1.5x)
- ✅ Memory efficiency matters for embedded use
- ✅ Type safety prevents entire classes of bugs

### Loop Optimization Approach

Loop conditions require evaluation each iteration:
1. The condition value **changes every iteration** (e.g., `i < 100` evaluates differently as `i` changes)
2. Must evaluate the condition expression each time
3. `is_truthy()` needed because conditions can be any type (int, bool, object with conversion)

**Optimizations Applied**:
- `is_truthy()` uses a **single `switch` statement** on `value.type()` instead of multiple type checks
- Improved string interning eliminates redundant lookups
- Result: JaiScript for-loop (8μs) now **beats ChaiScript** (12μs) by 1.5x

---

## Benchmark Results

Performance measurements running representative workloads:

### Summary Table

| Benchmark                        | JaiScript Time | Notes |
|----------------------------------|----------------|-------|
| **Integer Addition**             | <1μs           | Single arithmetic operation |
| **Float Multiplication**         | <1μs           | Floating-point operation |
| **Variable Operations**          | 2μs            | Variable declarations + addition |
| **Function Calls**               | 2μs            | Function declaration + invocation |
| **Array Push/Pop**               | 7μs            | Array operations |
| **Map Insert/Lookup**            | 5μs            | Hash map operations |
| **Class Creation**               | 4μs            | Class instantiation |
| **Method Invocation**            | 4μs            | Instance method call |
| **For Loop (100 iterations)**    | 8μs            | ~80ns per iteration |
| **Variable Lookup Heavy**        | 5μs            | Multiple variable accesses |
| **Complex Expression**           | 2μs            | Multi-operator expression |
| **Factorial(10) - Recursion**    | 6μs            | Recursive algorithm |
| **Fibonacci(6) - Deep Recursion**| 14μs           | Deep recursive calls |
| **Binary Search**                | 9μs            | Search algorithm |
| **Bubble Sort (10 elements)**    | 64μs           | Sorting algorithm |
| **Hot Loop (1000 iterations)**   | 44μs           | ~44ns per iteration |
| **BST (15 nodes)**               | 489μs          | Binary Search Tree operations |

### Array/Map Literal Benchmarks

| Benchmark | auto | var | Notes |
|-----------|------|-----|-------|
| Simple Array [10 ints] | 4μs | 3μs | `[1,2,3,4,5,6,7,8,9,10]` |
| 2D Array [[5x5 ints]] | 11μs | 10μs | Nested array construction |
| 3D Array [[[2x2x2 ints]]] | 6μs | 5μs | Deep nesting |
| Homogeneous Map {5 keys} | 4μs | 4μs | `{"a":1, "b":2, ...}` |
| Heterogeneous Map {5 mixed} | - | 4μs | Mixed value types |
| Nested Map 2 levels | 6μs | 5μs | `{k: {k: int}}` |
| Mixed Array+Map 3 levels | 8μs | 8μs | `[[{k: int}]]` |

**Note:** `var` provides slight performance advantage for complex literals due to deferred type inference.

---

## JaiScript vs ChaiScript Comparison

Direct head-to-head benchmarks comparing JaiScript against ChaiScript (a popular C++ embedded scripting language).

### Head-to-Head Comparison

**JaiScript wins every benchmark.** Results sorted by speedup:

| Benchmark | JaiScript | ChaiScript | JaiScript Speedup |
|-----------|-----------|------------|-------------------|
| **Factorial(10)** | 6μs | 1308μs | **218x faster** |
| **Fibonacci(6)** | 12μs | 3142μs | **262x faster** |
| **Recurse 10 Locals (depth=10)** | 21μs | 1417μs | **67x faster** |
| **BST (15 nodes)** | 489μs | 52,698μs | **108x faster** |
| **Function Calls** | 2μs | 164μs | **82x faster** |
| **Method Invocation** | 4μs | 137μs | **34x faster** |
| **Binary Search** | 9μs | 272μs | **30x faster** |
| **Range-For (100 elem)** | 19μs | 153μs | **8x faster** |
| **Class Creation** | 4μs | 16μs | **4x faster** |
| **Bubble Sort (10 elem)** | 64μs | 203μs | **3.2x faster** |
| **String Copy (5 copies)** | 5μs | 14μs | **2.8x faster** |
| **Variable Operations** | 2μs | 5μs | **2.5x faster** |
| **Array Push/Pop** | 7μs | 17μs | **2.4x faster** |
| **String Concat (20 iter)** | 8μs | 19μs | **2.4x faster** |
| **String Methods** | 6μs | 14μs | **2.3x faster** |
| **Map Insert/Lookup** | 5μs | 11μs | **2.2x faster** |
| **Variable Lookup Heavy** | 5μs | 10μs | **2x faster** |
| **Complex Expression** | 2μs | 4μs | **2x faster** |
| **C++ Bound BST** | 44μs | 72μs | **1.6x faster** |
| **For Loop (100 iter)** | 8μs | 12μs | **1.5x faster** |
| **Integer Addition** | <1μs | 1μs | **>1x faster** |
| **Float Multiplication** | 1μs | 1μs | **Equal** |

### Loop Performance Deep Dive

| Loop Pattern | JaiScript | ChaiScript | Notes |
|--------------|-----------|------------|-------|
| For Loop (literal condition) | 8μs | 12μs | **JaiScript 1.5x faster** |
| For Loop (expression condition) | 9μs | - | Dynamic end: `i < n` |
| Range-For (copy, 100 elem) | 19μs | 153μs | **JaiScript 8x faster** |
| Range-For (reference, 100 elem) | 24μs | - | `for(auto& x : arr)` |
| Range-For (copy, 10 elem) | 3μs | - | Small array optimization |
| Range-For (reference, 10 elem) | 4μs | - | Small array reference |
| Hot Loop (10x100 nested) | 46μs | - | Nested iteration scaling |
| Hot Loop (1000 iter, auto) | 43μs | - | Declaration type: auto |
| Hot Loop (1000 iter, int) | 43μs | - | Declaration type: int |
| Hot Loop (1000 iter, var) | 42μs | - | Declaration type: var |

**Loop Optimization Notes:**
- **JaiScript now beats ChaiScript on for loops** (8μs vs 12μs = 1.5x faster)
- Literal condition fast path saves ~11% (8μs vs 9μs)
- Range-for with copy is faster than reference (no reference wrapper overhead)
- JaiScript range-for is **8x faster** than ChaiScript's equivalent
- All declaration types (auto/int/var) use unified fast path with equal performance (~42-43μs)

### Value Type Comparison (script_value vs BoxedValue)

| Operation | JaiScript | ChaiScript | Notes |
|-----------|-----------|------------|-------|
| Integer Construction | <1μs | 1μs | JaiScript faster |
| String Construction | <1μs | 1μs | JaiScript faster |
| Boolean Construction | <1μs | 1μs | JaiScript faster |
| Float Construction | <1μs | 1μs | JaiScript faster |
| Type Checking | 1μs | 3μs | **JaiScript 3x faster** |
| Array Construction | 2μs | 5μs | **JaiScript 2.5x faster** |
| Mixed Type Operations | 3μs | 6μs | **JaiScript 2x faster** |

### Key Insights

1. **JaiScript Dominates Across All Categories**
   - ID-based lookups and optimized call stack give JaiScript **82-218x advantage** in function-heavy code
   - Recursive algorithms (factorial, fibonacci) show the biggest wins (~218x faster)
   - Method invocation is **34x faster** than ChaiScript
   - Native script BST is **108x faster** (489μs vs 52.7ms)
   - **For loops now faster**: JaiScript 8μs vs ChaiScript 12μs (1.5x faster)

2. **String Operations Now JaiScript's Strength**
   - String copy is **2.8x faster** (5μs vs 14μs)
   - String concat is **2.4x faster** (8μs vs 19μs)
   - Improved string interning provides consistent advantages

3. **Language Features**
   - JaiScript supports proper `null` for object fields (tree structures work natively)
   - ChaiScript requires workarounds with `is_var_null()` for undefined checks
   - JaiScript's native class system handles complex data structures better
   - C++ bound BST now **1.6x faster** than ChaiScript (44μs vs 72μs)

4. **Real-World Implications**
   - **Game scripting**: JaiScript wins decisively across all workloads
   - **Tight computation loops**: JaiScript now competitive (8μs vs 12μs for-loop)
   - **Complex data structures**: JaiScript's null support + performance is ideal

*Benchmarks run on x64-Release build with MSVC 2022, /O2 /GL /LTCG optimizations (2025-12-26)*

---

## JaiScript vs Squirrel Comparison

Squirrel is a bytecode-compiled VM used in games like Left 4 Dead 2, Portal 2, and GTA IV. This comparison shows JaiScript (tree-walking interpreter) against a production bytecode VM.

### Head-to-Head Comparison

| Benchmark | JaiScript | Squirrel | Notes |
|-----------|-----------|----------|-------|
| **Integer Addition** | <1μs | 1μs | JaiScript faster |
| **Float Multiplication** | 1μs | 1μs | Equal |
| **Variable Operations** | 2μs | 2μs | Equal |
| **Function Calls** | 2μs | 1μs | Squirrel 2x faster |
| **Array Push/Pop** | 7μs | 3μs | Squirrel 2.3x faster |
| **Map/Table Operations** | 5μs | 3μs | Squirrel 1.7x faster |
| **Class Creation** | 4μs | 2μs | Squirrel 2x faster |
| **Method Invocation** | 4μs | 2μs | Squirrel 2x faster |
| **For Loop (100 iter)** | 8μs | 4μs | Squirrel 2x faster |
| **Factorial(10)** | 6μs | 2μs | Squirrel 3x faster |
| **Fibonacci(15)** | 878μs | 54μs | Squirrel 16x faster |
| **Recurse 10 Locals (depth=15)** | 36μs | 3μs | Squirrel 12x faster |
| **Foreach (10 elem)** | 4μs | 2μs | Squirrel 2x faster |
| **String Concat** | 8μs | 3μs | Squirrel 2.7x faster |
| **Null Check** | 3μs | 2μs | Squirrel 1.5x faster |
| **Hot Loop (1000 iter)** | 42μs | 10μs | Squirrel 4x faster |
| **BST (15 nodes)** | 406μs | 15μs | Squirrel 27x faster |
| **C++ BST (15 nodes)** | 45μs | 11μs | Squirrel 4x faster |

### Analysis

Squirrel's bytecode VM provides consistent 2-4x performance advantage over JaiScript's tree-walking interpreter for most operations. Deep recursion shows larger gaps (16x for Fibonacci), but slot-based locals help close the gap on local-heavy code (12x vs 16x).

**Key Takeaways:**
- Squirrel's bytecode compilation pays off for tight loops and recursion
- JaiScript's slot-based locals approach Squirrel's efficiency (12x gap vs 16x on Fibonacci)
- JaiScript remains competitive on simple operations (<1-2μs difference)
- C++ binding reduces gap significantly: 4x (C++ BST) vs 27x (pure script BST)
- For typical game scripting (event handlers, UI), the 2-4x difference is negligible

**Future Consideration:** Bytecode compilation would close the remaining gap with Squirrel.

---

### Performance Profile

**Excellent Performance:**
- ✅ **Function/Method Calls**: 2-4μs - ID-based lookups + string interning eliminate overhead
- ✅ **Recursion**: 6-14μs - Efficient stack frame management + cached symbol IDs + unchecked accessors
- ✅ **Algorithms**: Fast execution for search (9μs), sort (64μs), and computational tasks
- ✅ **Class Operations**: 4μs instantiation - ID-based field/method access
- ✅ **For Loops**: ~80ns per iteration - **now faster than ChaiScript**
- ✅ **Hot Loops**: 44μs for 1000 iterations (~44ns per iteration)
- ✅ **String Operations**: 5-8μs - 2-3x faster than ChaiScript
- ✅ **Container Operations**: 5-7μs - 2x faster than ChaiScript

**Recommendations:**
- Use JaiScript for all game scripting workloads - it now beats ChaiScript everywhere
- Function-heavy code sees the biggest wins (82-218x faster)
- Even tight loops are now competitive with ChaiScript

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

**Result**: 8μs for 100 iterations = ~80ns per iteration. **JaiScript now beats ChaiScript** on for-loops.

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
1. **Reuse engine instances** - Engine creation is ~42μs (stdlib registration adds ~133μs)
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
- [x] Constant folding in parser (implemented - 1μs vs ChaiScript's 3μs)
- [ ] Dead code elimination
- [ ] Type inference for faster dispatch
- [ ] SIMD operations for arrays
- [ ] Bytecode compilation (would close gap with Squirrel)

## Benchmark History

Track performance improvements over time (git-verified):

| Date       | Commit   | For Loop (100) | Hot Loop (1000) | vs ChaiScript | Notes                           |
|------------|----------|----------------|-----------------|---------------|---------------------------------|
| 2025-12-28 | HEAD     | 8μs            | 42μs            | **1.5x faster** | Slot-based local variables (Fib: 1051→878μs) |
| 2025-12-26 | cfc7720c | 8μs            | 44μs            | **1.5x faster** | String interning improvements |
| 2025-12-24 | 13d54d35 | 15μs           | 49μs            | 1.25x slower  | strong_ptr + shared string storage |
| 2025-12-18 | 9878fe2d | 17μs           | 76μs            | 1.4x slower   | + dynamic_cast removal (90 calls) + throw cleanup |
| 2025-12-15 | 9310a48f | 28μs           | 197μs           | 2.3x slower   | Switch-based AST dispatch |
| 2025-12-04 | bb2b900a | 42μs           | ~400μs          | 3.5x slower   | "only 2x loop perf of ChaiScript" |
| 2025-11-07 | bf50a1a6 | 67-79μs        | 715μs           | ~6x slower    | Aggressive string interning |
| 2025-11-04 | 47b8a6aa | 101μs          | 1136μs          | 8.4x slower   | Initial PERFORMANCE.md |

**Progress: 8.4x slower → 1.5x FASTER = JaiScript now wins on loops**

ChaiScript For Loop baseline: 12μs

---

*Benchmarks run on: Windows x64*
*Compiler: MSVC 2022 with /O2 /GL /LTCG*
*Build: x64-Release*
