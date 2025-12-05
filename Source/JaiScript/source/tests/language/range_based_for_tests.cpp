#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai::foundry;

namespace jai::foundry::tests {

class range_based_for_tests : public suite {
public:
    range_based_for_tests() : suite("range_based_for") {}
    
    void forge_tests() override {
        test("array_value_iteration", [this]() {
            auto engine = engine::make();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto numbers = [1, 2, 3, 4, 5];
                auto sum = 0;
                for (auto x : numbers) {
                    sum += x;
                }
                sum
            )");
            
            check_eq(result.as<int>(), 15);
        });
        
        test("array_reference_iteration", [this]() {
            auto engine = engine::make();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto numbers = [1, 2, 3, 4, 5];
                for (auto& x : numbers) {
                    x *= 2;
                }
                auto sum = 0;
                for (auto x : numbers) {
                    sum += x;
                }
                sum
            )");
            
            check_eq(result.as<int>(), 30);
        });
        
        test("map_value_iteration", [this]() {
            auto engine = engine::make();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto scores = {"alice": 100, "bob": 85, "charlie": 92};
                auto total = 0;
                for (auto kv : scores) {
                    total += kv.second;
                }
                total
            )");
            
            check_eq(result.as<int>(), 277);
        });
        
        test("map_reference_iteration", [this]() {
            auto engine = engine::make();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto scores = {"alice": 100, "bob": 85, "charlie": 92};
                
                // Add 10 bonus points to each score
                for (auto& kv : scores) {
                    kv.second += 10;
                }
                
                scores["alice"] + scores["bob"] + scores["charlie"]
            )");
            
            check_eq(result.as<int>(), 307);
        });
        
        test("map_key_const_check", [this]() {
            auto engine = engine::make();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto test_map = {"key1": 100, "key2": 200};
                auto original_keys = "";
                for (auto kv : test_map) {
                    original_keys += kv.first + " ";
                }
                
                // Modifying kv.first should not affect the actual map keys
                for (auto& kv : test_map) {
                    kv.first = "modified";
                }
                
                auto after_keys = "";
                for (auto kv : test_map) {
                    after_keys += kv.first + " ";
                }
                
                original_keys == after_keys
            )");
            
            check_eq(result.as<bool>(), true);
        });
        
        test("nested_iteration", [this]() {
            auto engine = engine::make();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto matrix = [[1, 2], [3, 4], [5, 6]];
                auto sum = 0;
                for (auto row : matrix) {
                    for (auto val : row) {
                        sum += val;
                    }
                }
                sum
            )");
            
            check_eq(result.as<int>(), 21);
        });
        
        test("break_in_range_for", [this]() {
            auto engine = engine::make();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto numbers = [1, 2, 3, 4, 5];
                auto sum = 0;
                for (auto x : numbers) {
                    if (x > 3) {
                        break;
                    }
                    sum += x;
                }
                sum
            )");

            std::cout << "break_in_range_for result: " << result.as<int>() << " (expected 6)" << std::endl;
            check_eq(result.as<int>(), 6);
        });

        test("continue_in_range_for", [this]() {
            auto engine = engine::make();
            stdlib::register_all(engine);

            auto result = engine->execute(R"(
                auto numbers = [1, 2, 3, 4, 5];
                auto sum = 0;
                for (auto x : numbers) {
                    if (x == 3) {
                        continue;
                    }
                    sum += x;
                }
                sum
            )");

            std::cout << "continue_in_range_for result: " << result.as<int>() << " (expected 12)" << std::endl;
            check_eq(result.as<int>(), 12);
        });
        
        test("empty_container_iteration", [this]() {
            auto engine = engine::make();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto empty_array = [];
                auto count = 0;
                for (auto x : empty_array) {
                    count += 1;
                }
                
                auto empty_map = {};
                for (auto kv : empty_map) {
                    count += 1;
                }
                
                count
            )");
            
            check_eq(result.as<int>(), 0);
        });
        
        test("pair_member_access", [this]() {
            auto engine = engine::make();
            stdlib::register_all(engine);
            
            auto result = engine->execute(R"(
                auto data = {"x": 10, "y": 20};
                auto keys = "";
                auto sum = 0;
                
                for (auto kv : data) {
                    keys += kv.first + " ";
                    sum += kv.second;
                }
                
                keys + to_string(sum)
            )");
            
            check(result.as<std::string>().find("30") != std::string::npos);
        });
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::range_based_for_tests)