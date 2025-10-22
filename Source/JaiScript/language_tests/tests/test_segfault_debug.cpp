#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/bound_array.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/core/engine_make_object.hpp>
#include <iostream>

using namespace jai;
using namespace jai::foundry;

// Minimal test widget
struct SimpleWidget {
    std::string name;
    int value;

    SimpleWidget(const std::string& n, int v) : name(n), value(v) {
        std::cout << "SimpleWidget CTOR: " << name << " = " << value << std::endl;
    }

    SimpleWidget(const SimpleWidget& other) : name(other.name), value(other.value) {
        std::cout << "SimpleWidget COPY CTOR: " << name << " = " << value << std::endl;
    }

    ~SimpleWidget() {
        std::cout << "SimpleWidget DTOR: " << name << std::endl;
    }
};

int main() {
    std::cout << "=== Test 1: Create engine and widget ===" << std::endl;
    auto eng = engine::make();

    std::cout << "=== Test 2: Register SimpleWidget class ===" << std::endl;
    class_builder<SimpleWidget>(*eng, "SimpleWidget")
        .constructor<std::string, int>()
        .property("name", &SimpleWidget::name)
        .property("value", &SimpleWidget::value)
        .build();

    std::cout << "=== Test 3: Create script array with objects ===" << std::endl;
    script_value arr_val = eng->make_array();
    auto& arr = arr_val.as_array();

    std::cout << "=== Test 4: Add objects to array ===" << std::endl;
    arr.push_back(eng->make_object<SimpleWidget>("Button", 100));
    arr.push_back(eng->make_object<SimpleWidget>("Label", 200));

    std::cout << "=== Test 5: Check engine_ref on array elements ===" << std::endl;
    for (size_t i = 0; i < arr.size(); ++i) {
        std::cout << "Element " << i << " has valid engine_ref: "
                  << (arr[i].has_valid_engine_ref() ? "YES" : "NO") << std::endl;
    }

    std::cout << "=== Test 6: Create bound_array reference wrapper ===" << std::endl;
    bound_array<SimpleWidget> widgets_ref(arr_val.as_array(), eng->weak_from_this());
    std::cout << "widgets_ref size: " << widgets_ref.size() << std::endl;

    std::cout << "=== Test 7: Try to copy bound_array ===" << std::endl;
    try {
        bound_array<SimpleWidget> widgets_copy(widgets_ref);
        std::cout << "SUCCESS: bound_array copy succeeded, size: " << widgets_copy.size() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR: bound_array copy failed: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "=== Test 8: Try to pass bound_array by value to lambda ===" << std::endl;
    try {
        auto process = [](bound_array<SimpleWidget> widgets) {
            std::cout << "Inside lambda, size: " << widgets.size() << std::endl;
            return widgets.size();
        };

        size_t count = process(widgets_ref);
        std::cout << "SUCCESS: Lambda returned count: " << count << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR: Lambda failed: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "=== All tests passed! ===" << std::endl;
    return 0;
}
