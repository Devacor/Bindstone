#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;

int main() {
    try {
        auto engine = engine::make();
        jai::stdlib::register_all(engine);
        
        std::cout << "Testing final range-based for loop:\n";
        
        auto result = engine->execute(R"(
            auto scores = {"alice": 100, "bob": 85};
            auto total = 0;
            for (auto kv : scores) {
                total += kv.second;
            }
            total
        )");
        std::cout << "Total: " << result.template as<int>() << " (expected: 185)\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}