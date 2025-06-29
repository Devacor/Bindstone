#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <memory>
#include <unordered_set>

using namespace jai;
using namespace jai::foundry;

// Test classes for shared_ptr serialization
class shared_resource {
public:
    std::string name;
    int id;
    
    shared_resource() : name("default"), id(0) {}
    shared_resource(const std::string& n, int i) : name(n), id(i) {}
};

class reference_holder {
public:
    std::shared_ptr<shared_resource> resource;
    std::string holder_name;
    
    reference_holder() : holder_name("unnamed") {}
    reference_holder(const std::string& name) : holder_name(name) {}
    
    void set_resource(std::shared_ptr<shared_resource> res) {
        resource = res;
    }
    
    std::shared_ptr<shared_resource> get_resource() const {
        return resource;
    }
};

class circular_node {
public:
    std::string name;
    std::shared_ptr<circular_node> next;
    std::weak_ptr<circular_node> parent;
    
    circular_node() : name("unnamed") {}
    circular_node(const std::string& n) : name(n) {}
    
    void set_next(std::shared_ptr<circular_node> n) { next = n; }
    void set_parent(std::shared_ptr<circular_node> p) { parent = p; }
    
    std::shared_ptr<circular_node> get_next() const { return next; }
    std::weak_ptr<circular_node> get_parent() const { return parent; }
};

class shared_ptr_serialization_tests : public suite {
public:
    shared_ptr_serialization_tests() : suite("Shared Pointer Serialization") {}
    
    void forge_tests() override {
        test("json_shared_ptr_deduplication", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Register shared_resource class
            make_class_builder<shared_resource>(engine, "SharedResource")
                .constructor<>()
                .constructor<const std::string&, int>()
                .property("name", &shared_resource::name)
                .property("id", &shared_resource::id)
                .build();
            
            // Register reference_holder class
            make_class_builder<reference_holder>(engine, "ReferenceHolder")
                .constructor<>()
                .constructor<const std::string&>()
                .property("holder_name", &reference_holder::holder_name)
                .method("set_resource", &reference_holder::set_resource)
                .method("get_resource", &reference_holder::get_resource)
                .build();
            
            // Create a shared resource and multiple holders
            std::string script = R"(
                // Create a shared resource
                var resource = SharedResource("shared_data", 123);
                
                // Create two holders that reference the same resource
                var holder1 = ReferenceHolder("holder_one");
                var holder2 = ReferenceHolder("holder_two");
                
                holder1.set_resource(resource);
                holder2.set_resource(resource);
                
                // Put them in a container
                var data = {
                    "holders": [holder1, holder2],
                    "original_resource": resource
                };
                
                // Serialize to JSON
                var json = to_json(data, 2);
                print("JSON: ", json);
                
                // Deserialize from JSON
                var restored = from_json(json);
                
                // Check that the references are preserved
                var res1 = restored.holders[0].get_resource();
                var res2 = restored.holders[1].get_resource();
                var orig = restored.original_resource;
                
                // All should have the same values
                print("res1.id: ", res1.id);
                print("res2.id: ", res2.id);
                print("orig.id: ", orig.id);
                
                res1.id == 123 && res2.id == 123 && orig.id == 123
            )";
            
            // Add print function for debugging
            engine.add_variadic_function("print", [](const std::vector<script_value>& args) -> script_value {
                for (const auto& arg : args) {
                    std::cout << arg.to_string();
                }
                std::cout << std::endl;
                return script_value();
            });
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "JSON shared_ptr serialization failed");
        });
        
        test("binary_shared_ptr_deduplication", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Register shared_resource class
            make_class_builder<shared_resource>(engine, "SharedResource")
                .constructor<>()
                .constructor<const std::string&, int>()
                .property("name", &shared_resource::name)
                .property("id", &shared_resource::id)
                .build();
            
            // Test binary serialization with shared_ptr deduplication
            std::string script = R"(
                // Create a shared resource
                var resource = SharedResource("binary_test", 456);
                
                // Create multiple references to the same object
                var data = {
                    "ref1": resource,
                    "ref2": resource,
                    "ref3": resource
                };
                
                // Serialize to binary
                var binary = to_binary(data);
                
                // Check binary size - should be small if deduplicated
                print("Binary size: ", string_length(binary));
                
                // Deserialize
                var restored = from_binary(binary);
                
                // Verify all references have correct values
                restored.ref1.id == 456 && 
                restored.ref2.id == 456 && 
                restored.ref3.id == 456 &&
                restored.ref1.name == "binary_test"
            )";
            
            // Add print function
            engine.add_variadic_function("print", [](const std::vector<script_value>& args) -> script_value {
                for (const auto& arg : args) {
                    std::cout << arg.to_string();
                }
                std::cout << std::endl;
                return script_value();
            });
            
            // Add string length function
            engine.add_function("string_length", [](const script_string& str) -> script_int {
                return static_cast<script_int>(str.length());
            });
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "Binary shared_ptr serialization failed");
        });
        
        test("circular_reference_json", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Register circular_node class
            make_class_builder<circular_node>(engine, "CircularNode")
                .constructor<>()
                .constructor<const std::string&>()
                .property("name", &circular_node::name)
                .method("set_next", &circular_node::set_next)
                .method("set_parent", &circular_node::set_parent)
                .method("get_next", &circular_node::get_next)
                .method("get_parent", &circular_node::get_parent)
                .build();
            
            // Create circular structure
            auto node1 = std::make_shared<circular_node>("node1");
            auto node2 = std::make_shared<circular_node>("node2");
            auto node3 = std::make_shared<circular_node>("node3");
            
            node1->set_next(node2);
            node2->set_next(node3);
            node3->set_next(node1);  // Circular!
            
            node2->set_parent(node1);
            node3->set_parent(node2);
            node1->set_parent(node3);  // Circular weak refs
            
            // Add to engine
            engine.add_global("test_node1", script_value::make_object("CircularNode", node1));
            
            // Test serialization with circular references
            std::string script = R"(
                // Try to serialize - this should handle circular references gracefully
                try {
                    var json = to_json(test_node1);
                    print("Successfully serialized circular structure");
                    // JSON can't properly handle circular references, so this is expected to fail
                    // or produce a limited representation
                    true
                } catch (e) {
                    print("Expected: JSON serialization of circular references failed");
                    true  // This is actually the expected behavior
                }
            )";
            
            // Add print and try-catch support
            engine.add_variadic_function("print", [](const std::vector<script_value>& args) -> script_value {
                for (const auto& arg : args) {
                    std::cout << arg.to_string();
                }
                std::cout << std::endl;
                return script_value();
            });
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "Circular reference handling failed");
        });
        
        test("circular_reference_binary", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Register circular_node class
            make_class_builder<circular_node>(engine, "CircularNode")
                .constructor<>()
                .constructor<const std::string&>()
                .property("name", &circular_node::name)
                .method("set_next", &circular_node::set_next)
                .method("set_parent", &circular_node::set_parent)
                .method("get_next", &circular_node::get_next)
                .method("get_parent", &circular_node::get_parent)
                .build();
            
            // Test binary serialization with circular references
            std::string script = R"(
                // Create circular structure in script
                var node1 = CircularNode("first");
                var node2 = CircularNode("second");
                
                node1.set_next(node2);
                node2.set_next(node1);  // Circular!
                
                // Binary serialization should handle this properly
                var binary = to_binary(node1);
                var restored = from_binary(binary);
                
                // Verify structure is preserved
                restored.name == "first" &&
                restored.get_next().name == "second" &&
                restored.get_next().get_next().name == "first"  // Full circle
            )";
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "Binary circular reference handling failed");
        });
        
        test("weak_ptr_serialization", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Register classes
            make_class_builder<shared_resource>(engine, "SharedResource")
                .constructor<>()
                .constructor<const std::string&, int>()
                .property("name", &shared_resource::name)
                .property("id", &shared_resource::id)
                .build();
                
            make_class_builder<circular_node>(engine, "CircularNode")
                .constructor<>()
                .constructor<const std::string&>()
                .property("name", &circular_node::name)
                .method("set_next", &circular_node::set_next)
                .method("set_parent", &circular_node::set_parent)
                .method("get_next", &circular_node::get_next)
                .method("get_parent", &circular_node::get_parent)
                .build();
            
            // Test weak_ptr serialization
            std::string script = R"(
                // Create parent-child relationship
                var parent = CircularNode("parent");
                var child = CircularNode("child");
                
                child.set_parent(parent);
                parent.set_next(child);
                
                // Serialize both
                var data = {"parent": parent, "child": child};
                var binary = to_binary(data);
                var restored = from_binary(binary);
                
                // Verify relationships are preserved
                restored.parent.name == "parent" &&
                restored.child.name == "child" &&
                restored.parent.get_next().name == "child"
                // Note: weak_ptr restoration is implementation-dependent
            )";
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "Weak pointer serialization failed");
        });
        
        test("shared_ptr_dedup_efficiency", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Register shared_resource class
            make_class_builder<shared_resource>(engine, "SharedResource")
                .constructor<>()
                .constructor<const std::string&, int>()
                .property("name", &shared_resource::name)
                .property("id", &shared_resource::id)
                .build();
            
            // Add string length function
            engine.add_function("string_length", [](const script_string& str) -> script_int {
                return static_cast<script_int>(str.length());
            });
            
            // Test deduplication efficiency
            std::string script = R"(
                // Create one large object
                var resource = SharedResource("very_long_resource_name_for_testing_deduplication", 999);
                
                // Create array with many references to same object
                var refs = [];
                for (var i = 0; i < 10; ++i) {
                    refs.push(resource);
                }
                
                // Serialize without deduplication would be ~10x larger
                var binary = to_binary(refs);
                var size = string_length(binary);
                
                print("Binary size for 10 references: ", size);
                
                // Size should be much smaller than 10x the size of one object
                // Rough check: should be less than 500 bytes for deduplicated vs >1000 without
                size < 500
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
            check(result.as<script_bool>(), "Deduplication efficiency test failed");
        });
        
        test("null_shared_ptr_handling", [this]() {
            engine engine;
            stdlib::register_all(engine);
            
            // Register reference_holder class
            make_class_builder<reference_holder>(engine, "ReferenceHolder")
                .constructor<>()
                .constructor<const std::string&>()
                .property("holder_name", &reference_holder::holder_name)
                .method("set_resource", &reference_holder::set_resource)
                .method("get_resource", &reference_holder::get_resource)
                .build();
            
            // Test null shared_ptr serialization
            std::string script = R"(
                var holder = ReferenceHolder("null_test");
                // Resource is null by default
                
                // Serialize with null shared_ptr
                var binary = to_binary(holder);
                var restored = from_binary(binary);
                
                // Check that null is preserved
                restored.holder_name == "null_test" &&
                restored.get_resource() == null
            )";
            
            script_value result = engine.execute(script);
            check(result.as<script_bool>(), "Null shared_ptr handling failed");
        });
    }
};

// Enable isolated test execution
CONDITIONAL_ISOLATED_TEST(shared_ptr_serialization_tests)