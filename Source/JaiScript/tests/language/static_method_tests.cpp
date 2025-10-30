#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class static_method_tests : public suite {
public:
    static_method_tests() : suite("Static Method Tests") {}
    
    void forge_tests() override {
        test("basic_static_method", [&]() {
            auto eng = engine::make();
            
            eng->execute(R"(
                class MathUtils {
                    static function add(int a, int b) -> int {
                        return a + b;
                    }
                    
                    static function multiply(int x, int y) -> int {
                        return x * y;
                    }
                }
            )");
            
            // Call static methods directly via class name
            check_eq(eng->execute("MathUtils::add(5, 3)").as<int>(), 8);
            check_eq(eng->execute("MathUtils::multiply(4, 7)").as<int>(), 28);
        });
        
        test("static_method_with_string", [&]() {
            auto eng = engine::make();
            
            eng->execute(R"(
                class StringUtils {
                    static function concat(string a, string b) -> string {
                        return a + b;
                    }
                    
                    static function repeat(string str, int count) -> string {
                        auto result = "";
                        for (auto i = 0; i < count; i = i + 1) {
                            result = result + str;
                        }
                        return result;
                    }
                }
            )");
            
            check_eq(eng->execute("StringUtils::concat(\"Hello\", \" World\")").as<std::string>(), "Hello World");
            check_eq(eng->execute("StringUtils::repeat(\"A\", 3)").as<std::string>(), "AAA");
        });
        
        test("static_method_accessing_static_fields", [&]() {
            auto eng = engine::make();
            
            eng->execute(R"(
                class Counter {
                    static int count = 0;
                    
                    static function increment() {
                        count = count + 1;
                    }
                    
                    static function getCount() -> int {
                        return count;
                    }
                    
                    static function reset() {
                        count = 0;
                    }
                }
            )");

            // Initial count should be 0
            check_eq(eng->execute("Counter::getCount()").as<int>(), 0);
            
            // Increment and check
            eng->execute("Counter::increment()");
            check_eq(eng->execute("Counter::getCount()").as<int>(), 1);
            
            // Increment multiple times
            eng->execute("Counter::increment()");
            eng->execute("Counter::increment()");
            check_eq(eng->execute("Counter::getCount()").as<int>(), 3);
            
            // Reset and check
            eng->execute("Counter::reset()");
            check_eq(eng->execute("Counter::getCount()").as<int>(), 0);
        });
        
        test("static_method_with_inheritance", [&]() {
            auto eng = engine::make();

            eng->execute(R"(
                class Base {
                    static int base_value = 10;

                    static function getBaseValue() -> int {
                        return base_value;
                    }
                }

                class Derived : Base {
                    static int derived_value = 20;

                    static function getDerivedValue() -> int {
                        return derived_value;
                    }

                    static function getTotalValue() -> int {
                        // Static members follow C++ semantics: not inherited, must use explicit qualification
                        return Base::base_value + derived_value;
                    }
                }
            )");

            // Test base class static method
            check_eq(eng->execute("Base::getBaseValue()").as<int>(), 10);

            // Test derived class can access parent static fields via explicit qualification
            check_eq(eng->execute("Derived::getTotalValue()").as<int>(), 30);

            // Test derived class own static method
            check_eq(eng->execute("Derived::getDerivedValue()").as<int>(), 20);
        });
        
        test("static_method_void_return", [&]() {
            auto eng = engine::make();
            
            eng->execute(R"(
                class Logger {
                    static array<string> logs = [];
                    
                    static function log(string message) {
                        logs.push(message);
                    }
                    
                    static function clear() {
                        logs = [];
                    }
                    
                    static function getLogCount() -> int {
                        return logs.size();
                    }
                }
            )");
            
            // Initially no logs
            check_eq(eng->execute("Logger::getLogCount()").as<int>(), 0);
            
            // Add some logs
            eng->execute("Logger::log(\"First message\")");
            eng->execute("Logger::log(\"Second message\")");
            check_eq(eng->execute("Logger::getLogCount()").as<int>(), 2);
            
            // Clear logs
            eng->execute("Logger::clear()");
            check_eq(eng->execute("Logger::getLogCount()").as<int>(), 0);
        });
        
        test("static_method_errors", [&]() {
            auto eng = engine::make();
            
            eng->execute(R"(
                class TestClass {
                    static function staticMethod() -> string {
                        return "static";
                    }
                    
                    function instanceMethod() -> string {
                        return "instance";
                    }
                }
            )");
            
            // Try to access non-existent static method
            check_throws([&]() {
                eng->execute("TestClass::nonExistent()");
            });
            
            // Try to access instance method as static
            check_throws([&]() {
                eng->execute("TestClass::instanceMethod()");
            });
            
            // Try to use :: on non-class
            check_throws([&]() {
                eng->execute("auto x = 5; x::something()");
            });
        });
        
        test("static_method_with_complex_types", [&]() {
            auto eng = engine::make();
            
            eng->execute(R"(
                class ArrayUtils {
                    static function sum(array<int> arr) -> int {
                        auto total = 0;
                        for (auto i = 0; i < arr.size(); i = i + 1) {
                            total = total + arr[i];
                        }
                        return total;
                    }
                    
                    static function createRange(int start, int end) -> array<int> {
                        auto result = [];
                        for (auto i = start; i <= end; i = i + 1) {
                            result.push(i);
                        }
                        return result;
                    }
                }
            )");
            
            // Test array parameter
            check_eq(eng->execute("ArrayUtils::sum([1, 2, 3, 4, 5])").as<int>(), 15);
            
            // Test array return value - check the first few elements
            auto result = eng->execute("ArrayUtils::createRange(1, 5)");
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 5);
            check_eq(arr[0].as<int>(), 1);
            check_eq(arr[4].as<int>(), 5);
        });
        
        test("static_method_mixed_with_instance", [&]() {
            auto eng = engine::make();
            
            eng->execute(R"(
                class Calculator {
                    static string mode = "standard";
                    int value = 0;
                    
                    Calculator(int initial) {
                        value = initial;
                    }
                    
                    static function setMode(string newMode) {
                        mode = newMode;
                    }
                    
                    static function getMode() -> string {
                        return mode;
                    }
                    
                    function add(int x) -> int {
                        value = value + x;
                        return value;
                    }
                    
                    function getValue() -> int {
                        return value;
                    }
                }
            )");
            
            // Test static methods
            check_eq(eng->execute("Calculator::getMode()").as<std::string>(), "standard");
            eng->execute("Calculator::setMode(\"scientific\")");
            check_eq(eng->execute("Calculator::getMode()").as<std::string>(), "scientific");
            
            // Test instance methods
            eng->execute("auto calc = Calculator(10); calc");
            check_eq(eng->execute("calc.getValue()").as<int>(), 10);
            check_eq(eng->execute("calc.add(5)").as<int>(), 15);
            
            // Static methods don't affect instances and vice versa
            check_eq(eng->execute("Calculator::getMode()").as<std::string>(), "scientific");
        });
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::static_method_tests)