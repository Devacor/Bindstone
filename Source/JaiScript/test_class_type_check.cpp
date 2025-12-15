#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    auto eng = jai::engine::make();

    // Define two different classes
    eng->execute(R"(
        class Dog { var name = "Fido"; }
        class Cat { var name = "Whiskers"; }
    )");

    std::cout << "Test 1: Can we assign Cat to a Dog variable (auto)?" << std::endl;
    try {
        auto result = eng->execute(R"(
            auto myPet = Dog();
            myPet = Cat();  // Should this fail?
            myPet.name
        )");
        std::cout << "  ALLOWED: Cat assigned to Dog variable, name = " << result.as_string() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  REJECTED: " << e.what() << std::endl;
    }

    std::cout << "\nTest 2: Can we assign Cat to a Dog variable (var)?" << std::endl;
    try {
        auto result = eng->execute(R"(
            var myPet2 = Dog();
            myPet2 = Cat();  // var allows any type
            myPet2.name
        )");
        std::cout << "  ALLOWED: Cat assigned to Dog (var), name = " << result.as_string() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  REJECTED: " << e.what() << std::endl;
    }

    std::cout << "\nTest 3: Class inheritance check" << std::endl;
    try {
        auto result = eng->execute(R"(
            class Animal { var species = "unknown"; }
            class Bird : Animal { var canFly = true; }

            auto pet = Animal();
            pet = Bird();  // Should be allowed (Bird IS-A Animal)
            pet.species
        )");
        std::cout << "  ALLOWED: Bird assigned to Animal variable, species = " << result.as_string() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  REJECTED: " << e.what() << std::endl;
    }

    std::cout << "\nTest 4: Reverse inheritance (should fail)" << std::endl;
    try {
        auto result = eng->execute(R"(
            class Animal2 { var species = "unknown"; }
            class Bird2 : Animal2 { var canFly = true; }

            auto bird = Bird2();
            bird = Animal2();  // Should fail (Animal is NOT-A Bird)
            bird.canFly
        )");
        std::cout << "  ALLOWED: Animal assigned to Bird variable, canFly = " << result.as_bool() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  REJECTED: " << e.what() << std::endl;
    }

    return 0;
}
