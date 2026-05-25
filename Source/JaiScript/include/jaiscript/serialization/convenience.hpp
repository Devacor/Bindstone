#pragma once

#include "json_archive.hpp"
#include "binary_archive.hpp"
#include <cctype>

namespace jai {

// ============================================================================
// Base64 encoding/decoding (RFC 4648)
// ============================================================================
namespace detail {

inline constexpr char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

inline constexpr unsigned char base64_decode_table[] = {
    // 0-63: invalid (255), then mapped values
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255, // 0-15
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255, // 16-31
    255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63, // 32-47 (+ at 43, / at 47)
     52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,255,255,255, // 48-63 (0-9)
    255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, // 64-79 (A-O)
     15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255, // 80-95 (P-Z)
    255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, // 96-111 (a-o)
     41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255  // 112-127 (p-z)
};

inline std::string base64_encode(const std::string& input) {
    std::string result;
    result.reserve(((input.size() + 2) / 3) * 4);

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(input.data());
    size_t len = input.size();
    size_t i = 0;

    // Process 3 bytes at a time -> 4 base64 chars
    while (i + 2 < len) {
        result += base64_chars[(bytes[i] >> 2) & 0x3F];
        result += base64_chars[((bytes[i] & 0x03) << 4) | ((bytes[i+1] >> 4) & 0x0F)];
        result += base64_chars[((bytes[i+1] & 0x0F) << 2) | ((bytes[i+2] >> 6) & 0x03)];
        result += base64_chars[bytes[i+2] & 0x3F];
        i += 3;
    }

    // Handle remaining bytes
    if (i < len) {
        result += base64_chars[(bytes[i] >> 2) & 0x3F];

        if (i + 1 < len) {
            // 2 bytes remaining
            result += base64_chars[((bytes[i] & 0x03) << 4) | ((bytes[i+1] >> 4) & 0x0F)];
            result += base64_chars[(bytes[i+1] & 0x0F) << 2];
            result += '=';
        } else {
            // 1 byte remaining
            result += base64_chars[(bytes[i] & 0x03) << 4];
            result += "==";
        }
    }

    return result;
}

inline std::string base64_decode(const std::string& input) {
    std::string result;
    if (input.empty()) return result;

    // Count valid base64 characters (excluding padding)
    size_t valid_chars = 0;
    for (char c : input) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 128 && base64_decode_table[uc] != 255) {
            ++valid_chars;
        } else if (c == '=') {
            break; // Stop at padding
        }
    }

    result.reserve((valid_chars * 3) / 4);

    unsigned char buffer[4];
    size_t buffer_idx = 0;

    for (char c : input) {
        if (c == '=') break; // Padding marks end

        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 128) continue; // Skip invalid

        unsigned char val = base64_decode_table[uc];
        if (val == 255) continue; // Skip invalid

        buffer[buffer_idx++] = val;

        if (buffer_idx == 4) {
            // Decode 4 base64 chars -> 3 bytes
            result += static_cast<char>((buffer[0] << 2) | (buffer[1] >> 4));
            result += static_cast<char>((buffer[1] << 4) | (buffer[2] >> 2));
            result += static_cast<char>((buffer[2] << 6) | buffer[3]);
            buffer_idx = 0;
        }
    }

    // Handle remaining chars (with padding)
    if (buffer_idx >= 2) {
        result += static_cast<char>((buffer[0] << 2) | (buffer[1] >> 4));
    }
    if (buffer_idx >= 3) {
        result += static_cast<char>((buffer[1] << 4) | (buffer[2] >> 2));
    }

    return result;
}

} // namespace detail

// ============================================================================
// Convenience functions for serialization
// ============================================================================
// These provide a simple one-liner API for common serialization tasks.
// Engine is always required (JaiScript types are engine-bound).
// User context is optional for dependency injection during deserialization.

namespace detail {

// ============================================================================
// Smart pointer element type extraction
// ============================================================================
template<typename T> struct shared_ptr_element { using type = T; };
template<typename T> struct shared_ptr_element<std::shared_ptr<T>> { using type = T; };
template<typename T> using shared_ptr_element_t = typename shared_ptr_element<T>::type;

template<typename T> struct unique_ptr_element { using type = T; };
template<typename T, typename D> struct unique_ptr_element<std::unique_ptr<T, D>> { using type = T; };
template<typename T> using unique_ptr_element_t = typename unique_ptr_element<T>::type;

template<typename T> struct is_shared_ptr : std::false_type {};
template<typename T> struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};
template<typename T> inline constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

template<typename T> struct is_unique_ptr : std::false_type {};
template<typename T, typename D> struct is_unique_ptr<std::unique_ptr<T, D>> : std::true_type {};
template<typename T> inline constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

// Combined: is owning smart pointer (shared_ptr or unique_ptr, but NOT weak_ptr)
template<typename T>
inline constexpr bool is_owning_ptr_v = is_shared_ptr_v<T> || is_unique_ptr_v<T>;

// Get element type from either shared_ptr or unique_ptr
template<typename T>
using owning_ptr_element_t = std::conditional_t<
    is_shared_ptr_v<T>,
    shared_ptr_element_t<T>,
    std::conditional_t<is_unique_ptr_v<T>, unique_ptr_element_t<T>, T>
>;

// ============================================================================
// Type trait detection (unwraps shared_ptr to check pointee)
// ============================================================================

// Concept to detect property_owner types (has property_mgr member)
template<typename T, typename = void>
struct has_property_mgr : std::false_type {};

template<typename T>
struct has_property_mgr<T, std::void_t<decltype(std::declval<T>().property_mgr)>> : std::true_type {};

// Concept to detect types with templated save() method (using concrete archive for detection)
template<typename T, typename = void>
struct has_save_method : std::false_type {};

template<typename T>
struct has_save_method<T, std::void_t<decltype(std::declval<T>().save(std::declval<serialization::json_archive_writer&>()))>> : std::true_type {};

// Concept to detect types with templated load() method (using concrete archive for detection)
template<typename T, typename = void>
struct has_load_method : std::false_type {};

template<typename T>
struct has_load_method<T, std::void_t<decltype(std::declval<T>().load(std::declval<serialization::json_archive_reader&>()))>> : std::true_type {};

// Concept to detect types with templated serialize() method
template<typename T, typename = void>
struct has_serialize_method : std::false_type {};

template<typename T>
struct has_serialize_method<T, std::void_t<decltype(std::declval<T>().serialize(std::declval<serialization::json_archive_writer&>()))>> : std::true_type {};

// ============================================================================
// Smart pointer aware traits - unwrap owning pointers to check pointee type
// ============================================================================

// For save: check if T (or T's pointee for shared_ptr/unique_ptr) can be serialized directly
template<typename T>
constexpr bool can_use_direct_archive_v = []() {
    if constexpr (is_owning_ptr_v<T>) {
        using E = owning_ptr_element_t<T>;
        return has_property_mgr<E>::value || has_save_method<E>::value || has_serialize_method<E>::value;
    } else {
        return has_property_mgr<T>::value || has_save_method<T>::value || has_serialize_method<T>::value;
    }
}();

// For load: check if T (or T's pointee for shared_ptr/unique_ptr) can be deserialized directly
template<typename T>
constexpr bool can_use_direct_archive_read_v = []() {
    if constexpr (is_owning_ptr_v<T>) {
        using E = owning_ptr_element_t<T>;
        return has_property_mgr<E>::value || has_load_method<E>::value || has_serialize_method<E>::value;
    } else {
        return has_property_mgr<T>::value || has_load_method<T>::value || has_serialize_method<T>::value;
    }
}();

} // namespace detail

// ============================================================================
// JSON Serialization
// ============================================================================

// Serialize to JSON string
// Always wraps in begin_object/end_object for consistent format (like binary)
template<typename T>
std::string to_json(engine& eng, const T& value, int indent = 2) {
    serialization::json_archive_writer ar(indent, &eng);
    ar.begin_object("", 1);
    if constexpr (detail::can_use_direct_archive_v<T>) {
        // Types with property_mgr/save/serialize: serialize content directly
        // For shared_ptr/unique_ptr, this uses [id, {...}] format with dedup tracking
        ar.serialize_object_content(value);
    } else {
        // Basic types: serialize with "value0" key
        ar.serialize("value0", value);
    }
    ar.end_object();
    return ar.str();
}

// Serialize to JSON with user context
// Always wraps in begin_object/end_object for consistent format (like binary)
template<typename T, typename Context>
std::string to_json(engine& eng, const T& value, Context& ctx, int indent = 2) {
    serialization::json_archive_writer ar(indent, &eng);
    (void)ctx;
    ar.begin_object("", 1);
    if constexpr (detail::is_owning_ptr_v<T> && detail::can_use_direct_archive_v<T>) {
        if (value) {
            ar.serialize_object_content(*value);
        }
    } else if constexpr (detail::can_use_direct_archive_v<T>) {
        ar.serialize_object_content(value);  // Don't double-wrap
    } else {
        ar.serialize("value0", value);
    }
    ar.end_object();
    return ar.str();
}

// Deserialize from JSON string
// Always expects begin_object/end_object wrapper for consistent format (like binary)
template<typename T>
T from_json(engine& eng, const std::string& json) {
    serialization::json_archive_reader ar(json, &eng);
    std::string type_name;
    uint32_t version;
    ar.begin_object(type_name, version);
    T result{};
    if constexpr (detail::is_shared_ptr_v<T> && detail::can_use_direct_archive_read_v<T>
                   && std::is_default_constructible_v<detail::owning_ptr_element_t<T>>) {
        // shared_ptr<U> where U has property_mgr/load and is default-constructible
        using E = detail::owning_ptr_element_t<T>;
        result = std::make_shared<E>();
        ar(*result);  // Read as E, not shared_ptr<E>
    } else if constexpr (detail::is_unique_ptr_v<T> && detail::can_use_direct_archive_read_v<T>
                          && std::is_default_constructible_v<detail::owning_ptr_element_t<T>>) {
        // unique_ptr<U> where U has property_mgr/load and is default-constructible
        using E = detail::owning_ptr_element_t<T>;
        result = std::make_unique<E>();
        ar(*result);  // Read as E, not unique_ptr<E>
    } else if constexpr (detail::can_use_direct_archive_read_v<T>) {
        ar(result);
    } else {
        // Basic types: deserialize with "value0" key
        ar.serialize("value0", result);
    }
    ar.end_object();
    return std::move(result);
}

// Deserialize from JSON with user context (for dependency injection)
// Always expects begin_object/end_object wrapper for consistent format (like binary)
template<typename T, typename Context>
T from_json(engine& eng, const std::string& json, Context& ctx) {
    serialization::json_archive_reader ar(json, &eng);
    ar.template set_user_context<Context>(&ctx);
    std::string type_name;
    uint32_t version;
    ar.begin_object(type_name, version);
    T result{};
    if constexpr (detail::is_shared_ptr_v<T> && detail::can_use_direct_archive_read_v<T>
                   && std::is_default_constructible_v<detail::owning_ptr_element_t<T>>) {
        // shared_ptr<U> where U has property_mgr/load and is default-constructible
        using E = detail::owning_ptr_element_t<T>;
        result = std::make_shared<E>();
        ar(*result);  // Read as E, not shared_ptr<E>
    } else if constexpr (detail::is_unique_ptr_v<T> && detail::can_use_direct_archive_read_v<T>
                          && std::is_default_constructible_v<detail::owning_ptr_element_t<T>>) {
        // unique_ptr<U> where U has property_mgr/load and is default-constructible
        using E = detail::owning_ptr_element_t<T>;
        result = std::make_unique<E>();
        ar(*result);
    } else if constexpr (detail::can_use_direct_archive_read_v<T>) {
        ar(result);
    } else {
        ar.serialize("value0", result);
    }
    ar.end_object();
    return std::move(result);
}

// ============================================================================
// Binary Serialization
// ============================================================================

// Serialize to binary (returns vector)
// Always wraps in begin_object/end_object for consistent format with property sizes
template<typename T>
std::vector<uint8_t> to_binary(engine& eng, const T& value) {
    serialization::binary_archive_writer ar(&eng);
    ar.begin_object("", 1);
    if constexpr (detail::is_owning_ptr_v<T> && detail::can_use_direct_archive_v<T>) {
        if (value) {
            ar.serialize_object_content(*value);
        }
    } else if constexpr (detail::can_use_direct_archive_v<T>) {
        ar.serialize_object_content(value);  // Don't double-wrap
    } else {
        ar.serialize("value0", value);
    }
    ar.end_object();
    return ar.data();
}

// Serialize to binary string (for storage/transmission as string)
// Always wraps in begin_object/end_object for consistent format with property sizes
template<typename T>
std::string to_binary_string(engine& eng, const T& value) {
    serialization::binary_archive_writer ar(&eng);
    ar.begin_object("", 1);
    if constexpr (detail::is_owning_ptr_v<T> && detail::can_use_direct_archive_v<T>) {
        if (value) {
            ar.serialize_object_content(*value);
        }
    } else if constexpr (detail::can_use_direct_archive_v<T>) {
        ar.serialize_object_content(value);  // Don't double-wrap
    } else {
        ar.serialize("value0", value);
    }
    ar.end_object();
    auto& data = ar.data();
    return std::string(data.begin(), data.end());
}

// Serialize to binary with user context
template<typename T, typename Context>
std::string to_binary_string(engine& eng, const T& value, Context& ctx) {
    serialization::binary_archive_writer ar(&eng);
    (void)ctx;
    ar.begin_object("", 1);
    if constexpr (detail::is_owning_ptr_v<T> && detail::can_use_direct_archive_v<T>) {
        if (value) {
            ar.serialize_object_content(*value);
        }
    } else if constexpr (detail::can_use_direct_archive_v<T>) {
        ar.serialize_object_content(value);  // Don't double-wrap
    } else {
        ar.serialize("value0", value);
    }
    ar.end_object();
    auto& data = ar.data();
    return std::string(data.begin(), data.end());
}

// Deserialize from binary vector
// Always expects begin_object/end_object wrapper for consistent format
template<typename T>
T from_binary(engine& eng, const std::vector<uint8_t>& data) {
    serialization::binary_archive_reader ar(data, &eng);
    std::string type_name;
    uint32_t version;
    ar.begin_object(type_name, version);
    T result{};
    if constexpr (detail::is_shared_ptr_v<T> && detail::can_use_direct_archive_read_v<T>
                   && std::is_default_constructible_v<detail::owning_ptr_element_t<T>>) {
        using E = detail::owning_ptr_element_t<T>;
        result = std::make_shared<E>();
        ar(*result);
    } else if constexpr (detail::is_unique_ptr_v<T> && detail::can_use_direct_archive_read_v<T>
                          && std::is_default_constructible_v<detail::owning_ptr_element_t<T>>) {
        using E = detail::owning_ptr_element_t<T>;
        result = std::make_unique<E>();
        ar(*result);
    } else if constexpr (detail::can_use_direct_archive_read_v<T>) {
        ar(result);
    } else {
        ar.serialize("value0", result);
    }
    ar.end_object();
    return result;
}

// Deserialize from binary vector with user context
template<typename T, typename Context>
T from_binary(engine& eng, const std::vector<uint8_t>& data, Context& ctx) {
    serialization::binary_archive_reader ar(data, &eng);
    ar.template set_user_context<Context>(&ctx);
    std::string type_name;
    uint32_t version;
    ar.begin_object(type_name, version);
    T result{};
    if constexpr (detail::is_shared_ptr_v<T> && detail::can_use_direct_archive_read_v<T>
                   && std::is_default_constructible_v<detail::owning_ptr_element_t<T>>) {
        using E = detail::owning_ptr_element_t<T>;
        result = std::make_shared<E>();
        ar(*result);
    } else if constexpr (detail::is_unique_ptr_v<T> && detail::can_use_direct_archive_read_v<T>
                          && std::is_default_constructible_v<detail::owning_ptr_element_t<T>>) {
        using E = detail::owning_ptr_element_t<T>;
        result = std::make_unique<E>();
        ar(*result);
    } else if constexpr (detail::can_use_direct_archive_read_v<T>) {
        ar(result);
    } else {
        ar.serialize("value0", result);
    }
    ar.end_object();
    return result;
}

// Deserialize from binary string
template<typename T>
T from_binary_string(engine& eng, const std::string& data) {
    serialization::binary_archive_reader ar(
        reinterpret_cast<const uint8_t*>(data.data()),
        data.size(),
        &eng
    );
    std::string type_name;
    uint32_t version;
    ar.begin_object(type_name, version);
    T result{};
    if constexpr (detail::is_shared_ptr_v<T> && detail::can_use_direct_archive_read_v<T>
                   && std::is_default_constructible_v<detail::owning_ptr_element_t<T>>) {
        using E = detail::owning_ptr_element_t<T>;
        result = std::make_shared<E>();
        ar(*result);
    } else if constexpr (detail::is_unique_ptr_v<T> && detail::can_use_direct_archive_read_v<T>
                          && std::is_default_constructible_v<detail::owning_ptr_element_t<T>>) {
        using E = detail::owning_ptr_element_t<T>;
        result = std::make_unique<E>();
        ar(*result);
    } else if constexpr (detail::can_use_direct_archive_read_v<T>) {
        ar(result);
    } else {
        ar.serialize("value0", result);
    }
    ar.end_object();
    return result;
}

// Deserialize from binary string with user context
template<typename T, typename Context>
T from_binary_string(engine& eng, const std::string& data, Context& ctx) {
    serialization::binary_archive_reader ar(
        reinterpret_cast<const uint8_t*>(data.data()),
        data.size(),
        &eng
    );
    ar.template set_user_context<Context>(&ctx);
    std::string type_name;
    uint32_t version;
    ar.begin_object(type_name, version);
    T result{};
    if constexpr (detail::is_shared_ptr_v<T> && detail::can_use_direct_archive_read_v<T>
                   && std::is_default_constructible_v<detail::owning_ptr_element_t<T>>) {
        using E = detail::owning_ptr_element_t<T>;
        result = std::make_shared<E>();
        ar(*result);
    } else if constexpr (detail::is_unique_ptr_v<T> && detail::can_use_direct_archive_read_v<T>
                          && std::is_default_constructible_v<detail::owning_ptr_element_t<T>>) {
        using E = detail::owning_ptr_element_t<T>;
        result = std::make_unique<E>();
        ar(*result);
    } else if constexpr (detail::can_use_direct_archive_read_v<T>) {
        ar(result);
    } else {
        ar.serialize("value0", result);
    }
    ar.end_object();
    return result;
}

// ============================================================================
// Base64 Serialization (binary serialization + base64 encoding)
// ============================================================================

// Serialize to base64 string
template<typename T>
std::string to_base64(engine& eng, const T& value) {
    return detail::base64_encode(to_binary_string(eng, value));
}

// Serialize to base64 with user context
template<typename T, typename Context>
std::string to_base64(engine& eng, const T& value, Context& ctx) {
    return detail::base64_encode(to_binary_string(eng, value, ctx));
}

// Deserialize from base64 string
template<typename T>
T from_base64(engine& eng, const std::string& base64) {
    return from_binary_string<T>(eng, detail::base64_decode(base64));
}

// Deserialize from base64 with user context
template<typename T, typename Context>
T from_base64(engine& eng, const std::string& base64, Context& ctx) {
    return from_binary_string<T>(eng, detail::base64_decode(base64), ctx);
}

// ============================================================================
// Raw base64 encoding (for strings, not serialization)
// ============================================================================

inline std::string base64_encode(const std::string& input) {
    return detail::base64_encode(input);
}

inline std::string base64_decode(const std::string& input) {
    return detail::base64_decode(input);
}

} // namespace jai
