#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class namespace_tests : public suite {
public:
    namespace_tests() : suite("Namespace Tests") {}

    void forge_tests() override {
        test("namespace_basic_function", [&]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                namespace utils {
                    auto add(a, b) {
                        return a + b;
                    }
                }
                utils::add(10, 20)
            )");
            check_eq(result.as<int>(), 30);
        });

        test("namespace_function_overload", [&]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                namespace math {
                    auto square(x) {
                        return x * x;
                    }
                    auto square(x, y) {
                        return x * x + y * y;
                    }
                }
                auto r1 = math::square(5);
                auto r2 = math::square(3, 4);
                r1 + r2
            )");
            check_eq(result.as<int>(), 50);
        });

        test("namespace_variable", [&]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                namespace config {
                    auto MAX_SIZE = 100;
                    auto MIN_SIZE = 10;
                }
                config::MAX_SIZE + config::MIN_SIZE
            )");
            check_eq(result.as<int>(), 110);
        });

        test("namespace_class", [&]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                namespace shapes {
                    class Rectangle {
                    public:
                        int width;
                        int height;

                        Rectangle(w, h) {
                            width = w;
                            height = h;
                        }

                        auto area() {
                            return width * height;
                        }
                    };
                }
                auto rect = shapes::Rectangle(5, 10);
                rect.area()
            )");
            check_eq(result.as<int>(), 50);
        });

        test("namespace_nested_blocks", [&]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                namespace outer {
                    namespace inner {
                        auto getValue() {
                            return 42;
                        }
                    }
                }
                outer::inner::getValue()
            )");
            check_eq(result.as<int>(), 42);
        });

        test("namespace_scope_resolution_syntax", [&]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                namespace my::nested::deep {
                    auto getValue() {
                        return 99;
                    }
                }
                my::nested::deep::getValue()
            )");
            check_eq(result.as<int>(), 99);
        });

        test("namespace_mixed_members", [&]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                namespace mylib {
                    auto VERSION = 1;

                    auto getVersion() {
                        return VERSION;
                    }

                    class Info {
                    public:
                        auto describe() {
                            return 1;
                        }
                    };
                }

                auto v1 = mylib::VERSION;
                auto v2 = mylib::getVersion();
                auto info = mylib::Info();
                auto v3 = info.describe();

                v1 + v2 + v3
            )");
            check_eq(result.as<int>(), 3);
        });

        test("namespace_override_required", [&]() {
            auto eng = engine::make();
            check_throws([&]() {
                eng->execute(R"(
                    namespace test {
                        auto func(x) { return x; }
                        auto func(x) { return x * 2; }
                    }
                )");
            });
        });

        test("namespace_override_allowed", [&]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                namespace test {
                    auto func(x) { return x; }
                    auto func(x) override { return x * 2; }
                }
                test::func(5)
            )");
            check_eq(result.as<int>(), 10);
        });

        test("namespace_string_keyword_as_name", [&]() {
            auto eng = engine::make();
            auto result = eng->execute(R"(
                namespace string {
                    auto length(s) {
                        return 42;
                    }
                }
                string::length("test")
            )");
            check_eq(result.as<int>(), 42);
        });

        test("namespace_member_not_found", [&]() {
            auto eng = engine::make();
            check_throws([&]() {
                eng->execute(R"(
                    namespace test {
                        auto func() { return 1; }
                    }
                    test::nonexistent()
                )");
            });
        });

        test("namespace_function_arity_mismatch", [&]() {
            auto eng = engine::make();
            check_throws([&]() {
                eng->execute(R"(
                    namespace math {
                        auto add(a, b) { return a + b; }
                    }
                    math::add(1, 2, 3)
                )");
            });
        });

        test("namespace_complex_members", [&]() {
            auto eng = engine::make();

            // Test class with static method, instance method, and global variable
            eng->execute(R"(
                namespace my::nested {
                    class cat {
                    public:
                        static bool meow() {
                            return true;
                        }

                        bool climb() {
                            return true;
                        }
                    };

                    cat GLOBAL_CAT;
                }
            )");

            // Test static method access
            auto result1 = eng->execute("my::nested::cat::meow()");
            check_eq(result1.as<bool>(), true);

            // Test global variable instance method access
            auto result2 = eng->execute("my::nested::GLOBAL_CAT.climb()");
            check_eq(result2.as<bool>(), true);
        });

        test("namespace_overrides_class_static", [&]() {
            auto eng = engine::make();
            eng->execute(R"(
                class cat {
                public:
                    static auto meow() { return 1; }
                    static auto climb() { return 1; }
                    static auto meow(a) { return 5; }
                };

                namespace cat {
                    auto meow() override { return 2; }
                    auto dance() { return 2; }
                    auto meow(a, b) { return 10; }
                }
            )");

            // Namespace override of static method (0 params)
            auto r1 = eng->execute("cat::meow()");
            check_eq(r1.as<int>(), 2);

            // Class static method (1 param - no namespace override)
            auto r2 = eng->execute("cat::meow(1)");
            check_eq(r2.as<int>(), 5);

            // Namespace function (2 params)
            auto r3 = eng->execute("cat::meow(1, 2)");
            check_eq(r3.as<int>(), 10);

            // Class static method (no namespace override)
            auto r4 = eng->execute("cat::climb()");
            check_eq(r4.as<int>(), 1);

            // Namespace-only function
            auto r5 = eng->execute("cat::dance()");
            check_eq(r5.as<int>(), 2);
        });
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::namespace_tests)
