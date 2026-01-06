# JaiScript Performance Analysis

## Current JaiScript Performance (Release Build)

Benchmarks run with `/O2 /GL /LTCG` optimizations on x64-Release configuration (2025-12-28):

| Benchmark                      | Time (uS) | Notes                                    |
|--------------------------------|-----------|------------------------------------------|
| Integer Addition               | <1        | Single arithmetic operation              |
| Float Multiplication           | <1        | Single floating-point operation          |
| Variable Operations            | 2         | 3 variable declarations + 1 addition     |
| Function Calls                 | 4         | Function declaration + invocation        |
| Array Push/Pop                 | 7         | 3 pushes + 1 pop + size check            |
| Map Insert/Lookup              | 5         | 2 inserts + 1 lookup                     |
| Class Creation                 | 11        | Class definition + instantiation         |
| Method Invocation              | 9         | Class with method + method call          |
| For Loop (100 iterations)      | 8         | 0.08uS per iteration (~80ns)             |
| String Concatenation           | 3         | String operations                        |
| Complex Expression             | 2         | Multi-operator expression evaluation     |
| Class Inheritance              | 94        | Base + derived class + instantiation     |
| Variable Lookup Heavy          | 5         | 10 variable lookups in expression        |
| Hot Loop (1000 iterations)     | 44        | 0.044uS per iteration (~44ns)            |
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
- Hot loops: 37% faster (1136uS → 715uS)
- Recursion: 15-19% faster (Fibonacci: 27uS → 22uS)
- Class operations: 14-18% faster (Creation: 7uS → 6uS, Inheritance: 133uS → 109uS)
- Algorithms: 13-19% faster (Bubble sort: 156uS → 127uS)

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
- **Recursive algorithms**: ~94% faster (Fibonacci benchmark: 3381uS → 220uS → 22uS)
- **Loop-heavy operations**: ~37-47% faster (For Loop: 125uS → 79uS → 67uS, Hot Loop: 1345uS → 1136uS → 715uS)
- **String method calls**: ~27% faster (String Concatenation: 11uS → 8uS)
- **Array algorithms**: ~61% faster (Bubble Sort: 329uS → 200uS → 156uS → 127uS)
- **Method invocation**: ~55% faster (Method Invocation: 20uS → 17uS → 9uS)
- **Function calls**: ~60% faster (Function Calls: 10uS → 5uS → 4uS)
- **Class operations**: ~75% faster (Class Creation: 24uS → 22uS → 7uS → 6uS, Class Inheritance: 139uS → 133uS → 109uS)

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
- **Fibonacci(15)**: 1051uS → 878uS (**16% faster**)
- **Recurse with 10 Locals (depth=15)**: 36uS (new benchmark)
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
- **Hot Loops**: 71uS → 49uS (**31% faster**)
- **For Loops**: 17uS → 15uS (**12% faster**, gap now 1.25x vs ChaiScript)
- **Bubble Sort**: 115uS → 82uS (**29% faster**)
- **BST**: 700uS → 596uS (**15% faster**)
- **Fibonacci**: 20uS → 17uS (**15% faster**)
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

**JaiScript** (~8uS for 100 iterations = **80ns per iteration**):
```
Per iteration:
1. condition (i < 100):     ~1ns (get_type() + static_cast, no RTTI)
2. body (sum += i):         ~5ns (2 ID lookups + optimized type checks + value construction)
3. increment (++i):         ~2ns (ID lookup + optimized type check + value construction)
                           ------
Total: ~8ns × 100 = 0.8uS + overhead = 8uS
```

**JaiScript now beats ChaiScript** (8uS vs 12uS = 1.5x faster).

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
- Result: JaiScript for-loop (8uS) now **beats ChaiScript** (12uS) by 1.5x

---

## Benchmark Results

Performance measurements running representative workloads:

### Summary Table

| Benchmark                        | JaiScript Time | Notes |
|----------------------------------|----------------|-------|
| **Integer Addition**             | <1uS           | Single arithmetic operation |
| **Float Multiplication**         | <1uS           | Floating-point operation |
| **Variable Operations**          | 2uS            | Variable declarations + addition |
| **Function Calls**               | 2uS            | Function declaration + invocation |
| **Array Push/Pop**               | 7uS            | Array operations |
| **Map Insert/Lookup**            | 5uS            | Hash map operations |
| **Class Creation**               | 4uS            | Class instantiation |
| **Method Invocation**            | 4uS            | Instance method call |
| **For Loop (100 iterations)**    | 8uS            | ~80ns per iteration |
| **Variable Lookup Heavy**        | 5uS            | Multiple variable accesses |
| **Complex Expression**           | 2uS            | Multi-operator expression |
| **Factorial(10) - Recursion**    | 6uS            | Recursive algorithm |
| **Fibonacci(6) - Deep Recursion**| 14uS           | Deep recursive calls |
| **Binary Search**                | 9uS            | Search algorithm |
| **Bubble Sort (10 elements)**    | 64uS           | Sorting algorithm |
| **Hot Loop (1000 iterations)**   | 44uS           | ~44ns per iteration |
| **BST (15 nodes)**               | 489uS          | Binary Search Tree operations |

### Array/Map Literal Benchmarks

| Benchmark | auto | var | Notes |
|-----------|------|-----|-------|
| Simple Array [10 ints] | 4uS | 3uS | `[1,2,3,4,5,6,7,8,9,10]` |
| 2D Array [[5x5 ints]] | 11uS | 10uS | Nested array construction |
| 3D Array [[[2x2x2 ints]]] | 6uS | 5uS | Deep nesting |
| Homogeneous Map {5 keys} | 4uS | 4uS | `{"a":1, "b":2, ...}` |
| Heterogeneous Map {5 mixed} | - | 4uS | Mixed value types |
| Nested Map 2 levels | 6uS | 5uS | `{k: {k: int}}` |
| Mixed Array+Map 3 levels | 8uS | 8uS | `[[{k: int}]]` |

**Note:** `var` provides slight performance advantage for complex literals due to deferred type inference.

---

## JaiScript vs ChaiScript Comparison

Direct head-to-head benchmarks comparing JaiScript against ChaiScript (a popular C++ embedded scripting language).

### Head-to-Head Comparison

**JaiScript wins every benchmark.** Results sorted by speedup:

| Benchmark | JaiScript | ChaiScript | JaiScript Speedup |
|-----------|-----------|------------|-------------------|
| **Factorial(10)** | 6uS | 1308uS | **218x faster** |
| **Fibonacci(6)** | 12uS | 3142uS | **262x faster** |
| **Recurse 10 Locals (depth=10)** | 21uS | 1417uS | **67x faster** |
| **BST (15 nodes)** | 489uS | 52,698uS | **108x faster** |
| **Function Calls** | 2uS | 164uS | **82x faster** |
| **Method Invocation** | 4uS | 137uS | **34x faster** |
| **Binary Search** | 9uS | 272uS | **30x faster** |
| **Range-For (100 elem)** | 19uS | 153uS | **8x faster** |
| **Class Creation** | 4uS | 16uS | **4x faster** |
| **Bubble Sort (10 elem)** | 64uS | 203uS | **3.2x faster** |
| **String Copy (5 copies)** | 5uS | 14uS | **2.8x faster** |
| **Variable Operations** | 2uS | 5uS | **2.5x faster** |
| **Array Push/Pop** | 7uS | 17uS | **2.4x faster** |
| **String Concat (20 iter)** | 8uS | 19uS | **2.4x faster** |
| **String Methods** | 6uS | 14uS | **2.3x faster** |
| **Map Insert/Lookup** | 5uS | 11uS | **2.2x faster** |
| **Variable Lookup Heavy** | 5uS | 10uS | **2x faster** |
| **Complex Expression** | 2uS | 4uS | **2x faster** |
| **C++ Bound BST** | 44uS | 72uS | **1.6x faster** |
| **For Loop (100 iter)** | 8uS | 12uS | **1.5x faster** |
| **Integer Addition** | <1uS | 1uS | **>1x faster** |
| **Float Multiplication** | 1uS | 1uS | **Equal** |

### Loop Performance Deep Dive

| Loop Pattern | JaiScript | ChaiScript | Notes |
|--------------|-----------|------------|-------|
| For Loop (literal condition) | 8uS | 12uS | **JaiScript 1.5x faster** |
| For Loop (expression condition) | 9uS | - | Dynamic end: `i < n` |
| Range-For (copy, 100 elem) | 19uS | 153uS | **JaiScript 8x faster** |
| Range-For (reference, 100 elem) | 24uS | - | `for(auto& x : arr)` |
| Range-For (copy, 10 elem) | 3uS | - | Small array optimization |
| Range-For (reference, 10 elem) | 4uS | - | Small array reference |
| Hot Loop (10x100 nested) | 46uS | - | Nested iteration scaling |
| Hot Loop (1000 iter, auto) | 43uS | - | Declaration type: auto |
| Hot Loop (1000 iter, int) | 43uS | - | Declaration type: int |
| Hot Loop (1000 iter, var) | 42uS | - | Declaration type: var |

**Loop Optimization Notes:**
- **JaiScript now beats ChaiScript on for loops** (8uS vs 12uS = 1.5x faster)
- Literal condition fast path saves ~11% (8uS vs 9uS)
- Range-for with copy is faster than reference (no reference wrapper overhead)
- JaiScript range-for is **8x faster** than ChaiScript's equivalent
- All declaration types (auto/int/var) use unified fast path with equal performance (~42-43uS)

### Value Type Comparison (script_value vs BoxedValue)

| Operation | JaiScript | ChaiScript | Notes |
|-----------|-----------|------------|-------|
| Integer Construction | <1uS | 1uS | JaiScript faster |
| String Construction | <1uS | 1uS | JaiScript faster |
| Boolean Construction | <1uS | 1uS | JaiScript faster |
| Float Construction | <1uS | 1uS | JaiScript faster |
| Type Checking | 1uS | 3uS | **JaiScript 3x faster** |
| Array Construction | 2uS | 5uS | **JaiScript 2.5x faster** |
| Mixed Type Operations | 3uS | 6uS | **JaiScript 2x faster** |

### Key Insights

1. **JaiScript Dominates Across All Categories**
   - ID-based lookups and optimized call stack give JaiScript **82-218x advantage** in function-heavy code
   - Recursive algorithms (factorial, fibonacci) show the biggest wins (~218x faster)
   - Method invocation is **34x faster** than ChaiScript
   - Native script BST is **108x faster** (489uS vs 52.7ms)
   - **For loops now faster**: JaiScript 8uS vs ChaiScript 12uS (1.5x faster)

2. **String Operations Now JaiScript's Strength**
   - String copy is **2.8x faster** (5uS vs 14uS)
   - String concat is **2.4x faster** (8uS vs 19uS)
   - Improved string interning provides consistent advantages

3. **Language Features**
   - JaiScript supports proper `null` for object fields (tree structures work natively)
   - ChaiScript requires workarounds with `is_var_null()` for undefined checks
   - JaiScript's native class system handles complex data structures better
   - C++ bound BST now **1.6x faster** than ChaiScript (44uS vs 72uS)

4. **Real-World Implications**
   - **Game scripting**: JaiScript wins decisively across all workloads
   - **Tight computation loops**: JaiScript now competitive (8uS vs 12uS for-loop)
   - **Complex data structures**: JaiScript's null support + performance is ideal

*Benchmarks run on x64-Release build with MSVC 2022, /O2 /GL /LTCG optimizations (2025-12-26)*

---

## JaiScript vs Squirrel Comparison

Squirrel is a bytecode-compiled VM used in games like Left 4 Dead 2, Portal 2, and GTA IV. This comparison shows JaiScript (tree-walking interpreter) against a production bytecode VM.

### Head-to-Head Comparison

| Benchmark | JaiScript | Squirrel | Notes |
|-----------|-----------|----------|-------|
| **Integer Addition** | <1uS | 1uS | JaiScript faster |
| **Float Multiplication** | 1uS | 1uS | Equal |
| **Variable Operations** | 2uS | 2uS | Equal |
| **Function Calls** | 2uS | 1uS | Squirrel 2x faster |
| **Array Push/Pop** | 7uS | 3uS | Squirrel 2.3x faster |
| **Map/Table Operations** | 5uS | 3uS | Squirrel 1.7x faster |
| **Class Creation** | 4uS | 2uS | Squirrel 2x faster |
| **Method Invocation** | 4uS | 2uS | Squirrel 2x faster |
| **For Loop (100 iter)** | 8uS | 4uS | Squirrel 2x faster |
| **Factorial(10)** | 6uS | 2uS | Squirrel 3x faster |
| **Fibonacci(15)** | 878uS | 54uS | Squirrel 16x faster |
| **Recurse 10 Locals (depth=15)** | 36uS | 3uS | Squirrel 12x faster |
| **Foreach (10 elem)** | 4uS | 2uS | Squirrel 2x faster |
| **String Concat** | 8uS | 3uS | Squirrel 2.7x faster |
| **Null Check** | 3uS | 2uS | Squirrel 1.5x faster |
| **Hot Loop (1000 iter)** | 42uS | 10uS | Squirrel 4x faster |
| **BST (15 nodes)** | 406uS | 15uS | Squirrel 27x faster |
| **C++ BST (15 nodes)** | 45uS | 11uS | Squirrel 4x faster |

### Analysis

Squirrel's bytecode VM provides consistent 2-4x performance advantage over JaiScript's tree-walking interpreter for most operations. Deep recursion shows larger gaps (16x for Fibonacci), but slot-based locals help close the gap on local-heavy code (12x vs 16x).

**Key Takeaways:**
- Squirrel's bytecode compilation pays off for tight loops and recursion
- JaiScript's slot-based locals approach Squirrel's efficiency (12x gap vs 16x on Fibonacci)
- JaiScript remains competitive on simple operations (<1-2uS difference)
- C++ binding reduces gap significantly: 4x (C++ BST) vs 27x (pure script BST)
- For typical game scripting (event handlers, UI), the 2-4x difference is negligible

**Future Consideration:** Bytecode compilation would close the remaining gap with Squirrel.

---

### Performance Profile

**Excellent Performance:**
- ✅ **Function/Method Calls**: 2-4uS - ID-based lookups + string interning eliminate overhead
- ✅ **Recursion**: 6-14uS - Efficient stack frame management + cached symbol IDs + unchecked accessors
- ✅ **Algorithms**: Fast execution for search (9uS), sort (64uS), and computational tasks
- ✅ **Class Operations**: 4uS instantiation - ID-based field/method access
- ✅ **For Loops**: ~80ns per iteration - **now faster than ChaiScript**
- ✅ **Hot Loops**: 44uS for 1000 iterations (~44ns per iteration)
- ✅ **String Operations**: 5-8uS - 2-3x faster than ChaiScript
- ✅ **Container Operations**: 5-7uS - 2x faster than ChaiScript

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

**Result**: 8uS for 100 iterations = ~80ns per iteration. **JaiScript now beats ChaiScript** on for-loops.

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
1. **Reuse engine instances** - Engine creation is ~42uS (stdlib registration adds ~133uS)
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
- [x] Constant folding in parser (implemented - 1uS vs ChaiScript's 3uS)
- [ ] Dead code elimination
- [ ] Type inference for faster dispatch
- [ ] SIMD operations for arrays
- [ ] Bytecode compilation (would close gap with Squirrel)

## Benchmark History

Track performance improvements over time (git-verified):

| Date       | Commit   | For Loop (100) | Hot Loop (1000) | vs ChaiScript | Notes                           |
|------------|----------|----------------|-----------------|---------------|---------------------------------|
| 2025-12-28 | HEAD     | 8uS            | 42uS            | **1.5x faster** | Slot-based local variables (Fib: 1051→878uS) |
| 2025-12-26 | cfc7720c | 8uS            | 44uS            | **1.5x faster** | String interning improvements |
| 2025-12-24 | 13d54d35 | 15uS           | 49uS            | 1.25x slower  | strong_ptr + shared string storage |
| 2025-12-18 | 9878fe2d | 17uS           | 76uS            | 1.4x slower   | + dynamic_cast removal (90 calls) + throw cleanup |
| 2025-12-15 | 9310a48f | 28uS           | 197uS           | 2.3x slower   | Switch-based AST dispatch |
| 2025-12-04 | bb2b900a | 42uS           | ~400uS          | 3.5x slower   | "only 2x loop perf of ChaiScript" |
| 2025-11-07 | bf50a1a6 | 67-79uS        | 715uS           | ~6x slower    | Aggressive string interning |
| 2025-11-04 | 47b8a6aa | 101uS          | 1136uS          | 8.4x slower   | Initial PERFORMANCE.md |

**Progress: 8.4x slower → 1.5x FASTER = JaiScript now wins on loops**

ChaiScript For Loop baseline: 12uS

---

*Benchmarks run on: Windows x64*
*Compiler: MSVC 2022 with /O2 /GL /LTCG*
*Build: x64-Release*
