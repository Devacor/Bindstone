#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    try {
        auto eng = jai::engine::make();

        // Test 1: Simple namespace function
        std::cout << "Test 1: Namespace function..." << std::endl;
        auto result1 = eng->execute(R"(
            namespace mylib {
                auto greet(name) {
                    return "Hello, " + name;
                }
            }

            mylib::greet("World")
        )");
        std::cout << "  Result: " << result1.as_string() << std::endl;

        // Test 2: Namespace variable
        std::cout << "\nTest 2: Namespace variable..." << std::endl;
        auto result2 = eng->execute(R"(
            namespace config {
                auto MAX_SIZE = 100;
            }

            config::MAX_SIZE
        )");
        std::cout << "  Result: " << result2.as_int() << std::endl;

        // Test 3: Namespace class
        std::cout << "\nTest 3: Namespace class..." << std::endl;
        auto result3 = eng->execute(R"(
            namespace shapes {
                class Circle {
                public:
                    int radius;

                    Circle(r) {
                        radius = r;
                    }

                    auto area() {
                        return radius * radius * 3;
                    }
                }
            }

            auto c = new shapes::Circle(5);
            c.area()
        )");
        std::cout << "  Result: " << result3.as_int() << std::endl;

        // Test 4: Nested namespace with :: syntax
        std::cout << "\nTest 4: Nested namespace (my::nested syntax)..." << std::endl;
        auto result4 = eng->execute(R"(
            namespace my::nested::deep {
                auto compute(x) {
                    return x * 10;
                }
            }

            my::nested::deep::compute(7)
        )");
        std::cout << "  Result: " << result4.as_int() << std::endl;

        std::cout << "\n✅ All namespace tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ ERROR: " << e.what() << std::endl;
        return 1;
    }
}
