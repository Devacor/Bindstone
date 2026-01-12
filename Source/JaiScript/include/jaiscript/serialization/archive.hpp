#pragma once

#ifndef JAISCRIPT_SERIALIZATION_ARCHIVE_HPP
#define JAISCRIPT_SERIALIZATION_ARCHIVE_HPP
#define JAISCRIPT_ARCHIVE_HPP_INCLUDED

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

// Forward declarations
class binary_archive_writer;
class binary_archive_reader;
class json_archive_writer;
class json_archive_reader;

// Base tag type for all JaiScript archives (used for SFINAE type detection)
struct archive_base {
    static constexpr bool is_jai_archive = true;
};

// Name-value pair for serialization
template<typename T>
struct nvp {
    const char* name;
    T& value;

    nvp(const char* n, T& v) : name(n), value(v) {}
};

// Helper to create nvp (for use in initializer lists)
template<typename T>
nvp<T> make_nvp(const char* name, T& value) {
    return nvp<T>(name, value);
}

template<typename T>
nvp<const T> make_nvp(const char* name, const T& value) {
    return nvp<const T>(name, const_cast<const T&>(value));
}

// JAI_NVP macro - uses variable name as property name
#define JAI_NVP(x) ::jai::serialization::make_nvp(#x, x)

// ============================================================================
// Archive type detection
// ============================================================================
// Concept to detect JaiScript archives (has is_jai_archive static member that is true)
template<typename T>
concept jai_archive = requires { { T::is_jai_archive } -> std::convertible_to<bool>; } && T::is_jai_archive;

// Concept for non-JaiScript archives
template<typename T>
concept not_jai_archive = !jai_archive<T>;

// Direction detection traits - use in if constexpr for save/load branching
// These check if Archive derives from archive_writer_impl or archive_reader_impl
template<typename T>
inline constexpr bool is_save = std::is_base_of_v<archive_base, std::decay_t<T>> &&
    requires { typename std::decay_t<T>::is_writer_tag; };

template<typename T>
inline constexpr bool is_load = std::is_base_of_v<archive_base, std::decay_t<T>> &&
    requires { typename std::decay_t<T>::is_reader_tag; };

// SFINAE constraint: Archive MUST be a JaiScript archive
#define JAI_ONLY_ARCHIVE std::enable_if_t<::jai::serialization::jai_archive<Archive>, int> = 0

// Template declaration that only matches JaiScript archives
#define JAI_TEMPLATE template <class Archive, JAI_ONLY_ARCHIVE>

// ============================================================================
// CRTP Archive Writer Base Template
// ============================================================================
// All serialization functions call I/O methods through self() for static dispatch.
// Derived classes implement I/O methods as regular (non-virtual) methods.
// See JAI_ARCHIVE_DEVIRTUALIZATION.md for design rationale.

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
    explicit archive_writer_impl(engine* eng = nullptr) : engine_ref_(eng) {}

    ~archive_writer_impl() = default;

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
        write_primitive(value);
    }

    // Serialize custom types (named)
    template<typename T>
    std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_enum_v<T> && !std::is_same_v<T, std::string>>
    serialize(const char* name, const T& value) {
        self()->write_property_name(name);
        write_custom(value);
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

    // Variadic operator()
    template<typename T, typename... Rest>
    void operator()(const nvp<T>& first, const Rest&... rest) {
        serialize(first.name, first.value);
        if constexpr (sizeof...(rest) > 0) {
            operator()(rest...);
        }
    }

private:
    // Helper traits for SFINAE
    // First check for versioned save(Archive&, uint32_t) - for Cereal-compatible MV types
    template<typename T, typename = void>
    struct has_versioned_save_method : std::false_type {};
    template<typename T>
    struct has_versioned_save_method<T, std::void_t<decltype(std::declval<const T&>().save(std::declval<Derived&>(), std::uint32_t(0)))>> : std::true_type {};

    // Then check for non-versioned save(Archive&) - for JaiScript-only types
    template<typename T, typename = void>
    struct has_simple_save_method : std::false_type {};
    template<typename T>
    struct has_simple_save_method<T, std::void_t<decltype(std::declval<const T&>().save(std::declval<Derived&>()))>> : std::true_type {};

    // Combined: has either versioned or simple save method
    template<typename T>
    struct has_save_method : std::bool_constant<has_versioned_save_method<T>::value || has_simple_save_method<T>::value> {};

    // Check for serialize(Archive&) method
    template<typename T, typename = void>
    struct has_serialize_method : std::false_type {};
    template<typename T>
    struct has_serialize_method<T, std::void_t<decltype(std::declval<T&>().serialize(std::declval<Derived&>()))>> : std::true_type {};

    template<typename T, typename = void>
    struct has_property_mgr : std::false_type {};
    template<typename T>
    struct has_property_mgr<T, std::void_t<decltype(std::declval<T&>().property_mgr)>> : std::true_type {};

public:
    // operator() for types with member save method
    // Prefers versioned save(Archive&, uint32_t), falls back to simple save(Archive&)
    // Note: Does NOT wrap in begin_object/end_object - naming implies nesting
    // ar(obj) = inline properties, ar("name", obj) = nested object
    template<typename T, std::enable_if_t<has_save_method<T>::value, int> = 0>
    void operator()(const T& obj) {
        if constexpr (has_versioned_save_method<T>::value) {
            obj.save(*self(), 0);  // Call versioned save with version=0
        } else {
            obj.save(*self());     // Call simple save
        }
    }

    // operator() for types with serialize() method (but no save())
    // Note: Does NOT wrap - consistent with save() types. Wrapping via ar("name", obj).
    template<typename T, std::enable_if_t<
        !has_save_method<T>::value &&
        has_serialize_method<T>::value, int> = 0>
    void operator()(const T& obj) {
        const_cast<T&>(obj).serialize(*self());
    }

    // operator() for property_owner types WITHOUT custom save() or serialize()
    template<typename T, std::enable_if_t<
        !has_save_method<T>::value &&
        !has_serialize_method<T>::value &&
        has_property_mgr<T>::value, int> = 0>
    void operator()(const T& obj) {
        obj.property_mgr.save(*self());
    }

    // operator() for primitives (arithmetic, enum, string)
    template<typename T>
    std::enable_if_t<std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, std::string>>
    operator()(const T& value) {
        write_primitive(value);
    }

    // operator() fallback for types without save(), serialize(), property_mgr, or primitives
    // Uses ADL save() - no wrapping, consistent with other operator() overloads
    template<typename T, std::enable_if_t<
        !has_save_method<T>::value &&
        !has_serialize_method<T>::value &&
        !has_property_mgr<T>::value &&
        !std::is_arithmetic_v<T> &&
        !std::is_enum_v<T> &&
        !std::is_same_v<T, std::string>, int> = 0>
    void operator()(const T& value) {
        save(*self(), value);  // ADL - no wrapping
    }

    // ============================================================================
    // Array element serialization (no property name)
    // ============================================================================

    template<typename T>
    std::enable_if_t<std::is_arithmetic_v<T> || std::is_same_v<T, std::string>>
    write_element(const T& value) {
        write_primitive(value);
    }

    template<typename T>
    std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_same_v<T, std::string>>
    write_element(const T& value) {
        write_custom(value);
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
        else if constexpr (std::is_enum_v<T>) self()->write_int32(static_cast<std::underlying_type_t<T>>(value));
        else if constexpr (std::is_integral_v<T>) self()->write_int64(static_cast<int64_t>(value));
        else if constexpr (std::is_floating_point_v<T>) self()->write_float64(static_cast<double>(value));
    }

public:
    // Helper: write custom type - calls through self() for non-virtual dispatch
    // Detects versioned save(Archive&, uint32_t) first, then simple save(Archive&)
    // Made public for use by smart pointer serialization in property_serialization.hpp
    template<typename T>
    void write_custom(const T& value) {
        if constexpr (requires { const_cast<T&>(value).save(*self(), std::uint32_t(0)); }) {
            // Versioned save for Cereal-compatible types
            self()->begin_object("", 0);
            const_cast<T&>(value).save(*self(), 0);
            self()->end_object();
        }
        else if constexpr (requires { const_cast<T&>(value).save(*self()); }) {
            // Simple save for JaiScript-only types
            self()->begin_object("", 0);
            const_cast<T&>(value).save(*self());
            self()->end_object();
        }
        else if constexpr (requires { const_cast<T&>(value).serialize(*self()); }) {
            self()->begin_object("", 0);
            const_cast<T&>(value).serialize(*self());
            self()->end_object();
        }
        else if constexpr (requires { value.property_mgr; }) {
            self()->begin_object("", 0);
            value.property_mgr.save(*self());
            self()->end_object();
        }
        else {
            save(*self(), value);
        }
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
    // First check for versioned load(Archive&, uint32_t) - for Cereal-compatible MV types
    template<typename T, typename = void>
    struct has_versioned_load_method : std::false_type {};
    template<typename T>
    struct has_versioned_load_method<T, std::void_t<decltype(std::declval<T&>().load(std::declval<Derived&>(), std::uint32_t(0)))>> : std::true_type {};

    // Then check for non-versioned load(Archive&) - for JaiScript-only types
    template<typename T, typename = void>
    struct has_simple_load_method : std::false_type {};
    template<typename T>
    struct has_simple_load_method<T, std::void_t<decltype(std::declval<T&>().load(std::declval<Derived&>()))>> : std::true_type {};

    // Combined: has either versioned or simple load method
    template<typename T>
    struct has_load_method : std::bool_constant<has_versioned_load_method<T>::value || has_simple_load_method<T>::value> {};

    // Check for serialize(Archive&) method
    template<typename T, typename = void>
    struct has_serialize_method : std::false_type {};
    template<typename T>
    struct has_serialize_method<T, std::void_t<decltype(std::declval<T&>().serialize(std::declval<Derived&>()))>> : std::true_type {};

    template<typename T, typename = void>
    struct has_property_mgr : std::false_type {};
    template<typename T>
    struct has_property_mgr<T, std::void_t<decltype(std::declval<T&>().property_mgr)>> : std::true_type {};

public:
    // operator() for types with member load method
    // Prefers versioned load(Archive&, uint32_t), falls back to simple load(Archive&)
    // Note: Does NOT wrap - naming implies nesting. ar(obj) = inline, ar("name", obj) = nested.
    template<typename T, std::enable_if_t<has_load_method<T>::value, int> = 0>
    void operator()(T& obj) {
        if constexpr (has_versioned_load_method<T>::value) {
            obj.load(*self(), 0);  // Call versioned load with version=0
        } else {
            obj.load(*self());     // Call simple load
        }
    }

    // operator() for types with serialize() method (but no load())
    // Note: Does NOT wrap - consistent with load() types. Wrapping via ar("name", obj).
    template<typename T, std::enable_if_t<
        !has_load_method<T>::value &&
        has_serialize_method<T>::value, int> = 0>
    void operator()(T& obj) {
        obj.serialize(*self());
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
    // Uses ADL load() - no wrapping, consistent with other operator() overloads
    template<typename T, std::enable_if_t<
        !has_load_method<T>::value &&
        !has_serialize_method<T>::value &&
        !has_property_mgr<T>::value &&
        !std::is_arithmetic_v<T> &&
        !std::is_enum_v<T> &&
        !std::is_same_v<T, std::string>, int> = 0>
    void operator()(T& value) {
        load(*self(), value);  // ADL - no wrapping
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
        read_custom(value);
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
        else if constexpr (std::is_enum_v<T>) value = static_cast<T>(self()->read_int32());
        else if constexpr (std::is_integral_v<T>) value = static_cast<T>(self()->read_int64());
        else if constexpr (std::is_floating_point_v<T>) value = static_cast<T>(self()->read_float64());
    }

public:
    // Helper: read custom type - calls through self() for non-virtual dispatch
    // Detects versioned load(Archive&, uint32_t) first, then simple load(Archive&)
    // Made public for use by smart pointer serialization in property_serialization.hpp
    template<typename T>
    void read_custom(T& value) {
        if constexpr (requires { value.load(*self(), std::uint32_t(0)); }) {
            // Versioned load for Cereal-compatible types
            std::string type_name;
            uint32_t version;
            self()->begin_object(type_name, version);
            value.load(*self(), version);
            self()->end_object();
        }
        else if constexpr (requires { value.load(*self()); }) {
            // Simple load for JaiScript-only types
            std::string type_name;
            uint32_t version;
            self()->begin_object(type_name, version);
            value.load(*self());
            self()->end_object();
        }
        else if constexpr (requires { value.serialize(*self()); }) {
            std::string type_name;
            uint32_t version;
            self()->begin_object(type_name, version);
            value.serialize(*self());
            self()->end_object();
        }
        else if constexpr (requires { value.property_mgr; }) {
            std::string type_name;
            uint32_t version;
            self()->begin_object(type_name, version);
            value.property_mgr.load(*self());
            self()->end_object();
        }
        else {
            load(*self(), value);
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

// ============================================================================
// Generic STL container serialization support
// ============================================================================
// These free functions enable automatic serialization of std::vector<T> and
// std::array<T, N> for any T that has save/load support (member or free function).
// All functions are templated on Archive to preserve concrete archive type.
// See JAI_ARCHIVE_DEVIRTUALIZATION.md for design rationale.
//
// IMPORTANT: All functions use JAI_ONLY_ARCHIVE constraint to ensure they only
// match JaiScript archives. This prevents conflicts with Cereal which also uses
// ADL to find free save/load functions.

// --- std::vector ---
template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
void save(Archive& ar, const std::vector<T>& vec) {
    ar.begin_array(vec.size());
    for (const auto& elem : vec) {
        ar.write_element(elem);
    }
    ar.end_array();
}

template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
void load(Archive& ar, std::vector<T>& vec) {
    size_t size = ar.begin_array();
    vec.resize(size);
    for (size_t i = 0; i < size; ++i) {
        ar.read_element(vec[i]);
    }
    ar.end_array();
}

// --- std::array (compile-time size, no size serialization needed) ---
template<typename Archive, typename T, std::size_t N, JAI_ONLY_ARCHIVE>
void save(Archive& ar, const std::array<T, N>& arr) {
    ar.begin_array(N);
    for (const auto& elem : arr) {
        ar.write_element(elem);
    }
    ar.end_array();
}

template<typename Archive, typename T, std::size_t N, JAI_ONLY_ARCHIVE>
void load(Archive& ar, std::array<T, N>& arr) {
    size_t size = ar.begin_array();
    if (size != N) {
        throw serialization_error("Array size mismatch: expected " + std::to_string(N) +
                                  ", got " + std::to_string(size));
    }
    for (size_t i = 0; i < N; ++i) {
        ar.read_element(arr[i]);
    }
    ar.end_array();
}

// --- std::map (string keys use object format, other keys use array of pairs) ---
template<typename Archive, typename K, typename V, typename C, typename A, JAI_ONLY_ARCHIVE>
void save(Archive& ar, const std::map<K, V, C, A>& map) {
    if constexpr (std::is_same_v<K, std::string>) {
        // String keys: use map format (JSON object, binary map)
        ar.begin_map(map.size());
        for (const auto& [key, value] : map) {
            ar.write_map_key(key);
            ar.write_element(value);
        }
        ar.end_map();
    } else {
        // Non-string keys: serialize as array of pairs
        ar.begin_array(map.size());
        for (const auto& [key, value] : map) {
            ar.begin_array(2);
            ar.write_element(key);
            ar.write_element(value);
            ar.end_array();
        }
        ar.end_array();
    }
}

template<typename Archive, typename K, typename V, typename C, typename A, JAI_ONLY_ARCHIVE>
void load(Archive& ar, std::map<K, V, C, A>& map) {
    map.clear();
    if constexpr (std::is_same_v<K, std::string>) {
        // String keys: use map format
        size_t size = ar.begin_map();
        for (size_t i = 0; i < size; ++i) {
            std::string key;
            if (!ar.read_map_key(key)) break;
            V value;
            ar.read_element(value);
            map[key] = std::move(value);
        }
        ar.end_map();
    } else {
        // Non-string keys: deserialize from array of pairs
        size_t size = ar.begin_array();
        for (size_t i = 0; i < size; ++i) {
            ar.begin_array();
            K key;
            V value;
            ar.read_element(key);
            ar.read_element(value);
            ar.end_array();
            map[std::move(key)] = std::move(value);
        }
        ar.end_array();
    }
}

// --- std::unordered_map (same serialization strategy as std::map) ---
template<typename Archive, typename K, typename V, typename H, typename E, typename A, JAI_ONLY_ARCHIVE>
void save(Archive& ar, const std::unordered_map<K, V, H, E, A>& map) {
    if constexpr (std::is_same_v<K, std::string>) {
        ar.begin_map(map.size());
        for (const auto& [key, value] : map) {
            ar.write_map_key(key);
            ar.write_element(value);
        }
        ar.end_map();
    } else {
        ar.begin_array(map.size());
        for (const auto& [key, value] : map) {
            ar.begin_array(2);
            ar.write_element(key);
            ar.write_element(value);
            ar.end_array();
        }
        ar.end_array();
    }
}

template<typename Archive, typename K, typename V, typename H, typename E, typename A, JAI_ONLY_ARCHIVE>
void load(Archive& ar, std::unordered_map<K, V, H, E, A>& map) {
    map.clear();
    if constexpr (std::is_same_v<K, std::string>) {
        size_t size = ar.begin_map();
        for (size_t i = 0; i < size; ++i) {
            std::string key;
            if (!ar.read_map_key(key)) break;
            V value;
            ar.read_element(value);
            map[key] = std::move(value);
        }
        ar.end_map();
    } else {
        size_t size = ar.begin_array();
        for (size_t i = 0; i < size; ++i) {
            ar.begin_array();
            K key;
            V value;
            ar.read_element(key);
            ar.read_element(value);
            ar.end_array();
            map[std::move(key)] = std::move(value);
        }
        ar.end_array();
    }
}

// --- std::variant ---
namespace stl_detail {
    // Helper to save a variant by visiting it (templated on Archive)
    template<typename Archive, typename... Ts>
    struct variant_saver {
        Archive& ar;

        template<typename T>
        void operator()(const T& value) {
            ar.write_element(value);
        }
    };

    // Helper to load variant alternatives by index (templated on Archive)
    template<typename Archive, typename Variant, std::size_t I = 0>
    void load_variant_by_index(Archive& ar, Variant& v, std::size_t index) {
        if constexpr (I < std::variant_size_v<Variant>) {
            if (I == index) {
                using T = std::variant_alternative_t<I, Variant>;
                T value;
                ar.read_element(value);
                v = std::move(value);
            } else {
                load_variant_by_index<Archive, Variant, I + 1>(ar, v, index);
            }
        } else {
            throw serialization_error("Invalid variant index: " + std::to_string(index));
        }
    }
}

template<typename Archive, typename... Ts, JAI_ONLY_ARCHIVE>
void save(Archive& ar, const std::variant<Ts...>& v) {
    ar.begin_array(2);
    // Write the type index
    ar.write_element(static_cast<uint32_t>(v.index()));
    // Write the value
    std::visit(stl_detail::variant_saver<Archive, Ts...>{ar}, v);
    ar.end_array();
}

template<typename Archive, typename... Ts, JAI_ONLY_ARCHIVE>
void load(Archive& ar, std::variant<Ts...>& v) {
    ar.begin_array();
    uint32_t index;
    ar.read_element(index);
    stl_detail::load_variant_by_index<Archive>(ar, v, static_cast<std::size_t>(index));
    ar.end_array();
}

// --- std::optional ---
template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
void save(Archive& ar, const std::optional<T>& opt) {
    ar.begin_array(opt.has_value() ? 2 : 1);
    ar.write_element(opt.has_value());
    if (opt.has_value()) {
        ar.write_element(*opt);
    }
    ar.end_array();
}

template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
void load(Archive& ar, std::optional<T>& opt) {
    ar.begin_array();
    bool has_value;
    ar.read_element(has_value);
    if (has_value) {
        T value;
        ar.read_element(value);
        opt = std::move(value);
    } else {
        opt = std::nullopt;
    }
    ar.end_array();
}

// --- std::pair ---
template<typename Archive, typename T1, typename T2, JAI_ONLY_ARCHIVE>
void save(Archive& ar, const std::pair<T1, T2>& p) {
    ar.begin_array(2);
    ar.write_element(p.first);
    ar.write_element(p.second);
    ar.end_array();
}

template<typename Archive, typename T1, typename T2, JAI_ONLY_ARCHIVE>
void load(Archive& ar, std::pair<T1, T2>& p) {
    ar.begin_array();
    ar.read_element(p.first);
    ar.read_element(p.second);
    ar.end_array();
}

// --- std::tuple ---
namespace stl_detail {
    template<typename Archive, typename Tuple, std::size_t... Is>
    void save_tuple_impl(Archive& ar, const Tuple& t, std::index_sequence<Is...>) {
        (ar.write_element(std::get<Is>(t)), ...);
    }

    template<typename Archive, typename Tuple, std::size_t... Is>
    void load_tuple_impl(Archive& ar, Tuple& t, std::index_sequence<Is...>) {
        (ar.read_element(std::get<Is>(t)), ...);
    }
}

template<typename Archive, typename... Ts, JAI_ONLY_ARCHIVE>
void save(Archive& ar, const std::tuple<Ts...>& t) {
    ar.begin_array(sizeof...(Ts));
    stl_detail::save_tuple_impl(ar, t, std::index_sequence_for<Ts...>{});
    ar.end_array();
}

template<typename Archive, typename... Ts, JAI_ONLY_ARCHIVE>
void load(Archive& ar, std::tuple<Ts...>& t) {
    ar.begin_array();
    stl_detail::load_tuple_impl(ar, t, std::index_sequence_for<Ts...>{});
    ar.end_array();
}

} // namespace serialization

// ============================================================================
// jai::property<T> serialization support
// ============================================================================
// These functions are in the jai namespace (not jai::serialization) so they're
// found via ADL when serializing property<T> objects.
// Properties serialize as just their underlying value - the property name is
// handled by the caller (property_manager or direct ar.serialize() call).

// Forward declaration of property template
template<typename T> class property;

namespace serialization {

// --- jai::property<T> ---
// Serializes just the underlying value (property name handled by caller)
template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
void save(Archive& ar, const ::jai::property<T>& prop) {
    ar(prop.get());
}

template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
void load(Archive& ar, ::jai::property<T>& prop) {
    T value{};
    ar(value);
    prop = std::move(value);
}

} // namespace serialization
} // namespace jai
#endif // JAISCRIPT_SERIALIZATION_ARCHIVE_HPP
