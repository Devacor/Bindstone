#include <iostream>
#include "../../../include/jaiscript/core/engine.hpp"
#include "../../../include/jaiscript/jvm/compiler.hpp"
#include "../../../include/jaiscript/jvm/virtual_machine.hpp"
#include "../../../include/jaiscript/detail/parser.hpp"
#include "../../../include/jaiscript/detail/lexer.hpp"

using namespace jai;
using namespace jai::jvm;

int main() {
    try {
        // The failing script
        std::string script = R"(
            var sum = 0;
            for (var i = 0; i < 3; i = i + 1) {
                sum = sum + i;
            }
            sum;
        )";
        
        std::cout << "=== Testing VM execution with debug trace ===\n";
        
        // Parse the script
        lexer lex(script);
        auto tokens = lex.tokenize();
        parser parse(tokens, "test.jai");
        auto declarations = parse.parse();
        
        // Compile
        compiler comp;
        auto module = comp.compile(declarations);
        
        if (!module) {
            std::cerr << "Compilation failed!\n";
            return 1;
        }
        
        // Create VM with debug mode
        virtual_machine vm;
        vm.set_debug_mode(true);
        
        // Create and set global environment
        auto global_env = std::make_shared<environment>(nullptr);
        vm.set_global_environment(global_env);
        
        vm.load_module(module.get());
        
        std::cout << "\nExecuting VM...\n";
        script_value result = vm.execute();
        
        std::cout << "\nExecution complete. Result type: " << static_cast<int>(result.type());
        if (result.is_null()) {
            std::cout << " (NULL)";
        } else if (result.is_int()) {
            std::cout << " (int: " << result.as_int() << ")";
        }
        std::cout << "\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}