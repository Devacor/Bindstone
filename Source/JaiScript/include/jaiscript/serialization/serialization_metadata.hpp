#pragma once

#include <jaiscript/core/type_info.hpp>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <cstdint>
#include <typeindex>
#include <typeinfo>
#include <type_traits>

// Forward declarations to avoid circular dependencies
namespace jai {
    class script_value;
}

namespace jai {
namespace serialization {

class json_archive_writer;
class json_archive_reader;
class binary_archive_writer;
class binary_archive_reader;

enum class writer_archive_id : uint8_t {
    json = 0,
    binary = 1,
    unknown = 255
};

enum class reader_archive_id : uint8_t {
    json = 0,
    binary = 1,
    unknown = 255
};

template<typename Archive> struct writer_archive_id_trait {
    static constexpr writer_archive_id value = writer_archive_id::unknown;
};
template<typename Archive> struct reader_archive_id_trait {
    static constexpr reader_archive_id value = reader_archive_id::unknown;
};

// Uses function pointers (manual vtable) for minimal indirection.
// Only used for dynamic/runtime polymorphism - static code uses CRTP directly.
// See JAI_ARCHIVE_DEVIRTUALIZATION.md for design rationale.
class any_archive_writer {
    void* ptr_;
    writer_archive_id id_;
    void (*write_property_name_)(void*, const std::string&);
    void (*write_int8_)(void*, int8_t);
    void (*write_int16_)(void*, int16_t);
    void (*write_int32_)(void*, int32_t);
    void (*write_int64_)(void*, int64_t);
    void (*write_uint8_)(void*, uint8_t);
    void (*write_uint16_)(void*, uint16_t);
    void (*write_uint32_)(void*, uint32_t);
    void (*write_uint64_)(void*, uint64_t);
    void (*write_float32_)(void*, float);
    void (*write_float64_)(void*, double);
    void (*write_bool_)(void*, bool);
    void (*write_string_)(void*, const std::string&);
    void (*begin_object_)(void*, const std::string&, uint32_t);
    void (*end_object_)(void*);
    void (*begin_array_)(void*, size_t);
    void (*end_array_)(void*);
    void (*begin_map_)(void*, size_t);
    void (*end_map_)(void*);
    void (*write_map_key_)(void*, const std::string&);
    std::pair<uint32_t, bool> (*get_or_assign_shared_id_)(void*, const void*);

public:
    static constexpr bool is_text_format = true; // Conservative default for type-erased

    template<typename Archive>
    explicit any_archive_writer(Archive& ar)
        : ptr_(&ar)
        , id_(writer_archive_id_trait<std::decay_t<Archive>>::value)
        , write_property_name_([](void* p, const std::string& n) { static_cast<Archive*>(p)->write_property_name(n); })
        , write_int8_([](void* p, int8_t v) { static_cast<Archive*>(p)->write_int8(v); })
        , write_int16_([](void* p, int16_t v) { static_cast<Archive*>(p)->write_int16(v); })
        , write_int32_([](void* p, int32_t v) { static_cast<Archive*>(p)->write_int32(v); })
        , write_int64_([](void* p, int64_t v) { static_cast<Archive*>(p)->write_int64(v); })
        , write_uint8_([](void* p, uint8_t v) { static_cast<Archive*>(p)->write_uint8(v); })
        , write_uint16_([](void* p, uint16_t v) { static_cast<Archive*>(p)->write_uint16(v); })
        , write_uint32_([](void* p, uint32_t v) { static_cast<Archive*>(p)->write_uint32(v); })
        , write_uint64_([](void* p, uint64_t v) { static_cast<Archive*>(p)->write_uint64(v); })
        , write_float32_([](void* p, float v) { static_cast<Archive*>(p)->write_float32(v); })
        , write_float64_([](void* p, double v) { static_cast<Archive*>(p)->write_float64(v); })
        , write_bool_([](void* p, bool v) { static_cast<Archive*>(p)->write_bool(v); })
        , write_string_([](void* p, const std::string& v) { static_cast<Archive*>(p)->write_string(v); })
        , begin_object_([](void* p, const std::string& t, uint32_t v) { static_cast<Archive*>(p)->begin_object(t, v); })
        , end_object_([](void* p) { static_cast<Archive*>(p)->end_object(); })
        , begin_array_([](void* p, size_t s) { static_cast<Archive*>(p)->begin_array(s); })
        , end_array_([](void* p) { static_cast<Archive*>(p)->end_array(); })
        , begin_map_([](void* p, size_t s) { static_cast<Archive*>(p)->begin_map(s); })
        , end_map_([](void* p) { static_cast<Archive*>(p)->end_map(); })
        , write_map_key_([](void* p, const std::string& k) { static_cast<Archive*>(p)->write_map_key(k); })
        , get_or_assign_shared_id_([](void* p, const void* raw) { return static_cast<Archive*>(p)->get_or_assign_shared_id(raw); })
    {}

    void write_property_name(const std::string& name) { write_property_name_(ptr_, name); }
    void write_int8(int8_t v) { write_int8_(ptr_, v); }
    void write_int16(int16_t v) { write_int16_(ptr_, v); }
    void write_int32(int32_t v) { write_int32_(ptr_, v); }
    void write_int64(int64_t v) { write_int64_(ptr_, v); }
    void write_uint8(uint8_t v) { write_uint8_(ptr_, v); }
    void write_uint16(uint16_t v) { write_uint16_(ptr_, v); }
    void write_uint32(uint32_t v) { write_uint32_(ptr_, v); }
    void write_uint64(uint64_t v) { write_uint64_(ptr_, v); }
    void write_float32(float v) { write_float32_(ptr_, v); }
    void write_float64(double v) { write_float64_(ptr_, v); }
    void write_bool(bool v) { write_bool_(ptr_, v); }
    void write_string(const std::string& v) { write_string_(ptr_, v); }
    void begin_object(const std::string& type, uint32_t version) { begin_object_(ptr_, type, version); }
    void end_object() { end_object_(ptr_); }
    void begin_array(size_t size) { begin_array_(ptr_, size); }
    void end_array() { end_array_(ptr_); }
    void begin_map(size_t size) { begin_map_(ptr_, size); }
    void end_map() { end_map_(ptr_); }
    void write_map_key(const std::string& key) { write_map_key_(ptr_, key); }
    std::pair<uint32_t, bool> get_or_assign_shared_id(const void* raw) { return get_or_assign_shared_id_(ptr_, raw); }

    writer_archive_id archive_id() const { return id_; }
    void* raw_ptr() const { return ptr_; }

    // Usage: ar.dispatch([&](auto& concrete_ar) { concrete_ar(make_nvp("name", value)); });
    // Implementation is in archive.hpp after concrete archive definitions
    template<typename Fn>
    decltype(auto) dispatch(Fn&& fn);
};

class any_archive_reader {
    void* ptr_;
    reader_archive_id id_;
    bool (*seek_property_)(void*, const std::string&);
    bool (*read_property_name_)(void*, std::string&);
    int8_t (*read_int8_)(void*);
    int16_t (*read_int16_)(void*);
    int32_t (*read_int32_)(void*);
    int64_t (*read_int64_)(void*);
    uint8_t (*read_uint8_)(void*);
    uint16_t (*read_uint16_)(void*);
    uint32_t (*read_uint32_)(void*);
    uint64_t (*read_uint64_)(void*);
    float (*read_float32_)(void*);
    double (*read_float64_)(void*);
    bool (*read_bool_)(void*);
    std::string (*read_string_)(void*);
    bool (*begin_object_)(void*, std::string&, uint32_t&);
    void (*end_object_)(void*);
    size_t (*begin_array_)(void*);
    void (*end_array_)(void*);
    size_t (*begin_map_)(void*);
    void (*end_map_)(void*);
    bool (*read_map_key_)(void*, std::string&);
    bool (*has_property_)(void*, const std::string&);
    bool (*needs_property_keys_)(void*);
    void* (*get_engine_)(void*);  // Returns engine*
    void* (*get_user_context_)(void*, std::type_index);  // Returns user context by type_index
    void (*clear_property_value_)(void*);
    // Shared pointer ID tracking for type-erased smart pointer deserialization. The void
    // pointers carry a type_index so multiple-inheritance base-offset adjustment can happen
    // on the concrete side (this header can't see polymorphic_registry).
    bool (*has_deserialized_shared_)(void*, uint32_t);
    std::shared_ptr<void> (*get_deserialized_shared_adjusted_)(void*, uint32_t, std::type_index);
    void (*register_deserialized_shared_typed_)(void*, uint32_t, std::shared_ptr<void>, std::type_index);

public:
    template<typename Archive>
    explicit any_archive_reader(Archive& ar)
        : ptr_(&ar)
        , id_(reader_archive_id_trait<std::decay_t<Archive>>::value)
        , seek_property_([](void* p, const std::string& n) { return static_cast<Archive*>(p)->seek_property(n); })
        , read_property_name_([](void* p, std::string& n) { return static_cast<Archive*>(p)->read_property_name(n); })
        , read_int8_([](void* p) { return static_cast<Archive*>(p)->read_int8(); })
        , read_int16_([](void* p) { return static_cast<Archive*>(p)->read_int16(); })
        , read_int32_([](void* p) { return static_cast<Archive*>(p)->read_int32(); })
        , read_int64_([](void* p) { return static_cast<Archive*>(p)->read_int64(); })
        , read_uint8_([](void* p) { return static_cast<Archive*>(p)->read_uint8(); })
        , read_uint16_([](void* p) { return static_cast<Archive*>(p)->read_uint16(); })
        , read_uint32_([](void* p) { return static_cast<Archive*>(p)->read_uint32(); })
        , read_uint64_([](void* p) { return static_cast<Archive*>(p)->read_uint64(); })
        , read_float32_([](void* p) { return static_cast<Archive*>(p)->read_float32(); })
        , read_float64_([](void* p) { return static_cast<Archive*>(p)->read_float64(); })
        , read_bool_([](void* p) { return static_cast<Archive*>(p)->read_bool(); })
        , read_string_([](void* p) { return static_cast<Archive*>(p)->read_string(); })
        , begin_object_([](void* p, std::string& t, uint32_t& v) { return static_cast<Archive*>(p)->begin_object(t, v); })
        , end_object_([](void* p) { static_cast<Archive*>(p)->end_object(); })
        , begin_array_([](void* p) { return static_cast<Archive*>(p)->begin_array(); })
        , end_array_([](void* p) { static_cast<Archive*>(p)->end_array(); })
        , begin_map_([](void* p) { return static_cast<Archive*>(p)->begin_map(); })
        , end_map_([](void* p) { static_cast<Archive*>(p)->end_map(); })
        , read_map_key_([](void* p, std::string& k) { return static_cast<Archive*>(p)->read_map_key(k); })
        , has_property_([](void* p, const std::string& n) { return static_cast<Archive*>(p)->has_property(n); })
        , needs_property_keys_([](void*) { return Archive::needs_property_keys; })
        , get_engine_([](void* p) { return static_cast<void*>(static_cast<Archive*>(p)->get_engine()); })
        , get_user_context_([](void* p, std::type_index ti) -> void* {
            auto& ar = *static_cast<Archive*>(p);
            return ar.get_user_context_raw(ti);
        })
        , clear_property_value_([](void* p) { static_cast<Archive*>(p)->clear_property_value(); })
        , has_deserialized_shared_([](void* p, uint32_t id) { return static_cast<Archive*>(p)->has_deserialized_shared(id); })
        , get_deserialized_shared_adjusted_([](void* p, uint32_t id, std::type_index target) -> std::shared_ptr<void> {
            // Concrete side adjusts the complete pointer to the target base subobject.
            return static_cast<Archive*>(p)->get_deserialized_shared_adjusted(id, target);
        })
        , register_deserialized_shared_typed_([](void* p, uint32_t id, std::shared_ptr<void> complete, std::type_index type) {
            static_cast<Archive*>(p)->register_deserialized_shared_typed(id, std::move(complete), type);
        })
    {}

    bool seek_property(const std::string& name) { return seek_property_(ptr_, name); }
    bool read_property_name(std::string& name) { return read_property_name_(ptr_, name); }
    int8_t read_int8() { return read_int8_(ptr_); }
    int16_t read_int16() { return read_int16_(ptr_); }
    int32_t read_int32() { return read_int32_(ptr_); }
    int64_t read_int64() { return read_int64_(ptr_); }
    uint8_t read_uint8() { return read_uint8_(ptr_); }
    uint16_t read_uint16() { return read_uint16_(ptr_); }
    uint32_t read_uint32() { return read_uint32_(ptr_); }
    uint64_t read_uint64() { return read_uint64_(ptr_); }
    float read_float32() { return read_float32_(ptr_); }
    double read_float64() { return read_float64_(ptr_); }
    bool read_bool() { return read_bool_(ptr_); }
    std::string read_string() { return read_string_(ptr_); }
    bool begin_object(std::string& type, uint32_t& version) { return begin_object_(ptr_, type, version); }
    void end_object() { end_object_(ptr_); }
    size_t begin_array() { return begin_array_(ptr_); }
    void end_array() { end_array_(ptr_); }
    size_t begin_map() { return begin_map_(ptr_); }
    void end_map() { end_map_(ptr_); }
    bool read_map_key(std::string& key) { return read_map_key_(ptr_, key); }
    bool has_property(const std::string& name) { return has_property_(ptr_, name); }
    bool needs_property_keys() { return needs_property_keys_(ptr_); }

    template<typename Engine>
    Engine* get_engine() { return static_cast<Engine*>(get_engine_(ptr_)); }

    template<typename ContextType>
    ContextType* get_user_context() {
        void* raw = get_user_context_(ptr_, std::type_index(typeid(ContextType)));
        return static_cast<ContextType*>(raw);
    }

    bool has_deserialized_shared(uint32_t id) { return has_deserialized_shared_(ptr_, id); }

    // Get a previously deserialized shared_ptr by ID, cast to the target type. The void is
    // already adjusted to T's subobject, so the static_pointer_cast reinterprets a correct
    // address (multiple-inheritance safe).
    template<typename T>
    std::shared_ptr<T> get_deserialized_shared(uint32_t id) {
        auto void_ptr = get_deserialized_shared_adjusted_(ptr_, id, std::type_index(typeid(T)));
        return std::static_pointer_cast<T>(void_ptr);
    }

    // Register a newly deserialized shared_ptr, storing the complete object + its dynamic
    // type so later back-refs through any base view adjust correctly.
    template<typename T>
    void register_deserialized_shared(uint32_t id, std::shared_ptr<T> ptr) {
        if (!ptr) { return; }
        if constexpr (std::is_polymorphic_v<T>) {
            void* complete = dynamic_cast<void*>(ptr.get());
            register_deserialized_shared_typed_(ptr_, id, std::shared_ptr<void>(ptr, complete), std::type_index(typeid(*ptr)));
        } else {
            register_deserialized_shared_typed_(ptr_, id, std::static_pointer_cast<void>(ptr), std::type_index(typeid(T)));
        }
    }

    reader_archive_id archive_id() const { return id_; }
    void* raw_ptr() const { return ptr_; }

    void clear_property_value() { clear_property_value_(ptr_); }

    // Usage: ar.dispatch([&](auto& concrete_ar) { concrete_ar(make_nvp("name", value)); });
    // Implementation is in archive.hpp after concrete archive definitions
    template<typename Fn>
    decltype(auto) dispatch(Fn&& fn);
};

// Serialization metadata for properties
struct property_metadata {
    std::string name;
    type_info_ptr type;
    bool read_only = false;  // Skip during serialization if true

    // Network flags
    enum network_flags : uint32_t {
        none = 0,
        reliable = 1 << 0,
        unreliable = 1 << 1,
        immediate = 1 << 2,
        compress_position = 1 << 3,
        throttle_100ms = 1 << 4,
        throttle_500ms = 1 << 5
    };
    uint32_t network_flags = network_flags::none;

    // Direct serialization lambdas for runtime fallback (dynamic_binder-registered types)
    // These work with raw void* pointers to avoid needing script_value wrappers
    // save: (const void* cpp_object, any_archive_writer&) -> saves the property value
    // load: (void* cpp_object, any_archive_reader&) -> loads the property value
    std::function<void(const void*, any_archive_writer&)> save;
    std::function<void(void*, any_archive_reader&)> load;
};

// Class serialization metadata
struct class_metadata {
    std::string class_name;
    uint32_t current_version = 1;
    std::vector<property_metadata> properties;

    std::function<void(any_archive_writer&, const script_value&, uint32_t)> custom_save;
    std::function<script_value(any_archive_reader&, uint32_t)> custom_construct;

    std::vector<std::string> get_property_names() const {
        std::vector<std::string> result;
        result.reserve(properties.size());
        for (const auto& prop : properties) {
            result.push_back(prop.name);
        }
        return result;
    }
};

// Helper for network flags
struct network_flags {
    uint32_t flags;
    explicit network_flags(uint32_t f) : flags(f) {}
};

} // namespace serialization
} // namespace jai
