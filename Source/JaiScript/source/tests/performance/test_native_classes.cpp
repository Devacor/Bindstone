#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <chrono>

using namespace jai;
using namespace jai::foundry;
using namespace std::chrono;

namespace jai::foundry::tests {

class native_class_performance : public suite {
public:
    native_class_performance() : suite("Native Class Performance") {}
    
    void forge_tests() override {
        test("basic_class_performance", [this]() {
            auto engine = engine::make();
            jai::stdlib::register_all(engine);
            
            auto start = high_resolution_clock::now();
            
            engine->execute(R"(
                class Point {
                    float x = 0.0;
                    float y = 0.0;
                    
                    Point(float px, float py) {
                        x = px;
                        y = py;
                    }
                    
                    float distanceTo(Point other) {
                        auto dx = x - other.x;
                        auto dy = y - other.y;
                        return sqrt(dx * dx + dy * dy);
                    }
                }
                
                auto p1 = Point(3.0, 4.0);
                auto p2 = Point(0.0, 0.0);
                p1.distanceTo(p2);
            )");
            
            auto end = high_resolution_clock::now();
            auto duration = duration_cast<microseconds>(end - start).count();
            
            std::cout << "Basic class test completed in " << duration << " μs\n";
            check(true); // Just ensure it runs
        });
        
        test("inheritance_performance", [this]() {
            auto engine = engine::make();
            jai::stdlib::register_all(engine);
            
            auto start = high_resolution_clock::now();
            
            auto result = engine->execute(R"(
                class Animal {
                    string name = "";
                    int energy = 100;
                    
                    Animal(string n) {
                        name = n;
                    }
                    
                    void move() {
                        energy -= 10;
                    }
                }
                
                class Dog : Animal {
                    string breed = "";
                    
                    Dog(string n, string b) : super(n) {
                        breed = b;
                    }
                    
                    void bark() {
                        energy -= 5;
                    }
                }
                
                auto dog = Dog("Buddy", "Golden Retriever");
                dog.move();
                dog.bark();
                dog.energy;
            )");
            
            auto end = high_resolution_clock::now();
            auto duration = duration_cast<microseconds>(end - start).count();
            
            std::cout << "Inheritance test completed in " << duration << " μs\n";
            check_eq(result.as_int(), 85); // Started at 100, -10 for move, -5 for bark
        });
        
        test("cat_tiger_example", [this]() {
            auto engine = engine::make();
            jai::stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                class Cat {
                    int a = 0;
                    Cat(int val) { this.a = val; }
                }

                class Tiger : Cat {
                    int b = 5;
                    Tiger() : super(5) {}

                    void roar() {
                        print("ROAR! Tiger with a=" + to_string(this.a) + ", b=" + to_string(this.b));
                    }

                    int getTotal() {
                        return this.a + this.b;
                    }
                }

                auto tiger = Tiger();
                tiger.roar();
                tiger.getTotal();
            )");
            
            check_eq(result.as_int(), 10); // a=5, b=5
        });
        
        test("performance_vs_map_objects", [this]() {
            auto engine = engine::make();
            jai::stdlib::register_all(engine);
            
            // Test with native classes
            auto start1 = high_resolution_clock::now();
            engine->execute(R"(
                class Counter {
                    int value = 0;
                    
                    void increment() {
                        value++;
                    }
                    
                    int getValue() {
                        return value;
                    }
                }
                
                auto sum = 0;
                for (int i = 0; i < 100; i++) {
                    auto c = Counter();
                    c.increment();
                    c.increment();
                    sum += c.getValue();
                }
                sum;
            )");
            auto end1 = high_resolution_clock::now();
            auto class_time = duration_cast<microseconds>(end1 - start1).count();
            
            // Test with map-based objects
            auto start2 = high_resolution_clock::now();
            engine->execute(R"(
                auto makeCounter = []() {
                    return {"value": 0};
                };
                
                auto increment = [](auto c) {
                    c["value"] = c["value"] + 1;
                };
                
                auto getValue = [](auto c) {
                    return c["value"];
                };
                
                auto sum = 0;
                for (int i = 0; i < 100; i++) {
                    auto c = makeCounter();
                    increment(c);
                    increment(c);
                    sum += getValue(c);
                }
                sum;
            )");
            auto end2 = high_resolution_clock::now();
            auto map_time = duration_cast<microseconds>(end2 - start2).count();
            
            std::cout << "Native classes: " << class_time << " μs\n";
            std::cout << "Map objects: " << map_time << " μs\n";
            std::cout << "Native classes are " << (map_time / (double)class_time) << "x faster\n";
            
            check(class_time < map_time); // Native classes should be faster
        });
    }
};

} // namespace

FOUNDRY_REGISTER(jai::foundry::tests::native_class_performance)