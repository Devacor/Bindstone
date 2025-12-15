#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    try {
        auto eng = jai::engine::make();

        eng->execute(R"(
            class Base {
                auto internal() { return 42; }
                auto public_method() { return internal() * 2; }
            }

            class Derived : Base {
                auto internal() override { return 99; }
                auto public_method() override { return internal() * 3; }
            }
        )");

        std::cout << "Classes defined successfully\n";

        auto result = eng->execute("Base b = Derived(); b.public_method();");
        std::cout << "Result type: " << static_cast<int>(result.type()) << "\n";
        std::cout << "Type info ptr: " << static_cast<void*>(result.get_type_info()) << "\n";

        if (result.is_int()) {
            std::cout << "Value: " << result.as_int() << "\n";
            std::cout << "Expected: 297\n";
        } else {
            std::cout << "ERROR: Not an int!\n";
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}
