#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <iostream>
#include <vector>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

struct SimpleWidget {
    std::string name;
    int value;
    
    SimpleWidget() : name("default"), value(0) {}
    SimpleWidget(const std::string& n, int v) : name(n), value(v) {}
};

class conversion_isolation_tests : public suite {
public:
    conversion_isolation_tests() : suite("Conversion Isolation Tests") {}
    
    void forge_tests() override {
        test("test1_register_widget", [this]() {
            std::cout << "\n=== TEST 1: Register Widget ===" << std::endl;
            auto engine = make_engine();
            
            // Register Widget class
            dynamic_binder<SimpleWidget>(*engine, "SimpleWidget")
                .constructor<>()
                .constructor<const std::string&, int>()
                .property("name", &SimpleWidget::name)
                .property("value", &SimpleWidget::value)
                .build();
            
            // Check if conversions were registered
            auto conv_registry = engine->get_conversion_registry();
            std::cout << "Has conversion for SimpleWidget: " 
                     << conv_registry->has_conversion<SimpleWidget>() << std::endl;
            std::cout << "Has conversion for vector<SimpleWidget>: " 
                     << conv_registry->has_conversion<std::vector<SimpleWidget>>() << std::endl;
            
            // Try to use the conversion
            engine->add_function("count_widgets", [](std::vector<SimpleWidget> widgets) -> int {
                return static_cast<int>(widgets.size());
            });
            
            try {
                auto result = engine->execute(R"(
                    auto widgets = [];
                    widgets.push(SimpleWidget("first", 10));
                    count_widgets(widgets)
                )");
                
                check_eq(result.as<int>(), 1);
                std::cout << "SUCCESS: Conversion worked!" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "FAILED: " << e.what() << std::endl;
                throw;
            }
        });
        
        test("test2_register_widget_again", [this]() {
            std::cout << "\n=== TEST 2: Register Widget Again ===" << std::endl;
            auto engine = make_engine();  // Fresh engine
            
            // Register Widget class again
            dynamic_binder<SimpleWidget>(*engine, "SimpleWidget")
                .constructor<>()
                .constructor<const std::string&, int>()
                .property("name", &SimpleWidget::name)
                .property("value", &SimpleWidget::value)
                .build();
            
            // Check if conversions were registered
            auto conv_registry = engine->get_conversion_registry();
            std::cout << "Has conversion for SimpleWidget: " 
                     << conv_registry->has_conversion<SimpleWidget>() << std::endl;
            std::cout << "Has conversion for vector<SimpleWidget>: " 
                     << conv_registry->has_conversion<std::vector<SimpleWidget>>() << std::endl;
            
            // Try to use the conversion
            engine->add_function("count_widgets", [](std::vector<SimpleWidget> widgets) -> int {
                return static_cast<int>(widgets.size());
            });
            
            try {
                auto result = engine->execute(R"(
                    auto widgets = [];
                    widgets.push(SimpleWidget("second", 20));
                    count_widgets(widgets)
                )");
                
                check_eq(result.as<int>(), 1);
                std::cout << "SUCCESS: Conversion worked!" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "FAILED: " << e.what() << std::endl;
                throw;
            }
        });
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::conversion_isolation_tests)