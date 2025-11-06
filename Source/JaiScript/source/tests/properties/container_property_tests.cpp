#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/properties/macros.hpp>
#include <memory>
#include <vector>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Simple test class
class Cat {
public:
    std::string name;

    Cat() = default;
    explicit Cat(const std::string& n) : name(n) {}

    std::string meow_name() const {
        return "Meow! I'm " + name;
    }
};

// Test class with vector<Cat> property
class CatOwner {
public:
    std::vector<Cat> my_cats;

    CatOwner() = default;
};

class container_property_tests : public suite {
public:
    container_property_tests() : suite("Container Property Tests") {}

    void forge_tests() override {

        test("basic_vector_access", [this]() {
            // Test basic access to pre-populated vector
            auto eng = engine::make();

            class_builder<Cat>(*eng, "Cat")
                .constructor<>()
                .constructor<std::string>()
                .method("meow_name", &Cat::meow_name)
                .property("name", &Cat::name)
                .build();

            class_builder<CatOwner>(*eng, "CatOwner")
                .constructor<>()
                .property("my_cats", &CatOwner::my_cats)
                .build();

            // C++: Create and populate
            auto owner = std::make_shared<CatOwner>();
            owner->my_cats.emplace_back("Fred");
            owner->my_cats.emplace_back("Whiskers");
            eng->add_global("owner", eng->make_object(owner));

            // Script: Just access, don't modify yet
            auto result = eng->execute(R"(
                print("Size: " + owner.my_cats.size());
                print("First cat: " + owner.my_cats[0].name);
                owner.my_cats.size() == 2 && owner.my_cats[0].name == "Fred"
            )");

            check_eq(result.as<bool>(), true);
            std::cout << "✓ Basic vector access works" << std::endl;
        });

        test("vector_registration", [this]() {
            auto eng = engine::make();

            class_builder<Cat>(*eng, "Cat")
                .constructor<>()
                .constructor<std::string>()
                .method("meow_name", &Cat::meow_name)
                .property("name", &Cat::name)
                .build();

            // Should succeed - containers not validated at registration
            class_builder<CatOwner>(*eng, "CatOwner")
                .constructor<>()
                .property("my_cats", &CatOwner::my_cats)
                .build();

            std::cout << "✓ Can register vector<Cat> property" << std::endl;
        });

        test("cpp_populate_script_access", [this]() {
            auto eng = engine::make();

            class_builder<Cat>(*eng, "Cat")
                .constructor<>()
                .constructor<std::string>()
                .method("meow_name", &Cat::meow_name)
                .property("name", &Cat::name)
                .build();

            class_builder<CatOwner>(*eng, "CatOwner")
                .constructor<>()
                .property("my_cats", &CatOwner::my_cats)
                .build();

            // C++ populates
            auto owner = std::make_shared<CatOwner>();
            owner->my_cats.emplace_back("Fred");
            owner->my_cats.emplace_back("Whiskers");
            eng->add_global("owner", eng->make_object(owner));

            // Script accesses
            auto result = eng->execute(R"(
                owner.my_cats.size() == 2 &&
                owner.my_cats[0].name == "Fred"
            )");

            check_eq(result.as<bool>(), true);
            std::cout << "✓ C++ populates, script accesses" << std::endl;
        });

        test("script_populate_and_access", [this]() {
            auto eng = engine::make();

            class_builder<Cat>(*eng, "Cat")
                .constructor<>()
                .constructor<std::string>()
                .method("meow_name", &Cat::meow_name)
                .property("name", &Cat::name)
                .build();

            class_builder<CatOwner>(*eng, "CatOwner")
                .constructor<>()
                .property("my_cats", &CatOwner::my_cats)
                .build();

            // Script creates and populates
            auto result = eng->execute(R"(
                auto owner = CatOwner();
                owner.my_cats.push_back(Cat("Mittens"));
                owner.my_cats.push_back(Cat("Shadow"));
                owner.my_cats.size() == 2 &&
                owner.my_cats[0].meow_name() == "Meow! I'm Mittens"
            )");

            check_eq(result.as<bool>(), true);
            std::cout << "✓ Script creates and populates" << std::endl;
        });
    }
};

FOUNDRY_REGISTER(jai::foundry::tests::container_property_tests)

} // namespace jai::foundry::tests
