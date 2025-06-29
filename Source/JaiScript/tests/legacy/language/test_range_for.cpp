#include "../jai_test.hpp"
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::test;

// Helper to check parsing succeeds
void assertParseSucceeds(const std::string& code) {
    lexer lexer(code, "test.jai");
    auto tokens = lexer.tokenize();
    parser parser(tokens, "test.jai");
    auto ast = parser.parse();
    expect_false(parser.has_errors());
}

// Helper to check parsing fails
void assertParseFails(const std::string& code) {
    lexer lexer(code, "test.jai");
    auto tokens = lexer.tokenize();
    parser parser(tokens, "test.jai");
    auto ast = parser.parse();
    expect_true(parser.has_errors());
}

JAI_TEST_SUITE(RangeBasedForLoops)

JAI_TEST(basic_range_for_auto) {
    assertParseSucceeds("for (auto x : array) { }");
    assertParseSucceeds("for (auto element : collection) { print(element); }");
    assertParseSucceeds("for (auto item : items) sum += item;");
}

JAI_TEST(range_for_with_array_literals) {
    assertParseSucceeds("for (auto x : {1, 2, 3, 4, 5}) { }");
    assertParseSucceeds("for (auto str : {\"hello\", \"world\"}) { print(str); }");
}

JAI_TEST(range_for_with_expressions) {
    assertParseSucceeds("for (auto x : getArray()) { }");
    assertParseSucceeds("for (auto val : obj.getValues()) { process(val); }");
    assertParseSucceeds("for (auto item : container[index]) { }");
}

JAI_TEST(range_for_auto_reference) {
    assertParseSucceeds("for (auto& x : array) { }");
    assertParseSucceeds("for (auto& element : collection) { element *= 2; }");
    assertParseSucceeds("for (auto& item : items) modify(item);");
}

JAI_TEST(range_for_const_auto_reference) {
    assertParseSucceeds("for (const auto& x : array) { }");
    assertParseSucceeds("for (const auto& element : collection) { print(element); }");
    assertParseSucceeds("for (const auto& item : items) total += item.value;");
}

JAI_TEST(range_for_mixed_reference_types) {
    assertParseSucceeds(R"(
        for (auto& x : mutableArray) {
            x *= 2;
            for (const auto& y : constArray) {
                sum += x * y;
            }
        }
    )");
}

JAI_TEST(range_for_specific_types) {
    // Integer types
    assertParseSucceeds("for (int x : numbers) { }");
    assertParseSucceeds("for (int value : array) { sum += value; }");
    assertParseSucceeds("for (int& n : integers) { n++; }");
    assertParseSucceeds("for (const int& num : values) { print(num); }");
    
    // script_float types
    assertParseSucceeds("for (float x : floats) { }");
    assertParseSucceeds("for (float& value : measurements) { value *= 1.5; }");
    assertParseSucceeds("for (const float& f : data) { total += f; }");
    
    // script_string types
    assertParseSucceeds("for (string s : names) { }");
    assertParseSucceeds("for (string& str : strings) { str = upper(str); }");
    assertParseSucceeds("for (const string& text : messages) { print(text); }");
    
    // Custom types
    assertParseSucceeds("for (Point p : points) { }");
    assertParseSucceeds("for (GameObject& obj : objects) { obj.update(); }");
    assertParseSucceeds("for (const Entity& e : entities) { render(e); }");
}

JAI_TEST(nested_range_for_loops) {
    assertParseSucceeds(R"(
        for (auto row : matrix) {
            for (auto cell : row) {
                print(cell);
            }
        }
    )");
    
    assertParseSucceeds(R"(
        for (const auto& group : groups) {
            for (int value : group.values) {
                sum += value;
            }
        }
    )");
    
    assertParseSucceeds(R"(
        for (auto x : dimension1) {
            for (auto y : dimension2) {
                for (auto z : dimension3) {
                    cube[x][y][z] = process(x, y, z);
                }
            }
        }
    )");
}

JAI_TEST(mixed_traditional_and_range_for) {
    assertParseSucceeds(R"(
        for (int i = 0; i < n; i++) {
            for (auto& item : containers[i]) {
                item.index = i;
            }
        }
    )");
    
    assertParseSucceeds(R"(
        for (auto& container : containers) {
            for (int i = 0; i < container.size(); i++) {
                container[i] *= 2;
            }
        }
    )");
}

JAI_TEST(range_for_with_containers) {
    assertParseSucceeds("for (auto x : array<int>) { }");
    assertParseSucceeds("for (auto& element : array<float>) { element *= 2.0; }");
    assertParseSucceeds("for (const auto& item : array<string>) { print(item); }");
    
    assertParseSucceeds("for (auto pair : map<string, int>) { }");
    assertParseSucceeds("for (auto& kv : scores) { kv.second += 10; }");
    assertParseSucceeds("for (const auto& entry : database) { process(entry.first, entry.second); }");
}

JAI_TEST(nested_container_range_for) {
    assertParseSucceeds(R"(
        for (auto& row : array<array<int>>) {
            for (auto& cell : row) {
                cell = 0;
            }
        }
    )");
    
    assertParseSucceeds(R"(
        for (const auto& mapEntry : map<string, array<int>>) {
            for (int value : mapEntry.second) {
                sum += value;
            }
        }
    )");
}

JAI_TEST(range_for_with_control_flow) {
    assertParseSucceeds(R"(
        for (auto x : values) {
            if (x < 0) continue;
            if (x > 100) break;
            sum += x;
        }
    )");
}

JAI_TEST(range_for_in_class_methods) {
    assertParseSucceeds(R"(
        class Container {
            array<int> data;
            
            void processAll() {
                for (auto& item : data) {
                    item = transform(item);
                }
            }
            
            int sum() {
                int total = 0;
                for (const auto& value : data) {
                    total += value;
                }
                return total;
            }
        }
    )");
}

JAI_TEST(range_for_with_lambdas) {
    assertParseSucceeds(R"(
        auto process = [](array<int> arr) {
            for (auto x : arr) {
                print(x);
            }
        };
    )");
    
    assertParseSucceeds(R"(
        for (auto& item : items) {
            auto modifier = [&item](int delta) {
                item.value += delta;
            };
            modifier(10);
        }
    )");
}

JAI_TEST(complex_range_for_example) {
    assertParseSucceeds(R"(
        class GameWorld {
            array<GameObject> objects;
            map<string, Player> players;
            
            void update(float deltaTime) {
                for (auto& obj : objects) {
                    obj.update(deltaTime);
                    
                    for (const auto& player : players) {
                        if (obj.collidesWith(player.second)) {
                            obj.onPlayerCollision(player.second);
                        }
                    }
                }
                
                for (auto& playerPair : players) {
                    playerPair.second.update(deltaTime);
                }
            }
            
            void render() {
                for (const auto& obj : objects) {
                    if (obj.isVisible()) {
                        renderer.draw(obj);
                    }
                }
                
                for (const auto& player : players) {
                    renderer.draw(player.second);
                }
            }
        }
    )");
}

JAI_TEST(range_for_error_cases) {
    // Missing parts
    assertParseFails("for (auto x) { }");
    assertParseFails("for (: array) { }");
    assertParseFails("for (auto : ) { }");
    assertParseFails("for auto x : array { }");
    
    // Invalid syntax
    assertParseFails("for (auto x :: array) { }");
    assertParseFails("for (auto x in array) { }");
    assertParseFails("for (auto x : array;) { }");
    assertParseFails("for (auto x : array; condition) { }");
    
    // Invalid type specifiers
    assertParseFails("for (auto auto x : array) { }");
    assertParseFails("for (& x : array) { }");
    assertParseFails("for (const& x : array) { }");
}

JAI_TEST(range_for_execution_basic) {
    engine engine;
    
    // Test basic for loop execution (traditional style for now)
    script_value result = engine.execute(R"(
        sum = 0;
        for (var i = 1; i <= 5; i = i + 1) {
            sum = sum + i;
        }
        return sum;
    )");
    
    expect_eq(result.as<script_int>(), 15); // 1 + 2 + 3 + 4 + 5 = 15
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()