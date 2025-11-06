# Persistent Interpreter Design

## Key Benefits
1. **No repeated setup costs** - Initialize once, use many times
2. **Natural local variables support** - Just push/pop scopes
3. **Simpler code** - No complex initialization on every execute
4. **Better performance** - Avoid recreating dispatchers, copying globals, etc.

## Implementation Plan

### 1. Add Reset/Prepare Methods to Interpreter

```cpp
class Interpreter {
public:
    // Prepare for a new execution - clears transient state but keeps globals
    void prepareForExecution() {
        // Clear execution state
        valueStack_.clear();
        returnValue_ = Value();
        hasReturnValue_ = false;
        
        // Reset to global scope (keep globals, clear any leftover locals)
        while (environment_->getParent()) {
            environment_ = environment_->getParent();
        }
    }
    
    // Push a new scope for local variables
    std::shared_ptr<Environment> pushScope() {
        environment_ = std::make_shared<Environment>(environment_, string_symbolizer__);
        return environment_;
    }
    
    // Pop back to parent scope
    void popScope() {
        if (environment_->getParent()) {
            environment_ = environment_->getParent();
        }
    }
    
    // Define a variable in current scope
    void defineVariable(const std::string& name, const Value& value) {
        environment_->define(name, value);
    }
};
```

### 2. Update Engine Implementation

```cpp
class Engine::Implementation {
    // Single persistent interpreter
    std::unique_ptr<Interpreter> interpreter;
    
    Implementation() {
        interpreter = std::make_unique<Interpreter>(&string_symbolizer_);
        setupInterpreter();
    }
    
    void setupInterpreter() {
        // One-time setup of globals and functions
        std::unordered_map<std::string, Value> allGlobals = cppGlobals;
        allGlobals.insert(scriptGlobals.begin(), scriptGlobals.end());
        
        // Add dispatchers for overloaded functions...
        // Set up subscript resolver...
        
        interpreter->addGlobals(allGlobals);
    }
};
```

### 3. Clean Execute Implementation

```cpp
Value Engine::execute(const std::string& scriptContent, const LocalVariables& localVars) {
    try {
        // Prepare interpreter for new execution
        impl->interpreter->prepareForExecution();
        
        // Push scope for local variables if any
        bool hasLocals = !localVars.empty();
        if (hasLocals) {
            impl->interpreter->pushScope();
            for (const auto& [name, value] : localVars) {
                impl->interpreter->defineVariable(name, value);
            }
        }
        
        // Parse and execute
        Lexer lexer(scriptContent);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto declarations = parser.parse();
        
        Value result = impl->interpreter->execute(declarations);
        
        // Pop local scope if we pushed one
        if (hasLocals) {
            impl->interpreter->popScope();
        }
        
        // Update script globals (excluding locals)
        auto variables = impl->interpreter->getAllVariables();
        for (const auto& [name, value] : variables) {
            if (impl->cppGlobals.find(name) == impl->cppGlobals.end() && 
                localVars.find(name) == localVars.end()) {
                impl->scriptGlobals[name] = value;
            }
        }
        
        return result;
        
    } catch (const std::exception& e) {
        // Ensure interpreter is in good state even after error
        impl->interpreter->prepareForExecution();
        throw RuntimeError("Execution failed: " + std::string(e.what()));
    }
}
```

## Error Recovery

The key to preventing corruption is the try-catch with cleanup:
- Always call `prepareForExecution()` in the catch block
- This ensures the interpreter is ready for the next execution
- Stack unwinding naturally handles scope cleanup

## When to Recreate Interpreter

Only recreate when globals actually change:
```cpp
void Engine::addGlobal(const std::string& name, Value value) {
    impl->cppGlobals[name] = std::move(value);
    impl->interpreterNeedsRebuild = true;
}

Value Engine::execute(...) {
    if (impl->interpreterNeedsRebuild) {
        impl->setupInterpreter();
        impl->interpreterNeedsRebuild = false;
    }
    // ... rest of execution
}
```

This gives us:
- Fast execution for repeated scripts
- Clean local variable support
- Robust error handling
- Minimal code complexity