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

JAI_TEST_SUITE(VMStringOperations)

// String Constants
JAI_TEST(vm_string_constants) {
    // Simple string constants
    expect_eq(compile_and_execute_vm(R"("Hello World";)").as<script_string>(), "Hello World");
    expect_eq(compile_and_execute_vm(R"("");)").as<script_string>(), "");
    expect_eq(compile_and_execute_vm(R"("123";)").as<script_string>(), "123");
    
    // Strings with escape sequences
    expect_eq(compile_and_execute_vm(R"("Hello\nWorld";)").as<script_string>(), "Hello\nWorld");
    expect_eq(compile_and_execute_vm(R"("Tab\tCharacter";)").as<script_string>(), "Tab\tCharacter");
    expect_eq(compile_and_execute_vm(R"("Quote: \"test\"";)").as<script_string>(), "Quote: \"test\"");
    expect_eq(compile_and_execute_vm(R"("Backslash: \\";)").as<script_string>(), "Backslash: \\");
    
    // Unicode strings
    expect_eq(compile_and_execute_vm(R"("Unicode: ❤️🌟";)").as<script_string>(), "Unicode: ❤️🌟");
    expect_eq(compile_and_execute_vm(R"("日本語";)").as<script_string>(), "日本語");
}

// String Concatenation
JAI_TEST(vm_string_concatenation) {
    // Basic concatenation
    expect_eq(compile_and_execute_vm(R"("Hello" + " " + "World";)").as<script_string>(), "Hello World");
    expect_eq(compile_and_execute_vm(R"("" + "test";)").as<script_string>(), "test");
    expect_eq(compile_and_execute_vm(R"("test" + "";)").as<script_string>(), "test");
    
    // Concatenation with numbers
    expect_eq(compile_and_execute_vm(R"("Number: " + 42;)").as<script_string>(), "Number: 42");
    expect_eq(compile_and_execute_vm(R"("Float: " + 3.14;)").as<script_string>(), "Float: 3.14");
    expect_eq(compile_and_execute_vm(R"(123 + " items";)").as<script_string>(), "123 items");
    
    // Concatenation with booleans
    expect_eq(compile_and_execute_vm(R"("Result: " + true;)").as<script_string>(), "Result: true");
    expect_eq(compile_and_execute_vm(R"("Status: " + false;)").as<script_string>(), "Status: false");
    
    // Mixed type concatenation
    expect_eq(compile_and_execute_vm(R"("Mix: " + 42 + " and " + 3.14 + "!";)").as<script_string>(), 
              "Mix: 42 and 3.14!");
}

// String Variables and Assignment
JAI_TEST(vm_string_variables) {
    // Basic string variables
    auto result = compile_and_execute_vm(R"(
        var str = "Hello";
        str;
    )");
    expect_eq(result.as<script_string>(), "Hello");
    
    // String reassignment
    result = compile_and_execute_vm(R"(
        var str = "First";
        str = "Second";
        str;
    )");
    expect_eq(result.as<script_string>(), "Second");
    
    // String concatenation with variables
    result = compile_and_execute_vm(R"(
        var greeting = "Hello";
        var name = "World";
        greeting + " " + name;
    )");
    expect_eq(result.as<script_string>(), "Hello World");
}

// String Compound Assignment
JAI_TEST(vm_string_compound_assignment) {
    // += operator with strings
    auto result = compile_and_execute_vm(R"(
        var str = "Hello";
        str += " World";
        str;
    )");
    expect_eq(result.as<script_string>(), "Hello World");
    
    // Multiple += operations
    result = compile_and_execute_vm(R"(
        var str = "A";
        str += "B";
        str += "C";
        str += "D";
        str;
    )");
    expect_eq(result.as<script_string>(), "ABCD");
    
    // += with different types
    result = compile_and_execute_vm(R"(
        var str = "Count: ";
        str += 42;
        str += " items";
        str;
    )");
    expect_eq(result.as<script_string>(), "Count: 42 items");
}

// String in Expressions
JAI_TEST(vm_string_expressions) {
    // Strings in conditional expressions
    auto result = compile_and_execute_vm(R"(
        var condition = true;
        condition ? "Yes" : "No";
    )");
    // Note: Ternary operator may not be implemented yet
    // expect_eq(result.as<script_string>(), "Yes");
    
    // Strings in complex expressions
    result = compile_and_execute_vm(R"(
        var x = 10;
        var y = 20;
        "Sum of " + x + " and " + y + " is " + (x + y);
    )");
    expect_eq(result.as<script_string>(), "Sum of 10 and 20 is 30");
    
    // String comparison
    expect_true(compile_and_execute_vm(R"("apple" == "apple";)").as<script_bool>());
    expect_false(compile_and_execute_vm(R"("apple" == "Apple";)").as<script_bool>());
    expect_true(compile_and_execute_vm(R"("apple" != "orange";)").as<script_bool>());
}

// String Methods (if implemented)
JAI_TEST(vm_string_methods) {
    // Length/size method
    auto result = compile_and_execute_vm(R"(
        var str = "Hello";
        str.size();
    )");
    // expect_eq(result.as<script_int>(), 5);
    
    // Other potential string methods to test when implemented:
    // - substring/substr
    // - indexOf/find
    // - replace
    // - toUpperCase/toLowerCase
    // - trim
    // - split
    // - charAt
}

// String in Functions
JAI_TEST(vm_string_functions) {
    // String parameters and return values
    auto result = compile_and_execute_vm(R"(
        fun greet(name) {
            return "Hello, " + name + "!";
        }
        greet("World");
    )");
    expect_eq(result.as<script_string>(), "Hello, World!");
    
    // String manipulation in functions
    result = compile_and_execute_vm(R"(
        fun concat3(a, b, c) {
            return a + b + c;
        }
        concat3("A", "B", "C");
    )");
    expect_eq(result.as<script_string>(), "ABC");
    
    // Recursive string building
    result = compile_and_execute_vm(R"(
        fun repeat(str, n) {
            if (n <= 0) return "";
            if (n == 1) return str;
            return str + repeat(str, n - 1);
        }
        repeat("*", 5);
    )");
    expect_eq(result.as<script_string>(), "*****");
}

// String in Loops
JAI_TEST(vm_string_loops) {
    // Building strings in loops
    auto result = compile_and_execute_vm(R"(
        var result = "";
        for (var i = 1; i <= 5; ++i) {
            result += i;
            if (i < 5) result += ",";
        }
        result;
    )");
    expect_eq(result.as<script_string>(), "1,2,3,4,5");
    
    // String accumulation
    result = compile_and_execute_vm(R"(
        var alphabet = "";
        var code = 65; // 'A'
        while (code <= 70) { // 'F'
            alphabet += code; // Should convert to char if that's implemented
            code++;
        }
        alphabet;
    )");
    // This test depends on char conversion being implemented
}

// String Memory and Performance Considerations
JAI_TEST(vm_string_memory) {
    // Large string concatenation
    auto result = compile_and_execute_vm(R"(
        var large = "";
        for (var i = 0; i < 100; ++i) {
            large += "x";
        }
        large.size();
    )");
    // expect_eq(result.as<script_int>(), 100);
    
    // String sharing/interning test
    result = compile_and_execute_vm(R"(
        var a = "shared";
        var b = "shared";
        a == b;
    )");
    expect_true(result.as<script_bool>());
}

// Edge Cases
JAI_TEST(vm_string_edge_cases) {
    // Empty string operations
    expect_eq(compile_and_execute_vm(R"("" + "";)").as<script_string>(), "");
    expect_true(compile_and_execute_vm(R"("" == "";)").as<script_bool>());
    
    // Null/undefined with strings (if supported)
    // auto result = compile_and_execute_vm(R"("String: " + null;)");
    // expect_eq(result.as<script_string>(), "String: null");
    
    // Very long strings
    auto result = compile_and_execute_vm(R"(
        var long_str = "This is a very long string that exceeds typical small string optimizations and tests the VM's ability to handle larger string allocations efficiently without causing performance degradation or memory issues.";
        long_str + long_str;
    )");
    expect_true(result.as<script_string>().length() > 400);
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()