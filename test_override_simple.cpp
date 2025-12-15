#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    try {
        auto eng = jai::engine::make();

        eng->execute(R"(
            class Base {
                auto get_value() { return 42; }
            }

            class Derived : Base {
                auto get_value() override { return 99; }
            }
        )");

        std::cout << "Test 1: Direct call on Derived instance\n";
        auto result1 = eng->execute("Derived d = Derived(); d.get_value();");
        std::cout << "  Type: " << static_cast<int>(result1.type()) << "\n";
        std::cout << "  Type info: " << (result1.get_type_info() ? "valid" : "NULL") << "\n";
        if (result1.is_int()) {
            std::cout << "  Value: " << result1.as_int() << " (expected 99)\n";
        }

        std::cout << "\nTest 2: Base reference to Derived (polymorphism)\n";
        auto result2 = eng->execute("Base b = Derived(); b.get_value();");
        std::cout << "  Type: " << static_cast<int>(result2.type()) << "\n";
        std::cout << "  Type info: " << (result2.get_type_info() ? "valid" : "NULL") << "\n";
        if (result2.is_int()) {
            std::cout << "  Value: " << result2.as_int() << " (expected 99)\n";
        } else {
            std::cout << "  ERROR: Not an int!\n";
        }

        std::cout << "\nTest 3: Method that calls another method\n";
        eng->execute(R"(
            class Base2 {
                auto internal() { return 10; }
                auto public_method() { return internal() * 2; }
            }

            class Derived2 : Base2 {
                auto internal() override { return 20; }
                auto public_method() override { return internal() * 3; }
            }
        )");

        auto result3 = eng->execute("Base2 b2 = Derived2(); b2.public_method();");
        std::cout << "  Type: " << static_cast<int>(result3.type()) << "\n";
        std::cout << "  Type info: " << (result3.get_type_info() ? "valid" : "NULL") << "\n";
        if (result3.is_int()) {
            std::cout << "  Value: " << result3.as_int() << " (expected 60)\n";
        } else {
            std::cout << "  ERROR: Not an int!\n";
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}
