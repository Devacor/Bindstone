# JaiScript Exception Handling Design

> **DONE as of 2026-07 — shipped in BOTH backends** (interpreter and bytecode VM, full try/catch/
> throw parity; see `records_base` catchability in `vm_backend` and commit 94311244). The design
> below is the original sketch; where it disagrees with the addendum, the addendum wins.
>
> **Since this doc:**
> - **Typed throw values.** `throw 42` delivers the int 42 to `catch (e)` — the thrown VALUE is
>   preserved with its type, not stringified (the "e is a string" claims below are stale).
> - **Terminal-error tier.** Execution-budget exhaustion ALWAYS latches a terminal error (never
>   catchable); a `memory_cap` denial is catchable on its FIRST raise per execute, terminal on the
>   second. A latched terminal error skips every script `catch` to the host boundary
>   (`detail/execution_limits.hpp`, the catch-skip in the interpreter's try handling).
> - **Propagation mechanism.** The `is_unwinding_` flag sketch was superseded by
>   `checked_result<void>` propagation through the visitors.
> - `finally` remains unimplemented (still-future, as designed).

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
            throw script_exception("No exception to re-throw", expr->location);
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