#include <iostream>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    using namespace jai;
    
    try {
        auto engine = engine::make();
        stdlib::register_all(*engine);
        
        std::cout << "=== Basic JaiScript Class Tests ===" << std::endl;
        
        // Test 1: Simple class
        std::cout << "\n1. Simple class with fields:" << std::endl;
        auto result1 = engine->execute(R"JAI(
            class Point {
                int x = 0;
                int y = 0;
            }
            
            auto p = Point();
            p.x = 10;
            p.y = 20;
            p.x + p.y
        )JAI");
        
        std::cout << "Result: " << result1.as<int>() << std::endl;
        
        // Test 2: Class with constructor
        std::cout << "\n2. Class with constructor:" << std::endl;
        auto result2 = engine->execute(R"JAI(
            class Rectangle {
                int width = 0;
                int height = 0;
                
                Rectangle(int w, int h) {
                    width = w;
                    height = h;
                }
                
                int area() {
                    return width * height;
                }
            }
            
            auto rect = Rectangle(5, 3);
            rect.area()
        )JAI");
        
        std::cout << "Rectangle area: " << result2.as<int>() << std::endl;
        
        // Test 3: Method chaining
        std::cout << "\n3. Method chaining:" << std::endl;
        auto result3 = engine->execute(R"JAI(
            class Counter {
                int value = 0;
                
                Counter& add(int n) {
                    value = value + n;
                    return this;
                }
                
                int getValue() {
                    return value;
                }
            }
            
            auto counter = Counter();
            counter.add(5).add(10).add(3).getValue()
        )JAI");
        
        std::cout << "Counter value: " << result3.as<int>() << std::endl;
        
        // Test 4: Array of objects
        std::cout << "\n4. Array of objects:" << std::endl;
        auto result4 = engine->execute(R"JAI(
            class Item {
                string name = "";
                int value = 0;
                
                Item(string n, int v) {
                    name = n;
                    value = v;
                }
                
                int getValue() {
                    return value;
                }
            }
            
            auto items = [];
            items.push(Item("sword", 100));
            items.push(Item("shield", 75));
            items.push(Item("potion", 25));
            
            auto total = 0;
            for (auto i = 0; i < items.size(); i = i + 1) {
                total = total + items[i].getValue();
            }
            total
        )JAI");
        
        std::cout << "Total item value: " << result4.as<int>() << std::endl;
        
        std::cout << "\n=== All basic tests passed! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}