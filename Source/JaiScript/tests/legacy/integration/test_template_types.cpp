#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <memory>

using namespace jai;
using namespace jai::test;

// Template test class
template<typename T>
class Point {
public:
    Point() : x(T(0)), y(T(0)) {}
    Point(T x_, T y_) : x(x_), y(y_) {}
    
    T getX() const { return x; }
    T getY() const { return y; }
    void setX(T newX) { x = newX; }
    void setY(T newY) { y = newY; }
    
    T x, y;
};

// Wrapper template similar to SafeComponent
template<typename T>
class SafeComponent {
public:
    SafeComponent() : ptr_(std::make_shared<T>()) {}
    explicit SafeComponent(std::shared_ptr<T> ptr) : ptr_(ptr) {}
    
    std::shared_ptr<T> self() const { return ptr_; }
    T* operator->() { return ptr_.get(); }
    const T* operator->() const { return ptr_.get(); }
    
private:
    std::shared_ptr<T> ptr_;
};

// Simple button class for testing
class Button {
public:
    Button() : text_("") {}
    explicit Button(const std::string& text) : text_(text) {}
    
    void setText(const std::string& text) { text_ = text; }
    std::string getText() const { return text_; }
    
private:
    std::string text_;
};

JAI_TEST_SUITE(TemplateTypes)

JAI_TEST(point_int_template) {
    engine engine;
    
    // Register Point<int> as a concrete type
    make_class_builder<Point<int>>(engine, "Point<int>")
        .constructor<>()
        .constructor<int, int>()
        .method("getX", &Point<int>::getX)
        .method("getY", &Point<int>::getY)
        .method("setX", &Point<int>::setX)
        .method("setY", &Point<int>::setY)
        .build();
    
    // Test constructor with no args
    std::string script1 = R"(
        auto p1 = Point<int>();
        p1
    )";
    
    script_value result1 = engine.execute(script1);
    expect_true(result1.is_object());
    
    // Test constructor with args
    std::string script2 = R"(
        auto p2 = Point<int>(3, 4);
        p2.getX()
    )";
    
    script_value result2 = engine.execute(script2);
    expect_eq(result2.as<int>(), 3);
}

JAI_TEST(point_float_template) {
    engine engine;
    
    // Register Point<float> as a concrete type
    make_class_builder<Point<float>>(engine, "Point<float>")
        .constructor<>()
        .constructor<float, float>()
        .method("getX", &Point<float>::getX)
        .method("getY", &Point<float>::getY)
        .method("setX", &Point<float>::setX)
        .method("setY", &Point<float>::setY)
        .build();
    
    // Test with float template
    std::string script = R"(
        auto p = Point<float>(3.14, 2.71);
        p.getX() + p.getY()
    )";
    
    script_value result = engine.execute(script);
    expect_near(result.as<float>(), 5.85f, 0.01f);
}

JAI_TEST(multiple_template_types_in_same_engine) {
    engine engine;
    
    // Register both Point<int> and Point<float>
    make_class_builder<Point<int>>(engine, "Point<int>")
        .constructor<>()
        .constructor<int, int>()
        .method("getX", &Point<int>::getX)
        .method("getY", &Point<int>::getY)
        .build();
        
    make_class_builder<Point<float>>(engine, "Point<float>")
        .constructor<>()
        .constructor<float, float>()
        .method("getX", &Point<float>::getX)
        .method("getY", &Point<float>::getY)
        .build();
    
    // Test using both types
    std::string script = R"(
        auto pi = Point<int>(10, 20);
        auto pf = Point<float>(1.5, 2.5);
        pi.getX() + pf.getX()
    )";
    
    script_value result = engine.execute(script);
    expect_near(result.as<float>(), 11.5f, 0.01f);
}

JAI_TEST(safe_component_template) {
    engine engine;
    
    // Register Button first
    make_class_builder<Button>(engine, "Button")
        .constructor<>()
        .constructor<std::string>()
        .method("setText", &Button::setText)
        .method("getText", &Button::getText)
        .build();
    
    // Register SafeComponent<Button>
    make_class_builder<SafeComponent<Button>>(engine, "SafeComponent<Button>")
        .constructor<>()
        .method("self", &SafeComponent<Button>::self)
        .build();
    
    // Test SafeComponent usage
    std::string script = R"(
        auto sc = SafeComponent<Button>();
        auto btn = sc.self();
        btn.setText("Hello");
        btn.getText()
    )";
    
    script_value result = engine.execute(script);
    expect_eq(result.as<std::string>(), "Hello");
}

JAI_TEST(template_type_in_variable_declaration) {
    engine engine;
    
    // Register Point<int>
    make_class_builder<Point<int>>(engine, "Point<int>")
        .constructor<>()
        .constructor<int, int>()
        .method("getX", &Point<int>::getX)
        .method("getY", &Point<int>::getY)
        .build();
    
    // First test basic typed declaration
    try {
        engine.execute("int x = 42; x");
    } catch (const std::exception& e) {
        std::cout << "Basic typed declaration failed: " << e.what() << std::endl;
    }
    
    // Test explicit type declaration
    std::string script = R"(
        Point<int> p = Point<int>(5, 10);
        p.getX() + p.getY()
    )";
    
    script_value result = engine.execute(script);
    expect_eq(result.as<int>(), 15);
}

JAI_TEST(template_type_brace_initialization) {
    engine engine;
    
    // Register Point<float>
    make_class_builder<Point<float>>(engine, "Point<float>")
        .constructor<>()
        .constructor<float, float>()
        .property("x", &Point<float>::x)
        .property("y", &Point<float>::y)
        .build();
    
    // Test brace initialization
    std::string script = R"(
        auto p = Point<float>{};
        p.x + p.y
    )";
    
    script_value result = engine.execute(script);
    expect_near(result.as<float>(), 0.0f, 0.01f);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()