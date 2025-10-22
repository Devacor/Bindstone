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

    // First test: Create owned object with make_object (baseline)
    std::cout << "=== Test 1: Owned object (baseline) ===" << std::endl;
    auto owned_atlas = std::make_shared<TextureAtlas>();
    owned_atlas->name = "OwnedAtlas";
    js->add_global("owned", js->make_object(owned_atlas));

    try {
        std::cout << "About to execute script..." << std::endl;
        auto result = js->execute("42");
        std::cout << "Simple script works: " << result.as<int>() << std::endl;

        result = js->execute("owned.texture_count");
        std::cout << "✓ Owned object access works: " << result.as<int>() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ Owned object failed: " << e.what() << std::endl;
        return 1;
    }

    // Second test: Non-owned object via add_global_ref
    std::cout << "\n=== Test 2: Non-owned object via add_global_ref ===" << std::endl;
    TextureAtlas atlas;
    atlas.name = "MyAtlas";

    std::cout << "Initial C++ state:" << std::endl;
    std::cout << "  name: " << atlas.name << std::endl;
    std::cout << "  texture_count: " << atlas.texture_count << std::endl;

    js->add_global_ref("atlas", atlas);

    const char* script = R"(
        atlas.name = "UpdatedFromScript";
        atlas.load_texture("player.png");
        atlas.load_texture("enemy.png");
        atlas.get_count()
    )";

    try {
        auto result = js->execute(script);

        std::cout << "\nAfter script execution:" << std::endl;
        std::cout << "  C++ name: " << atlas.name << std::endl;
        std::cout << "  C++ texture_count: " << atlas.texture_count << std::endl;
        std::cout << "  Script result: " << result.as<int>() << std::endl;

        if (atlas.name == "UpdatedFromScript" && atlas.texture_count == 2 && result.as<int>() == 2) {
            std::cout << "\n✓ SUCCESS: add_global_ref works with custom objects!" << std::endl;
            return 0;
        } else {
            std::cout << "\n✗ FAIL: State not synchronized" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "\n✗ ERROR: " << e.what() << std::endl;
        return 1;
    }
}
