#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    std::cout << "Test 1: Simple class with function method" << std::endl;
    try {
        auto eng = jai::engine::make();
        eng->execute(R"(
            class Test1 {
                var x = 0;
                function foo() { x = 5; }
            }
            auto t = Test1();
            t.foo();
            t.x
        )");
        std::cout << "  OK" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  FAILED: " << e.what() << std::endl;
    }

    std::cout << "\nTest 2: Simple function with int param" << std::endl;
    try {
        auto eng = jai::engine::make();
        eng->execute(R"(
            class Test2 {
                function foo(int x) { }
            }
        )");
        std::cout << "  OK" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  FAILED: " << e.what() << std::endl;
    }

    std::cout << "\nTest 3: Minimal operator= test" << std::endl;
    try {
        auto eng = jai::engine::make();
        eng->execute(R"(
            class Test3 {
                function operator=(int x) { }
            }
        )");
        std::cout << "  Parsed OK" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  FAILED: " << e.what() << std::endl;
    }

    std::cout << "\nTest 4: Just operator identifier check" << std::endl;
    try {
        auto eng = jai::engine::make();
        eng->execute(R"(
            var operator = 5;
            operator
        )");
        std::cout << "  OK - operator is a valid identifier" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  FAILED: " << e.what() << std::endl;
    }

    return 0;
}
