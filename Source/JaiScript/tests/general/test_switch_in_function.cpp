#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        engine->execute(R"(
            function test() {
                auto result = "initial";
                switch (2) {
                    case 2:
                        result = "two";
                }
                return result;
            }
            test()
        )");
        
        auto result = engine->execute("test()");
        std::cout << "Result: " << result.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
