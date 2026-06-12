#pragma once

#ifndef JAISCRIPT_SERIALIZATION_ARCHIVE_IMPL_HPP
#define JAISCRIPT_SERIALIZATION_ARCHIVE_IMPL_HPP

#include <jaiscript/serialization/archive.hpp>

// ============================================================
// Configurable limits - define these before including to override
// ============================================================

// Maximum serialization depth (default: 1000)
// This is high enough for complex nested structures but prevents stack overflow
// from maliciously crafted data or circular references
#ifndef JAI_MAX_SERIALIZATION_DEPTH
#define JAI_MAX_SERIALIZATION_DEPTH 1000
#endif

#include <jaiscript/serialization/serialization_metadata.hpp>
#include <jaiscript/serialization/traits.hpp>
#include <jaiscript/serialization/polymorphic.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/type_info.hpp>
#include <jaiscript/core/engine.hpp>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <map>
#include <unordered_map>
#include <variant>
#include <optional>
#include <tuple>
#include <utility>
#include <concepts>
#include <stack>

namespace jai {

// Serialization-specific exception for better error categorization
class serialization_error : public runtime_error {
public:
    explicit serialization_error(const std::string& message) : runtime_error(message) {}
    explicit serialization_error(const char* message) : runtime_error(message) {}
    
    // Enhanced constructor with context information
    serialization_error(const std::string& message, const std::string& property_name, const std::string& class_name = "")
        : runtime_error(build_message(message, property_name, class_name)) {}

private:
    static std::string build_message(const std::string& message, const std::string& property_name, const std::string& class_name) {
        std::string result = "Serialization error: " + message;
        if (!property_name.empty()) {
            result += " (property: " + property_name;
            if (!class_name.empty()) {
                result += " in class: " + class_name;
            }
            result += ")";
        }
        return result;
    }
};

}

namespace jai {
namespace serialization {

// Forward/concept declarations provided by archive.hpp (included above)

// ADL-findable free-function detection. Must live at namespace scope: inside the
// archive classes, unqualified save/load/serialize would bind to the archives' own
// member functions at template-definition time on conforming compilers.
namespace adl_detect {
	template<typename Archive, typename T, typename = void>
	struct has_adl_save : std::false_type {};
	template<typename Archive, typename T>
	struct has_adl_save<Archive, T, std::void_t<decltype(save(std::declval<Archive&>(), std::declval<const T&>()))>> : std::true_type {};

	template<typename Archive, typename T, typename = void>
	struct has_adl_load : std::false_type {};
	template<typename Archive, typename T>
	struct has_adl_load<Archive, T, std::void_t<decltype(load(std::declval<Archive&>(), std::declval<T&>()))>> : std::true_type {};

	template<typename Archive, typename T, typename = void>
	struct has_adl_serialize : std::false_type {};
	template<typename Archive, typename T>
	struct has_adl_serialize<Archive, T, std::void_t<decltype(serialize(std::declval<Archive&>(), std::declval<T&>()))>> : std::true_type {};
}

// ============================================================================
// CRTP Archive Writer Base Template
// ============================================================================
// All serialization functions call I/O methods through self() for static dispatch.
// Derived classes implement I/O methods as regular (non-virtual) methods.
// See JAI_ARCHIVE_DEVIRTUALIZATION.md for design rationale.

// ============================================================================
// Serialization Context - tracks whether we're writing property values or array elements
// ============================================================================
// This enables automatic wrapping decisions:
// - In PropertyValue context: complex types are already inside an object, no wrapping needed
// - In ArrayElement context: complex types need to be wrapped in begin_object/end_object
enum class SerializationContext {
    Root,           // Top-level, before any container
    PropertyValue,  // After write_property_name, expecting a single value
    ArrayElement,   // Inside an array, expecting sequence of values
    MapValue,       // After write_map_key, expecting a single value
    ObjectBody      // Inside an object body - don't wrap the next complex value
};

template<class Derived>
class archive_writer_impl : public archive_base {
public:
    // Direction detection tag for is_save trait
    struct is_writer_tag {};

protected:
    // CRTP self accessor - casts to derived type for non-virtual dispatch
    Derived* self() { return static_cast<Derived*>(this); }
    const Derived* self() const { return static_cast<const Derived*>(this); }

    // Engine reference for creating script_values during serialization
    engine* engine_ref_;

    // Shared pointer tracking to prevent duplicate serialization
    std::unordered_map<const void*, uint32_t> shared_ptr_ids_;
    uint32_t next_shared_id_ = 1;  // 0 reserved for null

    // Depth tracking for recursion limit
    int current_depth_ = 0;

    // Context tracking for automatic wrapping decisions
    std::stack<SerializationContext> context_stack_;

    // RAII guard for depth tracking - throws on depth exceeded
    struct depth_guard {
        int& depth;
        bool acquired = false;

        depth_guard(int& d) : depth(d) {
            if (depth >= JAI_MAX_SERIALIZATION_DEPTH) {
                throw serialization_error(
                    "Maximum serialization depth (" + std::to_string(JAI_MAX_SERIALIZATION_DEPTH) +
                    ") exceeded - possible circular reference or deeply nested structure"
                );
            }
            ++depth;
            acquired = true;
        }

        ~depth_guard() {
            if (acquired) --depth;
        }

        // Non-copyable
        depth_guard(const depth_guard&) = delete;
        depth_guard& operator=(const depth_guard&) = delete;
    };

    // Check if a shared_ptr has been serialized before
    // Returns: pair<id, is_new> where is_new indicates if this is the first time
    std::pair<uint32_t, bool> track_shared_ptr(const void* raw_ptr) {
        if (raw_ptr == nullptr) {
            return {0, false};  // null pointer
        }

        auto it = shared_ptr_ids_.find(raw_ptr);
        if (it != shared_ptr_ids_.end()) {
            return {it->second, false};  // Already seen
        }

        // New shared_ptr
        uint32_t id = next_shared_id_++;
        shared_ptr_ids_[raw_ptr] = id;
        return {id, true};
    }

    std::map<std::string, bool> property_enabled_;
    uint32_t version_ = 0;

public:
    // Constructor that accepts engine pointer
    explicit archive_writer_impl(engine* eng = nullptr) : engine_ref_(eng) {
        context_stack_.push(SerializationContext::Root);
    }

    ~archive_writer_impl() = default;

    // ============================================================================
    // Context management - used by derived archives and serialization logic
    // ============================================================================
    SerializationContext current_context() const {
        return context_stack_.empty() ? SerializationContext::Root : context_stack_.top();
    }

    void push_context(SerializationContext ctx) {
        context_stack_.push(ctx);
    }

    void pop_context() {
        if (!context_stack_.empty() && context_stack_.top() != SerializationContext::Root) {
            context_stack_.pop();
        }
    }

    // Complex types with save/serialize/property_mgr need object wrapping UNLESS
    // we're already inside an object body that was created for this value.
    // ObjectBody context means the object wrapper was already written externally.
    bool needs_object_wrapper() const {
        auto ctx = current_context();
        return ctx != SerializationContext::ObjectBody;
    }

    // Transition from PropertyValue/MapValue context after writing a value
    void consume_value_context() {
        auto ctx = current_context();
        if (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue) {
            pop_context();
        }
    }

    // Set engine reference
    void set_engine(engine* eng) {
        engine_ref_ = eng;
    }

    // Get engine reference
    engine* get_engine() const {
        return engine_ref_;
    }

    // ============================================================================
    // Shared pointer ID tracking (for de-duplication)
    // ============================================================================
    std::pair<uint32_t, bool> get_or_assign_shared_id(const void* raw_ptr) {
        return track_shared_ptr(raw_ptr);
    }

    uint32_t lookup_shared_id(const void* raw_ptr) const {
        if (raw_ptr == nullptr) return 0;
        auto it = shared_ptr_ids_.find(raw_ptr);
        return it != shared_ptr_ids_.end() ? it->second : 0;
    }

    // Version being serialized
    void set_version(uint32_t version) { version_ = version; }
    uint32_t get_version() const { return version_; }

    // ============================================================================
    // Public API: serialize("name", value) - always use named properties
    // ============================================================================

    // Serialize primitives (named) - includes enums
    template<typename T>
    std::enable_if_t<std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, std::string>>
    serialize(const char* name, const T& value) {
        self()->write_property_name(name);
        push_context(SerializationContext::PropertyValue);
        write_primitive(value);
        consume_value_context();
    }

    // Serialize custom types (named)
    template<typename T>
    std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_enum_v<T> && !std::is_same_v<T, std::string>>
    serialize(const char* name, const T& value) {
        self()->write_property_name(name);
        push_context(SerializationContext::PropertyValue);
        write_value(value);  // Use write_value which is context-aware
        consume_value_context();
    }

    // operator() as alias for serialize()
    template<typename T>
    void operator()(const char* name, const T& value) {
        serialize(name, value);
    }

    // operator() for nvp
    template<typename T>
    void operator()(const nvp<T>& nv) {
        serialize(nv.name, nv.value);
    }

    // Variadic operator() for jai::serialization::nvp
    template<typename T, typename... Rest>
    void operator()(const nvp<T>& first, const Rest&... rest) {
        serialize(first.name, first.value);
        if constexpr (sizeof...(rest) > 0) {
            operator()(rest...);
        }
    }

private:
    // Helper traits for SFINAE — uses Derived (the concrete archive) instead of a stub type.
    // These traits are only evaluated inside member function template enable_if/if-constexpr,
    // which are instantiated at the call site where Derived is complete.
    // Uses ::jai::access::member_* to detect protected methods via friendship.

    // Check for versioned save(Archive&, uint32_t) - for Cereal-compatible MV types
    template<typename T, typename = void>
    struct has_versioned_save_method : std::false_type {};
    template<typename T>
    struct has_versioned_save_method<T, std::void_t<decltype(::jai::access::member_save(std::declval<Derived&>(), std::declval<const T&>(), std::uint32_t(0)))>> : std::true_type {};

    // Check for non-versioned save(Archive&) - for JaiScript-only types
    template<typename T, typename = void>
    struct has_simple_save_method : std::false_type {};
    template<typename T>
    struct has_simple_save_method<T, std::void_t<decltype(::jai::access::member_save(std::declval<Derived&>(), std::declval<const T&>()))>> : std::true_type {};

    // Combined: has either versioned or simple save method
    template<typename T>
    struct has_save_method : std::bool_constant<has_versioned_save_method<T>::value || has_simple_save_method<T>::value> {};

    // Check for versioned serialize(Archive&, uint32_t) method
    template<typename T, typename = void>
    struct has_versioned_serialize_method : std::false_type {};
    template<typename T>
    struct has_versioned_serialize_method<T, std::void_t<decltype(::jai::access::member_serialize(std::declval<Derived&>(), std::declval<T&>(), std::uint32_t(0)))>> : std::true_type {};

    // Check for non-versioned serialize(Archive&) method
    template<typename T, typename = void>
    struct has_simple_serialize_method : std::false_type {};
    template<typename T>
    struct has_simple_serialize_method<T, std::void_t<decltype(::jai::access::member_serialize(std::declval<Derived&>(), std::declval<T&>()))>> : std::true_type {};

    // Combined: has either versioned or simple serialize method
    template<typename T>
    struct has_serialize_method : std::bool_constant<has_versioned_serialize_method<T>::value || has_simple_serialize_method<T>::value> {};

    template<typename T, typename = void>
    struct has_property_mgr : std::false_type {};
    template<typename T>
    struct has_property_mgr<T, std::void_t<decltype(std::declval<T&>().property_mgr)>> : std::true_type {};

    // ADL-findable save(Archive&, const T&) / serialize(Archive&, T&) free functions
    template<typename T> using has_adl_save = adl_detect::has_adl_save<Derived, T>;
    template<typename T> using has_adl_serialize = adl_detect::has_adl_serialize<Derived, T>;

public:
    // operator() for types with member save method
    // Prefers versioned save(Archive&, uint32_t), falls back to simple save(Archive&)
    // Context-aware: wraps in begin_object/end_object when called from array/root context
    // Uses access:: to support protected methods that friend jai::access
    template<typename T, std::enable_if_t<has_save_method<T>::value, int> = 0>
    void operator()(const T& obj) {
        bool wrap = needs_object_wrapper();
        if (wrap) {
            self()->begin_object();
            push_context(SerializationContext::ObjectBody);  // Prevent nested wrapping
        }
        if constexpr (has_versioned_save_method<T>::value) {
            ::jai::access::member_save(*self(), obj, 0);  // Call versioned save with version=0
        } else {
            ::jai::access::member_save(*self(), obj);     // Call simple save
        }
        if (wrap) {
            pop_context();
            self()->end_object();
        }
    }

    // operator() for types with serialize() method (but no save())
    // Prefers versioned serialize(Archive&, uint32_t), falls back to simple serialize(Archive&)
    // Context-aware: wraps in begin_object/end_object when called from array/root context
    // Uses access:: to support protected methods that friend jai::access
    template<typename T, std::enable_if_t<
        !has_save_method<T>::value &&
        has_serialize_method<T>::value, int> = 0>
    void operator()(const T& obj) {
        bool wrap = needs_object_wrapper();
        if (wrap) {
            self()->begin_object();
            push_context(SerializationContext::ObjectBody);  // Prevent nested wrapping
        }
        if constexpr (has_versioned_serialize_method<T>::value) {
            ::jai::access::member_serialize(*self(), const_cast<T&>(obj), 0);  // Call versioned serialize with version=0
        } else {
            ::jai::access::member_serialize(*self(), const_cast<T&>(obj));     // Call simple serialize
        }
        if (wrap) {
            pop_context();
            self()->end_object();
        }
    }

    // operator() for property_owner types WITHOUT custom save() or serialize()
    // Context-aware: wraps in begin_object/end_object when called from array/root context
    template<typename T, std::enable_if_t<
        !has_save_method<T>::value &&
        !has_serialize_method<T>::value &&
        has_property_mgr<T>::value, int> = 0>
    void operator()(const T& obj) {
        bool wrap = needs_object_wrapper();
        if (wrap) {
            self()->begin_object();
            push_context(SerializationContext::ObjectBody);  // Prevent nested wrapping
        }
        obj.property_mgr.save(*self());
        if (wrap) {
            pop_context();
            self()->end_object();
        }
    }

    // operator() for primitives (arithmetic, enum, string)
    template<typename T>
    std::enable_if_t<std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, std::string>>
    operator()(const T& value) {
        write_primitive(value);
    }

    // operator() fallback for types without save(), serialize(), property_mgr, or primitives
    // Handles STL containers and smart pointers directly (no ADL needed)
    template<typename T, std::enable_if_t<
        !has_save_method<T>::value &&
        !has_serialize_method<T>::value &&
        !has_property_mgr<T>::value &&
        !std::is_arithmetic_v<T> &&
        !std::is_enum_v<T> &&
        !std::is_same_v<T, std::string>, int> = 0>
    void operator()(const T& value) {
        using namespace serialization_traits;

        // ====================================================================
        // Smart pointers (must check before containers since they're iterable)
        // ====================================================================
        // shared_ptr: serialize as {"$id": N, "$val": {...}} or {"$id": N} if reference
        // Polymorphic types get {"$id": N, "$type": "ConcreteType", "$val": {...}}
        if constexpr (is_std_shared_ptr_v<T>) {
            if (!value) {
                self()->write_null();
            } else {
                auto [id, is_new] = self()->get_or_assign_shared_id(value.get());
                self()->begin_object();
                push_context(SerializationContext::ObjectBody);
                self()->serialize("$id", id);
                if (is_new) {
                    write_shared_payload(value);
                }
                pop_context();
                self()->end_object();
            }
        }
        // weak_ptr: serialize as {"$ref": N, "$val"?: {...}} or null. The "$val" is present
        // only when the weak_ptr is the FIRST reference to the object in traversal order (a
        // forward reference): we then inline the data under the same id so the reader can
        // reconstruct it. Without this the old code wrote {"$ref": 0} (lookup_shared_id
        // returned 0 for the not-yet-written object) and the link was silently dropped on
        // load. Mirrors the shared_ptr path and the script weak_ptr ($weak_ptr_data) path;
        // because depth-first write/read order is identical, the data always precedes any
        // back-reference, so no forward reference survives to load time.
        else if constexpr (is_std_weak_ptr_v<T>) {
            if (auto shared = value.lock()) {
                auto [id, is_new] = self()->get_or_assign_shared_id(shared.get());
                self()->begin_object();
                push_context(SerializationContext::ObjectBody);
                self()->serialize("$ref", id);
                if (is_new) {
                    write_shared_payload(shared);
                }
                pop_context();
                self()->end_object();
            } else {
                self()->write_null();
            }
        }
        // unique_ptr: serialize as {"$val": {...}} or null
        else if constexpr (is_std_unique_ptr_v<T>) {
            if (!value) {
                self()->write_null();
            } else {
                self()->begin_object();
                push_context(SerializationContext::ObjectBody);
                self()->serialize("$val", *value);
                pop_context();
                self()->end_object();
            }
        }
        // ====================================================================
        // Special containers (check before generic traits)
        // ====================================================================
        // std::pair - serialize as 2-element array
        else if constexpr (is_std_pair_v<T>) {
            self()->begin_array(2);
            push_context(SerializationContext::ArrayElement);
            write_element(value.first);
            write_element(value.second);
            pop_context();
            self()->end_array();
        }
        // std::optional - serialize as [has_value, value?]
        else if constexpr (is_std_optional_v<T>) {
            self()->begin_array(value.has_value() ? 2 : 1);
            push_context(SerializationContext::ArrayElement);
            self()->write_bool(value.has_value());
            if (value.has_value()) {
                write_element(*value);
            }
            pop_context();
            self()->end_array();
        }
        // std::variant - serialize as [index, value]
        else if constexpr (is_std_variant_v<T>) {
            self()->begin_array(2);
            push_context(SerializationContext::ArrayElement);
            write_element(static_cast<uint32_t>(value.index()));
            std::visit([this](const auto& v) { write_element(v); }, value);
            pop_context();
            self()->end_array();
        }
        // std::tuple - serialize as array
        else if constexpr (is_std_tuple_v<T>) {
            constexpr auto size = std::tuple_size_v<T>;
            self()->begin_array(size);
            push_context(SerializationContext::ArrayElement);
            std::apply([this](const auto&... args) { (write_element(args), ...); }, value);
            pop_context();
            self()->end_array();
        }
        // std::array - serialize as fixed-size array
        else if constexpr (is_std_array_v<T>) {
            constexpr auto N = array_size_v<T>;
            self()->begin_array(N);
            push_context(SerializationContext::ArrayElement);
            for (const auto& elem : value) {
                write_element(elem);
            }
            pop_context();
            self()->end_array();
        }
        // ====================================================================
        // Generic container handling (covers list, deque, vector, set, map, etc.)
        // ====================================================================
        // Map-like containers (std::map, std::unordered_map, std::multimap, etc.)
        else if constexpr (is_map_like_v<T>) {
            using K = typename T::key_type;
            if constexpr (std::is_same_v<K, std::string>) {
                // String keys: serialize as JSON object
                self()->begin_map(value.size());
                for (const auto& [key, val] : value) {
                    self()->write_map_key(key);
                    push_context(SerializationContext::MapValue);
                    write_value(val);
                    consume_value_context();
                }
                self()->end_map();
            } else {
                // Non-string keys: serialize as array of [key, value] pairs
                self()->begin_array(value.size());
                push_context(SerializationContext::ArrayElement);
                for (const auto& [key, val] : value) {
                    self()->begin_array(2);
                    push_context(SerializationContext::ArrayElement);
                    write_element(key);
                    write_element(val);
                    pop_context();
                    self()->end_array();
                }
                pop_context();
                self()->end_array();
            }
        }
        // Set-like containers (std::set, std::unordered_set, std::multiset, etc.)
        else if constexpr (is_set_like_v<T>) {
            self()->begin_array(value.size());
            push_context(SerializationContext::ArrayElement);
            for (const auto& elem : value) {
                write_element(elem);
            }
            pop_context();
            self()->end_array();
        }
        // Sequence containers (std::vector, std::list, std::deque, std::forward_list)
        else if constexpr (is_sequence_container_v<T>) {
            // Use std::distance for forward_list which doesn't have size()
            size_t count = 0;
            if constexpr (requires { value.size(); }) {
                count = value.size();
            } else {
                count = static_cast<size_t>(std::distance(std::begin(value), std::end(value)));
            }
            self()->begin_array(count);
            push_context(SerializationContext::ArrayElement);
            for (const auto& elem : value) {
                write_element(elem);
            }
            pop_context();
            self()->end_array();
        }
        // ====================================================================
        // jai::property<T> - unwrap and serialize the underlying value
        // ====================================================================
        else if constexpr (is_jai_property_v<T>) {
            write_value(value.get());
        }
        // ====================================================================
        // Fallback: try ADL serialize(), then ADL save(), or provide compile-time error
        // ====================================================================
        else if constexpr (has_adl_serialize<T>::value) {
            // ADL serialize functions write named properties, so wrap in object
            bool wrap = needs_object_wrapper();
            if (wrap) {
                self()->begin_object();
                push_context(SerializationContext::ObjectBody);
            }
            serialize(*self(), const_cast<T&>(value));
            if (wrap) {
                pop_context();
                self()->end_object();
            }
        }
        else if constexpr (has_adl_save<T>::value) {
            // ADL save functions write named properties, so wrap in object
            bool wrap = needs_object_wrapper();
            if (wrap) {
                self()->begin_object();
                push_context(SerializationContext::ObjectBody);
            }
            save(*self(), value);
            if (wrap) {
                pop_context();
                self()->end_object();
            }
        } else {
            throw serialization_error(
                "Type '" + std::string(typeid(T).name()) + "' is not serializable. "
                "Add save/load, serialize, property_owner, or ADL functions — or mark the property transient.");
        }
    }

    // ============================================================================
    // Context-aware value writing
    // ============================================================================
    // write_value is the unified entry point for writing any value.
    // It checks the current context and wraps complex types when needed:
    // - In PropertyValue context: no wrapping (already inside an object)
    // - In ArrayElement context: wrap complex types with begin_object/end_object
    // - In Root context: wrap complex types (standalone serialization)

    template<typename T>
    void write_value(const T& value) {
        if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, std::string>) {
            // Primitives never need wrapping
            write_primitive(value);
        }
        else {
            // All other types: delegate to operator() which handles context-aware wrapping
            // for types with save/serialize/property_mgr, or manages container structure
            (*self())(value);
        }
    }

    // ============================================================================
    // Array element serialization (legacy API, delegates to write_value)
    // ============================================================================

    template<typename T>
    std::enable_if_t<std::is_arithmetic_v<T> || std::is_same_v<T, std::string>>
    write_element(const T& value) {
        write_primitive(value);
    }

    template<typename T>
    std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_same_v<T, std::string>>
    write_element(const T& value) {
        // Delegate to write_value which handles context-aware wrapping
        write_value(value);
    }

private:
    // Helper: write primitive value - calls through self() for non-virtual dispatch
    template<typename T>
    void write_primitive(const T& value) {
        if constexpr (std::is_same_v<T, int8_t>) self()->write_int8(value);
        else if constexpr (std::is_same_v<T, int16_t>) self()->write_int16(value);
        else if constexpr (std::is_same_v<T, int32_t>) self()->write_int32(value);
        else if constexpr (std::is_same_v<T, int64_t>) self()->write_int64(value);
        else if constexpr (std::is_same_v<T, uint8_t>) self()->write_uint8(value);
        else if constexpr (std::is_same_v<T, uint16_t>) self()->write_uint16(value);
        else if constexpr (std::is_same_v<T, uint32_t>) self()->write_uint32(value);
        else if constexpr (std::is_same_v<T, uint64_t>) self()->write_uint64(value);
        else if constexpr (std::is_same_v<T, float>) self()->write_float32(value);
        else if constexpr (std::is_same_v<T, double>) self()->write_float64(value);
        else if constexpr (std::is_same_v<T, bool>) self()->write_bool(value);
        else if constexpr (std::is_same_v<T, std::string>) self()->write_string(value);
        else if constexpr (std::is_enum_v<T>) write_primitive(static_cast<std::underlying_type_t<T>>(value));
        else if constexpr (std::is_integral_v<T>) self()->write_int64(static_cast<int64_t>(value));
        else if constexpr (std::is_floating_point_v<T>) self()->write_float64(static_cast<double>(value));
    }

    // Writes the polymorphic-aware payload of a shared object (the caller is already
    // inside the wrapping object body): "$type"+"$val" for a registered polymorphic
    // dynamic type, else "$val". Shared by the shared_ptr and the forward-referencing
    // weak_ptr write paths so the two cannot drift apart.
    template<typename SharedPtr>
    void write_shared_payload(const SharedPtr& value) {
        using elem_type = typename SharedPtr::element_type;
        if constexpr (std::is_polymorphic_v<elem_type>) {
            const auto& actual_type = typeid(*value);
            if (actual_type != typeid(elem_type)) {
                auto* entry = polymorphic_registry::instance().find(std::type_index(actual_type));
                if (entry && entry->save_fn) {
                    self()->serialize("$type", entry->name);
                    self()->write_property_name("$val");
                    push_context(SerializationContext::PropertyValue);
                    self()->begin_object();
                    push_context(SerializationContext::ObjectBody);
                    any_archive_writer wrapper(*self());
                    entry->save_fn(wrapper, value.get());
                    pop_context();
                    self()->end_object();
                    pop_context();
                    return;
                }
                // Writing the static type's payload here would silently slice the object
                // and load back as the wrong type — fail loudly instead (cereal semantics).
                throw serialization_error(std::string("Cannot serialize unregistered polymorphic type '") + actual_type.name() +
                    "' through pointer-to-base '" + typeid(elem_type).name() +
                    "'. Register it with jai::registrar<T, Context>(\"Name\") or derive from jai::property_owner<T, ...>.");
            }
        }
        self()->serialize("$val", *value);
    }

public:
    // Helper: write custom type - context-aware, delegates to write_value
    // Uses access:: to support protected methods that friend jai::access
    // Made public for use by smart pointer serialization in property_serialization.hpp
    template<typename T>
    void write_custom(const T& value) {
        // Delegate to write_value which is context-aware
        write_value(value);
    }

    // Serialize content into an already-opened object (for use by to_json/to_binary)
    // Pushes ObjectBody context so operator() doesn't wrap again
    template<typename T>
    void serialize_object_content(const T& value) {
        push_context(SerializationContext::ObjectBody);
        (*self())(value);
        pop_context();
    }
};

// ============================================================================
// CRTP Archive Reader Base Template
// ============================================================================
// All deserialization functions call I/O methods through self() for static dispatch.
// Derived classes implement I/O methods as regular (non-virtual) methods.
// See JAI_ARCHIVE_DEVIRTUALIZATION.md for design rationale.

template<class Derived>
class archive_reader_impl : public archive_base {
public:
    // Direction detection tag for is_load trait
    struct is_reader_tag {};

protected:
    // CRTP self accessor - casts to derived type for non-virtual dispatch
    Derived* self() { return static_cast<Derived*>(this); }
    const Derived* self() const { return static_cast<const Derived*>(this); }

    // Engine reference for creating script_values
    engine* engine_ref_;

    // Shared pointer tracking for deserialization (script_value based)
    std::unordered_map<uint32_t, script_value> id_to_shared_ptr_;

    // C++ shared_ptr tracking for property serialization (type-erased)
    std::unordered_map<uint32_t, std::shared_ptr<void>> cpp_shared_ptrs_;

    // User context storage (for dependency injection during deserialization)
    std::map<std::type_index, void*> user_contexts_;

    // Pre-read properties for factory-based deserialization
    std::map<std::string, script_value> preread_properties_;
    bool has_preread_properties_ = false;

    // Depth tracking for recursion limit
    int current_depth_ = 0;

    // RAII guard for depth tracking
    struct depth_guard {
        int& depth;
        bool acquired = false;

        depth_guard(int& d) : depth(d) {
            if (depth >= JAI_MAX_SERIALIZATION_DEPTH) {
                throw serialization_error(
                    "Maximum deserialization depth (" + std::to_string(JAI_MAX_SERIALIZATION_DEPTH) +
                    ") exceeded - possible malicious data or deeply nested structure"
                );
            }
            ++depth;
            acquired = true;
        }

        ~depth_guard() {
            if (acquired) --depth;
        }

        depth_guard(const depth_guard&) = delete;
        depth_guard& operator=(const depth_guard&) = delete;
    };

    // Register a reconstructed shared_ptr
    void register_shared_ptr(uint32_t id, const script_value& ptr) {
        if (id != 0) {
            id_to_shared_ptr_.insert_or_assign(id, ptr);
        }
    }

    // Get previously deserialized shared_ptr by ID
    script_value get_shared_ptr(uint32_t id) const {
        if (id == 0) {
            if (engine_ref_) {
                return script_value(std::monostate{}, engine_ref_);
            }
            throw serialization_error("Engine pointer is null during deserialization");
        }

        auto it = id_to_shared_ptr_.find(id);
        if (it != id_to_shared_ptr_.end()) {
            return it->second;
        }
        throw serialization_error("Invalid shared_ptr ID: " + std::to_string(id));
    }

    uint32_t version_ = 0;

public:
    explicit archive_reader_impl(engine* eng) : engine_ref_(eng) {}
    ~archive_reader_impl() = default;

    void set_engine(engine* eng) { engine_ref_ = eng; }
    engine* get_engine() const { return engine_ref_; }

    // ============================================================================
    // Shared pointer ID tracking
    // ============================================================================
    template<typename T>
    void register_deserialized_shared(uint32_t id, const std::shared_ptr<T>& ptr) {
        if (id != 0 && ptr) {
            cpp_shared_ptrs_[id] = std::static_pointer_cast<void>(ptr);
        }
    }

    template<typename T>
    std::shared_ptr<T> get_deserialized_shared(uint32_t id) const {
        if (id == 0) return nullptr;
        auto it = cpp_shared_ptrs_.find(id);
        if (it != cpp_shared_ptrs_.end()) {
            return std::static_pointer_cast<T>(it->second);
        }
        return nullptr;
    }

    bool has_deserialized_shared(uint32_t id) const {
        return id != 0 && cpp_shared_ptrs_.find(id) != cpp_shared_ptrs_.end();
    }

    // Non-templated versions for type-erased any_archive_reader
    std::shared_ptr<void> get_deserialized_shared_void(uint32_t id) const {
        if (id == 0) return nullptr;
        auto it = cpp_shared_ptrs_.find(id);
        if (it != cpp_shared_ptrs_.end()) {
            return it->second;
        }
        return nullptr;
    }

    void register_deserialized_shared_void(uint32_t id, std::shared_ptr<void> ptr) {
        if (id != 0 && ptr) {
            cpp_shared_ptrs_[id] = std::move(ptr);
        }
    }

    // User context support
    template<typename ContextType>
    ContextType* get_user_context() const {
        auto it = user_contexts_.find(std::type_index(typeid(ContextType)));
        if (it != user_contexts_.end()) {
            return static_cast<ContextType*>(it->second);
        }
        return nullptr;
    }

    template<typename ContextType>
    void set_user_context(ContextType* context) {
        user_contexts_[std::type_index(typeid(ContextType))] = context;
    }

    // Raw accessor for type-erased wrapper (any_archive_reader)
    void* get_user_context_raw(std::type_index ti) const {
        auto it = user_contexts_.find(ti);
        return it != user_contexts_.end() ? it->second : nullptr;
    }

    uint32_t get_version() const { return version_; }

    // ============================================================================
    // Public API: serialize("name", value)
    // ============================================================================

    template<typename T>
    std::enable_if_t<std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, std::string>>
    serialize(const char* name, T& value) {
        if (!self()->seek_property(name)) {
            throw serialization_error("Property '" + std::string(name) + "' not found during deserialization");
        }
        read_primitive(value);
    }

    template<typename T>
    std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_enum_v<T> && !std::is_same_v<T, std::string>>
    serialize(const char* name, T& value) {
        if (!self()->seek_property(name)) {
            throw serialization_error("Property '" + std::string(name) + "' not found during deserialization");
        }
        read_custom(value);
    }

    template<typename T>
    void operator()(const char* name, T& value) {
        serialize(name, value);
    }

    template<typename T>
    void operator()(const nvp<T>& nv) {
        serialize(nv.name, nv.value);
    }

    template<typename T, typename... Rest>
    void operator()(const nvp<T>& first, const Rest&... rest) {
        serialize(first.name, first.value);
        if constexpr (sizeof...(rest) > 0) {
            operator()(rest...);
        }
    }

private:
    // Helper traits for SFINAE — uses Derived (the concrete archive) instead of a stub type.
    // These traits are only evaluated inside member function template enable_if/if-constexpr,
    // which are instantiated at the call site where Derived is complete.
    // Uses ::jai::access::member_* to detect protected methods via friendship.

    // Check for versioned load(Archive&, uint32_t) - for Cereal-compatible MV types
    template<typename T, typename = void>
    struct has_versioned_load_method : std::false_type {};
    template<typename T>
    struct has_versioned_load_method<T, std::void_t<decltype(::jai::access::member_load(std::declval<Derived&>(), std::declval<T&>(), std::uint32_t(0)))>> : std::true_type {};

    // Check for non-versioned load(Archive&) - for JaiScript-only types
    template<typename T, typename = void>
    struct has_simple_load_method : std::false_type {};
    template<typename T>
    struct has_simple_load_method<T, std::void_t<decltype(::jai::access::member_load(std::declval<Derived&>(), std::declval<T&>()))>> : std::true_type {};

    // Combined: has either versioned or simple load method
    template<typename T>
    struct has_load_method : std::bool_constant<has_versioned_load_method<T>::value || has_simple_load_method<T>::value> {};

    // Check for versioned serialize(Archive&, uint32_t) method
    template<typename T, typename = void>
    struct has_versioned_serialize_method : std::false_type {};
    template<typename T>
    struct has_versioned_serialize_method<T, std::void_t<decltype(::jai::access::member_serialize(std::declval<Derived&>(), std::declval<T&>(), std::uint32_t(0)))>> : std::true_type {};

    // Check for non-versioned serialize(Archive&) method
    template<typename T, typename = void>
    struct has_simple_serialize_method : std::false_type {};
    template<typename T>
    struct has_simple_serialize_method<T, std::void_t<decltype(::jai::access::member_serialize(std::declval<Derived&>(), std::declval<T&>()))>> : std::true_type {};

    // Combined: has either versioned or simple serialize method
    template<typename T>
    struct has_serialize_method : std::bool_constant<has_versioned_serialize_method<T>::value || has_simple_serialize_method<T>::value> {};

    template<typename T, typename = void>
    struct has_property_mgr : std::false_type {};
    template<typename T>
    struct has_property_mgr<T, std::void_t<decltype(std::declval<T&>().property_mgr)>> : std::true_type {};

    // Check for static load_and_construct(Archive&, construct<T>&) method
    // Uses access:: to detect private/protected methods that friend jai::access
    template<typename T, typename = void>
    struct has_load_and_construct : std::false_type {};
    template<typename T>
    struct has_load_and_construct<T, std::void_t<
        decltype(::jai::access::load_and_construct(
            std::declval<Derived&>(),
            std::declval<construct<T>&>()
        ))
    >> : std::true_type {};

    // ADL-findable load(Archive&, T&) / serialize(Archive&, T&) free functions
    template<typename T> using has_adl_load = adl_detect::has_adl_load<Derived, T>;
    template<typename T> using has_adl_serialize = adl_detect::has_adl_serialize<Derived, T>;

    // Helper to load variant alternatives by index
    template<typename Variant, std::size_t I = 0>
    void load_variant_by_index(Variant& v, std::size_t index) {
        if constexpr (I < std::variant_size_v<Variant>) {
            if (I == index) {
                using T = std::variant_alternative_t<I, Variant>;
                T val{};
                read_element(val);
                v = std::move(val);
            } else {
                load_variant_by_index<Variant, I + 1>(v, index);
            }
        } else {
            throw serialization_error("Invalid variant index: " + std::to_string(index));
        }
    }

public:
    // operator() for types with member load method
    // Prefers versioned load(Archive&, uint32_t), falls back to simple load(Archive&)
    // Note: Does NOT wrap - naming implies nesting. ar(obj) = inline, ar("name", obj) = nested.
    // Uses access:: to support protected methods that friend jai::access
    template<typename T, std::enable_if_t<has_load_method<T>::value, int> = 0>
    void operator()(T& obj) {
        if constexpr (has_versioned_load_method<T>::value) {
            ::jai::access::member_load(*self(), obj, 0);  // Call versioned load with version=0
        } else {
            ::jai::access::member_load(*self(), obj);     // Call simple load
        }
    }

    // operator() for types with serialize() method (but no load())
    // Prefers versioned serialize(Archive&, uint32_t), falls back to simple serialize(Archive&)
    // Note: Does NOT wrap - consistent with load() types. Wrapping via ar("name", obj).
    // Uses access:: to support protected methods that friend jai::access
    template<typename T, std::enable_if_t<
        !has_load_method<T>::value &&
        has_serialize_method<T>::value, int> = 0>
    void operator()(T& obj) {
        if constexpr (has_versioned_serialize_method<T>::value) {
            ::jai::access::member_serialize(*self(), obj, 0);  // Call versioned serialize with version=0
        } else {
            ::jai::access::member_serialize(*self(), obj);     // Call simple serialize
        }
    }

    // operator() for property_owner types WITHOUT custom load() or serialize()
    template<typename T, std::enable_if_t<
        !has_load_method<T>::value &&
        !has_serialize_method<T>::value &&
        has_property_mgr<T>::value, int> = 0>
    void operator()(T& obj) {
        obj.property_mgr.load(*self());
    }

    // operator() for primitives (arithmetic, enum, string)
    template<typename T>
    std::enable_if_t<std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, std::string>>
    operator()(T& value) {
        read_primitive(value);
    }

    // operator() fallback for types without load(), serialize(), property_mgr, or primitives
    // Handles STL containers and smart pointers directly (no ADL needed)
    template<typename T, std::enable_if_t<
        !has_load_method<T>::value &&
        !has_serialize_method<T>::value &&
        !has_property_mgr<T>::value &&
        !std::is_arithmetic_v<T> &&
        !std::is_enum_v<T> &&
        !std::is_same_v<T, std::string>, int> = 0>
    void operator()(T& value) {
        using namespace serialization_traits;

        // std::vector
        if constexpr (is_std_vector_v<T>) {
            using elem_type = vector_element_t<T>;
            size_t size = self()->begin_array();
            value.clear();
            value.reserve(size);
            for (size_t i = 0; i < size; ++i) {
                elem_type elem{};
                read_element(elem);
                value.push_back(std::move(elem));
            }
            self()->end_array();
        }
        // std::array
        else if constexpr (is_std_array_v<T>) {
            constexpr auto N = array_size_v<T>;
            size_t size = self()->begin_array();
            if (size != N) {
                throw serialization_error("Array size mismatch: expected " + std::to_string(N) +
                                          ", got " + std::to_string(size));
            }
            for (size_t i = 0; i < N; ++i) {
                read_element(value[i]);
            }
            self()->end_array();
        }
        // std::map
        else if constexpr (is_std_map_v<T>) {
            using K = map_key_t<T>;
            using V = map_value_t<T>;
            value.clear();
            if constexpr (std::is_same_v<K, std::string>) {
                size_t size = self()->begin_map();
                for (size_t i = 0; i < size; ++i) {
                    std::string key;
                    if (!self()->read_map_key(key)) break;
                    V val{};
                    read_element(val);
                    value[key] = std::move(val);
                }
                self()->end_map();
            } else {
                size_t size = self()->begin_array();
                for (size_t i = 0; i < size; ++i) {
                    self()->begin_array();
                    K key{};
                    V val{};
                    read_element(key);
                    read_element(val);
                    self()->end_array();
                    value[std::move(key)] = std::move(val);
                }
                self()->end_array();
            }
        }
        // std::unordered_map
        else if constexpr (is_std_unordered_map_v<T>) {
            using K = unordered_map_key_t<T>;
            using V = unordered_map_value_t<T>;
            value.clear();
            if constexpr (std::is_same_v<K, std::string>) {
                size_t size = self()->begin_map();
                for (size_t i = 0; i < size; ++i) {
                    std::string key;
                    if (!self()->read_map_key(key)) break;
                    V val{};
                    read_element(val);
                    value[key] = std::move(val);
                }
                self()->end_map();
            } else {
                size_t size = self()->begin_array();
                for (size_t i = 0; i < size; ++i) {
                    self()->begin_array();
                    K key{};
                    V val{};
                    read_element(key);
                    read_element(val);
                    self()->end_array();
                    value[std::move(key)] = std::move(val);
                }
                self()->end_array();
            }
        }
        // Set-like containers (std::set, std::unordered_set, etc.) - stored as arrays
        else if constexpr (is_set_like_v<T>) {
            using elem_type = typename T::key_type;
            size_t size = self()->begin_array();
            value.clear();
            for (size_t i = 0; i < size; ++i) {
                elem_type elem{};
                read_element(elem);
                value.insert(std::move(elem));
            }
            self()->end_array();
        }
        // std::pair
        else if constexpr (is_std_pair_v<T>) {
            self()->begin_array();
            read_element(value.first);
            read_element(value.second);
            self()->end_array();
        }
        // std::shared_ptr - format: {"$id": N, "$val": {...}} or {"$id": N} or null
        // Polymorphic: {"$id": N, "$type": "ConcreteType", "$val": {...}}
        else if constexpr (is_std_shared_ptr_v<T>) {
            using elem_type = shared_ptr_element_t<T>;
            if (self()->peek_null()) {
                self()->read_null();
                value.reset();
                return;
            }
            if (!self()->begin_object()) {
                throw serialization_error("shared_ptr begin_object failed for " + std::string(typeid(elem_type).name()));
            }
            uint32_t id{};
            self()->serialize("$id", id);
            if (self()->has_deserialized_shared(id)) {
                value = self()->template get_deserialized_shared<elem_type>(id);
            } else {
                // First (data-bearing) encounter of this id, or a $id-only record with no
                // payload (-> null). read_shared_payload handles polymorphic / load_and_construct
                // / default construction and registers the object eagerly under id.
                value = self()->template read_shared_payload<elem_type>(id);
            }
            self()->end_object();
        }
        // std::weak_ptr - format: {"$ref": N, "$val"?: {...}} or null. "$val" appears when
        // the weak_ptr is the first (forward) reference to the object; we then reconstruct
        // and register it just like a shared_ptr so later shared/weak refs to this id
        // resolve. Previously a forward weak_ptr loaded as expired (data was never written),
        // silently dropping the link.
        else if constexpr (is_std_weak_ptr_v<T>) {
            using elem_type = weak_ptr_element_t<T>;
            if (self()->peek_null()) {
                self()->read_null();
                value.reset();
                return;
            }
            self()->begin_object();
            uint32_t id{};
            self()->serialize("$ref", id);
            if (self()->has_deserialized_shared(id)) {
                value = self()->template get_deserialized_shared<elem_type>(id);
            } else if (self()->has_property("$val") || self()->has_property("$type")) {
                value = self()->template read_shared_payload<elem_type>(id);
            } else {
                value.reset();
            }
            self()->end_object();
        }
        // std::unique_ptr - format: {"$val": {...}} or null
        else if constexpr (is_std_unique_ptr_v<T>) {
            using elem_type = unique_ptr_element_t<T>;
            if (self()->peek_null()) {
                self()->read_null();
                value.reset();
                return;
            }
            self()->begin_object();
            if constexpr (std::is_default_constructible_v<elem_type>) {
                value = std::make_unique<elem_type>();
                self()->serialize("$val", *value);
            } else {
                throw serialization_error("Cannot deserialize unique_ptr<T>: type is not default constructible");
            }
            self()->end_object();
        }
        // std::optional
        else if constexpr (is_std_optional_v<T>) {
            using elem_type = optional_element_t<T>;
            self()->begin_array();
            bool has_value = self()->read_bool();
            if (has_value) {
                elem_type val{};
                read_element(val);
                value = std::move(val);
            } else {
                value = std::nullopt;
            }
            self()->end_array();
        }
        // std::variant
        else if constexpr (is_std_variant_v<T>) {
            self()->begin_array();
            uint32_t index;
            read_element(index);
            load_variant_by_index(value, static_cast<std::size_t>(index));
            self()->end_array();
        }
        // std::tuple
        else if constexpr (is_std_tuple_v<T>) {
            self()->begin_array();
            std::apply([this](auto&... args) { (read_element(args), ...); }, value);
            self()->end_array();
        }
        // jai::property<T>
        else if constexpr (is_jai_property_v<T>) {
            // Deserialize the underlying value (property name handled by caller)
            using element_type = property_element_t<T>;
            element_type temp{};
            (*self())(temp);
            value = std::move(temp);
        }
        // Fallback: try ADL serialize(), then ADL load(), or provide compile-time error
        else if constexpr (has_adl_serialize<T>::value) {
            // ADL serialize functions read named properties, so expect object wrapper
            self()->begin_object();
            serialize(*self(), value);
            self()->end_object();
        }
        else if constexpr (has_adl_load<T>::value) {
            // ADL load functions read named properties, so expect object wrapper
            self()->begin_object();
            load(*self(), value);
            self()->end_object();
        } else {
            throw serialization_error(
                "Type '" + std::string(typeid(T).name()) + "' is not deserializable. "
                "Add save/load, serialize, property_owner, or ADL functions — or mark the property transient.");
        }
    }

    // ============================================================================
    // Array element deserialization (no property name)
    // ============================================================================

    template<typename T>
    std::enable_if_t<std::is_arithmetic_v<T> || std::is_same_v<T, std::string>>
    read_element(T& value) {
        read_primitive(value);
    }

    template<typename T>
    std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_same_v<T, std::string>>
    read_element(T& value) {
        // For custom types with serialize/load methods, expect object wrapper
        // since their serialize() will read named properties.
        // DON'T wrap smart pointers - they have their own array format,
        // and their pointed-to object will be wrapped when read_element(*ptr) is called.
        if constexpr (has_load_method<T>::value || has_serialize_method<T>::value ||
                      has_property_mgr<T>::value) {
            self()->begin_object();
            (*self())(value);
            self()->end_object();
        } else {
            // STL containers, smart pointers, variants, etc. handle their own structure
            (*self())(value);
        }
    }

protected:
    // Pre-read properties support
    void set_preread_properties(std::map<std::string, script_value> props) {
        preread_properties_ = std::move(props);
        has_preread_properties_ = true;
    }

    void clear_preread_properties() {
        preread_properties_.clear();
        has_preread_properties_ = false;
    }

    const script_value* get_preread_property(const std::string& name) const {
        auto it = preread_properties_.find(name);
        return it != preread_properties_.end() ? &it->second : nullptr;
    }

    bool has_preread_properties() const { return has_preread_properties_; }

private:
    // Helper: read primitive value - calls through self() for non-virtual dispatch
    template<typename T>
    void read_primitive(T& value) {
        if constexpr (std::is_same_v<T, int8_t>) value = self()->read_int8();
        else if constexpr (std::is_same_v<T, int16_t>) value = self()->read_int16();
        else if constexpr (std::is_same_v<T, int32_t>) value = self()->read_int32();
        else if constexpr (std::is_same_v<T, int64_t>) value = self()->read_int64();
        else if constexpr (std::is_same_v<T, uint8_t>) value = self()->read_uint8();
        else if constexpr (std::is_same_v<T, uint16_t>) value = self()->read_uint16();
        else if constexpr (std::is_same_v<T, uint32_t>) value = self()->read_uint32();
        else if constexpr (std::is_same_v<T, uint64_t>) value = self()->read_uint64();
        else if constexpr (std::is_same_v<T, float>) value = self()->read_float32();
        else if constexpr (std::is_same_v<T, double>) value = self()->read_float64();
        else if constexpr (std::is_same_v<T, bool>) value = self()->read_bool();
        else if constexpr (std::is_same_v<T, std::string>) value = self()->read_string();
        else if constexpr (std::is_enum_v<T>) { std::underlying_type_t<T> underlying; read_primitive(underlying); value = static_cast<T>(underlying); }
        else if constexpr (std::is_integral_v<T>) value = static_cast<T>(self()->read_int64());
        else if constexpr (std::is_floating_point_v<T>) value = static_cast<T>(self()->read_float64());
    }

    // Reconstructs the inlined payload ($type/$val) of a shared object at the current
    // object position, registers it under `id` (eagerly, so cyclic back-references inside
    // its own subtree resolve), and returns it. Shared by the shared_ptr read path and the
    // forward-referenced weak_ptr read path so they cannot drift apart. Returns null when no
    // payload is present (a pure back-reference whose target is registered elsewhere).
    template<typename elem_type>
    std::shared_ptr<elem_type> read_shared_payload(uint32_t id) {
        std::shared_ptr<elem_type> result;
        if constexpr (std::is_polymorphic_v<elem_type>) {
            std::string poly_type;
            if (self()->has_property("$type")) {
                self()->serialize("$type", poly_type);
            }
            if (!poly_type.empty()) {
                auto* entry = polymorphic_registry::instance().find(poly_type);
                if (entry && entry->load_fn) {
                    if (!self()->seek_property("$val")) {
                        throw serialization_error("Expected '$val' for polymorphic shared_ptr");
                    }
                    self()->begin_object();
                    any_archive_reader wrapper(*self());
                    // Pass id so load_fn registers the object eagerly (before its
                    // properties load), enabling subtree back-references to resolve.
                    auto void_ptr = entry->load_fn(wrapper, id);
                    result = std::static_pointer_cast<elem_type>(void_ptr);
                    self()->end_object();
                    self()->register_deserialized_shared(id, result);
                    return result;
                } else {
                    throw serialization_error("Unknown polymorphic type: " + poly_type);
                }
            }
            // No $type discriminator: fall through to concrete reconstruction below.
        }
        if (self()->has_property("$val")) {
            if constexpr (has_load_and_construct<elem_type>::value) {
                if (!self()->seek_property("$val")) {
                    throw serialization_error("Expected '$val' for shared_ptr with load_and_construct");
                }
                std::string type_name;
                uint32_t version = 0;
                self()->begin_object(type_name, version);
                construct<elem_type> c(result);
                c.set_on_construct([&]() { self()->register_deserialized_shared(id, result); });
                ::jai::access::load_and_construct(*self(), c);
                self()->end_object();
            } else if constexpr (std::is_default_constructible_v<elem_type>) {
                result = std::make_shared<elem_type>();
                self()->register_deserialized_shared(id, result);
                self()->serialize("$val", *result);
            } else {
                throw serialization_error("Cannot deserialize shared_ptr<" + std::string(typeid(elem_type).name()) + ">: type is not default constructible and has no load_and_construct");
            }
        }
        return result;
    }

public:
    // Helper: read custom type - calls through self() for non-virtual dispatch
    // Uses access:: to support protected methods that friend jai::access
    // Made public for use by smart pointer serialization in property_serialization.hpp
    template<typename T>
    void read_custom(T& value) {
        if constexpr (has_versioned_load_method<T>::value) {
            std::string type_name;
            uint32_t version;
            self()->begin_object(type_name, version);
            ::jai::access::member_load(*self(), value, version);
            self()->end_object();
        }
        else if constexpr (has_simple_load_method<T>::value) {
            std::string type_name;
            uint32_t version;
            self()->begin_object(type_name, version);
            ::jai::access::member_load(*self(), value);
            self()->end_object();
        }
        else if constexpr (has_versioned_serialize_method<T>::value) {
            std::string type_name;
            uint32_t version;
            self()->begin_object(type_name, version);
            ::jai::access::member_serialize(*self(), value, version);
            self()->end_object();
        }
        else if constexpr (has_simple_serialize_method<T>::value) {
            std::string type_name;
            uint32_t version;
            self()->begin_object(type_name, version);
            ::jai::access::member_serialize(*self(), value);
            self()->end_object();
        }
        else if constexpr (has_property_mgr<T>::value) {
            std::string type_name;
            uint32_t version;
            self()->begin_object(type_name, version);
            value.property_mgr.load(*self());
            self()->end_object();
        }
        else {
            // Use operator() which handles STL containers and ADL fallback
            (*self())(value);
        }
    }
};

// NOTE: Legacy archive_writer class has been DELETED.
// Use CRTP-based archive_writer_impl<Derived> instead.
// For type-erased callbacks, use any_archive_writer from serialization_metadata.hpp.

// NOTE: Legacy archive_reader class has been DELETED.
// Use CRTP-based archive_reader_impl<Derived> instead.
// For type-erased callbacks, use any_archive_reader from serialization_metadata.hpp.

// Registry for class serialization metadata - now tied to engine instance
class serialization_registry {
public:
    serialization_registry() = default;

    void register_class(const std::string& class_name, const class_metadata& metadata) {
        classes_[class_name] = metadata;
    }

    // Register class with type_index for runtime lookup by C++ type
    void register_class(const std::string& class_name, std::type_index type_idx, const class_metadata& metadata) {
        classes_[class_name] = metadata;
        type_to_class_name_[type_idx] = class_name;
    }

    const class_metadata* get_class_metadata(const std::string& class_name) const {
        auto it = classes_.find(class_name);
        return it != classes_.end() ? &it->second : nullptr;
    }

    // Get metadata by C++ type_index (for automatic serialization of dynamic_binder-registered types)
    const class_metadata* get_class_metadata_by_type(std::type_index type_idx) const {
        auto it = type_to_class_name_.find(type_idx);
        if (it != type_to_class_name_.end()) {
            return get_class_metadata(it->second);
        }
        return nullptr;
    }

    // Get class name by type_index
    const std::string* get_class_name_by_type(std::type_index type_idx) const {
        auto it = type_to_class_name_.find(type_idx);
        return it != type_to_class_name_.end() ? &it->second : nullptr;
    }

    bool has_class(const std::string& class_name) const {
        return classes_.find(class_name) != classes_.end();
    }

    bool has_class_by_type(std::type_index type_idx) const {
        return type_to_class_name_.find(type_idx) != type_to_class_name_.end();
    }

private:
    std::map<std::string, class_metadata> classes_;
    std::map<std::type_index, std::string> type_to_class_name_;
};

// NOTE: STL container serialization (vector, array, map, unordered_map, pair,
// optional, variant, tuple) and jai::property<T> are now handled internally by
// archive_writer_impl::operator() and archive_reader_impl::operator().
// No ADL free functions are needed. This avoids conflicts with Cereal's ADL-based
// serialization.

} // namespace serialization
} // namespace jai

// Include polymorphic registry after archive classes are defined
// (polymorphic.hpp needs archive types, archive templates need polymorphic_registry)
#include <jaiscript/serialization/polymorphic.hpp>

#endif // JAISCRIPT_SERIALIZATION_ARCHIVE_IMPL_HPP
