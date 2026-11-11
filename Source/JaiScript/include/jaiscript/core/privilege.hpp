#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace jai {

// Every privilege is a slot. The engine's own come first; a host's start at privilege_count, as an enum it declares and
// opts in with one declaration beside it, found by argument-dependent lookup:
//   enum class GamePrivilege : std::uint8_t { cheats = jai::privilege_count, editor };
//   void jai_privilege_enum(GamePrivilege);
// Only implemented and enforced capabilities belong in the engine's own set.
enum class privilege : std::uint8_t {
    filesystem,
    scoped_template,   // scoped_template and scoped_template_text: untrusted templates read under their own root (detail/template_library.hpp)
    network,           // jai_spider's requests and the runner's Download.start; off until the host enables it
};

inline constexpr std::uint8_t privilege_count = 3;
// one per value of a std::uint8_t enum
inline constexpr std::size_t privilege_slots = 256;

inline constexpr privilege filesystem = privilege::filesystem;
inline constexpr privilege scoped_template = privilege::scoped_template;
inline constexpr privilege network = privilege::network;

namespace detail {
    [[noreturn]] void throw_privilege_slot_out_of_range();

    template<typename E>
    concept privilege_enum = std::is_same_v<E, privilege> || (std::is_enum_v<E> && requires(E a_privilege) { jai_privilege_enum(a_privilege); });
}

// A set of slots: what a call requires, what an engine grants
class privilege_set {
public:
    constexpr privilege_set() noexcept = default;

    template<detail::privilege_enum E>
    constexpr privilege_set(E a_privilege) { add(a_privilege); }

    template<detail::privilege_enum E>
    constexpr privilege_set& add(E a_privilege) {
        const auto value = static_cast<std::underlying_type_t<E>>(a_privilege);
        if constexpr (std::is_signed_v<decltype(value)>) {
            if (value < 0) { detail::throw_privilege_slot_out_of_range(); }
        }
        return add_slot(static_cast<std::uint64_t>(value));
    }
    constexpr privilege_set& add_slot(std::uint64_t a_slot) {
        if (a_slot >= privilege_slots) { detail::throw_privilege_slot_out_of_range(); }
        words[a_slot / 64] |= std::uint64_t{1} << (a_slot % 64);
        return *this;
    }
    constexpr privilege_set& merge(const privilege_set& a_other) noexcept {
        for (std::size_t i = 0; i < words.size(); ++i) { words[i] |= a_other.words[i]; }
        return *this;
    }
    constexpr privilege_set& remove(const privilege_set& a_other) noexcept {
        for (std::size_t i = 0; i < words.size(); ++i) { words[i] &= ~a_other.words[i]; }
        return *this;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        for (std::uint64_t word : words) {
            if (word != 0) { return false; }
        }
        return true;
    }
    [[nodiscard]] constexpr bool contains(std::size_t a_slot) const noexcept {
        return a_slot < privilege_slots && ((words[a_slot / 64] >> (a_slot % 64)) & 1) != 0;
    }
    // The lowest slot of a_required this set lacks, privilege_slots when it holds them all
    [[nodiscard]] constexpr std::size_t first_missing(const privilege_set& a_required) const noexcept {
        for (std::size_t i = 0; i < words.size(); ++i) {
            if (const std::uint64_t missing = a_required.words[i] & ~words[i]; missing != 0) {
                return i * 64 + static_cast<std::size_t>(std::countr_zero(missing));
            }
        }
        return privilege_slots;
    }
    [[nodiscard]] constexpr bool covers(const privilege_set& a_required) const noexcept { return first_missing(a_required) == privilege_slots; }

    friend constexpr bool operator==(const privilege_set&, const privilege_set&) = default;

private:
    std::array<std::uint64_t, privilege_slots / 64> words{};
};

// Trailing options beside the privileges: a script statement may not drop the result (as a script declares [[nodiscard]]),
// and the call touches state other objects share, so a parallel body may not make it
struct nodiscard_option {};
struct serial_option {};
inline constexpr nodiscard_option nodiscard{};
inline constexpr serial_option serial{};

// What the trailing options of add_function, add_variadic_function and a binder's method, static_method and constructor add up
// to: any number, in any order, as in add_function("save", fn, jai::nodiscard, jai::serial, GamePrivilege::cheats)
struct function_options {
    privilege_set required;
    bool nodiscard = false;
    bool serial = false;
};

namespace detail {
    template<typename O>
    concept function_option = privilege_enum<std::remove_cvref_t<O>> || std::is_same_v<std::remove_cvref_t<O>, nodiscard_option> ||
                              std::is_same_v<std::remove_cvref_t<O>, serial_option>;

    template<typename O>
    constexpr void add_function_option(function_options& a_options, const O& a_option) {
        if constexpr (std::is_same_v<O, nodiscard_option>) {
            a_options.nodiscard = true;
        } else if constexpr (std::is_same_v<O, serial_option>) {
            a_options.serial = true;
        } else {
            a_options.required.add(a_option);
        }
    }

    template<function_option... Options>
    constexpr function_options fold_function_options(const Options&... a_options) {
        function_options result;
        (add_function_option(result, a_options), ...);
        return result;
    }

    template<privilege_enum... Privileges>
    constexpr privilege_set privilege_set_of(Privileges... a_privileges) {
        privilege_set result;
        (result.add(a_privileges), ...);
        return result;
    }
}

} // namespace jai
