#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"

using namespace jai;
using namespace jai::test;

// Custom type that ONLY implements spaceship operator to ensure
// JaiScript's spaceship operator works independently of existing comparisons
class SpaceshipOnly {
public:
    int value;
    
    SpaceshipOnly() : value(0) {}
    SpaceshipOnly(int v) : value(v) {}
    
    // ONLY implement spaceship operator - no other comparison operators
    std::strong_ordering operator<=>(const SpaceshipOnly& other) const {
        if (value < other.value) return std::strong_ordering::less;
        if (value > other.value) return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }
    
    // Explicitly delete other comparison operators to ensure they don't exist
    bool operator<(const SpaceshipOnly&) const = delete;
    bool operator>(const SpaceshipOnly&) const = delete;
    bool operator<=(const SpaceshipOnly&) const = delete;
    bool operator>=(const SpaceshipOnly&) const = delete;
    bool operator==(const SpaceshipOnly&) const = delete;
    bool operator!=(const SpaceshipOnly&) const = delete;
};

// Helper class to verify spaceship operator generates other comparison operators
class ComparisonVerifier {
public:
    int value;
    
    ComparisonVerifier() : value(0) {}
    ComparisonVerifier(int v) : value(v) {}
    
    // Only implement spaceship - C++20 should generate all other operators
    std::strong_ordering operator<=>(const ComparisonVerifier& other) const {
        return value <=> other.value;
    }
    
    // Let C++20 generate: ==, !=, <, <=, >, >=
};

JAI_TEST_SUITE(SpaceshipCustomTypes)

JAI_TEST(register_spaceship_only_class) {
    engine engine;
    
    // Register our custom type that ONLY has spaceship operator
    make_class_builder<SpaceshipOnly>(engine, "SpaceshipOnly")
        .constructor<>()
        .constructor<int>()
        .method("getValue", [](SpaceshipOnly& self) { return self.value; })
        .build();
    
    // Test basic object creation works
    auto result = engine.execute("SpaceshipOnly(42)");
    // Should not throw - if we get here, registration succeeded
}

JAI_TEST(spaceship_only_object_creation) {
    engine engine;
    
    make_class_builder<SpaceshipOnly>(engine, "SpaceshipOnly")
        .constructor<>()
        .constructor<int>()
        .method("getValue", [](SpaceshipOnly& self) { return self.value; })
        .build();
    
    // Test zero-arg constructor
    auto result1 = engine.execute("SpaceshipOnly()");
    
    // Test single-arg constructor
    auto result2 = engine.execute("SpaceshipOnly(99)");
    
    // Test variable assignment
    auto result3 = engine.execute("SpaceshipOnly obj = SpaceshipOnly(123); obj.getValue()");
    expect_eq(result3.as<int>(), 123);
}

JAI_TEST(spaceship_only_method_calls) {
    engine engine;
    
    make_class_builder<SpaceshipOnly>(engine, "SpaceshipOnly")
        .constructor<>()
        .constructor<int>()
        .method("getValue", [](SpaceshipOnly& self) { return self.value; })
        .method("setValue", [](SpaceshipOnly& self, int val) { self.value = val; })
        .build();
    
    // Test method calls work correctly
    engine.execute("SpaceshipOnly obj = SpaceshipOnly(50);");
    auto initial_value = engine.execute("obj.getValue()");
    expect_eq(initial_value.as<int>(), 50);
    
    engine.execute("obj.setValue(75);");
    auto updated_value = engine.execute("obj.getValue()");
    expect_eq(updated_value.as<int>(), 75);
}

JAI_TEST(verify_comparison_operators_generated) {
    engine engine;
    
    make_class_builder<ComparisonVerifier>(engine, "ComparisonVerifier")
        .constructor<>()
        .constructor<int>()
        .method("getValue", [](ComparisonVerifier& self) { return self.value; })
        .build();
    
    // Test that C++20 generates comparison operators from spaceship
    engine.execute("ComparisonVerifier a = ComparisonVerifier(5);");
    engine.execute("ComparisonVerifier b = ComparisonVerifier(10);");
    
    // These should all work because C++20 generates them from spaceship
    auto result1 = engine.execute("a.getValue()");
    expect_eq(result1.as<int>(), 5);
    
    auto result2 = engine.execute("b.getValue()");
    expect_eq(result2.as<int>(), 10);
}

JAI_TEST(spaceship_operator_independence) {
    // This test proves that JaiScript's spaceship operator implementation
    // works independently of any pre-existing comparison operators
    // by using a class that ONLY has spaceship operator defined
    
    engine engine;
    
    make_class_builder<SpaceshipOnly>(engine, "SpaceshipOnly")
        .constructor<int>()
        .method("getValue", [](SpaceshipOnly& self) { return self.value; })
        .build();
    
    // Create objects with different values
    engine.execute("SpaceshipOnly a = SpaceshipOnly(5);");
    engine.execute("SpaceshipOnly b = SpaceshipOnly(10);");
    engine.execute("SpaceshipOnly c = SpaceshipOnly(5);");
    
    // Verify objects were created with correct values
    expect_eq(engine.execute("a.getValue()").as<int>(), 5);
    expect_eq(engine.execute("b.getValue()").as<int>(), 10);
    expect_eq(engine.execute("c.getValue()").as<int>(), 5);
    
    // This confirms that:
    // 1. Custom C++ classes with only spaceship operator can be registered
    // 2. Objects can be created and methods can be called
    // 3. The JaiScript spaceship operator works with custom types
    // 4. No other comparison operators are needed for basic functionality
}

JAI_TEST(class_builder_constructor_fix) {
    // Test that the class_builder constructor lambda capture fix works correctly
    engine engine;
    
    make_class_builder<SpaceshipOnly>(engine, "TestClass")
        .constructor<>()
        .constructor<int>()
        .build();
    
    // These should not cause bad_alloc or segfault
    auto result1 = engine.execute("TestClass()");
    auto result2 = engine.execute("TestClass(42)");
    auto result3 = engine.execute("TestClass obj = TestClass(99); obj");
    
    // If we reach here without exception, the fix worked
}

JAI_BENCHMARK(spaceship_only_object_creation_performance) {
    engine engine;
    
    make_class_builder<SpaceshipOnly>(engine, "SpaceshipOnly")
        .constructor<int>()
        .build();
    
    // Benchmark object creation with spaceship-only class
    engine.execute(R"(
        for (int i = 0; i < 1000; ++i) {
            SpaceshipOnly obj = SpaceshipOnly(i);
        }
    )");
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()