#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        // Test which get_ref is called
        const char* code = R"(
            // Test simple identifier access
            auto x = 42;
            auto y = x;  // This should call get_ref
        )";
        
        auto result = engine->execute(code);
        std::cout << "Result: " << result.as<int>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}