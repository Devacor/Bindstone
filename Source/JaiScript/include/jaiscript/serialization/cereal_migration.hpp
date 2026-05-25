#pragma once

#ifndef JAISCRIPT_SERIALIZATION_CEREAL_MIGRATION_HPP
#define JAISCRIPT_SERIALIZATION_CEREAL_MIGRATION_HPP

// ============================================================================
// Cereal Migration Helper
// ============================================================================
//
// This header provides compatibility between JaiScript's archive system and
// Cereal's NameValuePair (cereal::make_nvp / CEREAL_NVP).
//
// If you're migrating from Cereal to JaiScript serialization, include this
// header AFTER archive.hpp to enable transparent handling of cereal::NameValuePair
// in JaiScript archive operator() calls.
//
// Usage:
//   #include <jaiscript/serialization/archive.hpp>
//   #include <jaiscript/serialization/cereal_migration.hpp>
//
// Then existing code like:
//   ar(CEREAL_NVP(my_field));
//   ar(cereal::make_nvp("name", value));
//
// will work with JaiScript archives, forwarding to ar.serialize(name, value).
//
// Once migration is complete, remove this include and replace CEREAL_NVP usage
// with direct ar.serialize("name", value) or ar("name", value) calls.
// ============================================================================

#ifndef JAISCRIPT_ARCHIVE_HPP_INCLUDED
#error "Include <jaiscript/serialization/archive.hpp> before cereal_migration.hpp"
#endif

#ifdef CEREAL_NVP

namespace jai::serialization {

// ============================================================================
// Writer compatibility - adds cereal::NameValuePair overloads to archive writers
// ============================================================================

// Mixin that adds cereal NVP support to any CRTP archive writer
template<class Derived>
struct cereal_writer_mixin {
    // Single cereal::NameValuePair
    template<typename T>
    void cereal_nvp(const cereal::NameValuePair<T>& nv) {
        static_cast<Derived*>(this)->serialize(nv.name, nv.value);
    }

    // Variadic cereal::NameValuePair
    template<typename T, typename... Rest>
    void cereal_nvp(const cereal::NameValuePair<T>& first, const Rest&... rest) {
        static_cast<Derived*>(this)->serialize(first.name, first.value);
        if constexpr (sizeof...(rest) > 0) {
            cereal_nvp(rest...);
        }
    }
};

// ============================================================================
// Reader compatibility - adds cereal::NameValuePair overloads to archive readers
// ============================================================================

template<class Derived>
struct cereal_reader_mixin {
    template<typename T>
    void cereal_nvp(cereal::NameValuePair<T>& nv) {
        static_cast<Derived*>(this)->serialize(nv.name, nv.value);
    }

    template<typename T, typename... Rest>
    void cereal_nvp(cereal::NameValuePair<T>& first, Rest&... rest) {
        static_cast<Derived*>(this)->serialize(first.name, first.value);
        if constexpr (sizeof...(rest) > 0) {
            cereal_nvp(rest...);
        }
    }
};

} // namespace jai::serialization

#endif // CEREAL_NVP

#endif // JAISCRIPT_SERIALIZATION_CEREAL_MIGRATION_HPP
