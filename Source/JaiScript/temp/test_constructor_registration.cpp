#include <iostream>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>

void debug_environment(jai::engine& engine, const std::string& label) {
    std::cout << "\n=== Environment check: " << label << " ===" << std::endl;
    
    // Check if Circle constructor exists
    if (engine.has_variable("Circle")) {
        auto circle_var = engine.get_variable("Circle");
        std::cout << "Circle variable exists: " << !circle_var.is_null() << std::endl;
        std::cout << "Circle is function: " << circle_var.is_function() << std::endl;
        std::cout << "Circle type: " << circle_var.type_name() << std::endl;
    } else {
        std::cout << "Circle variable does NOT exist" << std::endl;
    }
    
    // Check if Shape constructor exists
    if (engine.has_variable("Shape")) {
        auto shape_var = engine.get_variable("Shape");
        std::cout << "Shape variable exists: " << !shape_var.is_null() << std::endl;
        std::cout << "Shape is function: " << shape_var.is_function() << std::endl;
        std::cout << "Shape type: " << shape_var.type_name() << std::endl;
    } else {
        std::cout << "Shape variable does NOT exist" << std::endl;
    }
}

int main() {
    auto engine = jai::engine::make();
    
    int call_count = 0;
    engine->add_function("record", [&call_count, engine](const std::string& msg) {
        call_count++;
        std::cout << "Record #" << call_count << ": " << msg << std::endl;
        return jai::script_value(std::monostate{}, engine->weak_from_this());
    });
    
    try {
        std::cout << "\n=== Step 1: Define Shape class ===" << std::endl;
        engine->execute(R"(
            class Shape {
                auto name = "shape";
                
                void draw() {
                    record("Shape.draw");
                }
            }
        )");
        
        debug_environment(*engine, "After Shape definition");
        
        std::cout << "\n=== Step 2: Create Shape instance ===" << std::endl;
        engine->execute(R"(
            auto s = Shape();
            s.draw();
        )");
        
        std::cout << "\n=== Step 3: Define Circle class (inherits from Shape) ===" << std::endl;
        engine->execute(R"(
            class Circle : Shape {
                auto radius = 1.0;
                
                Circle() {
                    name = "circle";
                }
                
                override void draw() {
                    record("Circle.draw");
                }
            }
        )");
        
        debug_environment(*engine, "After Circle definition");
        
        std::cout << "\n=== Step 4: Try to create Circle instance ===" << std::endl;
        try {
            engine->execute(R"(
                auto c = Circle();
                c.draw();
            )");
            std::cout << "SUCCESS: Circle instance created!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "FAILED: " << e.what() << std::endl;
        }
        
        std::cout << "\n=== Step 5: Hot reload Shape class ===" << std::endl;
        engine->execute(R"(
            class Shape {
                auto name = "shape";
                auto version = 2;
                
                void draw() {
                    record("Shape.draw v2");
                }
            }
        )");
        
        debug_environment(*engine, "After Shape hot reload");
        
        std::cout << "\n=== Step 6: Hot reload Circle class ===" << std::endl;
        engine->execute(R"(
            class Circle : Shape {
                auto radius = 1.0;
                
                Circle() {
                    name = "circle";
                }
                
                override void draw() {
                    record("Circle.draw v2");
                }
            }
        )");
        
        debug_environment(*engine, "After Circle hot reload");
        
        std::cout << "\n=== Step 7: Try to create new Circle instance after hot reload ===" << std::endl;
        try {
            engine->execute(R"(
                auto c2 = Circle();
                c2.draw();
            )");
            std::cout << "SUCCESS: Circle instance created after hot reload!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "FAILED: " << e.what() << std::endl;
        }
        
        std::cout << "\n=== Final call count: " << call_count << " ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
