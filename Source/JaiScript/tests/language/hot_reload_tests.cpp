#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <iostream>
#include <iomanip>
#include <chrono>

using namespace jai::foundry;

namespace jai::foundry::tests {

class hot_reload_tests : public suite {
public:
    hot_reload_tests() : suite("Hot Reload Tests") {}

    // Reset any global state between tests
    void pre_test() override {
        // No global state to reset currently, but this ensures clean test isolation
    }

    void post_test() override {
        // Clean up after each test if needed
    }

    void forge_tests() override {
        test("basic_class_redefinition", [this]() {
            auto engine = jai::engine::make();
            
            // Add print and check functions
            engine->add_variadic_function("print", [engine](const std::vector<jai::script_value>& args) {
                for (const auto& arg : args) {
                    std::cout << arg.to_string() << " ";
                }
                std::cout << std::endl;
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            auto test_results = std::make_shared<std::vector<std::string>>();
            engine->add_function("check_value", [test_results, engine](const std::string& desc, bool result) {
                test_results->push_back(desc + ": " + (result ? "PASS" : "FAIL"));
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            // First definition
            engine->execute(R"(
                class Cat {
                    auto age = 0;
                    auto name = "default";
                    
                    void meow() {
                        print("Meow from", name);
                    }
                }
                
                auto cat = Cat();
                cat.age = 5;
                cat.name = "Fluffy";
                
                // Verify initial state
                check_value("initial age is 5", cat.age == 5);
                check_value("initial name is Fluffy", cat.name == "Fluffy");
            )");
            
            // Redefine class - remove age, add lives
            engine->execute(R"(
                class Cat {
                    auto name = "unnamed";
                    auto lives = 9;
                    
                    void dance() {
                        print(name, "is dancing!");
                    }
                }
                
                // Check field migration
                check_value("name kept as Fluffy", cat.name == "Fluffy");
                check_value("lives has default 9", cat.lives == 9);
                
                // Try to access removed field (should fail)
                auto age_accessible = false;
                try {
                    auto x = cat.age;
                    age_accessible = true;
                } catch (e) {
                    age_accessible = false;
                }
                check_value("age field removed", !age_accessible);
                
                // Test new method
                cat.dance();
            )");
            
            // Verify all checks passed
            for (const auto& result : *test_results) {
                if (result.find("FAIL") != std::string::npos) {
                    check(false, result);
                }
            }
        });
        
        test("inheritance_hot_reload", [this]() {
            auto engine = jai::engine::make();
            
            engine->add_variadic_function("print", [engine](const std::vector<jai::script_value>& args) {
                for (const auto& arg : args) {
                    std::cout << arg.to_string() << " ";
                }
                std::cout << std::endl;
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            auto test_results = std::make_shared<std::vector<std::string>>();
            engine->add_function("check_value", [test_results, engine](const std::string& desc, bool result) {
                test_results->push_back(desc + ": " + (result ? "PASS" : "FAIL"));
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            // Create base and derived classes
            engine->execute(R"(
                class Animal {
                    auto legs = 4;
                    auto sound = "generic";
                    
                    void make_sound() {
                        print(sound);
                    }
                }
                
                class Cat : Animal {
                    auto fur_color = "gray";
                    
                    void purr() {
                        print("Purrrr");
                    }
                }
                
                auto animal = Animal();
                auto cat = Cat();
                cat.sound = "meow";
                cat.fur_color = "orange";
                
                // Verify initial state
                check_value("cat sound is meow", cat.sound == "meow");
                check_value("cat fur_color is orange", cat.fur_color == "orange");
                check_value("cat legs is 4", cat.legs == 4);
            )");
            
            // Redefine base class - should update derived instances too
            engine->execute(R"(
                class Animal {
                    auto legs = 2;      // Changed default
                    auto species = "";  // New field
                    
                    void describe() {
                        print("Species:", species, "Legs:", legs);
                    }
                }
                
                // Check cascading update
                auto sound_accessible = false;
                try {
                    auto x = cat.sound;
                    sound_accessible = true;
                } catch (e) {
                    sound_accessible = false;
                }
                check_value("sound field removed", !sound_accessible);
                check_value("fur_color kept in derived", cat.fur_color == "orange");
                check_value("species added with default", cat.species == "");
                check_value("legs kept existing value", cat.legs == 4);
            )");
            
            // Verify all checks passed
            for (const auto& result : *test_results) {
                if (result.find("FAIL") != std::string::npos) {
                    check(false, result);
                }
            }
        });
        
        test("multi_level_inheritance_hot_reload", [this]() {
            auto engine = jai::engine::make();
            
            engine->add_variadic_function("print", [engine](const std::vector<jai::script_value>& args) {
                for (const auto& arg : args) {
                    std::cout << arg.to_string() << " ";
                }
                std::cout << std::endl;
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            auto test_results = std::make_shared<std::vector<std::string>>();
            engine->add_function("check_value", [test_results, engine](const std::string& desc, bool result) {
                test_results->push_back(desc + ": " + (result ? "PASS" : "FAIL"));
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            // Create three-level hierarchy
            engine->execute(R"(
                class Vehicle {
                    auto wheels = 0;
                    auto fuel = 100.0;
                    
                    void drive() {
                        fuel = fuel - 10.0;
                        print("Driving... Fuel:", fuel);
                    }
                }
                
                class Car : Vehicle {
                    auto doors = 4;
                    auto model = "Generic";
                    
                    Car() {
                        wheels = 4;
                    }
                }
                
                class SportsCar : Car {
                    auto turbo = false;
                    auto top_speed = 200;
                    
                    SportsCar() {
                        doors = 2;
                        model = "Sports";
                    }
                }
                
                auto vehicle = Vehicle();
                auto car = Car();
                auto sports = SportsCar();
                
                vehicle.wheels = 2;
                car.model = "Sedan";
                sports.turbo = true;
                sports.fuel = 50.0;
                
                // Verify initial hierarchy
                check_value("vehicle wheels is 2", vehicle.wheels == 2);
                check_value("car wheels is 4", car.wheels == 4);
                check_value("car model is Sedan", car.model == "Sedan");
                check_value("sports turbo is true", sports.turbo == true);
                check_value("sports fuel is 50", sports.fuel == 50.0);
            )");
            
            // Redefine base Vehicle class
            engine->execute(R"(
                class Vehicle {
                    auto fuel = 0.0;      // Changed default
                    auto electric = false; // New field
                    auto range = 0;       // New field
                    
                    void charge() {
                        fuel = 100.0;
                        print("Charging...");
                    }
                }
            )");
            
        });
        
        test("method_override_hot_reload", [this]() {
            auto engine = jai::engine::make();
            
            std::vector<std::string> method_calls;
            engine->add_function("record", [&method_calls, engine](const std::string& msg) {
                method_calls.push_back(msg);
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            engine->add_function("to_string", [](jai::script_float val) {
                return std::to_string(static_cast<int>(val));
            });
            
            // Initial class definitions with method overrides
            engine->execute(R"(
                class Shape {
                    auto name = "shape";
                    
                    void draw() {
                        record("Shape.draw");
                    }
                    
                    void describe() {
                        record("I am a " + name);
                    }
                }
                
                class Circle : Shape {
                    auto radius = 1.0;

                    Circle() {
                        name = "circle";
                    }

                    override void draw() {
                        record("Circle.draw with radius " + to_string(radius));
                    }
                }
                
                auto shape = Shape();
                auto circle = Circle();
                circle.radius = 5.0;
                
                shape.draw();
                circle.draw();
                circle.describe();
            )");
            
            check_eq(method_calls.size(), 3);
            check_eq(method_calls[0], "Shape.draw");
            check_eq(method_calls[1], "Circle.draw with radius 5");
            check_eq(method_calls[2], "I am a circle");
            method_calls.clear();
            
            // Redefine Circle to change override behavior
            engine->execute(R"(
                class Circle : Shape {
                    auto radius = 1.0;
                    auto filled = false;

                    Circle() {
                        name = "circle";
                    }

                    override void draw() {
                        record("NEW Circle.draw: " + (filled ? "filled" : "outline"));
                    }

                    override void describe() {
                        record("NEW Circle of radius " + to_string(radius));
                    }
                }
                
                // Existing instance should use new methods
                circle.filled = true;
                circle.draw();
                circle.describe();
            )");
            
            check_eq(method_calls.size(), 2);
            check_eq(method_calls[0], "NEW Circle.draw: filled");
            check_eq(method_calls[1], "NEW Circle of radius 5");
        });
        
        test("constructor_hot_reload", [this]() {
            auto engine = jai::engine::make();
            
            engine->add_variadic_function("print", [engine](const std::vector<jai::script_value>& args) {
                for (const auto& arg : args) {
                    std::cout << arg.to_string() << " ";
                }
                std::cout << std::endl;
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            // Class with constructor
            engine->execute(R"(
                class Point {
                    auto x = 0;
                    auto y = 0;
                    
                    Point() {
                        x = 1;
                        y = 1;
                    }
                    
                    Point(int px, int py) {
                        x = px;
                        y = py;
                    }
                }
                
                auto p1 = Point();
                auto p2 = Point(10, 20);
            )");
            
            // Redefine with different constructor behavior
            engine->execute(R"(
                class Point {
                    auto x = -1;
                    auto y = -1;
                    auto z = 0;  // New field
                    
                    Point() {
                        x = 0;
                        y = 0;
                        z = 0;
                    }
                    
                    Point(int px, int py, int pz) {
                        x = px;
                        y = py;
                        z = pz;
                    }
                }
                
                // Create new instance with new constructor
                auto p3 = Point(30, 40, 50);
            )");
        });
        
        test("complex_field_migration", [this]() {
            auto engine = jai::engine::make();
            
            auto test_results = std::make_shared<std::vector<std::string>>();
            engine->add_function("check_value", [test_results, engine](const std::string& desc, bool result) {
                test_results->push_back(desc + ": " + (result ? "PASS" : "FAIL"));
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            // Test with various field types
            engine->execute(R"(
                class DataHolder {
                    auto int_val = 42;
                    auto float_val = 3.14;
                    auto string_val = "hello";
                    auto array_val = [1, 2, 3];
                    auto map_val = {"key": "value"};
                    auto bool_val = true;
                }
                
                auto holder = DataHolder();
                holder.int_val = 100;
                holder.array_val = [10, 20, 30, 40];
                holder.map_val = {"a": 1, "b": 2};
                
                // Verify initial values
                check_value("int_val is 100", holder.int_val == 100);
                check_value("array has 4 elements", holder.array_val.size() == 4);
                check_value("array[0] is 10", holder.array_val[0] == 10);
                check_value("map has key a", holder.map_val.contains("a"));
                check_value("bool_val is true", holder.bool_val == true);
            )");
            
            // Redefine with some fields renamed/removed/added
            engine->execute(R"(
                class DataHolder {
                    auto int_val = 0;           // Same name, different default
                    auto double_val = 0.0;      // Renamed from float_val
                    auto text = "";             // Renamed from string_val
                    auto array_val = [];        // Same name
                    auto object_val = null;     // New field
                    // map_val removed
                    // bool_val removed
                }
                
                // Check field migration
                // Fields with same name keep values
                check_value("int_val kept as 100", holder.int_val == 100);
                check_value("array_val kept size 4", holder.array_val.size() == 4);
                
                // New fields get defaults
                check_value("double_val is 0.0", holder.double_val == 0.0);
                check_value("text is empty", holder.text == "");
                check_value("object_val is null", holder.object_val == null);
                
                // Removed fields are gone
                auto float_accessible = false;
                try {
                    auto x = holder.float_val;
                    float_accessible = true;
                } catch (e) {
                    float_accessible = false;
                }
                check_value("float_val removed", !float_accessible);
                
                auto map_accessible = false;
                try {
                    auto x = holder.map_val;
                    map_accessible = true;
                } catch (e) {
                    map_accessible = false;
                }
                check_value("map_val removed", !map_accessible);
            )");
            
            // Verify all checks passed
            for (const auto& result : *test_results) {
                if (result.find("FAIL") != std::string::npos) {
                    check(false, result);
                }
            }
        });
        
        test("multiple_instances_hot_reload", [this]() {
            auto engine = jai::engine::make();
            
            auto test_results = std::make_shared<std::vector<std::string>>();
            engine->add_function("check_value", [test_results, engine](const std::string& desc, bool result) {
                test_results->push_back(desc + ": " + (result ? "PASS" : "FAIL"));
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            // Create multiple instances of same class
            engine->execute(R"(
                class Player {
                    auto health = 100;
                    auto score = 0;
                    auto name = "Player";
                }
                
                auto p1 = Player();
                p1.name = "Alice";
                p1.score = 1000;
                
                auto p2 = Player();
                p2.name = "Bob";
                p2.health = 75;
                p2.score = 500;
                
                auto p3 = Player();
                p3.name = "Charlie";
                p3.health = 50;
                
                // Verify initial values
                check_value("p1 name is Alice", p1.name == "Alice");
                check_value("p1 score is 1000", p1.score == 1000);
                check_value("p2 name is Bob", p2.name == "Bob");
                check_value("p2 health is 75", p2.health == 75);
                check_value("p3 health is 50", p3.health == 50);
            )");
            
            // Redefine class
            engine->execute(R"(
                class Player {
                    auto health = 100;      // Same
                    auto level = 1;         // New
                    auto experience = 0;    // New
                    auto name = "Unknown";  // Same
                    // score removed
                }
                
                // Check all instances were migrated correctly
                check_value("p1 name kept as Alice", p1.name == "Alice");
                check_value("p1 health kept as 100", p1.health == 100);
                check_value("p1 level is default 1", p1.level == 1);
                
                auto score_accessible = false;
                try {
                    auto x = p1.score;
                    score_accessible = true;
                } catch (e) {
                    score_accessible = false;
                }
                check_value("p1 score removed", !score_accessible);
                
                check_value("p2 name kept as Bob", p2.name == "Bob");
                check_value("p2 health kept as 75", p2.health == 75);
                check_value("p2 level is default 1", p2.level == 1);
                
                check_value("p3 name kept as Charlie", p3.name == "Charlie");
                check_value("p3 health kept as 50", p3.health == 50);
            )");
            
            // Verify all checks passed
            for (const auto& result : *test_results) {
                if (result.find("FAIL") != std::string::npos) {
                    check(false, result);
                }
            }
        });
        
        test("hot_reload_migrate_lifecycle", [this]() {
            auto engine = jai::engine::make();
            
            auto test_results = std::make_shared<std::vector<std::string>>();
            engine->add_function("check_value", [test_results, engine](const std::string& desc, bool result) {
                test_results->push_back(desc + ": " + (result ? "PASS" : "FAIL"));
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            auto migration_log = std::make_shared<std::vector<std::string>>();
            engine->add_function("log_migration", [migration_log, engine](const std::string& msg) {
                migration_log->push_back(msg);
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            // First definition - no migration
            engine->execute(R"(
                class Config {
                    auto version = 1;
                    auto name = "default";
                    auto flags = 0;
                }
                
                auto cfg = Config();
                cfg.version = 10;
                cfg.name = "production";
                cfg.flags = 42;
            )");
            
            // Second definition - add hot_reload_migrate
            engine->execute(R"(
                class Config {
                    auto version = 2;
                    auto name = "unnamed";
                    auto settings = {};  // New field
                    // flags removed
                    
                    void hot_reload_migrate() {
                        log_migration("Migrate v1->v2");
                        // Convert old flags to settings
                        if (this.flags == 42) {
                            this.settings = {"debug": true, "level": 5};
                        }
                    }
                }
                
                check_value("cfg.version kept as 10", cfg.version == 10);
                check_value("cfg.name kept", cfg.name == "production");
                check_value("cfg has settings", cfg.settings.size() > 0);
            )");
            
            check_eq(migration_log->size(), 1);
            check_eq((*migration_log)[0], "Migrate v1->v2");
            migration_log->clear();
            
            // Third definition - different hot_reload_migrate
            engine->execute(R"(
                class Config {
                    auto version = 3;
                    auto name = "";
                    auto data = [];  // New field
                    // settings removed
                    
                    void hot_reload_migrate() {
                        log_migration("Migrate v2->v3");
                        // Convert settings to data array
                        if (this.settings.contains("debug")) {
                            this.data = [1, 2, 3];
                        }
                    }
                }
                
                check_value("cfg has data", cfg.data.size() == 3);
            )");
            
            check_eq(migration_log->size(), 1);
            check_eq((*migration_log)[0], "Migrate v2->v3");
            migration_log->clear();
            
            // Fourth definition - REMOVE hot_reload_migrate
            engine->execute(R"(
                class Config {
                    auto version = 4;
                    auto name = "final";
                    auto id = 0;  // New field
                    // data removed
                    // NO hot_reload_migrate!
                }
                
                check_value("cfg.version kept", cfg.version == 10);
                check_value("cfg.id is default", cfg.id == 0);
            )");
            
            // Should NOT have called any migration
            check_eq(migration_log->size(), 0);
            
            // Fifth definition - add it back, ensure old one isn't used
            engine->execute(R"(
                class Config {
                    auto version = 5;
                    auto name = "";
                    auto id = -1;
                    auto active = false;  // New field
                    
                    void hot_reload_migrate() {
                        log_migration("Migrate v4->v5");
                        this.active = (this.id >= 0);
                    }
                }
                
                check_value("cfg.active is true", cfg.active == true);
            )");
            
            check_eq(migration_log->size(), 1);
            check_eq((*migration_log)[0], "Migrate v4->v5");
            
            // Verify all checks passed
            for (const auto& result : *test_results) {
                if (result.find("FAIL") != std::string::npos) {
                    check(false, result);
                }
            }
        });
        
        test("performance_stress_test", [this]() {
            auto engine = jai::engine::make();
            
            auto test_results = std::make_shared<std::vector<std::string>>();
            engine->add_function("check_value", [test_results, engine](const std::string& desc, bool result) {
                test_results->push_back(desc + ": " + (result ? "PASS" : "FAIL"));
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            // Create many instances
            engine->execute(R"(
                class Entity {
                    auto x = 0.0;
                    auto y = 0.0;
                    auto health = 100;
                    auto active = true;
                }
                
                auto entities = [];
                for (auto i = 0; i < 100; i = i + 1) {
                    auto e = Entity();
                    e.x = i * 10.0;
                    e.y = i * 5.0;
                    e.health = 100 - i;
                    entities.push(e);
                }
            )");
            
            // Time the redefinition
            auto start = std::chrono::high_resolution_clock::now();
            
            engine->execute(R"(
                class Entity {
                    auto x = 0.0;
                    auto y = 0.0;
                    auto z = 0.0;        // New
                    auto health = 100;
                    auto armor = 0;      // New
                    auto damage = 10;    // New
                    // active removed
                }
            )");
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            
            // Should be reasonably fast even with 100 instances
            check_true(duration < 10000); // Less than 10ms
            
            // Verify a few instances were migrated correctly in script
            engine->execute(R"(
                // Check first entity
                auto e0 = entities[0];
                check_value("e0.x is 0", e0.x == 0.0);
                check_value("e0.health is 100", e0.health == 100);
                check_value("e0.z is 0", e0.z == 0.0);
                check_value("e0.armor is 0", e0.armor == 0);
                
                auto active_accessible = false;
                try {
                    auto x = e0.active;
                    active_accessible = true;
                } catch (e) {
                    active_accessible = false;
                }
                check_value("e0.active removed", !active_accessible);
                
                // Check 50th entity
                auto e50 = entities[50];
                check_value("e50.x is 500", e50.x == 500.0);
                check_value("e50.health is 50", e50.health == 50);
            )");
            
            // Verify all checks passed
            for (const auto& result : *test_results) {
                if (result.find("FAIL") != std::string::npos) {
                    check(false, result);
                }
            }
        });
        
        test("fields_unchanged_optimization", [this]() {
            auto engine = jai::engine::make();
            
            auto test_results = std::make_shared<std::vector<std::string>>();
            engine->add_function("check_value", [test_results, engine](const std::string& desc, bool result) {
                test_results->push_back(desc + ": " + (result ? "PASS" : "FAIL"));
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            auto migration_log = std::make_shared<std::vector<std::string>>();
            engine->add_function("log_migration", [migration_log, engine](const std::string& msg) {
                migration_log->push_back(msg);
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            // Create class with migration method
            engine->execute(R"(
                class Widget {
                    auto width = 100;
                    auto height = 50;
                    auto color = "blue";
                    
                    void process() {
                        log_migration("process called");
                    }
                    
                    void hot_reload_migrate() {
                        log_migration("migrate called - this should NOT happen when fields unchanged");
                    }
                }
                
                auto w1 = Widget();
                w1.width = 200;
                w1.height = 100;
                w1.color = "red";
                
                auto w2 = Widget();
                w2.width = 150;
            )");
            
            migration_log->clear();
            
            // Redefine with SAME fields (optimization should skip migration)
            engine->execute(R"(
                class Widget {
                    auto width = 100;    // Same field names
                    auto height = 50;    // Same field names
                    auto color = "blue"; // Same field names
                    
                    void process() {
                        log_migration("new process called");
                    }
                    
                    void render() {      // New method
                        log_migration("render called");
                    }
                    
                    void hot_reload_migrate() {
                        log_migration("migrate called - this should NOT happen when fields unchanged");
                    }
                }
                
                // Verify values preserved without migration
                check_value("w1.width preserved", w1.width == 200);
                check_value("w1.height preserved", w1.height == 100);
                check_value("w1.color preserved", w1.color == "red");
                check_value("w2.width preserved", w2.width == 150);
                
                // New methods should work
                w1.process();
                w1.render();
            )");
            
            // Migration should NOT have been called since fields didn't change
            if (migration_log->size() != 2) {
                std::cout << "\nfields_unchanged_optimization FAILURE:" << std::endl;
                std::cout << "  Expected migration_log size: 2" << std::endl;
                std::cout << "  Actual migration_log size: " << migration_log->size() << std::endl;
                std::cout << "  Log contents:" << std::endl;
                for (size_t i = 0; i < migration_log->size(); ++i) {
                    std::cout << "    [" << i << "] " << (*migration_log)[i] << std::endl;
                }
            }
            check_eq(migration_log->size(), 2);  // Only process and render
            check_eq((*migration_log)[0], "new process called");
            check_eq((*migration_log)[1], "render called");
            
            // Verify no migration was logged
            for (const auto& log : *migration_log) {
                check_true(log.find("migrate called") == std::string::npos, 
                          "hot_reload_migrate should not be called when fields unchanged");
            }
            
            migration_log->clear();
            
            // Now change fields - migration SHOULD happen
            engine->execute(R"(
                class Widget {
                    auto width = 100;
                    auto height = 50;
                    auto depth = 10;     // New field (replaces color)
                    
                    void hot_reload_migrate() {
                        log_migration("migrate called - fields changed");
                    }
                }
                
                check_value("w1.width still preserved", w1.width == 200);
                check_value("w1.depth has default", w1.depth == 10);
            )");
            
            // Now migration SHOULD have been called (once per instance: w1 and w2)
            check_eq(migration_log->size(), 2);
            check_eq((*migration_log)[0], "migrate called - fields changed");
            check_eq((*migration_log)[1], "migrate called - fields changed");
            
            // Verify all checks passed
            for (const auto& result : *test_results) {
                if (result.find("FAIL") != std::string::npos) {
                    check(false, result);
                }
            }
        });
        
        test("fields_unchanged_performance", [this]() {
            auto engine = jai::engine::make();
            
            // Add to_string function
            engine->add_function("to_string", [](jai::script_int val) {
                return std::to_string(val);
            });
            
            // Create a class with many instances
            engine->execute(R"(
                class DataPoint {
                    auto x = 0.0;
                    auto y = 0.0;
                    auto z = 0.0;
                    auto timestamp = 0;
                    auto label = "";
                    auto active = true;
                    auto category = 0;
                    auto priority = 1.0;
                    
                    void process() {
                        x = x + 1.0;
                    }
                }
                
                // Create many instances
                auto instances = [];
                for (auto i = 0; i < 1000; i = i + 1) {
                    auto dp = DataPoint();
                    dp.x = i * 1.0;
                    dp.y = i * 2.0;
                    dp.z = i * 3.0;
                    dp.timestamp = i;
                    dp.label = "point_" + to_string(i);
                    instances.push(dp);
                }
            )");
            
            // Time redefinition with SAME fields (optimization active)
            auto start_optimized = std::chrono::high_resolution_clock::now();
            
            engine->execute(R"(
                class DataPoint {
                    auto x = 0.0;          // Same fields
                    auto y = 0.0;
                    auto z = 0.0;
                    auto timestamp = 0;
                    auto label = "";
                    auto active = true;
                    auto category = 0;
                    auto priority = 1.0;
                    
                    void process() {       // Changed method
                        x = x + 2.0;
                        y = y + 1.0;
                    }
                    
                    void analyze() {       // New method
                        return x * y;
                    }
                }
            )");
            
            auto end_optimized = std::chrono::high_resolution_clock::now();
            auto duration_optimized = std::chrono::duration_cast<std::chrono::microseconds>(
                end_optimized - start_optimized).count();
            
            // Verify instances still work
            engine->execute(R"(
                // Check a few instances
                auto dp0 = instances[0];
                dp0.process();  // Should use new implementation
                
                auto dp500 = instances[500];
                dp500.analyze(); // New method should work
            )");
            
            // Now time redefinition with CHANGED fields (no optimization)
            auto start_unoptimized = std::chrono::high_resolution_clock::now();
            
            engine->execute(R"(
                class DataPoint {
                    auto x = 0.0;
                    auto y = 0.0;
                    auto z = 0.0;
                    auto timestamp = 0;
                    auto label = "";
                    auto active = true;
                    auto category = 0;
                    auto priority = 1.0;
                    auto weight = 1.0;     // NEW FIELD - forces migration
                    
                    void process() {
                        x = x + 3.0;
                    }
                }
            )");
            
            auto end_unoptimized = std::chrono::high_resolution_clock::now();
            auto duration_unoptimized = std::chrono::duration_cast<std::chrono::microseconds>(
                end_unoptimized - start_unoptimized).count();
            
            // Calculate speedup
            double speedup = static_cast<double>(duration_unoptimized) / duration_optimized;
            
            // Always output performance metrics
            std::cerr << "\nHot reload performance comparison (1000 instances):" << std::endl;
            std::cerr << "  Fields unchanged (optimized): " << duration_optimized << " μs" << std::endl;
            std::cerr << "  Fields changed (full migration): " << duration_unoptimized << " μs" << std::endl;
            std::cerr << "  Speedup: " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;
            std::cerr << std::endl;
            
            // The optimized version should be significantly faster
            check_true(duration_optimized < duration_unoptimized, 
                      "Optimized hot reload should be faster than full migration");
            
            // Expect at least 2x speedup for meaningful optimization
            check_true(speedup > 2.0, 
                      "Should see at least 2x speedup when fields unchanged");
        });
        
        test("identical_class_fingerprint", [this]() {
            auto engine = jai::engine::make();
            
            auto test_results = std::make_shared<std::vector<std::string>>();
            engine->add_function("check_value", [test_results, engine](const std::string& desc, bool result) {
                test_results->push_back(desc + ": " + (result ? "PASS" : "FAIL"));
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            std::vector<std::string> log_messages;
            engine->add_function("log_action", [&log_messages, engine](const std::string& msg) {
                log_messages.push_back(msg);
                return jai::script_value(std::monostate{}, engine->weak_from_this());
            });
            
            // Create initial class
            engine->execute(R"(
                auto global_calc = null;  // Declare global variable first
                
                class Calculator {
                    auto result = 0.0;
                    auto memory = 0.0;
                    
                    void add(x) {
                        result = result + x;
                        log_action("add called");
                    }
                    
                    void multiply(x) {
                        result = result * x;
                        log_action("multiply called");
                    }
                    
                    void store() {
                        memory = result;
                        log_action("store called");
                    }
                    
                    void recall() {
                        result = memory;
                        log_action("recall called");
                    }
                }
                
                global_calc = Calculator();
                global_calc.add(5);
                global_calc.multiply(3);
                global_calc.store();
                
                check_value("result is 15", global_calc.result == 15.0);
                check_value("memory is 15", global_calc.memory == 15.0);
            )");
            
            // Clear log
            log_messages.clear();
            
            // Redefine with IDENTICAL class (fingerprint should match)
            auto start_identical = std::chrono::high_resolution_clock::now();
            
            engine->execute(R"(
                class Calculator {
                    auto result = 0.0;
                    auto memory = 0.0;
                    
                    void add(x) {
                        result = result + x;
                        log_action("add called");
                    }
                    
                    void multiply(x) {
                        result = result * x;
                        log_action("multiply called");
                    }
                    
                    void store() {
                        memory = result;
                        log_action("store called");
                    }
                    
                    void recall() {
                        result = memory;
                        log_action("recall called");
                    }
                }
            )");
            
            auto end_identical = std::chrono::high_resolution_clock::now();
            auto duration_identical = std::chrono::duration_cast<std::chrono::microseconds>(
                end_identical - start_identical).count();
            
            // Methods should still work
            engine->execute(R"(
                global_calc.add(10);
                check_value("result is 25", global_calc.result == 25.0);
                check_value("memory still 15", global_calc.memory == 15.0);
            )");
            
            // Now change something to force actual redefinition
            auto start_changed = std::chrono::high_resolution_clock::now();
            
            engine->execute(R"(
                class Calculator {
                    auto result = 0.0;
                    auto memory = 0.0;
                    
                    void add(x) {
                        result = result + x;
                        log_action("add v2 called");  // Changed
                    }
                    
                    void multiply(x) {
                        result = result * x;
                        log_action("multiply called");
                    }
                    
                    void store() {
                        memory = result;
                        log_action("store called");
                    }
                    
                    void recall() {
                        result = memory;
                        log_action("recall called");
                    }
                }
            )");
            
            auto end_changed = std::chrono::high_resolution_clock::now();
            auto duration_changed = std::chrono::duration_cast<std::chrono::microseconds>(
                end_changed - start_changed).count();
            
            // Test new method behavior
            engine->execute(R"(
                global_calc.add(5);  // Should log "add v2 called"
            )");
            
            // Check that v2 was called
            bool found_v2 = false;
            for (const auto& msg : log_messages) {
                if (msg == "add v2 called") {
                    found_v2 = true;
                    break;
                }
            }
            check_true(found_v2, "Updated method should be called");
            
            // Fingerprint optimization should make identical redefinition essentially free
            std::cerr << "\nFingerprint optimization results:" << std::endl;
            std::cerr << "  Identical class redefinition: " << duration_identical << " μs" << std::endl;
            std::cerr << "  Changed class redefinition: " << duration_changed << " μs" << std::endl;
            std::cerr << std::endl;
            
            // Identical should be at least as fast (with small tolerance for timing variance)
            // Note: Most time is spent in parsing which is the same for both
            // The optimization saves time on instance migration, not parsing
            // At microsecond scale, timing variance can be ±10μs, so allow small tolerance
            int64_t tolerance_us = 10;  // Allow 10μs variance
            check_true(duration_identical <= duration_changed + tolerance_us,
                      "Identical class redefinition should be roughly as fast as changed (within tolerance)");
            
            // Verify all checks passed
            for (const auto& result : *test_results) {
                if (result.find("FAIL") != std::string::npos) {
                    check(false, result);
                }
            }
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::hot_reload_tests)