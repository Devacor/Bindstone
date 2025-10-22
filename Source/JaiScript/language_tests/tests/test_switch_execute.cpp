#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        engine->execute(R"(
            auto x = 2;
            auto case_executed = false;
            
            switch (x) {
                case 2:
                    case_executed = true;
            }
        )");
        
        auto result = engine->execute("case_executed");
        std::cout << "Case executed: " << result.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
