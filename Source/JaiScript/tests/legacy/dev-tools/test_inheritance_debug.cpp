#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;

// Same classes as in the test
class Shape {
public:
    virtual ~Shape() = default;
    virtual float area() const = 0;
    virtual std::string type() const = 0;
};

class Circle : public Shape {
public:
    Circle(float radius) : radius_(radius) {}
    
    float area() const override {
        return 3.14159f * radius_ * radius_;
    }
    
    std::string type() const override {
        return "Circle";
    }
    
    float getRadius() const { return radius_; }
    void setRadius(float r) { radius_ = r; }
    
private:
    float radius_;
};

int main() {
    try {
        engine engine;
        
        std::cout << "=== Registering Circle class ===\n";
        class_builder<Circle>(engine, "Circle")
            .constructor<float>()
            .method("area", [](Circle& self) -> float {
                std::cout << "area() called\n";
                float result = self.area();
                std::cout << "area() returning: " << result << "\n";
                return result;
            })
            .method("type", [](Circle& self) -> std::string {
                std::cout << "type() called\n";
                std::string result = self.type();
                std::cout << "type() returning: " << result << "\n";
                return result;
            })
            .build();
        
        std::cout << "\n=== Creating Circle instance ===\n";
        script_value result1 = engine.execute("auto c = Circle{5.0}; c;");
        std::cout << "Circle created successfully\n";
        
        std::cout << "\n=== Calling c.area() ===\n";
        script_value result2 = engine.execute("auto c = Circle{5.0}; c.area();");
        std::cout << "area() result: " << result2.to_string() << "\n";
        
        std::cout << "\n=== Calling c.type() ===\n";
        script_value result3 = engine.execute("auto c = Circle{5.0}; c.type();");
        std::cout << "type() result: " << result3.to_string() << "\n";
        
        std::cout << "\n=== Full test ===\n";
        script_value result4 = engine.execute(R"(
            auto c = Circle{5.0};
            string shapeType = c.type();
            shapeType;
        )");
        std::cout << "Final result: " << result4.to_string() << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}