# JaiScript Exception Handling Design

> Exceptions behave identically on both backends; the VM decides catchability by `records_base`.
>
> - **The catch variable is the thrown value; a run-time error is an error value.** A `throw value` binds the value
>   itself (`throw 42`: `e` is the int 42; an instance, an array or a map is the thrown one, never a copy). A
>   run-time error binds its message as a string - every string operation reads it unchanged (printing,
>   `to_string(e)`, `e + "..."`, `e == "text"`, a `string` slot, the string methods) - whose `type_of` is `"error"`
>   in a dynamic variable and that answers `e.kind` (`error_kind_of` over the failure's code;
>   `script_exception::kind` carries it through a rethrow). One engine call builds the variable at every catch on
>   both backends (`engine::make_caught_error`). A value with a node (a string, an instance, an array, a map) keys an
>   engine table entry holding the place, the stack (the same capture the uncaught trace uses,
>   `capture_stack_trace`, with columns) and the value, and answers `e.message`, `e.file`, `e.line`, `e.column`,
>   `e.stack` and `e.value`: a member read the receiver's own members did not answer consults the table before the
>   no-member error (`engine::caught_error_member`). A string's entry is dropped once the table holds the only
>   reference (a copy shares the node and answers the same; a string built from it is text only); an instance's,
>   array's or map's goes when its handlers end. A thrown scalar has no node and carries no place; a member read
>   through a handler's variable (`member_expr::on_caught`, the escape-mark pass) answers `.value` and `.message` for it.
>   A run-time error's dynamic type is one of the engine's per-kind error types (`engine::caught_error_tag`), so a
>   parallel body, where no entry is made, still tells an error and its kind.
>   `script_exception::thrown` tells a script throw (a rethrow keeps it) from a run-time error.
> - **Typed clauses are the parser's.** `catch (Type e)` clauses lower to one handler over a hidden variable: each
>   clause is `if (catch_takes#(v, "Type"))` (`engine::catch_clause_takes`: a class through `derives_from`, a
>   builtin kind by name, `error` for a run-time error) whose block declares `Type e = catch_value#(v)` (a call
>   result, so the declaration takes the value without a copy), and the chain ends in `throw;` when no clause
>   catches everything. Neither backend knows a clause.
> - **Terminal-error tier.** Execution-budget exhaustion ALWAYS latches a terminal error (never
>   catchable); a `memory_cap` denial is catchable on its FIRST raise per execute, terminal on the
>   second. A latched terminal error skips every script `catch` to the host boundary
>   (`detail/execution_limits.hpp`, the catch-skip in the interpreter's try handling).
> - **Propagation: two channels.** A script `throw` sets `is_unwinding_` and `current_exception_`; a run-time
>   error returns a failed `checked_result`; `try` catches either (interpreter `visit_try_stmt`).
> - **The catch variable is a block-scoped local of the catch body** (C++ `catch (e)`
>   semantics). It takes the parser-assigned slot `try_stmt::catch_slot_index` (with its name in
>   `catch_var_id`, the range-for variable's pattern), or at top level a pushed scope env; the handler
>   entry binds the caught value there (interpreter `visit_try_stmt`, vm `unwind_to_handler` through
>   `op_try_push`'s `c` operand), and `op_catch_end` / the statement's exit pops a top-level scope env.
>   It resolves locals -> members -> globals
>   like any local, shadows an outer name for the block only, is invisible to callees and
>   after the block (`Undefined variable 'e'`), is captured by lambdas like any local, and
>   survives suspension like any local (frame slots ride the fiber / saved frames).
>   `current_catch_var_id_` marks only "inside a catch" for rethrow (a nested `try` inside a catch keeps
>   the caught exception live for `throw;`).
> - **Coroutines park the exception trio.** `current_exception_` / `active_exception_value_`
>   / the in-catch marker are fiber state (vm `vm_coroutine_state`, interpreter
>   `interpreter_coroutine_state`): each `next()` installs the fiber's parked trio (a fresh
>   coroutine starts with no active exception - a bare `throw;` outside any catch in the
>   body raises `Bare throw with no active exception thrown.` even when the resumer sits inside a catch), a
>   yield parks it, and the resumer's trio comes back on every exit except an escaping
>   throw, whose exception pair stays live for the resumer's handler. So a resumer's own
>   `try/catch` between resumes never touches the fiber's `e`, and a resumer's `catch (e)`
>   driving a coroutine from a range-for keeps reading its own exception throughout.
> - **A host callable's exception is the runtime error of the site that called it.** A method, operator,
>   property getter or setter, or static the binder registered (`class_definition::add_method`,
>   `add_static_method`) returns its C++ exception as the catchable runtime error `cpp_exception` with the
>   exception's text, so a store `obj.v = x`, a read `obj.v`, an operator `a * b` and a compound `obj.v += x` raise it
>   where they stand, at top level or in a function, like a bound free function (`function_binder`). An archive skips a
>   host property that refuses a read or a store, and passes a host exception on (`to_json`, `from_json`, the
>   binary archive).
> - **A native exception is the running function's exception, on both backends.** A C++ exception raised while a
>   statement runs (a host call, a store through a property reference whose setter refuses, a read through a removed
>   element's reference) becomes a script exception where it was raised: that function's own `try` catches it, and
>   without one it unwinds to the callers like a script `throw`, each frame popped on the way (C++). The VM's `run`
>   converts in the running frame (`convert_cpp_exception_in_frame`, then `handle_throw_unwind`); the interpreter's
>   `visit_try_stmt` takes it from its block, `call_function` pops its frame on the way out, and `visit_call` converts
>   it at the call site.
> - **A bare `throw;` with no active exception** raises the catchable runtime error
>   `Bare throw with no active exception thrown.` (`invalid_operation`) where it stands.
> - Not built: `finally`.

The sketch below predates the built design; the paragraphs above are authoritative.

## Overview
Simple, effective exception handling that integrates cleanly with C++.

## Exception Type
```cpp
namespace jai {
    class script_exception : public std::runtime_error {
    public:
        script_exception(const std::string& message, const source_location& loc = {})
            : std::runtime_error(message), location_(loc) {}
        
        const source_location& location() const { return location_; }
        
    private:
        source_location location_;
    };
}
```

## Script Syntax

### Basic Try-Catch
```jaiscript
try {
    risky_operation();
    throw "Something went wrong!";
} catch {
    // Handle any exception
    print("An error occurred");
}
```

### Catch with Message
```jaiscript
try {
    throw "File not found: " + filename;
} catch (e) {
    print("Error: " + e);  // e is the exception message as a string
}
```

### Nested Try-Catch
```jaiscript
try {
    try {
        dangerous_stuff();
    } catch (inner) {
        print("Inner catch: " + inner);
        throw "Re-throwing: " + inner;  // Re-throw with new message
    }
} catch (outer) {
    print("Outer catch: " + outer);
}
```

### Finally Block (Optional for V2)
```jaiscript
try {
    open_file();
} catch (e) {
    print("Error: " + e);
} finally {
    close_file();  // Always executes
}
```

## Implementation Strategy

### 1. AST Nodes
```cpp
// Throw expression
class throw_expr : public expression {
public:
    expression_ptr value;  // Optional - throw; re-throws current exception
    
    throw_expr(expression_ptr val = nullptr) : value(val) {}
    void accept(ast_visitor* visitor) override { visitor->visit_throw_expr(this); }
};

// Try-catch statement
class try_stmt : public statement {
public:
    statement_ptr try_block;
    std::string catch_var;  // Optional - variable name for exception message
    statement_ptr catch_block;
    statement_ptr finally_block;  // Optional for future
    
    void accept(ast_visitor* visitor) override { visitor->visit_try_stmt(this); }
};
```

### 2. Interpreter State
```cpp
class interpreter {
    // ... existing members ...
    
    // Exception handling state
    std::optional<script_exception> current_exception_;
    bool is_unwinding_ = false;  // True when propagating an exception
};
```

### 3. Throw Implementation
```cpp
void interpreter::visit_throw_expr(throw_expr* expr) {
    if (expr->value) {
        expr->value->accept(this);
        script_value val = pop_value();
        
        // Convert to string for exception message
        std::string message = val.to_string();
        current_exception_ = script_exception(message, expr->location);
    } else {
        // Re-throw current exception
        if (!current_exception_) {
            return runtime_error(invalid_operation, "Bare throw with no active exception thrown.");
        }
    }
    
    is_unwinding_ = true;
}
```

### 4. Try-Catch Implementation
```cpp
void interpreter::visit_try_stmt(try_stmt* stmt) {
    // Save exception state
    auto saved_exception = current_exception_;
    auto saved_unwinding = is_unwinding_;
    
    // Reset state for try block
    current_exception_.reset();
    is_unwinding_ = false;
    
    // Execute try block
    stmt->try_block->accept(this);
    
    // Check if exception was thrown
    if (is_unwinding_ && current_exception_) {
        // Reset unwinding flag
        is_unwinding_ = false;
        
        // If catch variable specified, bind exception message
        if (!stmt->catch_var.empty()) {
            push_block_scope();
            add_variable(stmt->catch_var, 
                script_value(current_exception_->what()));
        }
        
        // Execute catch block
        stmt->catch_block->accept(this);
        
        // Clean up catch scope
        if (!stmt->catch_var.empty()) {
            pop_scope();
        }
        
        // Clear exception (it was handled)
        current_exception_.reset();
    }
    
    // Execute finally block if present (future feature)
    if (stmt->finally_block) {
        stmt->finally_block->accept(this);
    }
    
    // If still unwinding after catch, restore state
    if (is_unwinding_) {
        current_exception_ = saved_exception;
    }
}
```

### 5. Statement Execution Changes
Every statement visitor needs to check for unwinding:
```cpp
void interpreter::visit_expression_stmt(expression_stmt* stmt) {
    stmt->expression->accept(this);
    
    // Early exit if exception is propagating
    if (is_unwinding_) return;
    
    // Pop the expression value (statements don't leave values on stack)
    pop_value();
}
```

### 6. C++ Integration
```cpp
// In engine::execute()
try {
    auto result = interpreter_->execute(ast);
    
    // Check for unhandled script exception
    if (interpreter_->is_unwinding()) {
        throw interpreter_->get_current_exception();
    }
    
    return result;
} catch (const script_exception& e) {
    // Script exception bubbles up to C++
    throw;
} catch (const std::exception& e) {
    // Wrap C++ exceptions as script exceptions
    throw script_exception(e.what());
}
```

## Usage Examples

### Script-side
```jaiscript
// Simple error handling
try {
    auto result = divide(10, 0);
} catch {
    print("Division failed!");
}

// With message
try {
    auto data = load_json(filename);
} catch (error) {
    print("Failed to load JSON: " + error);
}

// Throwing custom messages
function validate_age(age) -> auto {
    if (age < 0) {
        throw "Age cannot be negative: " + age;
    }
    if (age > 150) {
        throw "Age seems unrealistic: " + age;
    }
    return age;
}
```

### C++ Integration
```cpp
engine eng;

// Uncaught exceptions bubble up
try {
    eng.execute("throw \"Script error!\";");
} catch (const jai::script_exception& e) {
    std::cerr << "Script error at " << e.location() << ": " << e.what() << "\n";
}

// C++ functions can throw script_exception
eng.add_function("risky_operation", []() {
    if (rand() % 2 == 0) {
        throw jai::script_exception("Random failure!");
    }
    return 42;
});
```

## Benefits
1. Simple to use - just catch strings
2. Clean C++ integration 
3. Proper stack unwinding
4. Location tracking for debugging
5. Minimal performance overhead when not throwing
6. Future-proof for finally blocks

## Implementation Order
1. Add `script_exception` class
2. Add AST nodes for throw_expr and try_stmt
3. Update parser to recognize try/catch/throw keywords
4. Implement interpreter changes
5. Add tests
6. Document in user guide