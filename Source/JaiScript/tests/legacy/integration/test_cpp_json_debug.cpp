#include <iostream>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;

class Point {
public:
    Point() : x(0.0), y(0.0) {}
    Point(double x, double y) : x(x), y(y) {}
    
    double x;
    double y;
};

int main() {
    engine engine;
    stdlib::register_all(engine);
    
    // Register Point class
    make_class_builder<Point>(engine, "Point")
        .constructor<>()
        .constructor<double, double>()
        .property("x", &Point::x)
        .property("y", &Point::y)
        .build();
    
    try {
        std::cout << "Creating Point(3.0, 4.0)..." << std::endl;
        script_value result = engine.execute("var p = Point(3.0, 4.0); p");
        std::cout << "Point created. Type: " << static_cast<int>(result.type()) << std::endl;
        
        // Test property access directly
        std::cout << "\nTesting property access..." << std::endl;
        result = engine.execute("var p3 = Point(3.0, 4.0); p3.x");
        std::cout << "p3.x = " << result.to_string() << std::endl;
        
        result = engine.execute("var p4 = Point(3.0, 4.0); p4.y");
        std::cout << "p4.y = " << result.to_string() << std::endl;
        
        std::cout << "\nCalling to_json on Point..." << std::endl;
        result = engine.execute("var p2 = Point(3.0, 4.0); to_json(p2)");
        std::cout << "Result type: " << static_cast<int>(result.type()) << std::endl;
        if (result.is_string()) {
            std::cout << "JSON: " << result.as_string() << std::endl;
        } else {
            std::cout << "Result: " << result.to_string() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "exception: " << e.what() << std::endl;
    }
    
    return 0;
}