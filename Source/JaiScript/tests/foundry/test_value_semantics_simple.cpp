#include "core/value_semantics_tests.hpp"

using namespace jai;

int main() {
    auto suite = jai::foundry::tests::value_semantics_tests();
    suite.forge_tests();
    
    // Run just the specific failing tests
    bool passed = true;
    
    try {
        // Test function_by_value_copies
        auto eng = jai::engine::make();
        jai::foundry::tests::TrackedObject::reset();
        
        class_builder<jai::foundry::tests::TrackedObject>(*eng, "TrackedObject")
            .constructor<int>()
            .method("modify", &jai::foundry::tests::TrackedObject::modify)
            .method("get", &jai::foundry::tests::TrackedObject::get)
            .property("value", &jai::foundry::tests::TrackedObject::value)
            .property("id", &jai::foundry::tests::TrackedObject::id)
            .build();
        
        // Register a function that takes by value
        eng->add_function("take_by_value", [](jai::foundry::tests::TrackedObject obj) -> int {
            return obj.get();
        });
        
        auto result = eng->execute("auto obj = TrackedObject(42); take_by_value(obj)");
        
        std::cout << "function_by_value_copies test:\n";
        std::cout << "  Copy count: " << jai::foundry::tests::TrackedObject::copy_count << " (expected: 2)\n";
        std::cout << "  Result: " << result.as<int>() << " (expected: 42)\n";
        
        if (jai::foundry::tests::TrackedObject::copy_count != 2 || result.as<int>() != 42) {
            std::cout << "  FAILED\n";
            passed = false;
        } else {
            std::cout << "  PASSED\n";
        }
    } catch (const std::exception& e) {
        std::cout << "function_by_value_copies test FAILED with exception: " << e.what() << "\n";
        passed = false;
    }
    
    try {
        // Test function_by_reference_no_copy
        auto eng = jai::engine::make();
        jai::foundry::tests::TrackedObject::reset();
        
        class_builder<jai::foundry::tests::TrackedObject>(*eng, "TrackedObject")
            .constructor<int>()
            .method("modify", &jai::foundry::tests::TrackedObject::modify)
            .method("get", &jai::foundry::tests::TrackedObject::get)
            .property("value", &jai::foundry::tests::TrackedObject::value)
            .property("id", &jai::foundry::tests::TrackedObject::id)
            .build();
        
        // Register a function that takes by reference
        eng->add_function("take_by_ref", [](jai::foundry::tests::TrackedObject& obj) -> int {
            return obj.get();
        });
        
        auto result = eng->execute("auto obj = TrackedObject(42); take_by_ref(obj)");
        
        std::cout << "\nfunction_by_reference_no_copy test:\n";
        std::cout << "  Copy count: " << jai::foundry::tests::TrackedObject::copy_count << " (expected: 1)\n";
        std::cout << "  Result: " << result.as<int>() << " (expected: 42)\n";
        
        if (jai::foundry::tests::TrackedObject::copy_count != 1 || result.as<int>() != 42) {
            std::cout << "  FAILED\n";
            passed = false;
        } else {
            std::cout << "  PASSED\n";
        }
    } catch (const std::exception& e) {
        std::cout << "function_by_reference_no_copy test FAILED with exception: " << e.what() << "\n";
        passed = false;
    }
    
    try {
        // Test map_assignment_copies
        auto eng = jai::engine::make();
        jai::foundry::tests::TrackedObject::reset();
        
        class_builder<jai::foundry::tests::TrackedObject>(*eng, "TrackedObject")
            .constructor<int>()
            .method("modify", &jai::foundry::tests::TrackedObject::modify)
            .method("get", &jai::foundry::tests::TrackedObject::get)
            .property("value", &jai::foundry::tests::TrackedObject::value)
            .property("id", &jai::foundry::tests::TrackedObject::id)
            .build();
        
        jai::foundry::tests::TrackedObject::verbose = true;
        eng->execute("auto obj = TrackedObject(42);");
        eng->execute("auto myMap = {};");
        eng->execute("myMap[\"key\"] = obj;");
        
        // Verify the value is in the map
        auto result = eng->execute("myMap[\"key\"].value");
        jai::foundry::tests::TrackedObject::verbose = false;
        
        std::cout << "\nmap_assignment_copies test:\n";
        std::cout << "  Copy count: " << jai::foundry::tests::TrackedObject::copy_count << " (expected: 2)\n";
        std::cout << "  Result: " << result.as<int>() << " (expected: 42)\n";
        
        if (jai::foundry::tests::TrackedObject::copy_count != 2 || result.as<int>() != 42) {
            std::cout << "  FAILED\n";
            passed = false;
        } else {
            std::cout << "  PASSED\n";
        }
    } catch (const std::exception& e) {
        std::cout << "map_assignment_copies test FAILED with exception: " << e.what() << "\n";
        passed = false;
    }
    
    return passed ? 0 : 1;
}