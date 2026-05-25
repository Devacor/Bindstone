#pragma once

#ifndef JAISCRIPT_SERIALIZATION_ARCHIVE_HPP
#define JAISCRIPT_SERIALIZATION_ARCHIVE_HPP
#define JAISCRIPT_ARCHIVE_HPP_INCLUDED

// Lightweight archive header for consumer code.
// Provides concepts, nvp helpers, access class, and forward declarations.
// For full CRTP archive implementations, include <jaiscript/serialization/archive_impl.hpp>.

#include <concepts>
#include <type_traits>
#include <string>
#include <memory>
#include <cstdint>

namespace jai {

class runtime_error;

namespace serialization {

// Base tag type for all JaiScript archives (used for concept detection)
struct archive_base {
    static constexpr bool is_jai_archive = true;
};

// ============================================================================
// Access class for protected serialize/save/load methods
// ============================================================================
class access {
public:
    template<class Archive, class T>
    static auto member_serialize(Archive& ar, T& t) -> decltype(t.serialize(ar)) {
        return t.serialize(ar);
    }
    template<class Archive, class T>
    static auto member_save(Archive& ar, const T& t) -> decltype(t.save(ar)) {
        return t.save(ar);
    }
    template<class Archive, class T>
    static auto member_load(Archive& ar, T& t) -> decltype(t.load(ar)) {
        return t.load(ar);
    }
    template<class Archive, class T>
    static auto member_serialize(Archive& ar, T& t, std::uint32_t version) -> decltype(t.serialize(ar, version)) {
        return t.serialize(ar, version);
    }
    template<class Archive, class T>
    static auto member_save(Archive& ar, const T& t, std::uint32_t version) -> decltype(t.save(ar, version)) {
        return t.save(ar, version);
    }
    template<class Archive, class T>
    static auto member_load(Archive& ar, T& t, std::uint32_t version) -> decltype(t.load(ar, version)) {
        return t.load(ar, version);
    }
    template<class T, class Archive, class Construct>
    static auto load_and_construct(Archive& ar, Construct& c) -> decltype(T::load_and_construct(ar, c)) {
        return T::load_and_construct(ar, c);
    }
    template<class T, class Archive, class Construct>
    static auto load_and_construct(Archive& ar, Construct& c, std::uint32_t version) -> decltype(T::load_and_construct(ar, c, version)) {
        return T::load_and_construct(ar, c, version);
    }
    template<typename T, typename... Args>
    static std::shared_ptr<T> make_shared(Args&&... args) {
        return std::shared_ptr<T>(new T(std::forward<Args>(args)...));
    }
    template<typename T, typename... Args>
    static std::unique_ptr<T> make_unique(Args&&... args) {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
};

// ============================================================================
// Name-value pair for serialization
// ============================================================================
template<typename T>
struct nvp {
    const char* name;
    T& value;
    nvp(const char* n, T& v) : name(n), value(v) {}
};

template<typename T>
nvp<T> make_nvp(const char* name, T& value) {
    return nvp<T>(name, value);
}

template<typename T>
nvp<const T> make_nvp(const char* name, const T& value) {
    return nvp<const T>(name, const_cast<const T&>(value));
}

#define JAI_NVP(x) ::jai::serialization::make_nvp(#x, x)

// ============================================================================
// Archive type detection concepts
// ============================================================================
template<typename T>
concept jai_archive = requires { { T::is_jai_archive } -> std::convertible_to<bool>; } && T::is_jai_archive;

template<typename T>
concept not_jai_archive = !jai_archive<T>;

template<typename T>
inline constexpr bool is_save = std::is_base_of_v<archive_base, std::decay_t<T>> &&
    requires { typename std::decay_t<T>::is_writer_tag; };

template<typename T>
inline constexpr bool is_load = std::is_base_of_v<archive_base, std::decay_t<T>> &&
    requires { typename std::decay_t<T>::is_reader_tag; };

#define JAI_ONLY_ARCHIVE std::enable_if_t<::jai::serialization::jai_archive<Archive>, int> = 0
#define JAI_TEMPLATE template <class Archive, JAI_ONLY_ARCHIVE>

// Forward declarations of concrete archive types
class json_archive_writer;
class json_archive_reader;
class binary_archive_writer;
class binary_archive_reader;
class polymorphic_registry;

template<class Derived> class archive_writer_impl;
template<class Derived> class archive_reader_impl;

} // namespace serialization
} // namespace jai

#endif // JAISCRIPT_SERIALIZATION_ARCHIVE_HPP
