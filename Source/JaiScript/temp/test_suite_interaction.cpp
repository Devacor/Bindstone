#include <iostream>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>

void test_basic_registration() {
    std::cout << "\n=== Test 1: Basic registration ===" << std::endl;
    auto engine1 = jai::engine::make();
    
    int call_count = 0;
    engine1->add_function("record", [&call_count, engine1](const std::string& msg) {
        call_count++;
        std::cout << "Record #" << call_count << ": " << msg << std::endl;
        return jai::script_value(std::monostate{}, engine1->weak_from_this());
    });
    
    engine1->execute(R"(
        record("test1");
        record("test2");
    )");
    
    std::cout << "Call count after test 1: " << call_count << std::endl;
}

void test_with_classes() {
    std::cout << "\n=== Test 2: With classes ===" << std::endl;
    auto engine2 = jai::engine::make();

    int call_count = 0;
    engine2->add_function("record", [&call_count, engine2](const std::string& msg) {
        call_count++;
        std::cout << "Record #" << call_count << ": " << msg << std::endl;
        return jai::script_value(std::monostate{}, engine2->weak_from_this());
    });

    try {
        engine2->execute(R"(
            class Shape {
                void draw() {
                    record("Shape.draw");
                }
            }

            class Circle : Shape {
                override void draw() {
                    record("Circle.draw");
                }
            }

            auto s = Shape();
            auto c = Circle();
            s.draw();
            c.draw();
        )");

        std::cout << "Call count after test 2: " << call_count << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR in test 2: " << e.what() << std::endl;
    }
}

void test_hot_reload() {
    std::cout << "\n=== Test 3: Hot reload ===" << std::endl;
    auto engine3 = jai::engine::make();
    
    int call_count = 0;
    engine3->add_function("record", [&call_count, engine3](const std::string& msg) {
        call_count++;
        std::cout << "Record #" << call_count << ": " << msg << std::endl;
        return jai::script_value(std::monostate{}, engine3->weak_from_this());
    });
    
    try {
        // Initial definition
        engine3->execute(R"(
            class Shape {
                void draw() {
                    record("Shape.draw v1");
                }
            }
            
            auto s = Shape();
            s.draw();
        )");
        
        // Hot reload
        engine3->execute(R"(
            class Shape {
                void draw() {
                    record("Shape.draw v2");
                }
            }
            
            s.draw();
        )");
        
        std::cout << "Call count after test 3: " << call_count << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR in test 3: " << e.what() << std::endl;
    }
}

void test_override_after_reload() {
    std::cout << "\n=== Test 4: Override after reload ===" << std::endl;
    auto engine4 = jai::engine::make();
    
    int call_count = 0;
    engine4->add_function("record", [&call_count, engine4](const std::string& msg) {
        call_count++;
        std::cout << "Record #" << call_count << ": " << msg << std::endl;
        return jai::script_value(std::monostate{}, engine4->weak_from_this());
    });
    
    try {
        // Define base class
        engine4->execute(R"(
            class Shape {
                void draw() {
                    record("Shape.draw");
                }
            }
        )");
        
        // Define derived class with override
        engine4->execute(R"(
            class Circle : Shape {
                override void draw() {
                    record("Circle.draw");
                }
            }
            
            auto c = Circle();
            c.draw();
        )");
        
        // Hot reload derived class
        engine4->execute(R"(
            class Circle : Shape {
                override void draw() {
                    record("Circle.draw v2");
                }
            }
            
            c.draw();
        )");
        
        std::cout << "Call count after test 4: " << call_count << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR in test 4: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "Testing engine isolation between tests..." << std::endl;
    
    test_basic_registration();
    test_with_classes();
    test_hot_reload();
    test_override_after_reload();
    
    std::cout << "\n=== All tests completed ===" << std::endl;
    return 0;
}
