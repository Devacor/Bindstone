#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jvm/vm_backend.hpp>
#include <jaiscript/jvm/virtual_machine.hpp>
#include <jaiscript/jvm/bytecode.hpp>
#include <jaiscript/jvm/compiler.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

using namespace jai;
using namespace jai::jvm;
using namespace jai::test;

JAI_TEST_SUITE(VMCompilerBasicTests)

JAI_TEST(compiler_arithmetic) {
    // Test basic arithmetic compilation and execution
    lexer lex("2 + 3 * 4");
    auto tokens = lex.tokenize();
    
    parser parse(tokens);
    auto declarations = parse.parse();
    
    compiler comp;
    auto mod = comp.compile(declarations);
    
    expect_true(mod != nullptr);
    expect_false(comp.has_errors());
    
    auto vm = create_vm();
    vm->load_module(mod.get());
    script_value result = vm->execute();
    
    expect_eq(result.as<script_int>(), 14); // 2 + (3 * 4) = 14
}

JAI_TEST(compiler_variables) {
    // Test variable declaration and access
    lexer lex("var x = 10; var y = 20; x + y;");
    auto tokens = lex.tokenize();
    
    parser parse(tokens);
    auto declarations = parse.parse();
    
    compiler comp;
    auto mod = comp.compile(declarations);
    
    expect_true(mod != nullptr);
    expect_false(comp.has_errors());
    
    auto vm = create_vm();
    string_symbolizer symbolizer;
    auto global_env = std::make_shared<environment>(&symbolizer);
    vm->set_global_environment(global_env);
    vm->load_module(mod.get());
    
    script_value result = vm->execute();
    expect_eq(result.as<script_int>(), 30);
}

JAI_TEST(compiler_if_statement) {
    // Test if statement compilation
    lexer lex("var x = 5; if (x > 3) { x = 10; } x;");
    auto tokens = lex.tokenize();
    
    parser parse(tokens);
    auto declarations = parse.parse();
    
    compiler comp;
    auto mod = comp.compile(declarations);
    
    expect_true(mod != nullptr);
    expect_false(comp.has_errors());
    
    auto vm = create_vm();
    string_symbolizer symbolizer;
    auto global_env = std::make_shared<environment>(&symbolizer);
    vm->set_global_environment(global_env);
    vm->load_module(mod.get());
    
    script_value result = vm->execute();
    expect_eq(result.as<script_int>(), 10);
}

JAI_TEST(compiler_while_loop) {
    // Test while loop compilation
    lexer lex("var i = 0; var sum = 0; while (i < 5) { sum = sum + i; i = i + 1; } sum;");
    auto tokens = lex.tokenize();
    
    parser parse(tokens);
    auto declarations = parse.parse();
    
    compiler comp;
    auto mod = comp.compile(declarations);
    
    expect_true(mod != nullptr);
    expect_false(comp.has_errors());
    
    auto vm = create_vm();
    string_symbolizer symbolizer;
    auto global_env = std::make_shared<environment>(&symbolizer);
    vm->set_global_environment(global_env);
    vm->load_module(mod.get());
    
    script_value result = vm->execute();
    expect_eq(result.as<script_int>(), 10); // 0+1+2+3+4 = 10
}

JAI_TEST(compiler_function_call) {
    // Test function declaration and call
    lexer lex("fun double(x) { return x * 2; } double(21);");
    auto tokens = lex.tokenize();
    
    parser parse(tokens);
    auto declarations = parse.parse();
    
    compiler comp;
    auto mod = comp.compile(declarations);
    
    expect_true(mod != nullptr);
    expect_false(comp.has_errors());
    
    auto vm = create_vm();
    vm->set_debug_mode(true);
    string_symbolizer symbolizer;
    auto global_env = std::make_shared<environment>(&symbolizer);
    vm->set_global_environment(global_env);
    vm->load_module(mod.get());
    
    script_value result = vm->execute();
    
    std::cout << "Function call result type: " << static_cast<int>(result.type()) << std::endl;
    std::cout << "Is int: " << result.is_int() << ", Is null: " << result.is_null() << std::endl;
    
    if (result.is_int()) {
        expect_eq(result.as<script_int>(), 42);
    } else {
        // For debugging - let's see what we actually got
        std::cout << "Got non-integer result: " << result.to_string() << std::endl;
        expect_true(false); // Fail the test but with debug info
    }
}

JAI_TEST(compiler_builtin_function) {
    // Test builtin function call compilation
    lexer lex("var x = 21; print(x);");
    auto tokens = lex.tokenize();
    
    parser parse(tokens);
    auto declarations = parse.parse();
    
    compiler comp;
    auto mod = comp.compile(declarations);
    
    expect_true(mod != nullptr);
    expect_false(comp.has_errors());
    
    auto vm = create_vm();
    string_symbolizer symbolizer;
    auto global_env = std::make_shared<environment>(&symbolizer);
    vm->set_global_environment(global_env);
    vm->load_module(mod.get());
    
    script_value result = vm->execute();
    // print() returns null, which is the expected result
    expect_true(result.is_null());
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()