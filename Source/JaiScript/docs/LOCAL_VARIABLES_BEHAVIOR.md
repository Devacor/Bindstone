# Local Variables Behavior Clarification

## Root Level Script Behavior - NO CHANGE

**Current behavior (preserved):**
```cpp
engine.execute("var x = 10;");  // Creates global variable x
engine.execute("print(x);");     // Prints 10 - x persists!
```

**With local variables:**
```cpp
// Local variables are ADDITIONAL, not replacing globals
LocalVariables locals = {{"y", Value(20)}};
engine.execute("var x = 10; print(y);", locals);  
// - Creates global x (persists)
// - Uses local y (doesn't persist)

engine.execute("print(x);");  // Still prints 10 - x is global
engine.execute("print(y);");  // ERROR - y was local to previous execution
```

## How It Works

The local variables create an **additional scope layer**:

```
[Local Variables Scope]  <- Temporary, per-execution
         ↓
[Global Variables Scope] <- Persistent, shared
```

When a script runs:
1. Local variables (if provided) are visible
2. Global variables are still visible
3. New variables declared at root level still become globals
4. Local variables shadow globals with same name (only during that execution)

## Examples

### Example 1: No locals = current behavior
```cpp
engine.execute("var counter = 0;");
engine.execute("counter = counter + 1;");
engine.execute("print(counter);");  // Prints 1
```

### Example 2: With locals - globals still work
```cpp
engine.execute("var counter = 0;");  // Global

LocalVariables locals = {{"increment", Value(5)}};
engine.execute("counter = counter + increment;", locals);  
// Uses local 'increment', modifies global 'counter'

engine.execute("print(counter);");  // Prints 5
engine.execute("print(increment);");  // ERROR - increment was local
```

### Example 3: Shadowing
```cpp
engine.execute("var x = 'global';");

LocalVariables locals = {{"x", Value("local")}};
engine.execute("print(x);", locals);  // Prints "local" (shadowed)

engine.execute("print(x);");  // Prints "global" (no shadowing)
```

### Example 4: Functions see both
```cpp
engine.execute(R"(
    function greet(name) -> auto {
        return prefix + " " + name;  // 'prefix' from locals
    }
)");

LocalVariables locals = {{"prefix", Value("Hello")}};
auto result = engine.execute("greet('World')", locals);  // "Hello World"

// Without locals, function would error (no prefix defined)
```

## Summary

- **No breaking changes** to existing scripts
- Root level declarations still create/modify globals
- Local variables are just an **optional overlay** for that execution
- Scripts can use both locals and globals together
- Perfect for passing parameters without polluting global namespace