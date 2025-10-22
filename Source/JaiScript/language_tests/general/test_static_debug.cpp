#include <jaiscript/jaiscript.hpp>
#include <iostream>

using namespace jai;

int main() {
    auto eng = engine::make();
    
    std::cout << "Testing static array access...\n";
    
    eng->execute(R"(
        class Test {
            static array<string> arr = ["x", "y", "z"];
            
            function test1() -> string {
                // This should work (subscript)
                return arr[0];
            }
            
            function test2() -> int {
                // This fails (method call)
                return arr.size();
            }
        }
    )");
    
    // Test explicit access first
    std::cout << "Explicit access:\n";
    auto explicit_result = eng->execute("Test::arr");
    std::cout << "  Type: " << static_cast<int>(explicit_result.type()) << "\n";
    std::cout << "  Is array: " << explicit_result.is_array() << "\n";
    std::cout << "  Size: " << explicit_result.as_array().size() << "\n";
    
    // Test implicit subscript
    std::cout << "\nImplicit subscript access:\n";
    try {
        auto subscript_result = eng->execute("auto t = Test(); t.test1()");
        std::cout << "  Result: " << subscript_result.as<std::string>() << "\n";
    } catch (const std::exception& e) {
        std::cout << "  Error: " << e.what() << "\n";
    }
    
    // Test implicit method call
    std::cout << "\nImplicit method call:\n";
    try {
        auto method_result = eng->execute("t.test2()");
        std::cout << "  Result: " << method_result.as<int>() << "\n";
    } catch (const std::exception& e) {
        std::cout << "  Error: " << e.what() << "\n";
    }
    
    return 0;
}