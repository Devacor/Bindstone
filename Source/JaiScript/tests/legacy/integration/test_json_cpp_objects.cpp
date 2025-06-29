#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <cmath>

using namespace jai;
using namespace jai::test;

// Test class with properties
class Point {
public:
    Point() : x(0.0), y(0.0) {}
    Point(double x, double y) : x(x), y(y) {}
    
    double x;
    double y;
    
    double distance() const {
        return std::sqrt(x * x + y * y);
    }
};

// More complex class
class Player {
public:
    Player() : name("Unknown"), health(100), level(1) {}
    Player(const std::string& name) : name(name), health(100), level(1) {}
    
    std::string name;
    int health;
    int level;
    bool alive = true;
    
    void takeDamage(int damage) {
        health -= damage;
        if (health <= 0) {
            health = 0;
            alive = false;
        }
    }
};

JAI_TEST_SUITE(JSONCppObjectTests)

JAI_TEST(to_json_cpp_simple_object) {
    engine engine;
    stdlib::register_all(engine);
    
    // Register Point class
    make_class_builder<Point>(engine, "Point")
        .constructor<>()
        .constructor<double, double>()
        .property("x", &Point::x)
        .property("y", &Point::y)
        .method("distance", &Point::distance)
        .build();
    
    // Create and serialize a point
    std::string script = R"(
        var p = Point(3.0, 4.0);
        to_json(p)
    )";
    
    script_value result = engine.execute(script);
    std::string json = result.as_string();
    
    // Should contain type and properties
    expect_true(json.find("\"_type_\":\"Point\"") != std::string::npos);
    expect_true(json.find("\"x\":3.0") != std::string::npos);
    expect_true(json.find("\"y\":4.0") != std::string::npos);
}

JAI_TEST(to_json_cpp_complex_object) {
    engine engine;
    stdlib::register_all(engine);
    
    // Register Player class
    make_class_builder<Player>(engine, "Player")
        .constructor<>()
        .constructor<const std::string&>()
        .property("name", &Player::name)
        .property("health", &Player::health)
        .property("level", &Player::level)
        .property("alive", &Player::alive)
        .method("takeDamage", &Player::takeDamage)
        .build();
    
    // Create and serialize a player
    std::string script = R"(
        var player = Player("Hero");
        player.takeDamage(30);
        to_json(player, 2)
    )";
    
    script_value result = engine.execute(script);
    std::string json = result.as_string();
    
    // Should contain type and all properties with pretty printing
    expect_true(json.find("\"_type_\": \"Player\"") != std::string::npos);
    expect_true(json.find("\"name\": \"Hero\"") != std::string::npos);
    expect_true(json.find("\"health\": 70") != std::string::npos);
    expect_true(json.find("\"level\": 1") != std::string::npos);
    expect_true(json.find("\"alive\": true") != std::string::npos);
    expect_true(json.find("\n") != std::string::npos); // Pretty printed
}

JAI_TEST(to_json_cpp_object_in_container) {
    engine engine;
    stdlib::register_all(engine);
    
    // Register Point class
    make_class_builder<Point>(engine, "Point")
        .constructor<>()
        .constructor<double, double>()
        .property("x", &Point::x)
        .property("y", &Point::y)
        .build();
    
    // Create array of points and map with points
    std::string script = R"(
        var points = [Point(1.0, 2.0), Point(3.0, 4.0), Point(5.0, 6.0)];
        var data = {
            "origin": Point(0.0, 0.0),
            "waypoints": points
        };
        to_json(data)
    )";
    
    script_value result = engine.execute(script);
    std::string json = result.as_string();
    
    // Should serialize nested objects properly
    expect_true(json.find("\"origin\":{\"_type_\":\"Point\",\"x\":0.0,\"y\":0.0}") != std::string::npos);
    expect_true(json.find("\"waypoints\":[") != std::string::npos);
    expect_true(json.find("{\"_type_\":\"Point\",\"x\":1.0,\"y\":2.0}") != std::string::npos);
}

JAI_TEST(to_json_cpp_object_default_constructor) {
    engine engine;
    stdlib::register_all(engine);
    
    // Register Player class
    make_class_builder<Player>(engine, "Player")
        .constructor<>()
        .property("name", &Player::name)
        .property("health", &Player::health)
        .property("level", &Player::level)
        .property("alive", &Player::alive)
        .build();
    
    // Create with default constructor
    std::string script = R"(
        var player = Player();
        to_json(player)
    )";
    
    script_value result = engine.execute(script);
    std::string json = result.as_string();
    
    // Should have default values
    expect_true(json.find("\"name\":\"Unknown\"") != std::string::npos);
    expect_true(json.find("\"health\":100") != std::string::npos);
    expect_true(json.find("\"level\":1") != std::string::npos);
    expect_true(json.find("\"alive\":true") != std::string::npos);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()