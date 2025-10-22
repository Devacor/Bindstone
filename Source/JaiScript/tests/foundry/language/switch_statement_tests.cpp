#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class switch_statement_tests : public suite {
public:
    switch_statement_tests() : suite("Switch Statement Tests") {}
    
    void forge_tests() override {
        test("basic_integer_switch", [this]() {
            auto engine = engine::make();
            jai::stdlib::register_all(engine);
            auto result = engine->execute(R"(
                auto x = 2;
                auto result = "";
                switch (x) {
                    case 1:
                        result = "one";
                    case 2:
                        result = "two";
                    case 3:
                        result = "three";
                    default:
                        result = "other";
                }
                result
            )");
            check_eq(result.template as<std::string>(), std::string("two"));
        });
        
        test("switch_with_fallthrough", [this]() {
            auto engine = engine::make();
            auto result = engine->execute(R"(
                auto x = 2;
                auto result = "";
                switch (x) {
                    case 1:
                        result = result + "one";
                        fallthrough;
                    case 2:
                        result = result + "two";
                        fallthrough;
                    case 3:
                        result = result + "three";
                        fallthrough;
                    default:
                        result = result + "default";
                }
                result
            )");
            check_eq(result.template as<std::string>(), std::string("twothreedefault"));
        });
        
        test("switch_with_explicit_break", [this]() {
            auto engine = engine::make();
            auto result = engine->execute(R"(
                auto x = 2;
                auto result = "";
                switch (x) {
                    case 1:
                        result = "one";
                        break;
                    case 2:
                        result = "two";
                        break;
                    case 3:
                        result = "three";
                        break;
                    default:
                        result = "other";
                }
                result
            )");
            check_eq(result.template as<std::string>(), std::string("two"));
        });
        
        test("switch_with_strings", [this]() {
            auto engine = engine::make();
            auto result = engine->execute(R"(
                auto name = "Bob";
                auto greeting = "";
                switch (name) {
                    case "Alice":
                        greeting = "Hello Alice!";
                    case "Bob":
                        greeting = "Hey Bob!";
                    case "Charlie":
                        greeting = "Hi Charlie!";
                    default:
                        greeting = "Hello stranger!";
                }
                greeting
            )");
            check_eq(result.template as<std::string>(), std::string("Hey Bob!"));
        });
        
        test("switch_no_match_default_case", [this]() {
            auto engine = engine::make();
            auto result = engine->execute(R"(
                auto x = 99;
                auto result = "";
                switch (x) {
                    case 1:
                        result = "one";
                    case 2:
                        result = "two";
                    default:
                        result = "default case";
                }
                result
            )");
            check_eq(result.template as<std::string>(), std::string("default case"));
        });
        
        test("switch_no_default", [this]() {
            auto engine = engine::make();
            auto result = engine->execute(R"(
                auto x = 99;
                auto result = "unchanged";
                switch (x) {
                    case 1:
                        result = "one";
                    case 2:
                        result = "two";
                }
                result
            )");
            check_eq(result.template as<std::string>(), std::string("unchanged"));
        });
        
        test("switch_with_expressions", [this]() {
            auto engine = engine::make();
            auto result = engine->execute(R"(
                auto x = 5;
                auto result = 0;
                switch (x + 5) {
                    case 8:
                        result = 8;
                    case 10:
                        result = 10;
                    case 12:
                        result = 12;
                    default:
                        result = -1;
                }
                result
            )");
            check_eq(result.template as<int>(), 10);
        });
        
        test("nested_switch", [this]() {
            auto engine = engine::make();
            auto result = engine->execute(R"(
                auto x = 1;
                auto y = 2;
                auto result = "";
                switch (x) {
                    case 1:
                        switch (y) {
                            case 1:
                                result = "x=1,y=1";
                            case 2:
                                result = "x=1,y=2";
                            default:
                                result = "x=1,y=other";
                        }
                    case 2:
                        result = "x=2";
                    default:
                        result = "x=other";
                }
                result
            )");
            check_eq(result.template as<std::string>(), std::string("x=1,y=2"));
        });
        
        test("switch_with_return", [this]() {
            auto engine = engine::make();
            auto result = engine->execute(R"(
                auto get_name = [](int x) {
                    switch (x) {
                        case 1:
                            return "one";
                        case 2:
                            return "two";
                        case 3:
                            return "three";
                        default:
                            return "other";
                    }
                };
                get_name(2)
            )");
            check_eq(result.template as<std::string>(), std::string("two"));
        });
        
        test("fallthrough_only_in_switch", [this]() {
            auto engine = engine::make();
            // This should fail to parse - fallthrough outside switch
            check_throws<parse_error>([&]() {
                engine->execute(R"(
                    fallthrough;
                )");
            });
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::switch_statement_tests)