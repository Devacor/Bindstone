#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        auto result = engine->execute(R"(
            auto x = 2;
            auto result = "";
            switch (x) {
                case 1:
                    result = result + "one";
                    fallthrough;
                case 2:
                    result = result + "two";
                    fallthrough;
                case 3:
                    result = result + "three";
                default:
                    result = result + "default";
            }
            result
        )");
        
        std::cout << "Result: '" << result.as<std::string>() << "'" << std::endl;
        std::cout << "Expected: 'twothreedefault'" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
