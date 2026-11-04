#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/testing/timing.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/core/jaibite.hpp>
#include <vector>
#include <map>
#include <numeric>
#include <cmath>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class container_conversion_tests : public suite {
public:
    container_conversion_tests() : suite("Container Conversion Tests") {}

    // one timed round: `calls` calls of the precompiled bite
    static std::function<double()> timed_calls(jai::jaibite& call, int calls = 20) {
        return [&call, calls] { return time_seconds([&call, calls] { for (int i = 0; i < calls; ++i) { call.execute(); } }); };
    }

    void forge_tests() override {
        test("large_vector_performance", [this]() {
            // ten 10k-element conversions cost about what a hundred 1k ones do; a quadratic conversion would cost ten times as much
            auto engine = make_engine();
            engine->execution_budget(0.0);   // a measurement, not a wall-clock gate

            engine->add_function("sum_large", [](std::vector<int> nums) -> int64_t {
                int64_t sum = 0;
                for (int n : nums) sum += n;
                return sum;
            });
            engine->execute("auto small_array = []; for (auto i = 0; i < 1000; i += 1) { small_array.push(i); }");
            engine->execute("auto large_array = []; for (auto i = 0; i < 10000; i += 1) { large_array.push(i); }");
            check_eq(engine->execute("sum_large(large_array)").as<int64_t>(), 49995000LL);

            auto small_call = engine->jaibite("sum_large(small_array);");
            auto large_call = engine->jaibite("sum_large(large_array);");
            check_speed_ratio(compare_speed("10 10k-element vector<int> conversions against 100 1k ones", timed_calls(small_call, 100), timed_calls(large_call, 10), 15), 4.0);
        });
        
        test("vector_int_basic", [this]() {
            auto engine = make_engine();
            
            engine->add_function("sum_ints", [](std::vector<int> nums) -> int {
                return std::accumulate(nums.begin(), nums.end(), 0);
            });
            
            auto result = engine->execute("sum_ints([1, 2, 3, 4, 5])");
            check_eq(result.as<int>(), 15);
        });
        
        test("vector_double_conversion", [this]() {
            auto engine = make_engine();
            
            // Test automatic int->double conversion in vectors
            engine->add_function("avg_doubles", [](std::vector<double> nums) -> double {
                if (nums.empty()) return 0.0;
                double sum = std::accumulate(nums.begin(), nums.end(), 0.0);
                return sum / nums.size();
            });
            
            auto result = engine->execute("avg_doubles([1, 2.5, 3, 4.5])");
            check_eq(result.as<double>(), 2.75);
        });
        
        test("vector_string_operations", [this]() {
            auto engine = make_engine();
            
            engine->add_function("join_strings", [](std::vector<std::string> strs, const std::string& sep) -> std::string {
                std::string result;
                for (size_t i = 0; i < strs.size(); ++i) {
                    if (i > 0) result += sep;
                    result += strs[i];
                }
                return result;
            });
            
            auto result = engine->execute("join_strings([\"hello\", \"world\", \"test\"], \" \")");
            check_eq(result.as<std::string>(), "hello world test");
        });
        
        test("vector_return_values", [this]() {
            auto engine = make_engine();
            
            engine->add_function("range", [](int start, int end) -> std::vector<int> {
                std::vector<int> result;
                for (int i = start; i < end; ++i) {
                    result.push_back(i);
                }
                return result;
            });
            
            auto result = engine->execute("range(1, 6)");
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 5u);
            check_eq(arr[0].as<int>(), 1);
            check_eq(arr[4].as<int>(), 5);
        });
        
        test("nested_vectors", [this]() {
            auto engine = make_engine();
            
            engine->add_function("sum_matrix", [](std::vector<std::vector<int>> matrix) -> int {
                int sum = 0;
                for (const auto& row : matrix) {
                    for (int val : row) {
                        sum += val;
                    }
                }
                return sum;
            });
            
            auto result = engine->execute("sum_matrix([[1, 2], [3, 4], [5, 6]])");
            check_eq(result.as<int>(), 21);
        });
        
        test("map_string_int_basic", [this]() {
            auto engine = make_engine();
            
            engine->add_function("get_score", [](std::map<std::string, int> scores, const std::string& name) -> int {
                auto it = scores.find(name);
                return it != scores.end() ? it->second : -1;
            });
            
            auto result = engine->execute("get_score({\"Alice\": 95, \"Bob\": 87, \"Charlie\": 92}, \"Bob\")");
            check_eq(result.as<int>(), 87);
        });
        
        test("map_return_values", [this]() {
            auto engine = make_engine();
            
            engine->add_function("create_scores", []() -> std::map<std::string, int> {
                return {{"Alice", 95}, {"Bob", 87}, {"Charlie", 92}};
            });
            
            auto result = engine->execute("create_scores()");
            check(result.is_map());
            auto map = result.as_map();
            check_eq(map.size(), 3u);
            auto bob_key = engine->make_value("Bob");
            auto it = map.find(bob_key);
            check(it != map.end());
            check_eq(it->second.as<int>(), 87);
        });
        
        test("map_int_string", [this]() {
            auto engine = make_engine();
            
            engine->add_function("number_names", [](std::map<int, std::string> nums) -> std::string {
                std::string result;
                for (const auto& [num, name] : nums) {
                    if (!result.empty()) result += ", ";
                    result += std::to_string(num) + "=" + name;
                }
                return result;
            });
            
            auto result = engine->execute("number_names({1: \"one\", 2: \"two\", 3: \"three\"})");
            check(result.as<std::string>().find("1=one") != std::string::npos);
            check(result.as<std::string>().find("2=two") != std::string::npos);
            check(result.as<std::string>().find("3=three") != std::string::npos);
        });
        
        test("empty_containers", [this]() {
            auto engine = make_engine();
            
            engine->add_function("is_empty_vec", [](std::vector<int> v) -> bool {
                return v.empty();
            });
            
            engine->add_function("is_empty_map", [](std::map<std::string, int> m) -> bool {
                return m.empty();
            });
            
            check(engine->execute("is_empty_vec([])").as<bool>());
            check(!engine->execute("is_empty_vec([1])").as<bool>());
            check(engine->execute("is_empty_map({})").as<bool>());
            check(!engine->execute("is_empty_map({\"a\": 1})").as<bool>());
        });
        
        test("type_mismatch_errors", [this]() {
            auto engine = make_engine();
            
            engine->add_function("needs_int_vec", [](std::vector<int>) {});
            
            check_throws<runtime_error>([&]() {
                engine->execute("needs_int_vec([\"not\", \"ints\"])");
            });
        });
        
        test("vector_of_maps", [this]() {
            auto engine = make_engine();
            
            engine->add_function("count_total_items", [](std::vector<std::map<std::string, int>> data) -> int {
                int count = 0;
                for (const auto& m : data) {
                    count += static_cast<int>(m.size());
                }
                return count;
            });
            
            auto result = engine->execute("count_total_items([{\"a\": 1, \"b\": 2}, {\"c\": 3}, {}])");
            check_eq(result.as<int>(), 3);
        });
        
        test("map_of_vectors", [this]() {
            auto engine = make_engine();
            
            engine->add_function("sum_all_groups", [](std::map<std::string, std::vector<int>> groups) -> int {
                int total = 0;
                for (const auto& [name, values] : groups) {
                    for (int v : values) {
                        total += v;
                    }
                }
                return total;
            });
            
            auto result = engine->execute("sum_all_groups({\"A\": [1, 2, 3], \"B\": [4, 5], \"C\": []})");
            check_eq(result.as<int>(), 15);
        });
        
        test("custom_type_vectors", [this]() {
            auto engine = make_engine();
            
            struct Point { double x, y; };
            
            dynamic_binder<Point>(*engine, "Point")
                .constructor<>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            engine->add_function("total_distance", [](std::vector<Point> points) -> double {
                double total = 0.0;
                for (const auto& p : points) {
                    total += std::sqrt(p.x * p.x + p.y * p.y);
                }
                return total;
            });
            
            auto result = engine->execute(R"(
                auto points = [];
                auto p1 = Point();
                p1.x = 3.0;
                p1.y = 4.0;
                points.push(p1);
                total_distance(points)
            )");
            
            check_eq(result.as<double>(), 5.0);
        });
        
        test("performance_comparison", [this]() {
            // a typed std::vector<int> parameter against a variadic call reading the same array: reported, not bounded
            auto engine = make_engine();

            engine->add_variadic_function("sum_variadic", [engine](std::vector<script_value> args) -> script_value {
                if (args.size() != 1 || !args[0].is_array()) {
                    throw runtime_error("Expected single array argument");
                }
                const auto& arr = args[0].as_array();
                int sum = 0;
                for (const auto& val : arr) {
                    sum += val.as<int>();
                }
                return engine->make_value(sum);
            });

            engine->add_function("sum_typed", [](std::vector<int> nums) -> int {
                return std::accumulate(nums.begin(), nums.end(), 0);
            });

            engine->execute("auto test_array = [];");
            engine->execute("for (auto i = 0; i < 1000; i += 1) { test_array.push(i); }");

            auto variadic = engine->jaibite("sum_variadic(test_array);");
            auto typed = engine->jaibite("sum_typed(test_array);");
            check_eq((int64_t)499500, variadic.execute().as_int(), "the variadic sum");
            check_eq((int64_t)499500, typed.execute().as_int(), "the typed sum");
            compare_speed("a typed vector<int> parameter against a variadic one (1000 elements)", timed_calls(variadic), timed_calls(typed));
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::container_conversion_tests)