#include <iostream>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    jai::engine engine;
    
    // Register standard library functions
    jai::stdlib::register_all(engine);
    
    // Example 1: Convert simple data to JSON
    std::string script1 = R"(
        var player_data = {
            "name": "Hero",
            "level": 10,
            "health": 100.0,
            "inventory": ["sword", "shield", "potion"],
            "stats": {
                "strength": 15,
                "defense": 12,
                "magic": 8
            }
        };
        
        // Convert to compact JSON
        var compact = to_json(player_data);
        print("Compact JSON:", compact);
        
        // Convert to pretty JSON with 2-space indent
        var pretty = to_json(player_data, 2);
        print("\nPretty JSON:\n", pretty);
    )";
    
    engine.execute(script1);
    
    // Example 2: Working with heterogeneous arrays
    std::string script2 = R"(
        var mixed_data = [
            42,
            "hello",
            true,
            null,
            {"nested": "object"},
            [1, 2, 3]
        ];
        
        print("\nMixed array as JSON:", to_json(mixed_data));
    )";
    
    engine.execute(script2);
    
    // Example 3: Config file pattern
    std::string script3 = R"(
        var config = {
            "graphics": {
                "resolution": "1920x1080",
                "fullscreen": false,
                "vsync": true,
                "quality": "high"
            },
            "audio": {
                "master_volume": 0.8,
                "music_volume": 0.6,
                "sfx_volume": 1.0
            },
            "controls": {
                "forward": "W",
                "back": "S",
                "left": "A", 
                "right": "D",
                "jump": "Space"
            }
        };
        
        // Save config to JSON string (in real use, you'd save to file)
        var config_json = to_json(config, 4);
        print("\nGame Config:\n", config_json);
    )";
    
    engine.execute(script3);
    
    return 0;
}