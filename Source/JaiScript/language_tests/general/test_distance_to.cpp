#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>
#include <cmath>

using namespace jai;

int main() {
    try {
        auto e = engine::make();
        jai::stdlib::register_all(e);
        e->add_function("sqrt", [](double x) -> double { return std::sqrt(x); });
        
        std::cout << "Test 1: Simple Point class\n";
        e->execute(R"(
            class Point {
                float x = 0.0;
                float y = 0.0;
                
                Point(float px, float py) {
                    x = px;
                    y = py;
                }
            }
            
            auto p1 = Point(3.0, 4.0);
            print("p1.x = " + to_string(p1.x) + ", p1.y = " + to_string(p1.y));
        )");
        
        std::cout << "\nTest 2: Method without parameters\n";
        e->execute(R"(
            class Point2 {
                float x = 0.0;
                float y = 0.0;
                
                Point2(float px, float py) {
                    x = px;
                    y = py;
                }
                
                float magnitude() {
                    return sqrt(x * x + y * y);
                }
            }
            
            auto p2 = Point2(3.0, 4.0);
            print("magnitude = " + to_string(p2.magnitude()));
        )");
        
        std::cout << "\nTest 3: Method with Point parameter\n";
        e->execute(R"(
            class Point3 {
                float x = 0.0;
                float y = 0.0;
                
                Point3(float px, float py) {
                    x = px;
                    y = py;
                }
                
                float distance_to(Point3 other) {
                    print("In distance_to, this.x = " + to_string(x) + ", other.x = " + to_string(other.x));
                    auto dx = x - other.x;
                    auto dy = y - other.y;
                    return sqrt(dx * dx + dy * dy);
                }
            }
            
            auto p3a = Point3(0.0, 0.0);
            auto p3b = Point3(3.0, 4.0);
            print("Distance = " + to_string(p3a.distance_to(p3b)));
        )");
        
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    
    return 0;
}