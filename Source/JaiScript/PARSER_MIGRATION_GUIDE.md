# Parser.cpp checked_result Migration Guide

This document provides the complete migration plan for all remaining functions in parser.cpp.

## Migration Status

### Completed Functions (19/42)
✅ report_error(), consume(), break_statement(), continue_statement()
✅ parse_type(), expression(), assignment(), ternary()
✅ logical_or(), logical_and(), bitwise_or(), bitwise_xor(), bitwise_and()
✅ equality(), relational(), shift(), additive(), multiplicative(), unary()

### Remaining Functions (23/42)

## CRITICAL ISSUE: primary() Function

The primary() function (lines 167-495, ~330 lines) has these issues:

### Line 246: try-catch block needs conversion
```cpp
// BEFORE (lines 245-277):
try {
    type_info_ptr type = parse_type();
    if (type && check(token_type::left_brace)) {
        // ... uses type
    }
} catch (const parse_error&) {
    // Not a valid template type
}

// AFTER:
auto type_result = parse_type();
if (type_result) {
    type_info_ptr type = std::move(type_result.value());
    if (type && check(token_type::left_brace)) {
        // ... uses type
    }
} else {
    // Not a valid template type, restore position
}
```

### Lines with expression() calls needing JAISCRIPT_TRY_ASSIGN:
- Line 255: `arguments.push_back(expression());`
- Line 268: `arguments.push_back(expression());`
- Line 288: `expression_ptr expr = expression();`
- Line 346: `elements.push_back(expression());`
- Line 352: `elements.push_back(expression());`
- Line 412: `arguments.push_back(expression());`
- Line 452: `arguments.push_back(expression());`
- Line 467: `arguments.push_back(expression());`

### Lines with consume() calls needing JAISCRIPT_TRY or JAISCRIPT_TRY_ASSIGN:
- Line 259: `consume(token_type::right_brace, ...)`
- Line 272: `consume(token_type::right_paren, ...)`
- Line 289: `consume(token_type::right_paren, ...)`
- Line 299: `consume(token_type::right_bracket, ...)`
- Line 355: `consume(token_type::right_bracket, ...)`
- Line 417: `consume(token_type::right_brace, ...)`
- Line 419: `consume(token_type::right_paren, ...)`
- Line 456: `consume(token_type::right_brace, ...)`
- Line 471: `consume(token_type::right_paren, ...)`

### Lines with parse_type() calls needing JAISCRIPT_TRY_ASSIGN:
- Line 246, 389, 401, 442: All need conversion from try-catch to checked_result

### Lines with lambda_expression() calls:
- Lines 305, 323, 337, 360, 370, 378: Need JAISCRIPT_TRY_ASSIGN

### Line 493: error() call needs conversion
```cpp
// BEFORE:
error("Expected expression", peek());
return nullptr;

// AFTER:
return report_error("Expected expression", peek());
```

## Remaining Function Migrations

### 1. postfix() (lines 927-994)
**Changes needed:**
- Change return type to `checked_result<expression_ptr>`
- Line 928: `JAISCRIPT_TRY_ASSIGN(expression_ptr expr, primary());`
- Line 938: `JAISCRIPT_TRY_ASSIGN(expr, finish_call(expr));`
- Lines 949, 953: `JAISCRIPT_TRY_ASSIGN(auto elem, expression());`
- Line 960: Replace `error()` with `return report_error()`
- Line 963: `JAISCRIPT_TRY_ASSIGN(expr, finish_member_access(expr, false));`
- Line 965: `JAISCRIPT_TRY_ASSIGN(expr, finish_member_access(expr, true));`
- Line 977: Replace `error()` with `return report_error()`
- Line 981: `JAISCRIPT_TRY_ASSIGN(expression_ptr index, expression());`
- Line 982: `JAISCRIPT_TRY(consume(token_type::right_bracket, ...))`

### 2. finish_call() (lines 1164-1178)
```cpp
checked_result<expression_ptr> parser::finish_call(expression_ptr callee) {
    std::vector<expression_ptr> arguments;

    if (!check(token_type::right_paren)) {
        arguments.reserve(4);
        do {
            JAISCRIPT_TRY_ASSIGN(auto arg, expression());
            arguments.push_back(std::move(arg));
        } while (match(token_type::comma));
    }

    JAISCRIPT_TRY_ASSIGN(token paren, consume(token_type::right_paren, "Expected ')' after arguments"));

    return std::make_shared<call_expr>(paren.location, callee, std::move(arguments));
}
```

### 3. finish_member_access() (lines 1180-1184)
```cpp
checked_result<expression_ptr> parser::finish_member_access(expression_ptr object, bool is_arrow) {
    JAISCRIPT_TRY_ASSIGN(token name, consume(token_type::identifier, "Expected member name"));
    uint64_t member_id = symbolizer_->intern(name.lexeme);
    return std::make_shared<member_expr>(name.location, object, name.lexeme, member_id, is_arrow);
}
```

### 4. parse_map_literal() (lines 706-772)
**Changes needed:**
- Change return type to `checked_result<expression_ptr>`
- Line 707: `JAISCRIPT_TRY(consume(token_type::left_brace, "Expected '{'"));`
- Line 712: `JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}'"));`
- Line 750, 754: `JAISCRIPT_TRY_ASSIGN(auto elem, expression());`
- Line 753: `JAISCRIPT_TRY(consume(token_type::colon, ...))`
- Lines 761-764: Multiple consume() and expression() calls
- Line 770: `JAISCRIPT_TRY(consume(token_type::right_brace, ...))`

### 5. parse_parameter_list() (lines 1186-1244)
**Changes needed:**
- Change return type to `checked_result<std::vector<parameter>>`
- Line 1202, 1205, 1215, 1220, 1229: All consume() calls need JAISCRIPT_TRY_ASSIGN
- Line 1211: `JAISCRIPT_TRY_ASSIGN(type_info_ptr type, parse_type());`
- Lines 1230, 1232, 1237: Replace error() with return report_error()

### 6. lambda_expression() (lines 1247-1279)
**Changes needed:**
- Change return type to `checked_result<expression_ptr>`
- Lines 1252, 1256, 1259, 1261, 1270, 1275: All consume() calls
- Line 1253: `JAISCRIPT_TRY_ASSIGN(auto [captures, default_mode], parse_capture_list());`
- Line 1260: `JAISCRIPT_TRY_ASSIGN(lambda->parameters, parse_parameter_list());`
- Line 1270: `JAISCRIPT_TRY_ASSIGN(lambda->return_type, parse_type());`
- Line 1276: `JAISCRIPT_TRY_ASSIGN(lambda->body, std::dynamic_pointer_cast<block_stmt>(block_statement()));`

### 7. parse_capture_list() (lines 1281-1352)
**Changes needed:**
- Change return type to `checked_result<std::pair<std::vector<lambda_expr::capture>, lambda_expr::capture_default>>`
- Line 1291: `JAISCRIPT_TRY_ASSIGN(auto name, parse_capture_name());` where parse_capture_name returns checked_result

### 8. All Statement Functions

#### return_statement() (lines 1538-1548)
```cpp
checked_result<statement_ptr> parser::return_statement() {
    token returnToken = previous();

    expression_ptr value = nullptr;
    if (!check(token_type::semicolon)) {
        JAISCRIPT_TRY_ASSIGN(value, expression());
    }

    JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after return value"));

    return std::make_shared<return_stmt>(returnToken.location, value);
}
```

#### try_statement(), if_statement(), while_statement(), for_statement(), switch_statement()
All need similar migrations - convert consume() calls and nested expression()/statement() calls.

#### expression_statement() (lines 1392-1396)
```cpp
checked_result<statement_ptr> parser::expression_statement() {
    JAISCRIPT_TRY_ASSIGN(expression_ptr expr, expression());
    JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after expression"));
    return std::make_shared<expression_stmt>(expr->location, expr);
}
```

#### block_statement() (lines 1379-1390)
```cpp
checked_result<statement_ptr> parser::block_statement() {
    token leftBrace = previous();
    std::vector<declaration_ptr> declarations;

    while (!check(token_type::right_brace) && !is_at_end()) {
        JAISCRIPT_TRY_ASSIGN(auto decl, declaration());
        declarations.push_back(std::move(decl));
    }

    JAISCRIPT_TRY(consume(token_type::right_brace, "Expected '}' after block"));

    return std::make_shared<block_stmt>(leftBrace.location, std::move(declarations));
}
```

#### statement() (lines 1355-1377)
```cpp
checked_result<statement_ptr> parser::statement() {
    if (match(token_type::left_brace)) return block_statement();
    if (match(token_type::if_keyword)) return if_statement();
    if (match(token_type::while_keyword)) return while_statement();
    if (match(token_type::for_keyword)) return for_statement();
    if (match(token_type::return_keyword)) return return_statement();
    if (match(token_type::break_keyword)) return break_statement();
    if (match(token_type::continue_keyword)) return continue_statement();
    if (match(token_type::try_keyword)) return try_statement();
    if (match(token_type::switch_keyword)) return switch_statement();

    // Check for fallthrough keyword
    if (match(token_type::fallthrough_keyword)) {
        if (!in_switch_case_) {
            return report_error("'fallthrough' can only be used inside a switch case", previous());
        }
        auto fallthrough = std::make_shared<fallthrough_stmt>(previous().location);
        JAISCRIPT_TRY(consume(token_type::semicolon, "Expected ';' after 'fallthrough'"));
        return fallthrough;
    }

    return expression_statement();
}
```

### 9. All Declaration Functions

Similar patterns - convert all consume(), expression(), statement(), parse_type() calls.

### 10. parse() Main Loop (lines 18-69)

```cpp
checked_result<std::vector<declaration_ptr>> parser::parse() {
    std::vector<declaration_ptr> declarations;

    while (!is_at_end()) {
        auto decl_result = declaration();

        if (!decl_result) {
            // Error occurred - synchronize and continue
            synchronize();
        } else {
            auto decl = std::move(decl_result.value());
            if (decl) {
                declarations.push_back(std::move(decl));
            }
        }
    }

    // If there were parse errors, return error
    if (!errors_.empty()) {
        return checked_result<std::vector<declaration_ptr>>(
            make_error_code(parse_error_code::invalid_expression),
            errors_[0]
        );
    }

    // Mark last expression as implicit return (existing logic)
    if (!declarations.empty()) {
        if (auto* expr_decl = dynamic_cast<expression_decl*>(declarations.back().get())) {
            expr_decl->implicit_return = true;
        } else if (auto* stmt_decl = dynamic_cast<statement_decl*>(declarations.back().get())) {
            // ... (keep existing logic)
        }
    }

    return declarations;
}
```

## Compilation Test Strategy

1. Migrate remaining expression functions (postfix, helpers, lambda)
2. Test compilation
3. Migrate statement functions
4. Test compilation
5. Migrate declaration functions
6. Test compilation
7. Migrate parse() main loop
8. Final compilation test

## Estimated Remaining Work

- ~1500 lines of code to migrate
- ~100 function calls to convert
- ~50 error() calls to replace with report_error()
- ~30 try-catch blocks to convert to checked_result checking
