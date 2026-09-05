#pragma once
// Script `==` on heavy operands is memberwise, like C++ containers (Dev ruling 2026-09-05):
// arrays elementwise, maps entrywise in key order, class instances fieldwise in slot order,
// each with an early out on a size or class mismatch and on the first unequal member. The
// recursion runs through the backend's full `==` (the `eq` callback), so a nested user
// operator== is honored and the parallel operator wall keeps its verdict. Identity is a
// separate question, answered by same_object(x, y) (engine.cpp). Cycles cannot arise: value
// fields cannot contain their owner and shared_ptr operands compare by pointer, so the
// depth cap below is a safety net for pathological value nesting, never a semantic.
// KEEP BYTE-PARALLEL: both backends call these from their handle_equal twins.
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <cstring>

namespace jai::detail {

    inline constexpr size_t k_structural_equality_max_depth = 200;
    inline constexpr std::string_view structural_equality_depth_text =
        "Cannot compare: the values nest deeper than 200 levels";

    struct equality_depth_scope {
        size_t& depth;
        explicit equality_depth_scope(size_t& d) : depth(d) { ++depth; }
        ~equality_depth_scope() { --depth; }
    };

    template <class EqualFn>
    inline checked_result<script_value> arrays_equal(const script_value& lv, const script_value& rv, engine* eng,
                                                     size_t& depth, EqualFn&& eq) {
        const script_array* a = lv.deref().unchecked_array_node();
        const script_array* b = rv.deref().unchecked_array_node();
        if (a == b) { return script_value(true, eng); }
        const size_t n = a->size();
        if (n != b->size()) { return script_value(false, eng); }
        if (depth >= k_structural_equality_max_depth) {
            return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation), structural_equality_depth_text);
        }
        equality_depth_scope scope(depth);
        if (a->is_typed() && b->is_typed() && a->kind() == b->kind()) {
            switch (a->kind()) {
            case script_array::kind_t::i64: return script_value(a->ints() == b->ints(), eng);
            case script_array::kind_t::f64: {
                const auto& x = a->floats(); const auto& y = b->floats();
                for (size_t i = 0; i < n; ++i) { if (!(x[i] == y[i])) { return script_value(false, eng); } }   // == on doubles: NaN never equal
                return script_value(true, eng);
            }
            case script_array::kind_t::b8:
            case script_array::kind_t::c8: return script_value(a->bytes() == b->bytes(), eng);
            case script_array::kind_t::str: {
                const auto& x = a->strs(); const auto& y = b->strs();
                for (size_t i = 0; i < n; ++i) {
                    const bool same = (x[i].get() == y[i].get()) || (x[i] && y[i] && *x[i] == *y[i]) || (!x[i] && !y[i]);
                    if (!same) { return script_value(false, eng); }
                }
                return script_value(true, eng);
            }
            default: break;   // obj: element `==` below (a user operator== may apply)
            }
        }
        if (!a->is_typed() && !b->is_typed()) {
            const auto& x = a->values(); const auto& y = b->values();
            for (size_t i = 0; i < n; ++i) {
                auto r = eq(x[i], y[i]);
                if (!r) { return r; }
                if (!r.value().unchecked_as_bool()) { return script_value(false, eng); }
            }
            return script_value(true, eng);
        }
        for (size_t i = 0; i < n; ++i) {
            script_value x = a->get(i, eng);
            script_value y = b->get(i, eng);
            auto r = eq(x, y);
            if (!r) { return r; }
            if (!r.value().unchecked_as_bool()) { return script_value(false, eng); }
        }
        return script_value(true, eng);
    }

    // Keys compare under the map's own equivalence (the ordering that placed them), values
    // under script `==`: {1: "a"} and {1.0: "a"} hold different keys and stay unequal.
    template <class EqualFn>
    inline checked_result<script_value> maps_equal(const script_value& lv, const script_value& rv, engine* eng,
                                                   size_t& depth, EqualFn&& eq) {
        const script_map& a = lv.deref().unchecked_as_map();
        const script_map& b = rv.deref().unchecked_as_map();
        if (&a == &b) { return script_value(true, eng); }
        if (a.size() != b.size()) { return script_value(false, eng); }
        if (depth >= k_structural_equality_max_depth) {
            return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation), structural_equality_depth_text);
        }
        equality_depth_scope scope(depth);
        auto ia = a.begin(); auto ib = b.begin();
        for (; ia != a.end(); ++ia, ++ib) {
            if ((ia->first <=> ib->first) != 0) { return script_value(false, eng); }
            auto r = eq(ia->second, ib->second);
            if (!r) { return r; }
            if (!r.value().unchecked_as_bool()) { return script_value(false, eng); }
        }
        return script_value(true, eng);
    }

    // Two script class instance VALUES of the SAME class (a derived instance is never equal to
    // a base one); fields in slot order, then any overflow fields by id. Returns nullopt when
    // either side is a shared_ptr handle or not a script class instance (identity rules apply).
    template <class EqualFn>
    inline std::optional<checked_result<script_value>> instances_equal(const script_value& lv, const script_value& rv, engine* eng,
                                                                       size_t& depth, EqualFn&& eq) {
        const script_value& l = lv.deref();
        const script_value& r = rv.deref();
        if (l.raw_storage_index() != script_value::TYPEID_OBJECT || r.raw_storage_index() != script_value::TYPEID_OBJECT) { return std::nullopt; }
        // shared_ptr handles compare by pointer, as in C++ (the value behind them compares fieldwise
        // only when both sides are values)
        if (l.type() == script_value_type::jai_shared_ptr_type || r.type() == script_value_type::jai_shared_ptr_type) { return std::nullopt; }
        const auto& lh = l.unchecked_object_handle();
        const auto& rh = r.unchecked_object_handle();
        if (!lh || !rh || !lh->is_class_instance_wrapper || !rh->is_class_instance_wrapper || !lh->data || !rh->data) { return std::nullopt; }
        if (lh.get() == rh.get()) { return checked_result<script_value>(script_value(true, eng)); }
        const auto* a = static_cast<const class_instance*>(lh->data.get());
        const auto* b = static_cast<const class_instance*>(rh->data.get());
        const class_definition* ca = a->get_class_definition();
        const class_definition* cb = b->get_class_definition();
        if (!ca || !cb || !ca->is_script_class() || !cb->is_script_class()) { return std::nullopt; }
        if (ca != cb) { return checked_result<script_value>(script_value(false, eng)); }
        if (depth >= k_structural_equality_max_depth) {
            return checked_result<script_value>(make_error_code(runtime_error_code::unsupported_operation), structural_equality_depth_text);
        }
        equality_depth_scope scope(depth);
        std::vector<std::pair<uint64_t, const script_value*>> fa;
        a->for_each_field([&](uint64_t id, const script_value& v) { fa.emplace_back(id, &v); });
        size_t nb = 0;
        b->for_each_field([&](uint64_t, const script_value&) { ++nb; });
        if (fa.size() != nb) { return checked_result<script_value>(script_value(false, eng)); }
        for (const auto& [id, va] : fa) {
            const script_value* vb = b->find_field_value(id);
            if (!vb) { return checked_result<script_value>(script_value(false, eng)); }
            auto res = eq(*va, *vb);
            if (!res) { return res; }
            if (!res.value().unchecked_as_bool()) { return checked_result<script_value>(script_value(false, eng)); }
        }
        return checked_result<script_value>(script_value(true, eng));
    }

    // same_object(x, y): identity. References resolve to their referents and shared_ptr
    // handles to the object they share; arrays, maps, class instances, host objects and
    // functions are the same object when they are one allocation. Primitives and strings
    // carry no identity in the language (strings share buffers copy-on-write), so two of
    // them are "the same" when they are equal values. A typed null is the same as null.
    inline bool same_object(const script_value& lv, const script_value& rv) {
        const script_value l = lv.deref().try_unwrap_transparent_wrapper();
        const script_value r = rv.deref().try_unwrap_transparent_wrapper();
        const size_t li = l.raw_storage_index();
        const size_t ri = r.raw_storage_index();
        if (l.is_null() || r.is_null()) { return l.is_null() && r.is_null(); }
        switch (li) {
        case script_value::TYPEID_ARRAY:
            return ri == script_value::TYPEID_ARRAY && l.unchecked_array_node() == r.unchecked_array_node();
        case script_value::TYPEID_MAP:
            return ri == script_value::TYPEID_MAP && &l.unchecked_as_map() == &r.unchecked_as_map();
        case script_value::TYPEID_OBJECT:
            return ri == script_value::TYPEID_OBJECT && l.unchecked_object_handle().get() == r.unchecked_object_handle().get();
        case script_value::TYPEID_STRING:
            return ri == script_value::TYPEID_STRING && l.unchecked_as_string() == r.unchecked_as_string();
        case script_value::TYPEID_INT:
        case script_value::TYPEID_FLOAT:
        case script_value::TYPEID_BOOL:
        case script_value::TYPEID_CHAR:
            return l == r;
        default:
            return li == ri && l.get_object_holder().get() == r.get_object_holder().get() && l.get_object_holder() != nullptr;
        }
    }

} // namespace jai::detail
