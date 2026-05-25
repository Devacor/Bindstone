#pragma once

// ============================================================================
// Archive Dispatch Implementation
// ============================================================================
// This header provides the implementation of dispatch() methods for
// any_archive_writer and any_archive_reader. These methods recover the
// concrete archive type at runtime, enabling full template instantiation
// for types that need it (shared_ptr, load_and_construct, etc.).
//
// Include this header when you need to use dispatch() on type-erased archives.
// The dispatch() method uses a switch on the stored archive ID to cast
// the void* pointer back to the concrete archive type, then calls your
// lambda with that concrete archive reference.
//
// Example:
//   any_archive_writer& ar = ...;
//   ar.dispatch([&](auto& concrete_ar) {
//       concrete_ar(make_nvp("name", my_shared_ptr));
//   });

#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/serialization/binary_archive.hpp>
#include <stdexcept>

namespace jai {
namespace serialization {

// ============================================================================
// any_archive_writer::dispatch() implementation
// ============================================================================
template<typename Fn>
decltype(auto) any_archive_writer::dispatch(Fn&& fn) {
    switch (id_) {
        case writer_archive_id::json:
            return fn(*static_cast<json_archive_writer*>(ptr_));
        case writer_archive_id::binary:
            return fn(*static_cast<binary_archive_writer*>(ptr_));
        default:
            throw std::runtime_error("Unknown writer archive type for dispatch");
    }
}

// ============================================================================
// any_archive_reader::dispatch() implementation
// ============================================================================
template<typename Fn>
decltype(auto) any_archive_reader::dispatch(Fn&& fn) {
    switch (id_) {
        case reader_archive_id::json:
            return fn(*static_cast<json_archive_reader*>(ptr_));
        case reader_archive_id::binary:
            return fn(*static_cast<binary_archive_reader*>(ptr_));
        default:
            throw std::runtime_error("Unknown reader archive type for dispatch");
    }
}

} // namespace serialization
} // namespace jai
