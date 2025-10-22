#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        jai::stdlib::register_all(*eng);
        
        // Test 1: Basic implicit field access
        std::cout << "Test 1: Basic implicit field access\n";
        
        // First test that basic class works
        eng->execute(R"(
            class TestClass {
                int x = 10;
            };
            var obj = TestClass();
            print("Created object");
        )");
        
        std::cout << "Basic class creation works\n";
        
        // Now test method call
        eng->execute(R"(
            class TestClass2 {
                int x = 10;
                int y = 20;
                
                int getSum() {
                    return x + y;  // Should work without 'this.'
                }
                
                void setX(int newX) {
                    x = newX;  // Should work without 'this.'
                }
            };
            
            var obj = TestClass2();
            print("Sum: " + to_string(obj.getSum()));  // Should print 30
            obj.setX(15);
            print("New sum: " + to_string(obj.getSum()));  // Should print 35
        )");
        
        // Test 2: Order-independent field access
        std::cout << "\nTest 2: Order-independent field access\n";
        eng->execute(R"(
            class OrderTest {
                int getZ() {
                    return z;  // Accessing field declared later
                }
                
                int z = 42;
            };
            
            var obj2 = OrderTest();
            print("Z value: " + to_string(obj2.getZ()));  // Should print 42
        )");
        
        // Test 3: Method calling without 'this.'
        std::cout << "\nTest 3: Method calling without 'this.'\n";
        auto class_result = eng->execute(R"(
            class MethodTest {
                int value = 100;
                
                int getValue() {
                    return value;
                }
                
                int getDoubleValue() {
                    return getValue() * 2;  // Calling method without 'this.'
                }
            };
        )");
        
        std::cout << "Class definition result type: " << (int)class_result.type() << "\n";
        if (class_result.is_null()) {
            std::cout << "Class definition returned null\n";
        }
        
        std::cout << "Class defined successfully\n";
        
        eng->execute(R"(
            var obj3 = MethodTest();
            print("Double value: " + to_string(obj3.getDoubleValue()));  // Should print 200
        )");
        
        // Test 4: Inheritance with implicit 'this'
        std::cout << "\nTest 4: Inheritance with implicit 'this'\n";
        eng->execute(R"(
            class Base {
                int baseField = 5;
                
                int getBaseField() {
                    return baseField;
                }
            };
            
            class Derived : Base {
                int derivedField = 10;
                
                int getTotal() {
                    return baseField + derivedField;  // Accessing both base and derived fields
                }
                
                int callBaseMethod() {
                    return getBaseField() + 1;  // Calling base method
                }
            };
            
            var obj4 = Derived();
            print("Total: " + to_string(obj4.getTotal()));  // Should print 15
            print("Base method + 1: " + to_string(obj4.callBaseMethod()));  // Should print 6
        )");
        
        // Test 5: Local variables vs fields
        std::cout << "\nTest 5: Local variables vs fields\n";
        eng->execute(R"(
            class ScopeTest {
                int x = 50;
                
                int testScope() {
                    int x = 10;  // Local variable shadows field
                    return x;  // Should return 10 (local)
                }
                
                int getFieldX() {
                    return x;  // Should return 50 (field)
                }
                
                int getThisX() {
                    int x = 20;
                    return this.x;  // Explicitly accessing field
                }
            };
            
            var obj5 = ScopeTest();
            print("Local x: " + to_string(obj5.testScope()));  // Should print 10
            print("Field x: " + to_string(obj5.getFieldX()));  // Should print 50
            print("this.x: " + to_string(obj5.getThisX()));  // Should print 50
        )");
        
        std::cout << "\nAll tests completed successfully!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}