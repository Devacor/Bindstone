#pragma once
// Test helpers that round-trip a value through each archive format. They let the
// existing JSON serialization tests double as binary-archive tests: a test builds
// its graph once and runs the same assertions for both `roundtrip_json` and
// `roundtrip_binary`. Mirrors the engine's Node::saveJai/saveBinary (ar(value)
// directly, no begin_object wrapper).

#include <jaiscript/core/engine.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/serialization/binary_archive.hpp>

namespace jai::foundry::tests {

template<typename T>
T roundtrip_json(jai::engine& eng, const T& in) {
    jai::serialization::json_archive_writer w(0, &eng);
    w(in);
    jai::serialization::json_archive_reader r(w.str(), &eng);
    T out{};
    r(out);
    return out;
}

template<typename T>
T roundtrip_binary(jai::engine& eng, const T& in) {
    jai::serialization::binary_archive_writer w(&eng);
    w(in);
    auto& d = w.data();
    jai::serialization::binary_archive_reader r(d.data(), d.size(), &eng);
    T out{};
    r(out);
    return out;
}

} // namespace jai::foundry::tests
