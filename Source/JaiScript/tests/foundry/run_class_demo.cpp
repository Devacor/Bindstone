#include <iostream>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    using namespace jai;
    
    try {
        auto engine = engine::make();
        stdlib::register_all(*engine);
        
        std::cout << "=== JaiScript Class Demo ===" << std::endl;
        
        // Test 1: Basic class with constructor
        std::cout << "\n1. Basic class with constructor:" << std::endl;
        auto result1 = engine->execute(R"JAI(
            class Point {
                int x = 0;
                int y = 0;
                
                Point(int a, int b) {
                    x = a;
                    y = b;
                }
                
                int sum() {
                    return x + y;
                }
            }
            
            auto p = Point(10, 20);
            p.sum()
        )JAI");
        
        std::cout << "Point sum: " << result1.as<int>() << std::endl;
        
        // Test 2: Class inheritance
        std::cout << "\n2. Class inheritance:" << std::endl;
        auto result2 = engine->execute(R"JAI(
            class Animal {
                string name = "";
                
                Animal(string n) {
                    name = n;
                }
                
                string speak() {
                    return name + " makes a sound";
                }
            }
            
            class Dog : Animal {
                string breed = "";
                
                Dog(string n, string b) : super(n) {
                    breed = b;
                }
                
                string speak() {
                    return name + " the " + breed + " barks!";
                }
            }
            
            auto dog = Dog("Rex", "Labrador");
            dog.speak()
        )JAI");
        
        std::cout << "Dog speak: " << result2.as<std::string>() << std::endl;
        
        // Test 3: Arrays of objects
        std::cout << "\n3. Arrays of objects:" << std::endl;
        auto result3 = engine->execute(R"JAI(
            class Student {
                string name = "";
                int grade = 0;
                
                Student(string n, int g) {
                    name = n;
                    grade = g;
                }
                
                string info() {
                    return name + ": " + to_string(grade);
                }
            }
            
            auto students = [];
            students.push(Student("Alice", 95));
            students.push(Student("Bob", 87));
            
            students[0].info() + ", " + students[1].info()
        )JAI");
        
        std::cout << "Students: " << result3.as<std::string>() << std::endl;
        
        std::cout << "\n=== All tests passed! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}