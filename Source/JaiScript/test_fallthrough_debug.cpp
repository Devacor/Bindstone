#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    try {
        auto eng = jai::engine::make();

        std::cout << "Testing for-loop fallthrough...\n" << std::endl;

        auto result = eng->execute(R"(
            var iterations = 0;
            for (var i = 0; i < 5; ++i) {
                iterations = iterations + 1;
                if (i == 2) {
                    i = 2.5;  // Change to float
                }
            }
            iterations
        )");

        std::cout << "Result type index: " << result.raw_storage_index() << std::endl;
        std::cout << "Iterations: " << result.as_int() << std::endl;
        std::cout << "Expected: 6" << std::endl;

        if (result.as_int() == 6) {
            std::cout << "\nPASSED!" << std::endl;
        } else {
            std::cout << "\nFAILED - got " << result.as_int() << " expected 6" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
