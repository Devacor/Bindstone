#pragma once

#ifndef JAISCRIPT_SERIALIZATION_CEREAL_MIGRATION_HPP
#define JAISCRIPT_SERIALIZATION_CEREAL_MIGRATION_HPP

#ifndef JAISCRIPT_ARCHIVE_HPP_INCLUDED
#error "Include <jaiscript/serialization/archive.hpp> before cereal_migration.hpp"
#endif

#ifdef CEREAL_NVP

namespace jai::serialization {

template<class Derived>
struct cereal_writer_mixin {
    template<typename T>
    void cereal_nvp(const cereal::NameValuePair<T>& nv) {
        static_cast<Derived*>(this)->serialize(nv.name, nv.value);
    }

    template<typename T, typename... Rest>
    void cereal_nvp(const cereal::NameValuePair<T>& first, const Rest&... rest) {
        static_cast<Derived*>(this)->serialize(first.name, first.value);
        if constexpr (sizeof...(rest) > 0) {
            cereal_nvp(rest...);
        }
    }
};

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
