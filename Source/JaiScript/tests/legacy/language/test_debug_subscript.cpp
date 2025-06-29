#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"

using namespace jai;
using namespace jai::test;

class MyVector {
public:
    std::vector<int> data;
    MyVector() = default;
    int& operator[](size_t index) { return data[index]; }
    void push_back(int value) { data.push_back(value); }
};

JAI_TEST_SUITE(DebugSubscript)

JAI_TEST(separate_execution_works) {
    engine engine;
    
    make_class_builder<MyVector>(engine, "MyVector")
        .constructor<>()
        .method("push_back", &MyVector::push_back);
    
    engine.add_function("[]", [](MyVector& vec, int index) -> int {
        return vec[index];
    });
    
    // Execute each part separately
    engine.execute("var arr = [10, 20, 30];");
    engine.execute("var a1 = arr[1];");
    
    std::cout << "Creating MyVector..." << std::endl;
    engine.execute("var vec = MyVector();");
    std::cout << "Adding elements..." << std::endl;
    engine.execute("vec.push_back(100);");
    engine.execute("vec.push_back(200);");
    std::cout << "About to call subscript..." << std::endl;
    engine.execute("var v1 = vec[1];");
    
    expect_eq(engine.get_variable("a1").as<int>(), 20);
    expect_eq(engine.get_variable("v1").as<int>(), 200);
}

JAI_TEST(combined_execution_fails) {
    engine engine;
    
    make_class_builder<MyVector>(engine, "MyVector")
        .constructor<>()
        .method("push_back", &MyVector::push_back);
    
    engine.add_function("[]", [](MyVector& vec, int index) -> int {
        return vec[index];
    });
    
    // Execute everything in one script - should fail
    engine.execute(R"(
        var arr = [10, 20, 30];
        var a1 = arr[1];
        
        var vec = MyVector();
        vec.push_back(100);
        vec.push_back(200);
        var v1 = vec[1];
    )");
    
    expect_eq(engine.get_variable("a1").as<int>(), 20);
    expect_eq(engine.get_variable("v1").as<int>(), 200);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()