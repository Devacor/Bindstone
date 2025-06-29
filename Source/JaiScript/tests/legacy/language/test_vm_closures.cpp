#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jvm/vm_backend.hpp>
#include <jaiscript/jvm/virtual_machine.hpp>
#include <jaiscript/jvm/bytecode.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

using namespace jai;
using namespace jai::jvm;
using namespace jai::test;

// Helper function to compile and execute JaiScript source using VM
script_value compile_and_execute_vm(const std::string& source) {
    lexer lex(source);
    auto tokens = lex.tokenize();
    
    parser parse(tokens);
    auto declarations = parse.parse();
    
    auto backend = create_vm_backend();
    return backend->execute(declarations);
}

JAI_TEST_SUITE(VMClosures)

// Basic Closure Creation
JAI_TEST(vm_basic_closure) {
    // Simple closure capturing a variable
    auto result = compile_and_execute_vm(R"(
        fun make_adder(n) {
            return fun(x) { return x + n; };
        }
        
        var add5 = make_adder(5);
        add5(10);
    )");
    expect_eq(result.as<script_int>(), 15);
    
    // Multiple closures from same factory
    result = compile_and_execute_vm(R"(
        fun make_adder(n) {
            return fun(x) { return x + n; };
        }
        
        var add3 = make_adder(3);
        var add7 = make_adder(7);
        add3(10) + add7(10);
    )");
    expect_eq(result.as<script_int>(), 30); // 13 + 17
}

// Nested Closures
JAI_TEST(vm_nested_closures) {
    // Closure returning closure
    auto result = compile_and_execute_vm(R"(
        fun make_multiplier_adder(m) {
            return fun(a) {
                return fun(x) {
                    return x * m + a;
                };
            };
        }
        
        var times2plus3 = make_multiplier_adder(2)(3);
        times2plus3(5);
    )");
    expect_eq(result.as<script_int>(), 13); // 5 * 2 + 3
    
    // Deep nesting
    result = compile_and_execute_vm(R"(
        fun level1(a) {
            return fun level2(b) {
                return fun level3(c) {
                    return fun level4(d) {
                        return a + b + c + d;
                    };
                };
            };
        }
        
        var f = level1(1)(2)(3)(4);
        f;
    )");
    expect_eq(result.as<script_int>(), 10); // 1 + 2 + 3 + 4
}

// Mutable Captured Variables
JAI_TEST(vm_mutable_upvalues) {
    // Modifying captured variables
    auto result = compile_and_execute_vm(R"(
        fun make_counter() {
            var count = 0;
            return fun() {
                count = count + 1;
                return count;
            };
        }
        
        var counter = make_counter();
        counter() + counter() + counter();
    )");
    expect_eq(result.as<script_int>(), 6); // 1 + 2 + 3
    
    // Multiple closures sharing same upvalue
    result = compile_and_execute_vm(R"(
        fun make_counter_pair() {
            var count = 0;
            var inc = fun() { count = count + 1; return count; };
            var dec = fun() { count = count - 1; return count; };
            return [inc, dec];
        }
        
        var pair = make_counter_pair();
        var inc = pair[0];
        var dec = pair[1];
        inc(); // count = 1
        inc(); // count = 2
        dec(); // count = 1
        inc(); // count = 2
    )");
    expect_eq(result.as<script_int>(), 2);
}

// Upvalue Lifetime
JAI_TEST(vm_upvalue_lifetime) {
    // Upvalues outlive their creating scope
    auto result = compile_and_execute_vm(R"(
        var getter;
        var setter;
        
        {
            var local = 42;
            getter = fun() { return local; };
            setter = fun(v) { local = v; };
        }
        
        getter();
    )");
    expect_eq(result.as<script_int>(), 42);
    
    // Modifying upvalue after scope exit
    result = compile_and_execute_vm(R"(
        var getter;
        var setter;
        
        {
            var local = 10;
            getter = fun() { return local; };
            setter = fun(v) { local = v; };
        }
        
        setter(20);
        getter();
    )");
    expect_eq(result.as<script_int>(), 20);
}

// Complex Upvalue Scenarios
JAI_TEST(vm_complex_upvalues) {
    // Capturing multiple variables
    auto result = compile_and_execute_vm(R"(
        fun make_calculator(a, b, c) {
            return fun(op) {
                if (op == "+") return a + b + c;
                if (op == "*") return a * b * c;
                return 0;
            };
        }
        
        var calc = make_calculator(2, 3, 4);
        calc("+") + calc("*");
    )");
    expect_eq(result.as<script_int>(), 9 + 24); // (2+3+4) + (2*3*4)
    
    // Capturing from different scopes
    result = compile_and_execute_vm(R"(
        var global = 100;
        
        fun outer(param) {
            var outer_local = 20;
            
            fun middle() {
                var middle_local = 3;
                
                return fun() {
                    return global + param + outer_local + middle_local;
                };
            }
            
            return middle();
        }
        
        var f = outer(10);
        f();
    )");
    expect_eq(result.as<script_int>(), 133); // 100 + 10 + 20 + 3
}

// Lambda Capture Syntax
JAI_TEST(vm_lambda_captures) {
    // Value capture
    auto result = compile_and_execute_vm(R"(
        var x = 10;
        var f = fun[x]() { return x; };
        x = 20;
        f();
    )");
    expect_eq(result.as<script_int>(), 10); // Captured by value
    
    // Reference capture
    result = compile_and_execute_vm(R"(
        var x = 10;
        var f = fun[&x]() { return x; };
        x = 20;
        f();
    )");
    expect_eq(result.as<script_int>(), 20); // Captured by reference
    
    // Mixed captures
    result = compile_and_execute_vm(R"(
        var a = 1;
        var b = 2;
        var c = 3;
        var f = fun[a, &b, c]() {
            b = b * 10;
            return a + b + c;
        };
        a = 10;
        c = 30;
        f() + b;
    )");
    expect_eq(result.as<script_int>(), 1 + 20 + 3 + 20); // a=1(val), b=20(ref), c=3(val), plus b=20
}

// Recursive Closures
JAI_TEST(vm_recursive_closures) {
    // Factorial using closure
    auto result = compile_and_execute_vm(R"(
        var factorial;
        factorial = fun(n) {
            if (n <= 1) return 1;
            return n * factorial(n - 1);
        };
        factorial(5);
    )");
    expect_eq(result.as<script_int>(), 120);
    
    // Fibonacci with memoization
    result = compile_and_execute_vm(R"(
        fun make_memoized_fib() {
            var cache = {};
            var fib;
            fib = fun(n) {
                if (n <= 1) return n;
                if (cache[n]) return cache[n];
                cache[n] = fib(n-1) + fib(n-2);
                return cache[n];
            };
            return fib;
        }
        
        var fib = make_memoized_fib();
        fib(10);
    )");
    expect_eq(result.as<script_int>(), 55);
}

// Closure Performance Patterns
JAI_TEST(vm_closure_patterns) {
    // Immediately invoked function expression (IIFE)
    auto result = compile_and_execute_vm(R"(
        var result = (fun(x) { return x * x; })(7);
        result;
    )");
    expect_eq(result.as<script_int>(), 49);
    
    // Closure-based private variables
    result = compile_and_execute_vm(R"(
        fun make_bank_account(initial) {
            var balance = initial;
            return {
                "deposit": fun(amount) { balance = balance + amount; },
                "withdraw": fun(amount) { balance = balance - amount; },
                "get_balance": fun() { return balance; }
            };
        }
        
        var account = make_bank_account(100);
        account["deposit"](50);
        account["withdraw"](30);
        account["get_balance"]();
    )");
    expect_eq(result.as<script_int>(), 120);
}

// Edge Cases
JAI_TEST(vm_closure_edge_cases) {
    // Empty closure
    auto result = compile_and_execute_vm(R"(
        fun make_constant() {
            return fun() { return 42; };
        }
        var f = make_constant();
        f();
    )");
    expect_eq(result.as<script_int>(), 42);
    
    // Closure capturing nothing but accessing globals
    result = compile_and_execute_vm(R"(
        var global = 100;
        fun make_global_accessor() {
            return fun() { return global; };
        }
        var f = make_global_accessor();
        global = 200;
        f();
    )");
    expect_eq(result.as<script_int>(), 200);
    
    // Very long upvalue chain
    result = compile_and_execute_vm(R"(
        fun chain(n) {
            if (n <= 0) return fun() { return 0; };
            var prev = chain(n - 1);
            return fun() { return n + prev(); };
        }
        var f = chain(10);
        f();
    )");
    expect_eq(result.as<script_int>(), 55); // Sum of 1..10
}

// Closure Memory Management
JAI_TEST(vm_closure_memory) {
    // Many closures
    auto result = compile_and_execute_vm(R"(
        var closures = [];
        for (var i = 0; i < 100; ++i) {
            closures.push(fun[i]() { return i; });
        }
        closures[50]();
    )");
    expect_eq(result.as<script_int>(), 50);
    
    // Circular reference through closures
    result = compile_and_execute_vm(R"(
        fun make_circular() {
            var obj = {};
            obj["method"] = fun() { return obj; };
            return obj;
        }
        var circular = make_circular();
        circular["method"]() == circular;
    )");
    expect_true(result.as<script_bool>());
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()