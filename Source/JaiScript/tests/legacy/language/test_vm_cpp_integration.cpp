#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/jvm/vm_backend.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <memory>
#include <vector>

using namespace jai;
using namespace jai::jvm;
using namespace jai::test;

// Test classes for binding
class point {
public:
    script_float x, y;
    
    point() : x(0), y(0) {}
    point(script_float x_, script_float y_) : x(x_), y(y_) {}
    
    script_float distance() const {
        return std::sqrt(x * x + y * y);
    }
    
    point add(const point& other) const {
        return point(x + other.x, y + other.y);
    }
    
    void translate(script_float dx, script_float dy) {
        x += dx;
        y += dy;
    }
    
    script_string to_string() const {
        return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }
    
    static point origin() {
        return point(0, 0);
    }
};

class counter {
private:
    script_int count_;
public:
    counter() : count_(0) {}
    counter(script_int initial) : count_(initial) {}
    
    void increment() { count_++; }
    void decrement() { count_--; }
    script_int get() const { return count_; }
    void set(script_int value) { count_ = value; }
    void add(script_int amount) { count_ += amount; }
    
    counter& chain_increment() {
        count_++;
        return *this;
    }
};

class container {
private:
    std::vector<script_value> items_;
public:
    void push(const script_value& item) {
        items_.push_back(item);
    }
    
    script_value pop() {
        if (items_.empty()) return script_value();
        auto last = items_.back();
        items_.pop_back();
        return last;
    }
    
    script_int size() const {
        return static_cast<script_int>(items_.size());
    }
    
    script_value at(script_int index) const {
        if (index < 0 || index >= static_cast<script_int>(items_.size())) {
            throw std::out_of_range("Index out of bounds");
        }
        return items_[index];
    }
    
    void clear() {
        items_.clear();
    }
};

// Helper to create engine with VM backend and C++ bindings
std::unique_ptr<engine> create_vm_engine_with_bindings() {
    auto eng = std::make_unique<engine>();
    
    // Bind point class
    make_class_builder<point>(*eng, "point")
        .constructor<>()
        .constructor<script_float, script_float>()
        .property("x", &point::x)
        .property("y", &point::y)
        .method("distance", &point::distance)
        .method("add", &point::add)
        .method("translate", &point::translate)
        .method("to_string", &point::to_string)
        .static_method("origin", &point::origin)
        .build();
    
    // Bind counter class
    make_class_builder<counter>(*eng, "counter")
        .constructor<>()
        .constructor<script_int>()
        .method("increment", &counter::increment)
        .method("decrement", &counter::decrement)
        .method("get", &counter::get)
        .method("set", &counter::set)
        .method("add", &counter::add)
        .method("chain_increment", &counter::chain_increment)
        .build();
    
    // Bind container class
    make_class_builder<container>(*eng, "container")
        .constructor<>()
        .method("push", &container::push)
        .method("pop", &container::pop)
        .method("size", &container::size)
        .method("at", &container::at)
        .method("clear", &container::clear)
        .build();
    
    // Set VM backend
    eng->set_backend(create_vm_backend());
    
    return eng;
}

JAI_TEST_SUITE(VMCppIntegration)

// Basic Class Construction
JAI_TEST(vm_cpp_class_construction) {
    auto engine = create_vm_engine_with_bindings();
    
    // Default constructor
    auto result = engine->execute("var p = point(); p.x + p.y;");
    expect_eq(result.as<script_float>(), 0.0);
    
    // Parameterized constructor
    result = engine->execute("var p = point(3.0, 4.0); p.distance();");
    expect_near(result.as<script_float>(), 5.0, 0.001);
    
    // Multiple instances
    result = engine->execute(R"(
        var p1 = point(1.0, 2.0);
        var p2 = point(3.0, 4.0);
        p1.x + p2.y;
    )");
    expect_eq(result.as<script_float>(), 5.0);
}

// Property Access
JAI_TEST(vm_cpp_property_access) {
    auto engine = create_vm_engine_with_bindings();
    
    // Read properties
    auto result = engine->execute(R"(
        var p = point(10.0, 20.0);
        p.x;
    )");
    expect_eq(result.as<script_float>(), 10.0);
    
    // Write properties
    result = engine->execute(R"(
        var p = point();
        p.x = 5.0;
        p.y = 7.0;
        p.x + p.y;
    )");
    expect_eq(result.as<script_float>(), 12.0);
    
    // Property modification
    result = engine->execute(R"(
        var p = point(3.0, 4.0);
        p.x = p.x * 2;
        p.y = p.y + 1;
        p.distance();
    )");
    expect_near(result.as<script_float>(), std::sqrt(36.0 + 25.0), 0.001);
}

// Method Calls
JAI_TEST(vm_cpp_method_calls) {
    auto engine = create_vm_engine_with_bindings();
    
    // Simple method call
    auto result = engine->execute(R"(
        var c = counter(10);
        c.increment();
        c.get();
    )");
    expect_eq(result.as<script_int>(), 11);
    
    // Method with parameters
    result = engine->execute(R"(
        var c = counter(5);
        c.add(15);
        c.get();
    )");
    expect_eq(result.as<script_int>(), 20);
    
    // Method returning object
    result = engine->execute(R"(
        var p1 = point(1.0, 2.0);
        var p2 = point(3.0, 4.0);
        var p3 = p1.add(p2);
        p3.x + p3.y;
    )");
    expect_eq(result.as<script_float>(), 10.0);
}

// Static Methods
JAI_TEST(vm_cpp_static_methods) {
    auto engine = create_vm_engine_with_bindings();
    
    auto result = engine->execute(R"(
        var p = point.origin();
        p.x + p.y;
    )");
    expect_eq(result.as<script_float>(), 0.0);
}

// Method Chaining
JAI_TEST(vm_cpp_method_chaining) {
    auto engine = create_vm_engine_with_bindings();
    
    auto result = engine->execute(R"(
        var c = counter();
        c.chain_increment().chain_increment().chain_increment().get();
    )");
    expect_eq(result.as<script_int>(), 3);
}

// Complex Object Interactions
JAI_TEST(vm_cpp_complex_interactions) {
    auto engine = create_vm_engine_with_bindings();
    
    // Objects in containers
    auto result = engine->execute(R"(
        var cont = container();
        cont.push(point(1.0, 2.0));
        cont.push(point(3.0, 4.0));
        cont.push(point(5.0, 6.0));
        cont.size();
    )");
    expect_eq(result.as<script_int>(), 3);
    
    // Retrieving objects
    result = engine->execute(R"(
        var cont = container();
        var p = point(10.0, 20.0);
        cont.push(p);
        var retrieved = cont.pop();
        retrieved.distance();
    )");
    expect_near(result.as<script_float>(), std::sqrt(500.0), 0.001);
}

// Objects in Script Functions
JAI_TEST(vm_cpp_objects_in_functions) {
    auto engine = create_vm_engine_with_bindings();
    
    // Passing objects to functions
    auto result = engine->execute(R"(
        fun calculate_distance(p) {
            return p.distance();
        }
        
        var p = point(3.0, 4.0);
        calculate_distance(p);
    )");
    expect_eq(result.as<script_float>(), 5.0);
    
    // Returning objects from functions
    result = engine->execute(R"(
        fun make_point(x, y) {
            return point(x, y);
        }
        
        var p = make_point(6.0, 8.0);
        p.distance();
    )");
    expect_eq(result.as<script_float>(), 10.0);
}

// Objects in Arrays
JAI_TEST(vm_cpp_objects_in_arrays) {
    auto engine = create_vm_engine_with_bindings();
    
    auto result = engine->execute(R"(
        var points = [point(1.0, 1.0), point(2.0, 2.0), point(3.0, 3.0)];
        var sum = 0.0;
        for (var i = 0; i < points.size(); ++i) {
            sum = sum + points[i].x;
        }
        sum;
    )");
    expect_eq(result.as<script_float>(), 6.0);
}

// Objects in Maps
JAI_TEST(vm_cpp_objects_in_maps) {
    auto engine = create_vm_engine_with_bindings();
    
    auto result = engine->execute(R"(
        var map = {
            "origin": point(0.0, 0.0),
            "unit_x": point(1.0, 0.0),
            "unit_y": point(0.0, 1.0)
        };
        map["unit_x"].x + map["unit_y"].y;
    )");
    expect_eq(result.as<script_float>(), 2.0);
}

// Lambda Methods with C++ Objects
JAI_TEST(vm_cpp_objects_with_lambdas) {
    auto engine = create_vm_engine_with_bindings();
    
    // Using objects in closures
    auto result = engine->execute(R"(
        var p = point(5.0, 5.0);
        var get_distance = fun() { return p.distance(); };
        p.x = 3.0;
        p.y = 4.0;
        get_distance();
    )");
    expect_eq(result.as<script_float>(), 5.0);
    
    // Object methods in higher-order functions
    result = engine->execute(R"(
        fun apply_to_point(p, operation) {
            return operation(p);
        }
        
        var p = point(10.0, 20.0);
        apply_to_point(p, fun(pt) { return pt.x * pt.y; });
    )");
    expect_eq(result.as<script_float>(), 200.0);
}

// Error Handling with C++ Objects
JAI_TEST(vm_cpp_object_errors) {
    auto engine = create_vm_engine_with_bindings();
    
    // Invalid method calls
    try {
        engine->execute("var p = point(); p.invalid_method();");
        expect_true(false); // Should throw
    } catch (const runtime_error&) {
        expect_true(true);
    }
    
    // Out of bounds access
    try {
        engine->execute(R"(
            var cont = container();
            cont.push(1);
            cont.at(10);
        )");
        expect_true(false); // Should throw
    } catch (const std::exception&) {
        expect_true(true);
    }
}

// Performance Pattern - Object Pooling
JAI_TEST(vm_cpp_object_pooling) {
    auto engine = create_vm_engine_with_bindings();
    
    auto result = engine->execute(R"(
        var pool = [];
        for (var i = 0; i < 10; ++i) {
            pool.push(point(i, i));
        }
        
        var sum = 0.0;
        for (var i = 0; i < pool.size(); ++i) {
            sum = sum + pool[i].distance();
        }
        sum > 0.0;
    )");
    expect_true(result.as<script_bool>());
}

// Smart Pointer Integration (if supported)
JAI_TEST(vm_cpp_smart_pointers) {
    auto engine = create_vm_engine_with_bindings();
    
    // Shared ownership
    auto result = engine->execute(R"(
        var p1 = point(1.0, 2.0);
        var p2 = p1; // Should handle reference/copy correctly
        p1.x = 10.0;
        p2.x; // Depends on value/reference semantics
    )");
    // The expected value depends on whether objects are passed by value or reference
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()