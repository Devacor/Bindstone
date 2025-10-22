#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <iostream>
#include <cmath>

class Point {
public:
    double x, y;

    Point() : x(0), y(0) {}
    Point(double x, double y) : x(x), y(y) {}

    double length() const { return std::sqrt(x * x + y * y); }
};

int main() {
    auto eng = jai::engine::make();

    std::cout << "Building Point class..." << std::endl;
    jai::class_builder<Point>(*eng, "Point")
        .constructor<>()
        .constructor<double, double>()
        .method("length", &Point::length)
        .property("x", &Point::x)
        .property("y", &Point::y)
        .build();

    std::cout << "Testing construction..." << std::endl;
    auto result = eng->execute("auto p = Point(3, 4); p.length()");
    std::cout << "Result: " << result.as<double>() << std::endl;

    if (std::abs(result.as<double>() - 5.0) < 0.001) {
        std::cout << "✓ SUCCESS!" << std::endl;
        return 0;
    } else {
        std::cout << "✗ FAIL: Expected 5.0" << std::endl;
        return 1;
    }
}
