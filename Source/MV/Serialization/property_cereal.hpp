#pragma once

// Cereal serialization support for jai::property<T>
// Include this header when using jai::property with cereal archives
//
// This file lives in MV (not JaiScript) because JaiScript is designed
// to be dependency-free. This provides the bridge between jai::property
// and Cereal archives for MV types.

#include <jaiscript/properties/property.hpp>

// Forward declare cereal namespace
namespace cereal {
    class access;
}

namespace cereal {

    // Serialize jai::property<T> by forwarding to the underlying value
    // This allows property<T> to work transparently with cereal archives

    // Save function for jai::property<T>
    template<class Archive, typename T>
    void save(Archive& ar, const jai::property<T>& prop) {
        ar(prop.get());
    }

    // Load function for jai::property<T>
    // Loads directly into the property's value to avoid requiring default constructor
    template<class Archive, typename T>
    void load(Archive& ar, jai::property<T>& prop) {
        ar(prop.get());
    }

    // For deleted_property, save does nothing and load skips the value
    template<class Archive, typename T>
    void save(Archive& ar, const jai::deleted_property<T>& prop) {
        // deleted properties don't save anything
    }

    template<class Archive, typename T>
    void load(Archive& ar, jai::deleted_property<T>& prop) {
        // Read and discard the value
        T dummy;
        ar(dummy);
    }

} // namespace cereal
