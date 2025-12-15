# Parser checked_result Migration Plan

## Objective

Migrate the parser from exception-based error handling to `checked_result<T>` for zero-overhead error handling. Exceptions should only be thrown at the user-facing API boundary (`engine::execute()`), not internally during parsing.

## Current Architecture (Exception-Based)

### Error Flow
1. **Parser collects errors** in `errors_` vector during parsing
2. **Individual parsing functions** throw `parse_error` exceptions when they encounter errors
3. **`parse()` catches** these exceptions in a try-catch loop and calls `synchronize()` for error recovery
4. **After parsing**, if `errors_` is not empty, `parse()` throws a `parse_error` exception
5. **Engine catches** the exception and re-throws it to the user

### Current Code Structure

**parser.hpp:**
```cpp
class parser {
public:
    std::vector<declaration_ptr> parse();  // Can throw parse_error

private:
    void error(const std::string& message, const token& token);  // Throws parse_error

    // All these can throw parse_error:
    declaration_ptr declaration();
    declaration_ptr class_declaration();
    declaration_ptr function_declaration();
    statement_ptr statement();
    expression_ptr expression();
    // ... ~20 more parsing functions
};
```

**parser.cpp:**
```cpp
std::vector<declaration_ptr> parser::parse() {
    while (!is_at_end()) {
        try {
            auto decl = declaration();
            if (decl) {
                declarations.push_back(decl);
            }
        } catch (const parse_error&) {
            synchronize();  // Error recovery
        }
    }

    if (!errors_.empty()) {
        throw parse_error(errors_[0], source_location{});  // Throw to user
    }

    return declarations;
}

void parser::error(const std::string& message, const token& token) {
    errors_.push_back(format_error(message, token));
    throw parse_error(message, token.location);  // Exception for unwinding
}
```

## Target Architecture (checked_result-Based)

### Error Flow
1. **Parser collects errors** in `errors_` vector during parsing (unchanged)
2. **Individual parsing functions** return `checked_result<T>` with error codes when they encounter errors
3. **`parse()` checks** returned `checked_result` and calls `synchronize()` for error recovery
4. **After parsing**, if `errors_` is not empty, `parse()` returns `checked_result<vector<declaration_ptr>>` with error
5. **Engine converts** the `checked_result` to an exception at the API boundary

### New Code Structure

**parser.hpp:**
```cpp
#include <jaiscript/core/checked_result.hpp>
#include <jaiscript/core/parse_errors.hpp>

class parser {
public:
    // Returns checked_result instead of throwing
    checked_result<std::vector<declaration_ptr>> parse();

private:
    // Returns error instead of throwing
    checked_result<void> report_error(const std::string& message, const token& token);

    // All these now return checked_result<T>:
    checked_result<declaration_ptr> declaration();
    checked_result<declaration_ptr> class_declaration();
    checked_result<declaration_ptr> function_declaration();
    checked_result<statement_ptr> statement();
    checked_result<expression_ptr> expression();
    // ... ~20 more parsing functions
};
```

**parser.cpp:**
```cpp
checked_result<std::vector<declaration_ptr>> parser::parse() {
    std::vector<declaration_ptr> declarations;

    while (!is_at_end()) {
        auto decl_result = declaration();

        if (!decl_result) {
            // Error occurred - synchronize and continue
            synchronize();
        } else if (decl_result.value()) {
            declarations.push_back(std::move(decl_result.value()));
        }
    }

    // If there were parse errors, return error
    if (!errors_.empty()) {
        return checked_result<std::vector<declaration_ptr>>(
            make_error_code(parse_error_code::invalid_expression),
            errors_[0]
        );
    }

    // Mark last expression as implicit return
    // ... (unchanged logic)

    return checked_result<std::vector<declaration_ptr>>(std::move(declarations));
}

checked_result<void> parser::report_error(const std::string& message, const token& token) {
    std::stringstream ss;
    ss << token.location.to_string() << ": " << message;
    errors_.push_back(ss.str());

    // Return error code instead of throwing
    return checked_result<void>(
        make_error_code(parse_error_code::unexpected_token),
        message
    );
}
```

## Files Created (Already Done)

### 1. `include/jaiscript/core/parse_errors.hpp`
- Defines `parse_error_code` enum with ~25 error codes
- Defines `parse_error_category_impl` for error messages
- Provides `make_error_code(parse_error_code)` helper

### 2. `source/implementation/parse_errors.cpp`
- Implements `parse_error_category_impl::message()` with human-readable messages
- Provides global `parse_error_category()` instance

### 3. `source/CMakeLists.txt`
- Added `implementation/parse_errors.cpp` to `JAISCRIPT_SOURCES`

## Files to Modify

### 1. `include/jaiscript/detail/parser.hpp`

**Changes needed:**
- Add includes: `#include <jaiscript/core/checked_result.hpp>` and `#include <jaiscript/core/parse_errors.hpp>`
- Change `parse()` signature: `std::vector<declaration_ptr> parse()` → `checked_result<std::vector<declaration_ptr>> parse()`
- Change `error()` to `report_error()`: `void error(...)` → `checked_result<void> report_error(...)`
- Update ALL parsing function signatures (~25 functions):
  - `declaration_ptr declaration()` → `checked_result<declaration_ptr> declaration()`
  - `declaration_ptr class_declaration()` → `checked_result<declaration_ptr> class_declaration()`
  - `declaration_ptr function_declaration()` → `checked_result<declaration_ptr> function_declaration()`
  - `statement_ptr statement()` → `checked_result<statement_ptr> statement()`
  - `statement_ptr if_statement()` → `checked_result<statement_ptr> if_statement()`
  - `expression_ptr expression()` → `checked_result<expression_ptr> expression()`
  - `expression_ptr assignment()` → `checked_result<expression_ptr> assignment()`
  - ... (all ~25 parsing functions)

**Full list of functions to update:**
- `declaration()`
- `class_declaration()`
- `namespace_declaration()`
- `function_declaration()`
- `variable_declaration()`
- `include_declaration()`
- `import_declaration()`
- `statement()`
- `expression_statement()`
- `block_statement()`
- `if_statement()`
- `while_statement()`
- `for_statement()`
- `return_statement()`
- `break_statement()`
- `continue_statement()`
- `try_statement()`
- `switch_statement()`
- `expression()`
- `assignment()`
- `ternary()`
- `logical_or()`
- `logical_and()`
- `bitwise_or()`
- `bitwise_xor()`
- `bitwise_and()`
- `equality()`
- `relational()`
- `shift()`
- `additive()`
- `multiplicative()`
- `unary()`
- `postfix()`
- `primary()`
- `finish_call()`
- `finish_member_access()`
- `parse_map_literal()`

### 2. `source/implementation/parser.cpp`

**Pattern for migration:**

**BEFORE (exception-based):**
```cpp
expression_ptr parser::primary() {
    if (match(TOKEN_INT_LITERAL)) {
        return std::make_shared<int_literal_expr>(previous().int_value);
    }

    if (match(TOKEN_IDENTIFIER)) {
        return std::make_shared<identifier_expr>(previous().text);
    }

    error("Expected expression", peek());
    return nullptr;  // Unreachable
}
```

**AFTER (checked_result-based):**
```cpp
checked_result<expression_ptr> parser::primary() {
    if (match(TOKEN_INT_LITERAL)) {
        return std::make_shared<int_literal_expr>(previous().int_value);
    }

    if (match(TOKEN_IDENTIFIER)) {
        return std::make_shared<identifier_expr>(previous().text);
    }

    return report_error("Expected expression", peek());
}
```

**BEFORE (with nested calls):**
```cpp
expression_ptr parser::unary() {
    if (match({TOKEN_MINUS, TOKEN_NOT, TOKEN_BITWISE_NOT})) {
        token op = previous();
        expression_ptr right = unary();  // Recursive call
        return std::make_shared<unary_expr>(op, right);
    }

    return postfix();
}
```

**AFTER (with error propagation):**
```cpp
checked_result<expression_ptr> parser::unary() {
    if (match({TOKEN_MINUS, TOKEN_NOT, TOKEN_BITWISE_NOT})) {
        token op = previous();

        // Use JAISCRIPT_TRY macro for early return on error
        JAISCRIPT_TRY_ASSIGN(auto right, unary());

        return std::make_shared<unary_expr>(op, right);
    }

    return postfix();
}
```

**Key patterns:**

1. **Simple return:** `return value;` → `return checked_result<T>(value);`
2. **Error return:** `error(...); return nullptr;` → `return report_error(...);`
3. **Nested call (assign):** `auto x = func();` → `JAISCRIPT_TRY_ASSIGN(auto x, func());`
4. **Nested call (direct):** `return func();` → `return func();` (already returns checked_result)
5. **Consume token:** `consume(TOKEN, "msg")` needs to return `checked_result<token>`

**consume() migration:**

**BEFORE:**
```cpp
token parser::consume(token_type type, const std::string& message) {
    if (check(type)) return advance();
    error(message, peek());
    return token{};  // Unreachable
}
```

**AFTER:**
```cpp
checked_result<token> parser::consume(token_type type, const std::string& message) {
    if (check(type)) return advance();
    return report_error(message, peek());
}
```

### 3. `source/implementation/engine.cpp`

**Location:** Find the `execute()` method that calls `parser::parse()`

**BEFORE:**
```cpp
script_value engine::execute(const std::string& source) {
    // ... lexing ...

    parser p(tokens, symbolizer, registered_template_types, filename);
    std::vector<declaration_ptr> ast = p.parse();  // Can throw

    // ... interpretation ...
}
```

**AFTER:**
```cpp
script_value engine::execute(const std::string& source) {
    // ... lexing ...

    parser p(tokens, symbolizer, registered_template_types, filename);
    auto parse_result = p.parse();

    // Convert checked_result to exception at API boundary
    if (!parse_result) {
        throw parse_error(parse_result.message(), source_location{});
    }

    std::vector<declaration_ptr> ast = std::move(parse_result.value());

    // ... interpretation ...
}
```

## Migration Strategy

### Phase 1: Bottom-Up Migration (Leaf Functions First)

Start with functions that don't call other parsing functions:

1. **Leaf functions** (no parsing function calls):
   - `primary()` - No recursive calls
   - `break_statement()` - No parsing calls
   - `continue_statement()` - No parsing calls
   - Update these to return `checked_result<T>`

2. **One level up** (call leaf functions):
   - `postfix()` - Calls `primary()` and `finish_call()`
   - `unary()` - Calls `postfix()`
   - Update to use `JAISCRIPT_TRY_ASSIGN`

3. **Continue up the tree** until all functions migrated

### Phase 2: Update consume() and Helper Functions

Functions like `consume()` need special attention:

```cpp
// Update signature
checked_result<token> consume(token_type type, const std::string& message);

// All call sites change from:
token t = consume(TOKEN_SEMICOLON, "Expected ';'");

// To:
JAISCRIPT_TRY_ASSIGN(token t, consume(TOKEN_SEMICOLON, "Expected ';'"));
```

### Phase 3: Update parse() Main Loop

```cpp
checked_result<std::vector<declaration_ptr>> parser::parse() {
    std::vector<declaration_ptr> declarations;

    while (!is_at_end()) {
        auto decl_result = declaration();

        if (!decl_result) {
            // Error occurred - already in errors_ vector
            synchronize();
        } else {
            auto decl = decl_result.value();
            if (decl) {
                declarations.push_back(std::move(decl));
            }
        }
    }

    // Check for accumulated errors
    if (!errors_.empty()) {
        return checked_result<std::vector<declaration_ptr>>(
            make_error_code(parse_error_code::invalid_expression),
            errors_[0]  // Return first error message
        );
    }

    // Mark last expression declaration as implicit return
    // ... (existing logic unchanged)

    return declarations;
}
```

### Phase 4: Update Engine Boundary

In `engine.cpp`, convert the final `checked_result` to exception:

```cpp
auto parse_result = parser.parse();
if (!parse_result) {
    // Convert to exception at user-facing API boundary
    throw parse_error(parse_result.message(), source_location{});
}
auto ast = std::move(parse_result.value());
```

## Helper Macros (Already Defined)

The `checked_result.hpp` already defines these macros:

```cpp
// Early return on error
#define JAISCRIPT_TRY(expr) \
    do { \
        auto __result = (expr); \
        if (!__result) [[unlikely]] { \
            return __result.error(); \
        } \
    } while(0)

// Early return on error with value extraction
#define JAISCRIPT_TRY_ASSIGN(var, expr) \
    auto __temp_result_##__LINE__ = (expr); \
    if (!__temp_result_##__LINE__) [[unlikely]] { \
        return __temp_result_##__LINE__.error(); \
    } \
    var = std::move(__temp_result_##__LINE__.value());
```

## Testing Strategy

1. **Build incrementally** - After each function migration, rebuild and run tests
2. **All existing tests should pass** - The external behavior is unchanged
3. **Error messages should be identical** - Users see the same errors
4. **Focus on parser_tests.cpp** - Ensure all parsing edge cases still work
5. **Performance validation** - Parsing should be faster without exception overhead

## Expected Performance Improvement

**Current (exception-based):**
- Every parse error throws and catches an exception
- Exception overhead: ~1-5μs per error on modern CPUs
- Stack unwinding, destructor calls, etc.

**After (checked_result-based):**
- Parse errors return error codes (no unwinding)
- Error path overhead: ~0.1μs (just a bool check and copy)
- **10-50x faster error handling**

For a file with 10 parse errors:
- Before: ~10-50μs in error handling overhead
- After: ~1μs in error handling overhead

## Migration Checklist

- [x] Create `parse_errors.hpp` with error codes
- [x] Create `parse_errors.cpp` with error messages
- [x] Update `CMakeLists.txt` to include `parse_errors.cpp`
- [ ] Update `parser.hpp` signatures (~35 functions)
- [ ] Update `consume()` to return `checked_result<token>`
- [ ] Migrate leaf parsing functions (5-10 functions)
- [ ] Migrate mid-level parsing functions (10-15 functions)
- [ ] Migrate top-level parsing functions (5-10 functions)
- [ ] Update `parse()` main loop to use checked_result
- [ ] Update `engine.cpp` to convert checked_result to exception
- [ ] Remove old `error()` function, replace with `report_error()`
- [ ] Build and run all tests
- [ ] Verify error messages are unchanged
- [ ] Benchmark parsing performance

## Common Pitfalls to Avoid

1. **Forgetting JAISCRIPT_TRY_ASSIGN:** Don't just assign the result of a parsing function - use the macro to check for errors
2. **Wrong return type:** Ensure `checked_result<T>` wraps the correct type
3. **Missing error propagation:** Every parsing function that calls another must check the result
4. **consume() call sites:** There are MANY calls to `consume()` - each needs updating
5. **Null pointer returns:** Some functions return `nullptr` on error - these should return error codes instead

## Example: Complete Function Migration

**BEFORE:**
```cpp
statement_ptr parser::if_statement() {
    token if_token = consume(TOKEN_IF, "Expected 'if'");
    consume(TOKEN_LEFT_PAREN, "Expected '(' after 'if'");

    expression_ptr condition = expression();

    consume(TOKEN_RIGHT_PAREN, "Expected ')' after condition");

    statement_ptr then_branch = statement();
    statement_ptr else_branch = nullptr;

    if (match(TOKEN_ELSE)) {
        else_branch = statement();
    }

    return std::make_shared<if_stmt>(condition, then_branch, else_branch);
}
```

**AFTER:**
```cpp
checked_result<statement_ptr> parser::if_statement() {
    JAISCRIPT_TRY_ASSIGN(token if_token, consume(TOKEN_IF, "Expected 'if'"));
    JAISCRIPT_TRY(consume(TOKEN_LEFT_PAREN, "Expected '(' after 'if'"));

    JAISCRIPT_TRY_ASSIGN(expression_ptr condition, expression());

    JAISCRIPT_TRY(consume(TOKEN_RIGHT_PAREN, "Expected ')' after condition"));

    JAISCRIPT_TRY_ASSIGN(statement_ptr then_branch, statement());
    statement_ptr else_branch = nullptr;

    if (match(TOKEN_ELSE)) {
        JAISCRIPT_TRY_ASSIGN(else_branch, statement());
    }

    return std::make_shared<if_stmt>(condition, then_branch, else_branch);
}
```

## Success Criteria

- ✅ All tests pass (305+ tests)
- ✅ Error messages unchanged from user perspective
- ✅ No exceptions thrown during parsing (only at engine boundary)
- ✅ Performance improvement measurable (use benchmarks from PERFORMANCE.md)
- ✅ Code compiles without warnings
- ✅ All parsing functions return `checked_result<T>`

## Notes

- The `parse_error` exception class in `types.hpp` can remain for the engine boundary
- Keep the `errors_` vector for collecting all errors during error recovery
- The `synchronize()` function remains unchanged
- This migration is purely internal - user-facing API stays the same
