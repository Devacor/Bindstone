#pragma once

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/static_binder.hpp>
#include <jaiscript/core/static_binder_impl.hpp>
#include <jaiscript/core/template_binder.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/properties/macros.hpp>
#include <jaiscript/serialization/binary_archive.hpp>
#include <cmath>

namespace jai::foundry::tests {

// ============================================================================
// Test Template 1: Simple template with JAI_TEMPLATE_BINDER
// Uses unique name to avoid static state conflicts
// ============================================================================
template<typename T>
struct TplBinderVec2 {
    T x = T{};
    T y = T{};

    TplBinderVec2() = default;
    TplBinderVec2(T x_, T y_) : x(x_), y(y_) {}

    T length_squared() const { return x * x + y * y; }
    TplBinderVec2<T> add(const TplBinderVec2<T>& other) const {
        return TplBinderVec2<T>(x + other.x, y + other.y);
    }
};

// ============================================================================
// Test Template 2: Simple struct with auto_bind_template
// Note: property_owner + JAI_PROPERTY doesn't work in template classes due to
// macro limitations. Use plain members for template types.
// ============================================================================
template<typename T>
struct TplBinderPoint : public auto_bind_template<TplBinderPoint, T> {
    T px = T{};
    T py = T{};

    TplBinderPoint() = default;
    TplBinderPoint(T x_, T y_) : px(x_), py(y_) {}

    T magnitude() const {
        return static_cast<T>(std::sqrt(static_cast<double>(px * px + py * py)));
    }
};

// ============================================================================
// Test Template 3: Two-parameter template
// ============================================================================
template<typename K, typename V>
struct TplBinderKVPair {
    K key{};
    V value{};

    TplBinderKVPair() = default;
    TplBinderKVPair(K k, V v) : key(k), value(v) {}

    K get_key() const { return key; }
    V get_value() const { return value; }
};

// ============================================================================
// Test Template 4: Auto-detection test - NO JAI_BIND_TEMPLATE for this one
// This tests that template_binder_accessor can work without explicit registration
// ============================================================================
template<typename T>
struct TplAutoDetectVec {
    T a = T{};
    T b = T{};

    TplAutoDetectVec() = default;
    TplAutoDetectVec(T a_, T b_) : a(a_), b(b_) {}

    T dot() const { return a * b; }
};

} // namespace jai::foundry::tests

// ============================================================================
// JAI_TEMPLATE_BINDER registrations (must be outside namespace)
// ============================================================================

// Template binder for TplBinderVec2<T> - uses NAMED variant for namespaced type
JAI_TEMPLATE_BINDER_NAMED(TplBinderVec2, jai::foundry::tests::TplBinderVec2, "TplBinderVec2",
    .property("x", &jai::foundry::tests::TplBinderVec2<T>::x)
    .property("y", &jai::foundry::tests::TplBinderVec2<T>::y)
    .method("length_squared", &jai::foundry::tests::TplBinderVec2<T>::length_squared)
);

// Template binder for TplBinderPoint<T> (property_owner - properties auto-discovered)
JAI_TEMPLATE_BINDER_NAMED(TplBinderPoint, jai::foundry::tests::TplBinderPoint, "TplBinderPoint",
    .method("magnitude", &jai::foundry::tests::TplBinderPoint<T>::magnitude)
);

// Template binder for TplBinderKVPair<K, V> (two parameters)
JAI_TEMPLATE_BINDER_2_NAMED(TplBinderKVPair, jai::foundry::tests::TplBinderKVPair, "TplBinderKVPair",
    .property("key", &jai::foundry::tests::TplBinderKVPair<T1, T2>::key)
    .property("value", &jai::foundry::tests::TplBinderKVPair<T1, T2>::value)
    .method("get_key", &jai::foundry::tests::TplBinderKVPair<T1, T2>::get_key)
    .method("get_value", &jai::foundry::tests::TplBinderKVPair<T1, T2>::get_value)
);

// Template binder for TplAutoDetectVec<T> - NO JAI_BIND_TEMPLATE!
// This tests auto-detection via template_binder_accessor
JAI_TEMPLATE_BINDER_NAMED(TplAutoDetectVec, jai::foundry::tests::TplAutoDetectVec, "TplAutoDetectVec",
    .property("a", &jai::foundry::tests::TplAutoDetectVec<T>::a)
    .property("b", &jai::foundry::tests::TplAutoDetectVec<T>::b)
    .method("dot", &jai::foundry::tests::TplAutoDetectVec<T>::dot)
);

// ============================================================================
// JAI_BIND_TEMPLATE instantiations for specific types
// ============================================================================

// Explicit instantiations for TplBinderVec2 - uses NAMED variant for namespaced type
JAI_BIND_TEMPLATE_NAMED(TplBinderVec2, jai::foundry::tests::TplBinderVec2, int);
JAI_BIND_TEMPLATE_NAMED(TplBinderVec2, jai::foundry::tests::TplBinderVec2, float);
JAI_BIND_TEMPLATE_NAMED(TplBinderVec2, jai::foundry::tests::TplBinderVec2, double);

// Explicit instantiations for TplBinderKVPair (two params)
JAI_BIND_TEMPLATE_2_NAMED(TplBinderKVPair, jai::foundry::tests::TplBinderKVPair, int, std::string);
JAI_BIND_TEMPLATE_2_NAMED(TplBinderKVPair, jai::foundry::tests::TplBinderKVPair, std::string, double);

namespace jai::foundry::tests {

// ============================================================================
// Test Suite
// ============================================================================

class template_binder_tests : public suite {
public:
    template_binder_tests() : suite("Template Binder") {}

    void forge_tests() override {
        // ================================================================
        // type_name_helper tests
        // ================================================================

        test("type_name_helper_primitives", [this]() {
            check_eq(type_name_helper<int>::name(), std::string("int32"), "int32 name");
            check_eq(type_name_helper<float>::name(), std::string("float"), "float name");
            check_eq(type_name_helper<double>::name(), std::string("double"), "double name");
            check_eq(type_name_helper<bool>::name(), std::string("bool"), "bool name");
            check_eq(type_name_helper<std::string>::name(), std::string("string"), "string name");
        });

        test("type_name_helper_integers", [this]() {
            check_eq(type_name_helper<int8_t>::name(), std::string("int8"), "int8 name");
            check_eq(type_name_helper<int16_t>::name(), std::string("int16"), "int16 name");
            check_eq(type_name_helper<int>::name(), std::string("int32"), "int32 name");
            check_eq(type_name_helper<int64_t>::name(), std::string("int"), "int (native 64-bit)");
            check_eq(type_name_helper<uint8_t>::name(), std::string("uint8"), "uint8 name");
            check_eq(type_name_helper<uint16_t>::name(), std::string("uint16"), "uint16 name");
            check_eq(type_name_helper<uint32_t>::name(), std::string("uint32"), "uint32 name");
            check_eq(type_name_helper<uint64_t>::name(), std::string("uint"), "uint (native 64-bit)");
        });

        // ================================================================
        // JAI_TEMPLATE_BINDER type name generation
        // ================================================================

        test("template_binder_type_names", [this]() {
            // Check generated type names
            auto int_name = jai_template_binder_TplBinderVec2<int>::type_name();
            auto float_name = jai_template_binder_TplBinderVec2<float>::type_name();
            auto double_name = jai_template_binder_TplBinderVec2<double>::type_name();

            check_eq(int_name, std::string("TplBinderVec2<int32>"), "int32 variant name");
            check_eq(float_name, std::string("TplBinderVec2<float>"), "float variant name");
            check_eq(double_name, std::string("TplBinderVec2<double>"), "double variant name");
        });

        test("template_binder_2_type_names", [this]() {
            // Check two-parameter type names
            auto name1 = jai_template_binder_TplBinderKVPair<int, std::string>::type_name();
            auto name2 = jai_template_binder_TplBinderKVPair<std::string, double>::type_name();

            check_eq(name1, std::string("TplBinderKVPair<int32, string>"), "int32,string variant");
            check_eq(name2, std::string("TplBinderKVPair<string, double>"), "string,double variant");
        });

        // ================================================================
        // static_type_registry tests
        // ================================================================

        test("static_type_registry_registration", [this]() {
            // Types registered via JAI_BIND_TEMPLATE should be marked
            check(static_type_registry::is_registered<TplBinderVec2<int>>(),
                  "TplBinderVec2<int> is registered");
            check(static_type_registry::is_registered<TplBinderVec2<float>>(),
                  "TplBinderVec2<float> is registered");
            check(static_type_registry::is_registered<TplBinderVec2<double>>(),
                  "TplBinderVec2<double> is registered");

            // Unregistered variant should not be marked
            check(!static_type_registry::is_registered<TplBinderVec2<int64_t>>(),
                  "TplBinderVec2<int64_t> NOT registered");
        });

        test("static_type_registry_two_param", [this]() {
            check(static_type_registry::is_registered<TplBinderKVPair<int, std::string>>(),
                  "TplBinderKVPair<int, string> is registered");
            check(static_type_registry::is_registered<TplBinderKVPair<std::string, double>>(),
                  "TplBinderKVPair<string, double> is registered");

            // Unregistered combination
            check(!static_type_registry::is_registered<TplBinderKVPair<int, int>>(),
                  "TplBinderKVPair<int, int> NOT registered");
        });

        // ================================================================
        // has_static_type trait tests
        // ================================================================

        test("has_static_type_for_template_variants", [this]() {
            // Registered variants have static type info
            check(has_static_type_v<TplBinderVec2<int>>,
                  "TplBinderVec2<int> has static type");
            check(has_static_type_v<TplBinderVec2<float>>,
                  "TplBinderVec2<float> has static type");

            // Two-param variants
            check(has_static_type_v<TplBinderKVPair<int, std::string>>,
                  "TplBinderKVPair<int, string> has static type");
        });

        // ================================================================
        // Engine binding tests
        // ================================================================

        test("bind_template_variant_to_engine", [this]() {
            auto eng = engine::make();

            // Bind using the static type info from JAI_BIND_TEMPLATE
            eng->bind_static_type<TplBinderVec2<int>>();

            // Create instance via script
            eng->execute("auto v = TplBinderVec2<int32>();");

            // Access properties
            eng->execute("v.x = 3;");
            eng->execute("v.y = 4;");

            check_eq(eng->execute("v.x").as<int>(), 3, "x property works");
            check_eq(eng->execute("v.y").as<int>(), 4, "y property works");

            // Call method
            check_eq(eng->execute("v.length_squared()").as<int>(), 25, "length_squared method works");
        });

        test("bind_multiple_template_variants", [this]() {
            auto eng = engine::make();

            // Bind multiple variants
            eng->bind_static_type<TplBinderVec2<int>>();
            eng->bind_static_type<TplBinderVec2<float>>();
            eng->bind_static_type<TplBinderVec2<double>>();

            // Use int32 variant
            eng->execute("auto vi = TplBinderVec2<int32>();");
            eng->execute("vi.x = 1; vi.y = 2;");
            check_eq(eng->execute("vi.length_squared()").as<int>(), 5, "int32 variant works");

            // Use float variant
            eng->execute("auto vf = TplBinderVec2<float>();");
            eng->execute("vf.x = 1.5; vf.y = 2.0;");
            auto len_sq = eng->execute("vf.length_squared()").as<float>();
            check(std::abs(len_sq - 6.25f) < 0.001f, "float variant works");

            // Use double variant
            eng->execute("auto vd = TplBinderVec2<double>();");
            eng->execute("vd.x = 3.0; vd.y = 4.0;");
            check_eq(eng->execute("vd.length_squared()").as<double>(), 25.0, "double variant works");
        });

        test("bind_two_param_template_variant", [this]() {
            auto eng = engine::make();

            eng->bind_static_type<TplBinderKVPair<int, std::string>>();

            eng->execute("auto kv = TplBinderKVPair<int32, string>();");
            eng->execute("kv.key = 42;");
            eng->execute("kv.value = \"hello\";");

            check_eq(eng->execute("kv.get_key()").as<int>(), 42, "get_key works");
            check_eq(eng->execute("kv.get_value()").as<std::string>(), "hello", "get_value works");
        });

        // ================================================================
        // auto_bind_template CRTP tests
        // ================================================================

        test("auto_bind_template_registers_on_instantiation", [this]() {
            // TplBinderPoint uses auto_bind_template, so instantiating it
            // should auto-register the type

            // Create an instance - this triggers static initialization
            TplBinderPoint<int> pt;
            (void)pt;  // Suppress unused warning

            // The type should now be marked as registered
            check(static_type_registry::is_registered<TplBinderPoint<int>>(),
                  "TplBinderPoint<int> auto-registered via CRTP");
        });

        test("auto_bind_template_different_params", [this]() {
            // Instantiate different parameter types
            TplBinderPoint<float> pt_float;
            TplBinderPoint<double> pt_double;
            (void)pt_float;
            (void)pt_double;

            check(static_type_registry::is_registered<TplBinderPoint<float>>(),
                  "TplBinderPoint<float> auto-registered");
            check(static_type_registry::is_registered<TplBinderPoint<double>>(),
                  "TplBinderPoint<double> auto-registered");
        });

        // ================================================================
        // Serialization tests
        // ================================================================

        test("template_variant_serialization", [this]() {
            TplBinderVec2<int> v1{10, 20};

            // Serialize
            std::vector<uint8_t> buffer;
            {
                serialization::binary_archive_writer ar(buffer);
                ar.begin_object("TplBinderVec2<int32>", 1);
                jai_static_type<TplBinderVec2<int>>::save(ar, v1);
                ar.end_object();
            }

            // Deserialize
            auto eng = engine::make();
            TplBinderVec2<int> v2;
            {
                serialization::binary_archive_reader ar(buffer, eng.get());
                std::string type_name;
                uint32_t version;
                ar.begin_object(type_name, version);
                jai_static_type<TplBinderVec2<int>>::load(ar, v2);
                ar.end_object();
            }

            check_eq(v2.x, int{10}, "x serialized correctly");
            check_eq(v2.y, int{20}, "y serialized correctly");
        });

        // ================================================================
        // Compile-time property count
        // ================================================================

        test("template_binder_property_counts", [this]() {
            // Check compile-time property counts
            constexpr size_t vec2_props = jai_static_type<TplBinderVec2<int>>::binder.property_count();
            constexpr size_t vec2_methods = jai_static_type<TplBinderVec2<int>>::binder.method_count();

            check_eq(vec2_props, size_t(2), "TplBinderVec2 has 2 properties");
            check_eq(vec2_methods, size_t(1), "TplBinderVec2 has 1 method");

            constexpr size_t kv_props = jai_static_type<TplBinderKVPair<int, std::string>>::binder.property_count();
            constexpr size_t kv_methods = jai_static_type<TplBinderKVPair<int, std::string>>::binder.method_count();

            check_eq(kv_props, size_t(2), "TplBinderKVPair has 2 properties");
            check_eq(kv_methods, size_t(2), "TplBinderKVPair has 2 methods");
        });

        // ================================================================
        // Shared C++ object tests
        // ================================================================

        test("shared_template_object_access", [this]() {
            auto eng = engine::make();

            eng->bind_static_type<TplBinderVec2<double>>();

            // Create C++ object and share with script
            auto cpp_vec = std::make_shared<TplBinderVec2<double>>(5.0, 12.0);
            eng->add_global("vec", eng->make_object(cpp_vec));

            // Script reads property
            check_eq(eng->execute("vec.x").as<double>(), 5.0, "Script reads x");
            check_eq(eng->execute("vec.y").as<double>(), 12.0, "Script reads y");

            // Script calls method
            check_eq(eng->execute("vec.length_squared()").as<double>(), 169.0, "Script calls method");

            // Script modifies property
            eng->execute("vec.x = 8.0;");
            check_eq(cpp_vec->x, 8.0, "C++ sees script modification");

            // Verify method reflects change
            check_eq(eng->execute("vec.length_squared()").as<double>(), 208.0, "Method reflects change");
        });

        // ================================================================
        // Idempotent binding tests
        // ================================================================

        test("bind_static_type_is_idempotent", [this]() {
            auto eng = engine::make();

            // Bind the same type multiple times - should not throw or cause issues
            eng->bind_static_type<TplBinderVec2<int>>();
            eng->bind_static_type<TplBinderVec2<int>>();
            eng->bind_static_type<TplBinderVec2<int>>();

            // Should still work correctly
            eng->execute("auto v = TplBinderVec2<int32>();");
            eng->execute("v.x = 7; v.y = 24;");
            check_eq(eng->execute("v.length_squared()").as<int>(), 625, "Type still works after multiple binds");
        });

        test("bind_static_types_multiple_same", [this]() {
            auto eng = engine::make();

            // Use bind_static_types with same type listed multiple times
            eng->bind_static_types<TplBinderVec2<float>, TplBinderVec2<float>, TplBinderVec2<double>>();

            // Both should work
            eng->execute("auto vf = TplBinderVec2<float>();");
            eng->execute("auto vd = TplBinderVec2<double>();");
            check(true, "Multiple binds of same type didn't crash");
        });

        // ================================================================
        // Auto-detection tests (has_template_binder_v, template_binder_accessor)
        // ================================================================

        test("has_template_binder_detection", [this]() {
            // Types with JAI_TEMPLATE_BINDER should be detected
            check(has_template_binder_v<TplAutoDetectVec<int>>,
                  "TplAutoDetectVec<int> detected via has_template_binder_v");
            check(has_template_binder_v<TplAutoDetectVec<float>>,
                  "TplAutoDetectVec<float> detected");
            check(has_template_binder_v<TplBinderVec2<double>>,
                  "TplBinderVec2<double> detected");

            // Two-param types should use has_template_binder_2_v
            check(has_template_binder_2_v<TplBinderKVPair<int, std::string>>,
                  "TplBinderKVPair<int, string> detected via has_template_binder_2_v");

            // Combined check
            check(has_any_template_binder_v<TplAutoDetectVec<int>>,
                  "TplAutoDetectVec<int> detected via has_any_template_binder_v");
            check(has_any_template_binder_v<TplBinderKVPair<std::string, double>>,
                  "TplBinderKVPair detected via has_any_template_binder_v");

            // Plain types without binder should NOT be detected
            check(!has_template_binder_v<std::string>,
                  "std::string NOT detected as template binder type");
            check(!has_any_template_binder_v<int>,
                  "int NOT detected as template binder type");
        });

        test("template_binder_accessor_type_name", [this]() {
            // Get type name via accessor (no JAI_BIND_TEMPLATE needed!)
            auto name_int = template_binder_accessor<TplAutoDetectVec<int>>::type_name();
            auto name_float = template_binder_accessor<TplAutoDetectVec<float>>::type_name();

            check_eq(name_int, std::string("TplAutoDetectVec<int32>"), "accessor type_name for int");
            check_eq(name_float, std::string("TplAutoDetectVec<float>"), "accessor type_name for float");
        });

        test("auto_detect_bind_without_explicit_registration", [this]() {
            auto eng = engine::make();

            // Bind TplAutoDetectVec<int> - there's NO JAI_BIND_TEMPLATE for this!
            // The engine should use template_binder_accessor as fallback
            eng->bind_static_type<TplAutoDetectVec<int>>();

            // Create instance and use it in script
            eng->execute("auto v = TplAutoDetectVec<int32>();");
            eng->execute("v.a = 6;");
            eng->execute("v.b = 7;");

            check_eq(eng->execute("v.a").as<int>(), 6, "auto-detected property a");
            check_eq(eng->execute("v.b").as<int>(), 7, "auto-detected property b");
            check_eq(eng->execute("v.dot()").as<int>(), 42, "auto-detected method dot");
        });

        test("auto_detect_serialization", [this]() {
            // Serialize a type that only has JAI_TEMPLATE_BINDER, no JAI_BIND_TEMPLATE
            TplAutoDetectVec<float> v1{3.0f, 4.0f};

            // Serialize using template_binder_accessor
            std::vector<uint8_t> buffer;
            {
                serialization::binary_archive_writer ar(buffer);
                ar.begin_object("TplAutoDetectVec<float>", 1);
                template_binder_accessor<TplAutoDetectVec<float>>::save(ar, v1);
                ar.end_object();
            }

            // Deserialize
            auto eng = engine::make();
            TplAutoDetectVec<float> v2;
            {
                serialization::binary_archive_reader ar(buffer, eng.get());
                std::string type_name;
                uint32_t version;
                ar.begin_object(type_name, version);
                template_binder_accessor<TplAutoDetectVec<float>>::load(ar, v2);
                ar.end_object();
            }

            check(std::abs(v2.a - 3.0f) < 0.001f, "a serialized via accessor");
            check(std::abs(v2.b - 4.0f) < 0.001f, "b serialized via accessor");
        });

        test("auto_detect_multiple_variants", [this]() {
            auto eng = engine::make();

            // Bind multiple variants of auto-detect type
            eng->bind_static_type<TplAutoDetectVec<int>>();
            eng->bind_static_type<TplAutoDetectVec<float>>();
            eng->bind_static_type<TplAutoDetectVec<double>>();

            // All should work
            eng->execute("auto vi = TplAutoDetectVec<int32>();");
            eng->execute("auto vf = TplAutoDetectVec<float>();");
            eng->execute("auto vd = TplAutoDetectVec<double>();");

            eng->execute("vi.a = 2; vi.b = 3;");
            eng->execute("vf.a = 2.5; vf.b = 4.0;");
            eng->execute("vd.a = 1.5; vd.b = 3.0;");

            check_eq(eng->execute("vi.dot()").as<int>(), 6, "int variant works");
            check(std::abs(eng->execute("vf.dot()").as<float>() - 10.0f) < 0.001f, "float variant works");
            check(std::abs(eng->execute("vd.dot()").as<double>() - 4.5) < 0.001, "double variant works");
        });

        // ================================================================
        // Auto integer variant binding tests
        // ================================================================

        test("auto_bind_int32_also_binds_int64", [this]() {
            auto eng = engine::make();

            // Bind only int (which is int32_t on most platforms)
            // This should auto-bind int64_t variant as well
            eng->bind_static_type<TplAutoDetectVec<int>>();

            // int32 variant should work (the one we explicitly bound)
            eng->execute("auto v32 = TplAutoDetectVec<int32>();");
            eng->execute("v32.a = 5; v32.b = 6;");
            check_eq(eng->execute("v32.dot()").as<int>(), 30, "int32 variant works");

            // int (64-bit native) variant should also be available
            eng->execute("auto v64 = TplAutoDetectVec<int>();");
            eng->execute("v64.a = 7; v64.b = 8;");
            check_eq(eng->execute("v64.dot()").as<int64_t>(), 56, "int (64-bit) variant auto-bound");
        });

        test("auto_bind_int64_also_binds_int32", [this]() {
            auto eng = engine::make();

            // Bind only int64_t
            // This should auto-bind int32_t variant as well
            eng->bind_static_type<TplAutoDetectVec<int64_t>>();

            // int (64-bit native) variant should work
            eng->execute("auto v64 = TplAutoDetectVec<int>();");
            eng->execute("v64.a = 10; v64.b = 11;");
            check_eq(eng->execute("v64.dot()").as<int64_t>(), 110, "int (64-bit) variant works");

            // int32 variant should also be available
            eng->execute("auto v32 = TplAutoDetectVec<int32>();");
            eng->execute("v32.a = 3; v32.b = 4;");
            check_eq(eng->execute("v32.dot()").as<int>(), 12, "int32 variant auto-bound");
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::template_binder_tests)
