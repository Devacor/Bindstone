#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

class simple_shared_ptr_tests : public suite {
public:
    simple_shared_ptr_tests() : suite("Simple Shared Pointer Tests") {}
    
    void forge_tests() override {
        test("basic_shared_ptr_creation", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Test that we can create shared_ptr script_values directly
            script_value basic_val(42);
            script_value shared_ptr_val = script_value::make_shared_ptr(basic_val);
            
            check(shared_ptr_val.type() == value_type::jai_shared_ptr_type, "Should be shared_ptr type");
            
            // Test JSON serialization of the shared_ptr
            engine.add_global("test_shared_ptr", shared_ptr_val);
            
            std::string script = R"(
                var json = to_json(test_shared_ptr);
                print("Shared ptr JSON: ", json);
                json
            )";
            
            // Add print function
            engine.add_variadic_function("print", [](const std::vector<script_value>& args) -> script_value {
                for (const auto& arg : args) {
                    std::cout << arg.to_string();
                }
                std::cout << std::endl;
                return script_value();
            });
            
            script_value result = engine.execute(script);
            std::string json = result.as_string();
            
            // Check that it's not just "null"
            check(json != "null", "Shared ptr should not serialize as null");
            std::cout << "Actual JSON: " << json << std::endl;
        });
        
        test("shared_ptr_deduplication_simple", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Create two shared_ptrs to the same value
            script_value basic_val(123);
            script_value shared_ptr1 = script_value::make_shared_ptr(basic_val);
            script_value shared_ptr2 = shared_ptr1; // Same shared_ptr
            
            // Debug: Check types before adding to globals
            std::cout << "shared_ptr1 type: " << static_cast<int>(shared_ptr1.type()) << std::endl;
            std::cout << "shared_ptr2 type: " << static_cast<int>(shared_ptr2.type()) << std::endl;
            
            // Put them in a map
            engine.add_global("ptr1", shared_ptr1);
            engine.add_global("ptr2", shared_ptr2);
            
            // Debug: Check what we get back from globals
            auto retrieved1 = engine.get_variable("ptr1");
            auto retrieved2 = engine.get_variable("ptr2");
            std::cout << "retrieved1 type: " << static_cast<int>(retrieved1.type()) << std::endl;
            std::cout << "retrieved2 type: " << static_cast<int>(retrieved2.type()) << std::endl;
            
            std::string script = R"(
                print("ptr1 type from script: ", typeof(ptr1));
                print("ptr2 type from script: ", typeof(ptr2));
                var data = {"first": ptr1, "second": ptr2};
                var json = to_json(data, 2);
                print("Dedup test JSON: ", json);
                json
            )";
            
            // Add print and typeof functions
            engine.add_variadic_function("print", [](const std::vector<script_value>& args) -> script_value {
                for (const auto& arg : args) {
                    std::cout << arg.to_string();
                }
                std::cout << std::endl;
                return script_value();
            });
            
            engine.add_variadic_function("typeof", [](const std::vector<script_value>& args) -> script_value {
                if (args.size() != 1) {
                    throw runtime_error("typeof expects 1 argument");
                }
                return script_value(std::to_string(static_cast<int>(args[0].type())));
            });
            
            script_value result = engine.execute(script);
            std::string json = result.as_string();
            
            // Check for deduplication markers
            check(json.find("_shared_ptr_id") != std::string::npos || 
                  json.find("_shared_ptr_ref") != std::string::npos, 
                  "Should contain shared_ptr deduplication markers");
        });
    }
};

CONDITIONAL_ISOLATED_TEST(simple_shared_ptr_tests)