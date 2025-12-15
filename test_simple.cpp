#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    auto eng = jai::engine::make();

    try {
        eng->execute(R"(
            class Test {
                static int value = 42;

                function get() -> int {
                    return value;
                }
            }
        )");

        auto result = eng->execute("auto t = Test(); t.get()");
        std::cout << "Result: " << result.as<int>() << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
