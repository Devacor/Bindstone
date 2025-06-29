// Minimal test to measure template instantiation overhead
#include <iostream>
#include <chrono>

// Simulate JaiScript's approach (minimal templates)
namespace jaiscript_style {
    class engine {
        void* bindings[100];
        int count = 0;
    public:
        template<typename Func>
        void add_function(const char* name, Func f) {
            // Simple storage - minimal template instantiation
            bindings[count++] = reinterpret_cast<void*>(&f);
        }
    };
}

// Simulate ChaiScript's approach (heavy templates)
namespace chaiscript_style {
    template<typename T> struct remove_const { typedef T type; };
    template<typename T> struct remove_const<const T> { typedef T type; };
    template<typename T> struct function_traits;
    
    template<typename R, typename... Args>
    struct function_traits<R(Args...)> {
        using return_type = R;
        static constexpr size_t arity = sizeof...(Args);
    };
    
    template<typename Func>
    class bound_function {
        Func f;
    public:
        bound_function(Func func) : f(func) {}
        
        template<typename... Args>
        auto operator()(Args&&... args) -> decltype(f(std::forward<Args>(args)...)) {
            return f(std::forward<Args>(args)...);
        }
    };
    
    class engine {
        void* bindings[100];
        int count = 0;
    public:
        template<typename Func>
        void add_function(const char* name, Func f) {
            // Heavy template instantiation per function type
            using traits = function_traits<Func>;
            auto bound = new bound_function<Func>(f);
            bindings[count++] = bound;
        }
    };
}

// Test functions with different signatures to force template instantiation
int func1(int a) { return a * 2; }
float func2(float a, float b) { return a + b; }
double func3(double a, double b, double c) { return a * b + c; }
bool func4(int a, int b) { return a > b; }
void func5(int& a) { a++; }

int main() {
    std::cout << "=== Template Instantiation Test ===" << std::endl;
    
    // Test JaiScript style
    {
        jaiscript_style::engine e;
        // Register functions - minimal template instantiation
        e.add_function("func1", func1);
        e.add_function("func2", func2);
        e.add_function("func3", func3);
        e.add_function("func4", func4);
        e.add_function("func5", func5);
        
        // Register lambdas with different signatures
        for (int i = 0; i < 20; i++) {
            e.add_function("lambda", [i](int x) { return x + i; });
        }
    }
    
    // Test ChaiScript style
    {
        chaiscript_style::engine e;
        // Same registrations but with heavy template instantiation
        e.add_function("func1", func1);
        e.add_function("func2", func2);
        e.add_function("func3", func3);
        e.add_function("func4", func4);
        e.add_function("func5", func5);
        
        // Each lambda has different capture, forcing new instantiation
        for (int i = 0; i < 20; i++) {
            e.add_function("lambda", [i](int x) { return x + i; });
        }
    }
    
    std::cout << "Compile this file to see the difference in compilation time!" << std::endl;
    return 0;
}