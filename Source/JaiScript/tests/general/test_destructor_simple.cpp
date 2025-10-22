#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    auto js = jai::engine::make();

    // Track destructor calls
    int dtor_count = 0;

    // Add getter and setter functions for dtor_count
    js->add_function("get_dtor_count", [&dtor_count]() {
        return dtor_count;
    });
    js->add_function("inc_dtor_count", [&dtor_count]() {
        dtor_count++;
    });

    js->add_function("print", [](const std::string& msg) {
        std::cout << msg << std::endl;
    });

    const char* script_part1 = R"(
        class Resource {
            string name = "";

            Resource(string n) {
                name = n;
            }

            ~Resource() {
                print("Destructor called for: " + name);
                inc_dtor_count();
            }
        }

        {
            auto r1 = Resource("file1");
            auto r2 = Resource("file2");
        }
    )";

    const char* script_part2 = R"(
        get_dtor_count()
    )";

    try {
        js->execute(script_part1);
        std::cout << "=== After execute(script_part1), C++ dtor_count: " << dtor_count << " ===" << std::endl;

        auto result = js->execute(script_part2);
        std::cout << "=== After execute(script_part2) ===" << std::endl;
        std::cout << "Result type: " << (result.is_int() ? "int" : result.is_float() ? "float" : "other") << std::endl;
        std::cout << "Result is_null: " << result.is_null() << std::endl;
        std::cout << "C++ dtor_count value: " << dtor_count << std::endl;

        // Debug: check the int64 value before conversion
        auto int64_val = result.as<int64_t>();
        std::cout << "Result as int64_t: " << int64_val << std::endl;

        int count = result.as<int>();

        std::cout << "\nDestructor count: " << count << std::endl;

        if (count == 2) {
            std::cout << "✓ SUCCESS: Both destructors called on scope exit!" << std::endl;
            return 0;
        } else {
            std::cout << "✗ FAIL: Expected 2 destructors, got " << count << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "✗ ERROR: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "=== End of main() ===" << std::endl;
}
