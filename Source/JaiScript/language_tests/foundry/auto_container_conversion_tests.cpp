#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/core/bound_array.hpp>
#include <vector>
#include <map>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Test class for auto-registration
struct Widget {
    std::string name;
    int value;

    Widget() : name("default"), value(0) {}
    Widget(const std::string& n, int v) : name(n), value(v) {}
};

// Animal hierarchy for inheritance test (moved outside lambda for MSVC compatibility)
struct Animal {
    std::string name;
    virtual ~Animal() = default;
    virtual std::string speak() const = 0;
};

struct Dog : Animal {
    Dog() { name = "Dog"; }
    Dog(const std::string& n) { name = n; }
    std::string speak() const override { return "Woof!"; }
};

struct Cat : Animal {
    Cat() { name = "Cat"; }
    Cat(const std::string& n) { name = n; }
    std::string speak() const override { return "Meow!"; }
};

class auto_container_conversion_tests : public suite {
public:
    auto_container_conversion_tests() : suite("Auto Container Conversion Tests") {}
    
    void forge_tests() override {
        test("auto_vector_registration", [this]() {
            auto engine = engine::make();
            
            // Register Widget class - this should automatically register vector<Widget>
            class_builder<Widget>(*engine, "Widget")
                .constructor<>()
                .constructor<const std::string&, int>()
                .property("name", &Widget::name)
                .property("value", &Widget::value)
                .build();
            
            // Add a function that takes vector<Widget> by value
            engine->add_function("count_widgets", [](std::vector<Widget> widgets) -> int {
                return widgets.size();
            });
            
            // This should work without explicit conversion registration
            auto result = engine->execute(R"(
                auto widgets = [];
                widgets.push(Widget("first", 10));
                widgets.push(Widget("second", 20));
                count_widgets(widgets)
            )");
            
            check_eq(result.as<int>(), 2);
        });
        
        test("auto_map_string_registration", [this]() {
            auto engine = engine::make();
            
            // Register Widget class
            class_builder<Widget>(*engine, "Widget")
                .constructor<>()
                .constructor<const std::string&, int>()
                .property("name", &Widget::name)
                .property("value", &Widget::value)
                .build();
            
            // Add a function that takes map<string, Widget>
            engine->add_function("get_widget_value", [](std::map<std::string, Widget> widgets, const std::string& key) -> int {
                auto it = widgets.find(key);
                return it != widgets.end() ? it->second.value : -1;
            });
            
            // This should work without explicit conversion registration
            auto result = engine->execute(R"(
                auto widgets = {};
                widgets["first"] = Widget("first", 100);
                widgets["second"] = Widget("second", 200);
                get_widget_value(widgets, "second")
            )");
            
            check_eq(result.as<int>(), 200);
        });
        
        test("auto_map_int_registration", [this]() {
            auto engine = engine::make();
            
            // Register Widget class
            class_builder<Widget>(*engine, "Widget")
                .constructor<>()
                .constructor<const std::string&, int>()
                .property("name", &Widget::name)
                .property("value", &Widget::value)
                .build();
            
            // Add a function that takes map<int, Widget>
            engine->add_function("get_widget_by_id", [](std::map<int, Widget> widgets, int id) -> std::string {
                auto it = widgets.find(id);
                return it != widgets.end() ? it->second.name : "not found";
            });
            
            // This should work without explicit conversion registration
            auto result = engine->execute(R"(
                auto widgets = {};
                widgets[1] = Widget("first", 100);
                widgets[2] = Widget("second", 200);
                get_widget_by_id(widgets, 2)
            )");
            
            check_eq(result.as<std::string>(), "second");
        });
        
        test("nested_auto_registration", [this]() {
            auto engine = engine::make();
            
            // Register Widget class
            class_builder<Widget>(*engine, "Widget")
                .constructor<>()
                .constructor<const std::string&, int>()
                .property("name", &Widget::name)
                .property("value", &Widget::value)
                .build();
            
            // Add a function that returns vector<Widget>
            engine->add_function("create_widgets", []() -> std::vector<Widget> {
                return {Widget("a", 1), Widget("b", 2), Widget("c", 3)};
            });
            
            // And one that takes a bound_array for zero-copy performance
            engine->add_function("sum_widget_values", [](const bound_array<Widget>& widgets) -> int {
                int sum = 0;
                for (const auto& w : widgets) {
                    sum += w.value;
                }
                return sum;
            });
            
            // Test round-trip conversion
            auto result = engine->execute(R"(
                auto widgets = create_widgets();
                sum_widget_values(widgets)
            )");
            
            check_eq(result.as<int>(), 6); // 1 + 2 + 3
        });
        
        test("non_default_constructible_type", [this]() {
            auto engine = engine::make();
            
            struct NonDefaultWidget {
                std::string name;
                int value;
                
                // Only has parameterized constructor
                NonDefaultWidget(const std::string& n, int v) : name(n), value(v) {}
            };
            
            // Register NonDefaultWidget - should only register vector conversions, not map
            class_builder<NonDefaultWidget>(*engine, "NonDefaultWidget")
                .constructor<const std::string&, int>()
                .property("name", &NonDefaultWidget::name)
                .property("value", &NonDefaultWidget::value)
                .build();
            
            // Vector should work - use by value for simple count
            engine->add_function("count_ndwidgets", [](std::vector<NonDefaultWidget> widgets) -> int {
                return widgets.size();
            });
            
            auto result = engine->execute(R"(
                auto widgets = [];
                widgets.push(NonDefaultWidget("test", 42));
                count_ndwidgets(widgets)
            )");
            
            check_eq(result.as<int>(), 1);
        });
        
        test("complex_nested_containers", [this]() {
            auto engine = engine::make();
            
            // Register Widget class
            class_builder<Widget>(*engine, "Widget")
                .constructor<>()
                .constructor<const std::string&, int>()
                .property("name", &Widget::name)
                .property("value", &Widget::value)
                .build();
            
            // Test vector<vector<Widget>> - use by value for nested containers
            engine->add_function("count_all_widgets", [](std::vector<std::vector<Widget>> widget_groups) -> int {
                int count = 0;
                for (const auto& group : widget_groups) {
                    count += group.size();
                }
                return count;
            });
            
            auto result = engine->execute(R"(
                auto groups = [];
                auto group1 = [];
                group1.push(Widget("a", 1));
                group1.push(Widget("b", 2));
                groups.push(group1);
                
                auto group2 = [];
                group2.push(Widget("c", 3));
                groups.push(group2);
                
                count_all_widgets(groups)
            )");
            
            check_eq(result.as<int>(), 3);
        });
        
        test("custom_type_with_custom_methods", [this]() {
            auto engine = engine::make();
            
            struct Calculator {
                double accumulator = 0.0;
                
                Calculator() = default;
                Calculator(double initial) : accumulator(initial) {}
                
                void add(double value) { accumulator += value; }
                void multiply(double value) { accumulator *= value; }
                double result() const { return accumulator; }
            };
            
            // Register Calculator with methods
            class_builder<Calculator>(*engine, "Calculator")
                .constructor<>()
                .constructor<double>()
                .method("add", &Calculator::add)
                .method("multiply", &Calculator::multiply)
                .method("result", &Calculator::result)
                .property("accumulator", &Calculator::accumulator)
                .build();
            
            // Test that vector<Calculator> works automatically - use bound_array for performance
            engine->add_function("sum_all_results", [](const bound_array<Calculator>& calcs) -> double {
                double sum = 0.0;
                for (const auto& calc : calcs) {
                    sum += calc.result();
                }
                return sum;
            });
            
            auto result = engine->execute(R"(
                auto calcs = [];
                
                auto c1 = Calculator(10.0);
                c1.add(5.0);
                calcs.push(c1);
                
                auto c2 = Calculator(2.0);
                c2.multiply(3.0);
                calcs.push(c2);
                
                sum_all_results(calcs)
            )");
            
            check_eq(result.as<double>(), 21.0); // (10+5) + (2*3) = 15 + 6 = 21
        });
        
        test("inheritance_and_containers", [this]() {
            auto engine = engine::make();

            // Register base and derived classes (Animal, Dog, Cat defined at namespace scope)
            class_builder<Animal>(*engine, "Animal")
                .property("name", &Animal::name)
                .method("speak", &Animal::speak)
                .build();
            
            class_builder<Dog>(*engine, "Dog")
                .constructor<>()
                .constructor<const std::string&>()
                .base_class<Animal>()
                .build();
            
            class_builder<Cat>(*engine, "Cat")
                .constructor<>()
                .constructor<const std::string&>()
                .base_class<Animal>()
                .build();
            
            // Test that vector<Dog> and vector<Cat> work - use by value
            engine->add_function("count_dogs", [](std::vector<Dog> dogs) -> int {
                return dogs.size();
            });
            
            // Test std::vector<script_value>& which should be allowed
            engine->add_function("count_script_values", [](const std::vector<script_value>& values) -> int {
                return values.size();
            });
            
            // Debug: Try explicitly registering the conversion
            // This shouldn't be necessary since script_value vectors are native
            if (!engine->get_conversion_registry()->has_conversion<std::vector<script_value>>()) {
                std::cout << "Registering std::vector<script_value> conversion..." << std::endl;
                engine->add_vector_conversion<script_value>();
            }
            
            // Add some debug output
            std::cout << "\n=== Testing inheritance_and_containers ===" << std::endl;
            
            // First test each function individually
            std::cout << "Testing count_dogs..." << std::endl;
            auto test1 = engine->execute(R"(
                auto dogs = [];
                dogs.push(Dog("Rex"));
                dogs.push(Dog("Buddy"));
                count_dogs(dogs)
            )");
            std::cout << "count_dogs result: " << test1.as<int>() << std::endl;
            
            std::cout << "Testing count_script_values..." << std::endl;
            auto test2 = engine->execute(R"(
                auto values = [1, 2, 3, 4, 5];
                count_script_values(values)
            )");
            std::cout << "count_script_values result: " << test2.as<int>() << std::endl;
            
            // Now test the combined expression
            std::cout << "Testing combined expression..." << std::endl;
            auto result = engine->execute(R"(
                auto dogs = [];
                dogs.push(Dog("Rex"));
                dogs.push(Dog("Buddy"));
                
                auto values = [1, 2, 3, 4, 5];
                
                count_dogs(dogs) + count_script_values(values)
            )");
            
            check_eq(result.as<int>(), 7); // 2 dogs + 5 values
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::auto_container_conversion_tests)