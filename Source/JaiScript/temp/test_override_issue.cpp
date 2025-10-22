#include <iostream>
#include <vector>
#include <jaiscript/core/engine.hpp>

int main() {
    auto engine = jai::engine::make();

    std::vector<std::string> method_calls;
    engine->add_function("record", [&method_calls](const std::string& msg) {
        method_calls.push_back(msg);
        return jai::script_value(std::monostate{}, std::weak_ptr<jai::engine>{});
    });

    try {
        std::cout << "=== Test 1: Initial class definition with override ===" << std::endl;
        engine->execute(R"(
            class Shape {
                auto name = "shape";

                void draw() {
                    record("Shape.draw");
                }

                void describe() {
                    record("I am a " + name);
                }
            }

            class Circle : Shape {
                auto radius = 1.0;

                Circle() {
                    name = "circle";
                }

                override void draw() {
                    record("Circle.draw");
                }
            }

            auto shape = Shape();
            auto circle = Circle();

            shape.draw();
            circle.draw();
        )");

        std::cout << "Initial calls:" << std::endl;
        for (const auto& call : method_calls) {
            std::cout << "  " << call << std::endl;
        }
        method_calls.clear();

        std::cout << "\n=== Test 2: Hot reload Circle class ===" << std::endl;
        engine->execute(R"(
            class Circle : Shape {
                auto radius = 1.0;
                auto filled = false;

                Circle() {
                    name = "circle";
                }

                override void draw() {
                    record("NEW Circle.draw");
                }

                override void describe() {
                    record("NEW Circle.describe");
                }
            }

            circle.draw();
            circle.describe();
        )");

        std::cout << "After hot reload:" << std::endl;
        for (const auto& call : method_calls) {
            std::cout << "  " << call << std::endl;
        }

        std::cout << "\nTest passed!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}