#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        jai::stdlib::register_all(*engine);
        
        std::cout << "Testing inheritance with implicit this" << std::endl;
        
        const char* code = R"(
            class Animal {
                string name = "Unknown";
                
                Animal(string n) {
                    name = n;  // Implicit this
                }
                
                void speak() {
                    print("Animal " + name + " speaks");  // Implicit this
                }
                
                string getName() {
                    return name;  // Implicit this
                }
            }
            
            class Dog : Animal {
                string breed = "Mixed";
                
                Dog(string n, string b) : super(n) {
                    breed = b;  // Implicit this
                }
                
                void speak() override {
                    print("Dog " + getName() + " barks! Breed: " + breed);  // Implicit method call and field access
                }
                
                void wagTail() {
                    print(name + " wags tail happily");  // Access parent field implicitly
                }
            }
            
            auto dog = Dog("Buddy", "Golden Retriever");
            dog.speak();
            dog.wagTail();
            
            // Test parent method call
            auto animal = Animal("Generic");
            animal.speak();
        )";
        
        engine->execute(code);
        std::cout << "All inheritance tests passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}