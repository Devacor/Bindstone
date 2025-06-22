# JaiScript Performance Improvement Plan

## Executive Summary

Based on performance benchmarks against ChaiScript, JaiScript shows:
- **22x faster startup time** (69 μs vs 1510 μs)
- **2-5x slower execution** of pre-parsed code
- **Fast parsing** with AST caching

This document outlines actionable optimizations to achieve execution performance that exceeds ChaiScript while maintaining our startup advantage.

## Performance Goals

1. **Match or exceed ChaiScript's execution speed** for common operations
2. **Maintain fast startup time** (< 100 μs)
3. **Keep implementation simple** - avoid over-engineering
4. **Preserve ease of embedding** and API compatibility

## High-Impact, Low-Effort Optimizations

### Phase 1: Quick Wins (1-2 days effort)

#### 1.1 Eliminate Stack-Based Value Operations
**Current Issue**: Every expression evaluation uses `push/pop` on `std::stack<Value>`
```cpp
// Current slow approach
void pushValue(Value v) { stack_.push(std::move(v)); }
Value popValue() { 
    Value v = stack_.top(); 
    stack_.pop(); 
    return v; 
}
```

**Solution**: Use pre-allocated vector with index
```cpp
// Fast approach
class ValueStack {
    std::vector<Value> values_;
    size_t top_ = 0;
public:
    ValueStack() { values_.reserve(256); }
    void push(Value&& v) { 
        if (top_ >= values_.size()) values_.resize(values_.size() * 2);
        values_[top_++] = std::move(v); 
    }
    Value& top() { return values_[top_-1]; }
    Value pop() { return std::move(values_[--top_]); }
};
```
**Expected Impact**: 15-20% execution speedup

#### 1.2 Optimize Variable Lookup with String Interning
**Current Issue**: String hashing/comparison on every variable access
```cpp
// Current slow approach
std::unordered_map<std::string, Value> variables_;
```

**Solution**: Intern strings and use integer handles
```cpp
class StringInterner {
    std::unordered_map<std::string_view, uint32_t> intern_map_;
    std::vector<std::string> strings_;
public:
    uint32_t intern(std::string_view str) {
        if (auto it = intern_map_.find(str); it != intern_map_.end()) {
            return it->second;
        }
        uint32_t id = strings_.size();
        strings_.emplace_back(str);
        intern_map_[strings_.back()] = id;
        return id;
    }
};

// Then use: std::unordered_map<uint32_t, Value> variables_;
```
**Expected Impact**: 20-30% speedup for variable-heavy code

#### 1.3 Inline Critical Functions
**Current Issue**: Function call overhead for frequently used operations

**Solution**: Add inline hints and move to headers
```cpp
// In value.hpp
inline bool Value::isTruthy() const {
    return std::visit([](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) return v;
        if constexpr (std::is_same_v<T, nullptr_t>) return false;
        if constexpr (std::is_arithmetic_v<T>) return v != 0;
        if constexpr (std::is_same_v<T, std::string>) return !v.empty();
        return true;
    }, storage_);
}
```
**Expected Impact**: 5-10% overall speedup

### Phase 2: Medium Effort Optimizations (3-5 days)

#### 2.1 Replace Visitor Pattern with Type-Tagged Union
**Current Issue**: Virtual function overhead for every AST node visit

**Solution**: Use enum dispatch
```cpp
enum class ExprType : uint8_t {
    Integer, Float, String, Variable, Binary, Call, Lambda
};

struct Expr {
    ExprType type;
    union {
        Int int_value;
        Float float_value;
        struct { uint32_t name_id; } var;
        struct { Expr* left; Expr* right; TokenType op; } binary;
        // ... other variants
    };
};

Value Interpreter::evaluate(Expr* expr) {
    switch (expr->type) {
        case ExprType::Integer: return Value(expr->int_value);
        case ExprType::Variable: return env_->get(expr->var.name_id);
        case ExprType::Binary: return evaluateBinary(expr);
        // ...
    }
}
```
**Expected Impact**: 25-35% execution speedup

#### 2.2 Optimize Function Calls with Pre-allocated Frames
**Current Issue**: Creating new Environment object for each function call

**Solution**: Use stack-allocated activation records
```cpp
struct CallFrame {
    uint32_t* var_ids;      // Variable name IDs
    Value* locals;          // Local values
    size_t num_locals;
    CallFrame* parent;
};

class CallStack {
    std::vector<uint8_t> memory_;  // Pre-allocated memory
    size_t offset_ = 0;
    CallFrame* current_ = nullptr;
public:
    CallFrame* pushFrame(size_t num_locals) {
        // Allocate from pre-allocated memory
        CallFrame* frame = allocate<CallFrame>();
        frame->locals = allocate<Value>(num_locals);
        frame->parent = current_;
        current_ = frame;
        return frame;
    }
};
```
**Expected Impact**: 20-30% speedup for function-heavy code

#### 2.3 Implement Fast-Path for Common Operations
**Current Issue**: Generic code paths for simple operations

**Solution**: Special-case common patterns
```cpp
Value Interpreter::visitBinaryExpr(BinaryExpr* expr) {
    // Fast path for integer arithmetic
    if (expr->left->type == ExprType::Integer && 
        expr->right->type == ExprType::Integer) {
        Int left = static_cast<IntegerExpr*>(expr->left)->value;
        Int right = static_cast<IntegerExpr*>(expr->right)->value;
        switch (expr->op) {
            case TokenType::Plus: return Value(left + right);
            case TokenType::Minus: return Value(left - right);
            // ...
        }
    }
    // Fall back to generic path
    return evaluateGenericBinary(expr);
}
```
**Expected Impact**: 15-25% speedup for arithmetic-heavy code

### Phase 3: Advanced Optimizations (1-2 weeks)

#### 3.1 Simple Bytecode Compiler
**Rationale**: Eliminate AST traversal overhead entirely

**Basic Design**:
```cpp
enum class OpCode : uint8_t {
    LOAD_CONST, LOAD_VAR, STORE_VAR,
    ADD, SUB, MUL, DIV,
    CALL, RETURN, JUMP, JUMP_IF_FALSE
};

struct ByteCode {
    std::vector<uint8_t> code;
    std::vector<Value> constants;
    
    void emit(OpCode op) { code.push_back(static_cast<uint8_t>(op)); }
    void emit16(uint16_t val) {
        code.push_back(val & 0xFF);
        code.push_back(val >> 8);
    }
};

// Simple stack-based VM
Value VM::execute(ByteCode& bc) {
    size_t ip = 0;
    while (ip < bc.code.size()) {
        switch (static_cast<OpCode>(bc.code[ip++])) {
            case OpCode::LOAD_CONST: {
                uint16_t idx = read16(bc.code, ip);
                push(bc.constants[idx]);
                break;
            }
            case OpCode::ADD: {
                Value b = pop();
                Value a = pop();
                push(a + b);  // Assuming operator+ defined
                break;
            }
            // ...
        }
    }
}
```
**Expected Impact**: 3-5x execution speedup

#### 3.2 Memory Pool Allocator
**Solution**: Eliminate allocation overhead
```cpp
template<size_t BlockSize = 4096>
class PoolAllocator {
    struct Block {
        uint8_t data[BlockSize];
        Block* next;
    };
    Block* current_;
    size_t offset_;
public:
    template<typename T>
    T* allocate() {
        size_t size = sizeof(T);
        if (offset_ + size > BlockSize) {
            // Allocate new block
        }
        T* ptr = reinterpret_cast<T*>(&current_->data[offset_]);
        offset_ += size;
        return ptr;
    }
};
```
**Expected Impact**: 10-20% overall speedup, reduced memory fragmentation

## Implementation Strategy

### Phase: Quick Wins
- Development milestone-2: Implement ValueStack, string interning, and inlining
- Development milestone: Benchmark and validate improvements
- Development milestone-5: Replace visitor pattern with enum dispatch

### Phase: Core Optimizations  
- Development milestone-2: Implement pre-allocated call frames
- Development milestone: Add fast paths for common operations
- Development milestone-5: Begin bytecode compiler design

### Phase: Advanced Features
- Development milestone-3: Complete basic bytecode compiler
- Development milestone-5: Implement memory pool, final optimizations

## Expected Final Performance

With all optimizations implemented:
- **Arithmetic operations**: 2-3x faster than ChaiScript
- **Loop execution**: 1.5-2x faster than ChaiScript  
- **Function calls**: On par with ChaiScript
- **Variable access**: 2x faster than ChaiScript
- **Startup time**: Still 20x faster than ChaiScript

## Risks and Mitigations

1. **API Breaking Changes**: Keep external API stable, changes internal only
2. **Complexity Growth**: Each optimization in separate commit, measure impact
3. **Memory Usage**: Pool allocators may increase memory, but improve locality
4. **Debugging Difficulty**: Maintain debug build with optimizations disabled

## Success Metrics

1. **Performance**: Beat ChaiScript on all benchmark categories
2. **Simplicity**: Core interpreter remains under 2000 LOC
3. **Compatibility**: All existing tests pass
4. **Memory**: Peak memory usage increases < 20%

## Conclusion

By focusing on high-impact, low-effort optimizations first, JaiScript can achieve superior execution performance while maintaining its key advantages of fast startup and simple implementation. The phased approach allows for incremental improvements with measurable results at each stage.