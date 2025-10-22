#include <iostream>
#include <memory>
#include <concepts>

class engine {};

// Type trait to detect if a type has a constructor that takes std::weak_ptr<engine>
template<typename T>
concept has_engine_constructor = requires(std::weak_ptr<engine> eng) {
    T(eng);
};

struct WithEngine {
    explicit WithEngine(std::weak_ptr<engine> eng) {}
};

struct WithoutEngine {
    WithoutEngine() = default;
};

int main() {
    std::cout << "WithEngine has engine constructor: " << has_engine_constructor<WithEngine> << std::endl;
    std::cout << "WithoutEngine has engine constructor: " << has_engine_constructor<WithoutEngine> << std::endl;
    
    if constexpr (has_engine_constructor<WithEngine>) {
        std::cout << "Creating WithEngine with engine reference" << std::endl;
        auto eng = std::make_shared<engine>();
        WithEngine w(std::weak_ptr<engine>(eng));
    }
    
    if constexpr (has_engine_constructor<WithoutEngine>) {
        std::cout << "This should not print" << std::endl;
    } else {
        std::cout << "Creating WithoutEngine with default constructor" << std::endl;
        WithoutEngine w;
    }
    
    return 0;
}