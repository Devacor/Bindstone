#include "../../External/ChaiScript-6.1.0/include/chaiscript/chaiscript.hpp"
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
    chaiscript::ChaiScript chai;
    
    // Bind the C++ class
    chai.add(chaiscript::constructor<TestClass(int, const std::string&)>(), "TestClass");
    chai.add(chaiscript::fun(&TestClass::getValue), "getValue");
    chai.add(chaiscript::fun(&TestClass::setValue), "setValue");
    chai.add(chaiscript::fun(&TestClass::getName), "getName");
    chai.add(chaiscript::fun(&TestClass::setName), "setName");
    chai.add(chaiscript::fun(&TestClass::calculate), "calculate");
    chai.add(chaiscript::fun(&TestClass::value), "value");
    chai.add(chaiscript::fun(&TestClass::name), "name");
    
    // Test script with class usage
    auto result = chai.eval(R"(
        var obj = TestClass(42, "test");
        obj.setValue(100);
        var calc_result = obj.calculate(5, 3);
        calc_result;
    )");
    
    std::cout << "ChaiScript result: " << chaiscript::boxed_cast<int>(result) << std::endl;
    return 0;
}