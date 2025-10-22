#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <iostream>
#include <string>

// Simple custom class to test
class TextureAtlas {
public:
    int texture_count = 0;
    std::string name = "default";

    void load_texture(const std::string& tex_name) {
        std::cout << "Loading texture: " << tex_name << std::endl;
        texture_count++;
    }

    int get_count() const {
        return texture_count;
    }
};

int main() {
    auto js = jai::engine::make();

    // Register the class
    jai::class_builder<TextureAtlas>(*js, "TextureAtlas")
        .constructor<>()
        .property("texture_count", &TextureAtlas::texture_count)
        .property("name", &TextureAtlas::name)
        .method("load_texture", &TextureAtlas::load_texture)
        .method("get_count", &TextureAtlas::get_count)
        .build();

    std::cout << "Class registered successfully" << std::endl;

    // Test 1: Create object owned by shared_ptr
    std::cout << "\n=== Test 1: Owned by shared_ptr ===" << std::endl;
    auto atlas = std::make_shared<TextureAtlas>();
    atlas->name = "SharedAtlas";
    atlas->texture_count = 5;

    std::cout << "C++ initial state: name=" << atlas->name << ", count=" << atlas->texture_count << std::endl;

    js->add_global("atlas", js->make_object(atlas));
    std::cout << "Added to globals" << std::endl;

    try {
        std::cout << "Executing: atlas.texture_count" << std::endl;
        auto result = js->execute("atlas.texture_count");
        std::cout << "Result: " << result.as<int>() << std::endl;

        if (result.as<int>() == 5) {
            std::cout << "✓ Reading property works!" << std::endl;
        } else {
            std::cout << "✗ Wrong value" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "✗ ERROR: " << e.what() << std::endl;
        return 1;
    }

    // Test 2: Modify via script
    std::cout << "\n=== Test 2: Modify via script ===" << std::endl;
    try {
        std::cout << "Executing: atlas.texture_count = 10" << std::endl;
        js->execute("atlas.texture_count = 10");

        std::cout << "C++ state after script: count=" << atlas->texture_count << std::endl;

        if (atlas->texture_count == 10) {
            std::cout << "✓ Writing property works!" << std::endl;
        } else {
            std::cout << "✗ Value not updated in C++" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "✗ ERROR: " << e.what() << std::endl;
        return 1;
    }

    // Test 3: Call method
    std::cout << "\n=== Test 3: Call method ===" << std::endl;
    try {
        std::cout << "Executing: atlas.load_texture(\"player.png\")" << std::endl;
        js->execute("atlas.load_texture(\"player.png\")");

        std::cout << "C++ state after method call: count=" << atlas->texture_count << std::endl;

        if (atlas->texture_count == 11) {
            std::cout << "✓ Method call works!" << std::endl;
        } else {
            std::cout << "✗ Method didn't execute properly" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "✗ ERROR: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n✓✓✓ ALL TESTS PASSED ✓✓✓" << std::endl;
    return 0;
}
