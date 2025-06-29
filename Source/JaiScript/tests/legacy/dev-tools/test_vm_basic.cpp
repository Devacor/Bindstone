#include "jai_test.hpp"
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

JAI_TEST_SUITE(VMBasic)

JAI_TEST(vm_simple_numbers) {
    // Test basic number literals
    auto result = compile_and_execute_vm("42;");
    expect_eq(result.as<script_int>(), 42);
}

JAI_TEST(vm_simple_arithmetic) {
    // Test basic arithmetic compilation and execution
    auto result = compile_and_execute_vm("2 + 3;");
    expect_eq(result.as<script_int>(), 5);
    
    result = compile_and_execute_vm("10 - 4;");
    expect_eq(result.as<script_int>(), 6);
    
    result = compile_and_execute_vm("3 * 7;");
    expect_eq(result.as<script_int>(), 21);
    
    result = compile_and_execute_vm("15 / 3;");
    expect_eq(result.as<script_int>(), 5);
}

JAI_TEST(vm_arithmetic_precedence) {
    // Test precedence
    auto result = compile_and_execute_vm("2 + 3 * 4;");
    expect_eq(result.as<script_int>(), 14);
    
    result = compile_and_execute_vm("(2 + 3) * 4;");
    expect_eq(result.as<script_int>(), 20);
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()