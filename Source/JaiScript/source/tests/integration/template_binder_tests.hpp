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
#include <jaiscript/stdlib/stdlib.hpp>
#include <cmath>

namespace jai::foundry::tests {

// Uses unique name to avoid static state conflicts
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

// Note: property_owner + JAI_PROPERTY doesn't work in template classes due to
// macro limitations. Use plain members for template types.
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

template<typename K, typename V>
struct TplBinderKVPair {
    K key{};
    V value{};

    TplBinderKVPair() = default;
    TplBinderKVPair(K k, V v) : key(k), value(v) {}

    K get_key() const { return key; }
    V get_value() const { return value; }
};

template<typename T>
struct TplAutoDetectVec {
    T a = T{};
    T b = T{};

    TplAutoDetectVec() = default;
    TplAutoDetectVec(T a_, T b_) : a(a_), b(b_) {}

    T dot() const { return a * b; }
};

// ---- template-instantiation type-identity audit fixture (2026-07, GLOOM follow-up).
// The shared_ptr intern bug was a type-identity KEYING failure; this pair pins that
// no other path keys two distinct C++ instantiations alike. Same method names,
// per-instantiation distinct behavior - any cross-talk shows immediately.
struct audit_pt_base {
    virtual ~audit_pt_base() = default;
    int base_tag() const { return 99; }
};

template<typename T>
struct audit_pt : audit_pt_base {
    T x{};
    audit_pt() = default;
    explicit audit_pt(T v) : x(v) {}
    int kind() const;                                  // 1 for int, 2 for float
    T doubled() const { return static_cast<T>(x + x); }
};
template<> inline int audit_pt<int>::kind() const { return 1; }
template<> inline int audit_pt<float>::kind() const { return 2; }

// Never registered anywhere: the opaque-token half of the audit.
template<typename T>
struct audit_opaque { T v{}; };

inline void register_audit_points(jai::engine& e) {
    dynamic_binder<audit_pt_base>(e, "PBase")
        .constructor<>()
        .method("base_tag", &audit_pt_base::base_tag)
        .build();
    dynamic_binder<audit_pt<int>>(e, "PointI")
        .constructor<>()
        .constructor<int>()
        .property("x", &audit_pt<int>::x)
        .method("kind", &audit_pt<int>::kind)
        .method("doubled", &audit_pt<int>::doubled)
        .base_class<audit_pt_base>()
        .build();
    dynamic_binder<audit_pt<float>>(e, "PointF")
        .constructor<>()
        .constructor<float>()
        .property("x", &audit_pt<float>::x)
        .method("kind", &audit_pt<float>::kind)
        .method("doubled", &audit_pt<float>::doubled)
        .base_class<audit_pt_base>()
        .build();
}

} // namespace jai::foundry::tests

// JAI_TEMPLATE_BINDER registrations (must be outside namespace)

JAI_TEMPLATE_BINDER_NAMED(TplBinderVec2, jai::foundry::tests::TplBinderVec2, "TplBinderVec2",
    .property("x", &jai::foundry::tests::TplBinderVec2<T>::x)
    .property("y", &jai::foundry::tests::TplBinderVec2<T>::y)
    .method("length_squared", &jai::foundry::tests::TplBinderVec2<T>::length_squared)
);

JAI_TEMPLATE_BINDER_NAMED(TplBinderPoint, jai::foundry::tests::TplBinderPoint, "TplBinderPoint",
    .method("magnitude", &jai::foundry::tests::TplBinderPoint<T>::magnitude)
);

JAI_TEMPLATE_BINDER_2_NAMED(TplBinderKVPair, jai::foundry::tests::TplBinderKVPair, "TplBinderKVPair",
    .property("key", &jai::foundry::tests::TplBinderKVPair<T1, T2>::key)
    .property("value", &jai::foundry::tests::TplBinderKVPair<T1, T2>::value)
    .method("get_key", &jai::foundry::tests::TplBinderKVPair<T1, T2>::get_key)
    .method("get_value", &jai::foundry::tests::TplBinderKVPair<T1, T2>::get_value)
);

JAI_TEMPLATE_BINDER_NAMED(TplAutoDetectVec, jai::foundry::tests::TplAutoDetectVec, "TplAutoDetectVec",
    .property("a", &jai::foundry::tests::TplAutoDetectVec<T>::a)
    .property("b", &jai::foundry::tests::TplAutoDetectVec<T>::b)
    .method("dot", &jai::foundry::tests::TplAutoDetectVec<T>::dot)
);

JAI_BIND_TEMPLATE_NAMED(TplBinderVec2, jai::foundry::tests::TplBinderVec2, int);
JAI_BIND_TEMPLATE_NAMED(TplBinderVec2, jai::foundry::tests::TplBinderVec2, float);
JAI_BIND_TEMPLATE_NAMED(TplBinderVec2, jai::foundry::tests::TplBinderVec2, double);

JAI_BIND_TEMPLATE_2_NAMED(TplBinderKVPair, jai::foundry::tests::TplBinderKVPair, int, std::string);
JAI_BIND_TEMPLATE_2_NAMED(TplBinderKVPair, jai::foundry::tests::TplBinderKVPair, std::string, double);

namespace jai::foundry::tests {

class template_binder_tests : public suite {
public:
    template_binder_tests() : suite("Template Binder") {}

    void forge_tests() override {

        // -------------------- template-instantiation type-identity audit (2026-07)
        // Matrix result at audit time: registration/lookup, construction+dispatch,
        // shared_ptr paths (incl. the fixed composite intern), container elements,
        // conversion_registry, base upcasts, opaque tokens - ALL keyed correctly
        // (std::type_index or pointee-id keys); these pins keep them that way.
        // type_of() reports "object" for registered instances BY DESIGN (3ca8c06b)
        // and does not distinguish instantiations; registration_fingerprint does.

        test("audit_instantiations_register_and_dispatch_distinctly", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                register_audit_points(*e);
                // (a) registration/lookup: distinct definitions by name AND type_index
                auto def_i = e->get_class_definition("PointI");
                auto def_f = e->get_class_definition("PointF");
                check_not_null(def_i.get());
                check_not_null(def_f.get());
                check(def_i.get() != def_f.get(), "PointI/PointF share a class_definition");
                check(e->get_class_definition_by_type(std::type_index(typeid(audit_pt<int>))).get() == def_i.get(), "type_index->PointI");
                check(e->get_class_definition_by_type(std::type_index(typeid(audit_pt<float>))).get() == def_f.get(), "type_index->PointF");
                // (b) construction + method dispatch: same names, distinct behavior
                check_eq((int64_t)1, e->execute("auto i = PointI(21); i.kind()").as_int());
                check_eq((int64_t)2, e->execute("auto f = PointF(1.5); f.kind()").as_int());
                check_eq((int64_t)42, e->execute("i.doubled()").as_int());
                check_near(3.0, e->execute("f.doubled()").as_float(), 0.0001);
            }
        });

        test("audit_shared_ptr_paths_key_per_instantiation", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                register_audit_points(*e);
                // (c) the composite intern path (the fixed one: new/shared_ptr<T>() nodes)
                // and the factory path must both distinguish instantiations
                check_eq((int64_t)1, e->execute("var a = new PointI(); a.kind()").as_int());
                check_eq((int64_t)2, e->execute("var b = new PointF(); b.kind()").as_int());
                check_eq((int64_t)1, e->execute("auto c = shared_ptr<PointI>(); c.kind()").as_int());
                check_eq((int64_t)2, e->execute("auto d = shared_ptr<PointF>(); d.kind()").as_int());
                check_eq((int64_t)1, e->execute("shared_ptr<PointI> g = PointI(); g.kind()").as_int());
                check_eq((int64_t)2, e->execute("shared_ptr<PointF> h = PointF(); h.kind()").as_int());
                // wrong-pointee decl errors (typed sp enforcement ruling)
                check_throws([&]() { e->execute("shared_ptr<PointF> w = PointI();"); });
                // host-side: the factory returns distinct type_infos
                auto* ti_i = e->get_type_info_object(e->symbolize("PointI"));
                auto* ti_f = e->get_type_info_object(e->symbolize("PointF"));
                check(ti_i != nullptr && ti_f != nullptr && ti_i != ti_f, "object type_infos distinct");
                check(e->get_type_info_shared_ptr(ti_i) != e->get_type_info_shared_ptr(ti_f),
                      "shared_ptr type_infos collapsed across instantiations");
            }
        });

        test("audit_container_elements_and_upcast_keep_identity", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                register_audit_points(*e);
                // (d) container elements: each array's elements keep their own class
                check_eq((int64_t)1, e->execute("array<PointI> ai = [PointI(5)]; ai[0].kind()").as_int());
                check_eq((int64_t)2, e->execute("array<PointF> af = [PointF(2.5)]; af[0].kind()").as_int());
                // (f) upcast to the common base from both: each keeps ITS class
                check_eq((int64_t)1, e->execute("shared_ptr<PBase> u1 = PointI(7); u1.kind()").as_int());
                check_eq((int64_t)2, e->execute("shared_ptr<PBase> u2 = PointF(7.0); u2.kind()").as_int());
                check_eq((int64_t)99, e->execute("u1.base_tag()").as_int());
                check_eq((int64_t)99, e->execute("u2.base_tag()").as_int());
            }
        });

        test("audit_conversion_registry_keys_per_instantiation", [this]() {
            // (e) a conversion registered for ONE instantiation is invisible to the
            // other. Uses the never-bound audit_opaque pair: dynamic_binder installs
            // converters for every class it registers (a REGISTERED sibling reporting
            // has_conversion()==true is the binder working, not key cross-talk).
            auto e = jai::engine::make();
            auto reg = e->get_conversion_registry();
            check_not_null(reg.get());
            check_false(reg->has_conversion<audit_opaque<int>>());
            check_false(reg->has_conversion<audit_opaque<float>>());
            reg->register_conversion<audit_opaque<int>>(
                [](const script_value& v) { audit_opaque<int> p; p.v = static_cast<int>(v.as_int()); return p; },
                [eng = e.get()](const audit_opaque<int>& p) { return script_value(static_cast<script_int>(p.v), eng); });
            check_true(reg->has_conversion<audit_opaque<int>>());
            check_false(reg->has_conversion<audit_opaque<float>>());   // keyed on std::type_index: no inheritance
            auto roundtrip = reg->convert_from_script<audit_opaque<int>>(script_value((script_int)11, e.get()));
            check_eq(11, roundtrip.v);
        });

        test("audit_opaque_tokens_stay_pointer_identity", [this]() {
            // (g) unregistered instantiations passed through the host are opaque
            // tokens: non-null, pointer identity, no cross-instantiation equality
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                register_audit_points(*e);
                audit_opaque<double> od1; audit_opaque<double> od2; audit_opaque<int> oi;
                e->add_global("od1", e->make_value(&od1));
                e->add_global("od1b", e->make_value(&od1));   // same pointer, second token
                e->add_global("od2", e->make_value(&od2));
                e->add_global("oi", e->make_value(&oi));
                check_true(e->execute("od1 != null").as_bool());
                check_true(e->execute("od1 == od1b").as_bool());   // pointer identity
                check_false(e->execute("od1 == od2").as_bool());
                check_false(e->execute("od1 == oi").as_bool());    // no cross-instantiation equality
            }
        });

        test("audit_registration_fingerprint_distinguishes_instantiations", [this]() {
            // (h) type_of reports "object" for registered instances BY DESIGN and does
            // not distinguish; the registration fingerprint DOES.
            auto e_i = jai::engine::make();
            dynamic_binder<audit_pt<int>>(*e_i, "AuditP").constructor<>().method("kind", &audit_pt<int>::kind).build();
            auto e_f = jai::engine::make();
            dynamic_binder<audit_pt<float>>(*e_f, "AuditP").constructor<>().method("kind", &audit_pt<float>::kind).build();
            // same registered NAME + shape, but the instances behave as their own C++ types
            check_eq((int64_t)1, e_i->execute("AuditP().kind()").as_int());
            check_eq((int64_t)2, e_f->execute("AuditP().kind()").as_int());
            jai::stdlib::register_all(*e_i);
            check_eq(std::string("object"), e_i->execute("type_of(AuditP())").as<std::string>());
            // and differently-shaped registrations fingerprint differently
            auto e_two = jai::engine::make();
            register_audit_points(*e_two);
            check_ne(e_i->registration_fingerprint(), e_two->registration_fingerprint());
        });

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

        test("template_binder_type_names", [this]() {
            auto int_name = jai_template_binder_TplBinderVec2<int>::type_name();
            auto float_name = jai_template_binder_TplBinderVec2<float>::type_name();
            auto double_name = jai_template_binder_TplBinderVec2<double>::type_name();

            check_eq(int_name, std::string("TplBinderVec2<int32>"), "int32 variant name");
            check_eq(float_name, std::string("TplBinderVec2<float>"), "float variant name");
            check_eq(double_name, std::string("TplBinderVec2<double>"), "double variant name");
        });

        test("template_binder_2_type_names", [this]() {
            auto name1 = jai_template_binder_TplBinderKVPair<int, std::string>::type_name();
            auto name2 = jai_template_binder_TplBinderKVPair<std::string, double>::type_name();

            check_eq(name1, std::string("TplBinderKVPair<int32, string>"), "int32,string variant");
            check_eq(name2, std::string("TplBinderKVPair<string, double>"), "string,double variant");
        });

        test("static_type_registry_registration", [this]() {
            check(static_type_registry::is_registered<TplBinderVec2<int>>(),
                  "TplBinderVec2<int> is registered");
            check(static_type_registry::is_registered<TplBinderVec2<float>>(),
                  "TplBinderVec2<float> is registered");
            check(static_type_registry::is_registered<TplBinderVec2<double>>(),
                  "TplBinderVec2<double> is registered");

            check(!static_type_registry::is_registered<TplBinderVec2<int64_t>>(),
                  "TplBinderVec2<int64_t> NOT registered");
        });

        test("static_type_registry_two_param", [this]() {
            check(static_type_registry::is_registered<TplBinderKVPair<int, std::string>>(),
                  "TplBinderKVPair<int, string> is registered");
            check(static_type_registry::is_registered<TplBinderKVPair<std::string, double>>(),
                  "TplBinderKVPair<string, double> is registered");

            check(!static_type_registry::is_registered<TplBinderKVPair<int, int>>(),
                  "TplBinderKVPair<int, int> NOT registered");
        });

        test("has_static_type_for_template_variants", [this]() {
            check(has_static_type_v<TplBinderVec2<int>>,
                  "TplBinderVec2<int> has static type");
            check(has_static_type_v<TplBinderVec2<float>>,
                  "TplBinderVec2<float> has static type");

            check(has_static_type_v<TplBinderKVPair<int, std::string>>,
                  "TplBinderKVPair<int, string> has static type");
        });

        test("bind_template_variant_to_engine", [this]() {
            auto eng = make_engine();

            eng->bind_static_type<TplBinderVec2<int>>();

            eng->execute("auto v = TplBinderVec2<int32>();");

            eng->execute("v.x = 3;");
            eng->execute("v.y = 4;");

            check_eq(eng->execute("v.x").as<int>(), 3, "x property works");
            check_eq(eng->execute("v.y").as<int>(), 4, "y property works");

            check_eq(eng->execute("v.length_squared()").as<int>(), 25, "length_squared method works");
        });

        test("bind_multiple_template_variants", [this]() {
            auto eng = make_engine();

            eng->bind_static_type<TplBinderVec2<int>>();
            eng->bind_static_type<TplBinderVec2<float>>();
            eng->bind_static_type<TplBinderVec2<double>>();

            eng->execute("auto vi = TplBinderVec2<int32>();");
            eng->execute("vi.x = 1; vi.y = 2;");
            check_eq(eng->execute("vi.length_squared()").as<int>(), 5, "int32 variant works");

            eng->execute("auto vf = TplBinderVec2<float>();");
            eng->execute("vf.x = 1.5; vf.y = 2.0;");
            auto len_sq = eng->execute("vf.length_squared()").as<float>();
            check(std::abs(len_sq - 6.25f) < 0.001f, "float variant works");

            eng->execute("auto vd = TplBinderVec2<double>();");
            eng->execute("vd.x = 3.0; vd.y = 4.0;");
            check_eq(eng->execute("vd.length_squared()").as<double>(), 25.0, "double variant works");
        });

        test("bind_two_param_template_variant", [this]() {
            auto eng = make_engine();

            eng->bind_static_type<TplBinderKVPair<int, std::string>>();

            eng->execute("auto kv = TplBinderKVPair<int32, string>();");
            eng->execute("kv.key = 42;");
            eng->execute("kv.value = \"hello\";");

            check_eq(eng->execute("kv.get_key()").as<int>(), 42, "get_key works");
            check_eq(eng->execute("kv.get_value()").as<std::string>(), "hello", "get_value works");
        });

        test("auto_bind_template_registers_on_instantiation", [this]() {
            TplBinderPoint<int> pt;
            (void)pt;  // Suppress unused warning
            check(static_type_registry::is_registered<TplBinderPoint<int>>(),
                  "TplBinderPoint<int> auto-registered via CRTP");
        });

        test("auto_bind_template_different_params", [this]() {
            TplBinderPoint<float> pt_float;
            TplBinderPoint<double> pt_double;
            (void)pt_float;
            (void)pt_double;

            check(static_type_registry::is_registered<TplBinderPoint<float>>(),
                  "TplBinderPoint<float> auto-registered");
            check(static_type_registry::is_registered<TplBinderPoint<double>>(),
                  "TplBinderPoint<double> auto-registered");
        });

        test("template_variant_serialization", [this]() {
            TplBinderVec2<int> v1{10, 20};

            std::vector<uint8_t> buffer;
            {
                serialization::binary_archive_writer ar(buffer);
                ar.begin_object("TplBinderVec2<int32>", 1);
                jai_static_type<TplBinderVec2<int>>::save(ar, v1);
                ar.end_object();
            }

            auto eng = make_engine();
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

        test("template_binder_property_counts", [this]() {
            constexpr size_t vec2_props = jai_static_type<TplBinderVec2<int>>::binder.property_count();
            constexpr size_t vec2_methods = jai_static_type<TplBinderVec2<int>>::binder.method_count();

            check_eq(vec2_props, size_t(2), "TplBinderVec2 has 2 properties");
            check_eq(vec2_methods, size_t(1), "TplBinderVec2 has 1 method");

            constexpr size_t kv_props = jai_static_type<TplBinderKVPair<int, std::string>>::binder.property_count();
            constexpr size_t kv_methods = jai_static_type<TplBinderKVPair<int, std::string>>::binder.method_count();

            check_eq(kv_props, size_t(2), "TplBinderKVPair has 2 properties");
            check_eq(kv_methods, size_t(2), "TplBinderKVPair has 2 methods");
        });

        test("shared_template_object_access", [this]() {
            auto eng = make_engine();

            eng->bind_static_type<TplBinderVec2<double>>();

            auto cpp_vec = std::make_shared<TplBinderVec2<double>>(5.0, 12.0);
            eng->add_global("vec", eng->make_object(cpp_vec));

            check_eq(eng->execute("vec.x").as<double>(), 5.0, "Script reads x");
            check_eq(eng->execute("vec.y").as<double>(), 12.0, "Script reads y");

            check_eq(eng->execute("vec.length_squared()").as<double>(), 169.0, "Script calls method");

            eng->execute("vec.x = 8.0;");
            check_eq(cpp_vec->x, 8.0, "C++ sees script modification");

            check_eq(eng->execute("vec.length_squared()").as<double>(), 208.0, "Method reflects change");
        });

        test("bind_static_type_is_idempotent", [this]() {
            auto eng = make_engine();

            eng->bind_static_type<TplBinderVec2<int>>();
            eng->bind_static_type<TplBinderVec2<int>>();
            eng->bind_static_type<TplBinderVec2<int>>();

            eng->execute("auto v = TplBinderVec2<int32>();");
            eng->execute("v.x = 7; v.y = 24;");
            check_eq(eng->execute("v.length_squared()").as<int>(), 625, "Type still works after multiple binds");
        });

        test("bind_static_types_multiple_same", [this]() {
            auto eng = make_engine();

            eng->bind_static_types<TplBinderVec2<float>, TplBinderVec2<float>, TplBinderVec2<double>>();

            eng->execute("auto vf = TplBinderVec2<float>();");
            eng->execute("auto vd = TplBinderVec2<double>();");
            check(true, "Multiple binds of same type didn't crash");
        });

        test("has_template_binder_detection", [this]() {
            check(has_template_binder_v<TplAutoDetectVec<int>>,
                  "TplAutoDetectVec<int> detected via has_template_binder_v");
            check(has_template_binder_v<TplAutoDetectVec<float>>,
                  "TplAutoDetectVec<float> detected");
            check(has_template_binder_v<TplBinderVec2<double>>,
                  "TplBinderVec2<double> detected");

            check(has_template_binder_2_v<TplBinderKVPair<int, std::string>>,
                  "TplBinderKVPair<int, string> detected via has_template_binder_2_v");

            check(has_any_template_binder_v<TplAutoDetectVec<int>>,
                  "TplAutoDetectVec<int> detected via has_any_template_binder_v");
            check(has_any_template_binder_v<TplBinderKVPair<std::string, double>>,
                  "TplBinderKVPair detected via has_any_template_binder_v");

            check(!has_template_binder_v<std::string>,
                  "std::string NOT detected as template binder type");
            check(!has_any_template_binder_v<int>,
                  "int NOT detected as template binder type");
        });

        test("template_binder_accessor_type_name", [this]() {
            auto name_int = template_binder_accessor<TplAutoDetectVec<int>>::type_name();
            auto name_float = template_binder_accessor<TplAutoDetectVec<float>>::type_name();

            check_eq(name_int, std::string("TplAutoDetectVec<int32>"), "accessor type_name for int");
            check_eq(name_float, std::string("TplAutoDetectVec<float>"), "accessor type_name for float");
        });

        test("auto_detect_bind_without_explicit_registration", [this]() {
            auto eng = make_engine();

            eng->bind_static_type<TplAutoDetectVec<int>>();

            eng->execute("auto v = TplAutoDetectVec<int32>();");
            eng->execute("v.a = 6;");
            eng->execute("v.b = 7;");

            check_eq(eng->execute("v.a").as<int>(), 6, "auto-detected property a");
            check_eq(eng->execute("v.b").as<int>(), 7, "auto-detected property b");
            check_eq(eng->execute("v.dot()").as<int>(), 42, "auto-detected method dot");
        });

        test("auto_detect_serialization", [this]() {
            TplAutoDetectVec<float> v1{3.0f, 4.0f};

            std::vector<uint8_t> buffer;
            {
                serialization::binary_archive_writer ar(buffer);
                ar.begin_object("TplAutoDetectVec<float>", 1);
                template_binder_accessor<TplAutoDetectVec<float>>::save(ar, v1);
                ar.end_object();
            }

            auto eng = make_engine();
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
            auto eng = make_engine();

            eng->bind_static_type<TplAutoDetectVec<int>>();
            eng->bind_static_type<TplAutoDetectVec<float>>();
            eng->bind_static_type<TplAutoDetectVec<double>>();

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

        test("auto_bind_int32_also_binds_int64", [this]() {
            auto eng = make_engine();

            eng->bind_static_type<TplAutoDetectVec<int>>();

            eng->execute("auto v32 = TplAutoDetectVec<int32>();");
            eng->execute("v32.a = 5; v32.b = 6;");
            check_eq(eng->execute("v32.dot()").as<int>(), 30, "int32 variant works");

            eng->execute("auto v64 = TplAutoDetectVec<int>();");
            eng->execute("v64.a = 7; v64.b = 8;");
            check_eq(eng->execute("v64.dot()").as<int64_t>(), 56, "int (64-bit) variant auto-bound");
        });

        test("auto_bind_int64_also_binds_int32", [this]() {
            auto eng = make_engine();

            eng->bind_static_type<TplAutoDetectVec<int64_t>>();

            eng->execute("auto v64 = TplAutoDetectVec<int>();");
            eng->execute("v64.a = 10; v64.b = 11;");
            check_eq(eng->execute("v64.dot()").as<int64_t>(), 110, "int (64-bit) variant works");

            eng->execute("auto v32 = TplAutoDetectVec<int32>();");
            eng->execute("v32.a = 3; v32.b = 4;");
            check_eq(eng->execute("v32.dot()").as<int>(), 12, "int32 variant auto-bound");
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::template_binder_tests)
