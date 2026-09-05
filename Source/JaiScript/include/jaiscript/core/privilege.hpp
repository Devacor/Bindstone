#pragma once

#include <cstdint>

namespace jai {

// Engine-owned grants. Only implemented/enforced capabilities belong in this set.
// A scoped enum leaves room for privilege::serialization without colliding with
// the existing jai::serialization C++ namespace.
enum class privilege : uint64_t {
    none = 0,
    filesystem = 1,
};

inline constexpr privilege filesystem = privilege::filesystem;

constexpr privilege operator|(privilege left, privilege right) noexcept {
    return static_cast<privilege>(static_cast<uint64_t>(left) | static_cast<uint64_t>(right));
}

constexpr privilege operator&(privilege left, privilege right) noexcept {
    return static_cast<privilege>(static_cast<uint64_t>(left) & static_cast<uint64_t>(right));
}

constexpr bool valid_privileges(privilege flags) noexcept {
    return (static_cast<uint64_t>(flags) & ~static_cast<uint64_t>(privilege::filesystem)) == 0;
}

} // namespace jai
