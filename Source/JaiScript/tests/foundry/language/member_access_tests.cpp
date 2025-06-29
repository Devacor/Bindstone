#include "member_access_tests.hpp"
#include <jaiscript/core/class_builder.hpp>

namespace jai::foundry::tests {

// Test class for member access
class TestObject {
public:
    TestObject(int val = 0) : value_(val) {}
    
    int getValue() const { return value_; }
    void setValue(int val) { value_ = val; }
    void increment() { value_++; }
    
private:
    int value_;
};

void member_access_tests::forge_tests() {
    // Register test class once for all tests
    engine engine;
    make_class_builder<TestObject>(engine, "TestObject")
        .constructor<int>()
        .method("getValue", &TestObject::getValue)
        .method("setValue", &TestObject::setValue)
        .method("increment", &TestObject::increment)
        .build();
        
    test("array_method_on_subscript", [&engine]() {
        engine.execute("auto nested = [[1, 2, 3], [4, 5], [6, 7, 8, 9]];");
        auto result = engine.execute("nested[1].size()");
        check_eq(result.as<int>(), 2);
        
        // Test mutation
        engine.execute("nested[0].push(4);");
        auto newSize = engine.execute("nested[0].size()");
        check_eq(newSize.as<int>(), 4);
    });
    
    test("map_method_on_subscript", [&engine]() {
        engine.execute("auto mapOfMaps = {{\"first\", {{\"a\", 1}, {\"b\", 2}}}, {\"second\", {{\"x\", 10}}}};");
        auto result = engine.execute("mapOfMaps[\"first\"].size()");
        check_eq(result.as<int>(), 2);
        
        auto empty = engine.execute("mapOfMaps[\"second\"].empty()");
        check_eq(empty.as<bool>(), false);
    });
    
    test("cpp_object_method_on_subscript", [&engine]() {
        engine.execute("auto objects = {{\"test\", TestObject(42)}};");
        auto result = engine.execute("objects[\"test\"].getValue()");
        check_eq(result.as<int>(), 42);
        
        // Test mutation
        engine.execute("objects[\"test\"].increment();");
        auto newVal = engine.execute("objects[\"test\"].getValue()");
        check_eq(newVal.as<int>(), 43);
    });
    
    test("array_of_objects_member_access", [&engine]() {
        engine.execute("auto arr = [TestObject(10), TestObject(20), TestObject(30)];");
        auto result = engine.execute("arr[1].getValue()");
        check_eq(result.as<int>(), 20);
        
        engine.execute("arr[2].setValue(99);");
        auto updated = engine.execute("arr[2].getValue()");
        check_eq(updated.as<int>(), 99);
    });
    
    test("deeply_nested_member_access", [&engine]() {
        // Test multiple levels of subscript + member access
        engine.execute("auto deep = {{\"level1\", {{\"level2\", [TestObject(777)]}}}}; ");
        auto result = engine.execute("deep[\"level1\"][\"level2\"][0].getValue()");
        check_eq(result.as<int>(), 777);
        
        // Modify through the chain
        engine.execute("deep[\"level1\"][\"level2\"][0].setValue(888);");
        auto updated = engine.execute("deep[\"level1\"][\"level2\"][0].getValue()");
        check_eq(updated.as<int>(), 888);
    });
    
    test("reference_preserving_member_access", [&engine]() {
        // Ensure auto& works with member access
        engine.execute("auto myMap = {{\"nums\", [10, 20, 30]}};");
        engine.execute("auto& ref = myMap[\"nums\"];");
        engine.execute("ref.push(40);");
        
        // Verify change through original path
        auto size = engine.execute("myMap[\"nums\"].size()");
        check_eq(size.as<int>(), 4);
        auto last = engine.execute("myMap[\"nums\"].back()");
        check_eq(last.as<int>(), 40);
    });
    
    test("mixed_container_operations", [&engine]() {
        // Complex scenario mixing different types
        engine.execute("auto data = {"
                      "  {\"arrays\", [[1, 2], [3, 4, 5]]},"
                      "  {\"objects\", [TestObject(100), TestObject(200)]}"
                      "};");
        
        // Array method on nested array
        auto arrSize = engine.execute("data[\"arrays\"][0].size()");
        check_eq(arrSize.as<int>(), 2);
        
        // Object method on nested object
        auto objVal = engine.execute("data[\"objects\"][1].getValue()");
        check_eq(objVal.as<int>(), 200);
        
        // Mutation through subscript
        engine.execute("data[\"arrays\"][1].pop();");
        auto newSize = engine.execute("data[\"arrays\"][1].size()");
        check_eq(newSize.as<int>(), 2);
    });
}

} // namespace jai::foundry::tests