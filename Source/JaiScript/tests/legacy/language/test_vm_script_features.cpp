#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jvm/vm_backend.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

using namespace jai;
using namespace jai::jvm;
using namespace jai::test;

// Helper function to compile and execute JaiScript source using VM
script_value compile_and_execute_vm(const std::string& source) {
    // Parse the source into AST
    lexer lex(source);
    auto tokens = lex.tokenize();
    
    parser parse(tokens);
    auto declarations = parse.parse();
    
    // Create VM backend and execute
    auto backend = create_vm_backend();
    return backend->execute(declarations);
}

JAI_TEST_SUITE(VMScriptFeatures)

JAI_TEST(vm_simple_arithmetic) {
    // Test basic arithmetic compilation and execution
    auto result = compile_and_execute_vm("2 + 3 * 4;");
    expect_eq(result.as<script_int>(), 14);
    
    result = compile_and_execute_vm("(2 + 3) * 4;");
    expect_eq(result.as<script_int>(), 20);
}

JAI_TEST(vm_variables_and_scoping) {
    // Test variable declaration and scoping
    auto result = compile_and_execute_vm(R"(
        var x = 10;
        var y = 20;
        x + y;
    )");
    expect_eq(result.as<script_int>(), 30);
    
    // Test block scoping
    result = compile_and_execute_vm(R"(
        var x = 5;
        {
            var y = 10;
            x = x + y;
        }
        x;
    )");
    expect_eq(result.as<script_int>(), 15);
}

JAI_TEST(vm_functions) {
    // Test function declaration and calls
    auto result = compile_and_execute_vm(R"(
        fun add(a, b) {
            return a + b;
        }
        
        add(5, 7);
    )");
    expect_eq(result.as<script_int>(), 12);
    
    // Test recursive functions
    result = compile_and_execute_vm(R"(
        fun factorial(n) {
            if (n <= 1) {
                return 1;
            } else {
                return n * factorial(n - 1);
            }
        }
        
        factorial(5);
    )");
    expect_eq(result.as<script_int>(), 120);
}

JAI_TEST(vm_control_flow) {
    // Test if/else statements
    auto result = compile_and_execute_vm(R"(
        var x = 10;
        if (x > 5) {
            x = x * 2;
        } else {
            x = x / 2;
        }
        x;
    )");
    expect_eq(result.as<script_int>(), 20);
    
    // Test loops
    result = compile_and_execute_vm(R"(
        var sum = 0;
        for (var i = 1; i <= 5; ++i) {
            sum = sum + i;
        }
        sum;
    )");
    expect_eq(result.as<script_int>(), 15);
}

JAI_TEST(vm_lambdas_basic) {
    // Test basic lambda expressions
    auto result = compile_and_execute_vm(R"(
        var square = fun(x) { return x * x; };
        square(6);
    )");
    expect_eq(result.as<script_int>(), 36);
}

JAI_TEST(vm_lambdas_with_captures) {
    // Test lambda expressions with closures
    auto result = compile_and_execute_vm(R"(
        fun make_adder(n) {
            return fun(x) { return x + n; };
        }
        
        var add5 = make_adder(5);
        add5(10);
    )");
    expect_eq(result.as<script_int>(), 15);
}

JAI_TEST(vm_arrays) {
    // Test array operations
    auto result = compile_and_execute_vm(R"(
        var arr = [1, 2, 3, 4, 5];
        arr[2];
    )");
    expect_eq(result.as<script_int>(), 3);
    
    // Test array methods
    result = compile_and_execute_vm(R"(
        var arr = [10, 20, 30];
        arr.size();
    )");
    expect_eq(result.as<script_int>(), 3);
}

JAI_TEST(vm_complex_expressions) {
    // Test complex nested expressions
    auto result = compile_and_execute_vm(R"(
        fun fibonacci(n) {
            if (n <= 1) return n;
            return fibonacci(n-1) + fibonacci(n-2);
        }
        
        var nums = [fibonacci(5), fibonacci(6), fibonacci(7)];
        nums[0] + nums[1] + nums[2];
    )");
    expect_eq(result.as<script_int>(), 5 + 8 + 13); // 26
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()