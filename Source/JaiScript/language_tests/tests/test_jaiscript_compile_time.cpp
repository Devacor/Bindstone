#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>
#include <string>

// Test C++ class for binding
class TestClass {
public:
    int value;
    std::string name;
    
    TestClass(int v, const std::string& n) : value(v), name(n) {}
    
    int getValue() const { return value; }
    void setValue(int v) { value = v; }
    std::string getName() const { return name; }
    void setName(const std::string& n) { name = n; }
    
    int calculate(int x, int y) const {
        return value + x * y;
    }
};

int main() {
    auto engine = jai::engine::make();
    jai::stdlib::register_all(engine);
    
    // Bind the C++ class
    jai::class_builder<TestClass>(*engine, "TestClass")
        .constructor<int, std::string>()
        .method("getValue", &TestClass::getValue)
        .method("setValue", &TestClass::setValue)
        .method("getName", &TestClass::getName)
        .method("setName", &TestClass::setName)
        .method("calculate", &TestClass::calculate)
        .property("value", &TestClass::value)
        .property("name", &TestClass::name)
        .build();
    
    // Test script with class usage
    auto result = engine->execute(R"(
        auto obj = TestClass(42, "test");
        obj.setValue(100);
        auto calc_result = obj.calculate(5, 3);
        calc_result;
    )");
    
    std::cout << "JaiScript result: " << result.as<int>() << std::endl;
    return 0;
}