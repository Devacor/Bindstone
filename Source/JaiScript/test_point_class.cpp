#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>
#include <cmath>

using namespace jai;

int main() {
    try {
        std::cout << "Creating engine..." << std::endl;
        auto e = engine::make();
        jai::stdlib::register_all(e);
        
        std::cout << "Registering sqrt..." << std::endl;
        e->add_function("sqrt", [](double x) -> double { return std::sqrt(x); });
        
        std::cout << "Defining Point class..." << std::endl;
        e->execute(R"(
            class Point {
                float x = 0.0;
                float y = 0.0;
                
                Point(float px, float py) {
                    print("Point constructor called with " + to_string(px) + ", " + to_string(py));
                    this.x = px;
                    this.y = py;
                }
                
                float distance_to(Point other) {
                    print("distance_to called");
                    auto dx = x - other.x;
                    auto dy = y - other.y;
                    return sqrt(dx * dx + dy * dy);
                }
            }
        )");
        std::cout << "Point class defined" << std::endl;
        
        std::cout << "Creating Point instance..." << std::endl;
        e->execute("auto p1 = Point(3.0, 4.0);");
        std::cout << "Point instance created" << std::endl;
        
        std::cout << "Creating second Point instance..." << std::endl;
        e->execute("auto p2 = Point(0.0, 0.0);");
        std::cout << "Second Point instance created" << std::endl;
        
        std::cout << "Calling distance_to..." << std::endl;
        auto result = e->execute("p1.distance_to(p2)");
        std::cout << "Distance: " << result.as<double>() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}