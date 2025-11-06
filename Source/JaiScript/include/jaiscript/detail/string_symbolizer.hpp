#pragma once

#ifndef __JAISCRIPT_DETAIL_STRING_SYMBOLIZER_HPP__
#define __JAISCRIPT_DETAIL_STRING_SYMBOLIZER_HPP__

#include <unordered_map>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>

namespace jai {

    // Transparent hasher for string_view lookup in unordered_map<string, ...>
    struct string_hash {
        using is_transparent = void;  // Enable heterogeneous lookup

        [[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }

        [[nodiscard]] size_t operator()(const std::string& s) const noexcept {
            return std::hash<std::string>{}(s);
        }
    };

    // Transparent equality for string_view lookup
    struct string_equal {
        using is_transparent = void;

        [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
            return lhs == rhs;
        }
    };

    // String symbolizer for faster variable lookups (like FName in Unreal engine)
    // IMPORTANT: This is a LOCAL-ONLY optimization. String IDs are NOT deterministic
    // across sessions/machines. Always serialize actual string names, never IDs!
    // For network sync or save/load, use the original string keys, not symbolized IDs.
    class string_symbolizer {
    private:
        std::unordered_map<std::string, uint64_t, string_hash, string_equal> string_id_map_;
        std::vector<std::string> strings_;
        mutable uint64_t cached_this_id_ = UINT64_MAX;  // Cached ID for "this"

    public:
        string_symbolizer() {
            // Reserve capacity for typical script usage
            strings_.reserve(256);
            string_id_map_.reserve(256);
        }

        uint64_t intern(std::string_view str) {
            // C++20 heterogeneous lookup - avoid string construction on lookup
            // Only create string if we need to insert
            if (auto it = string_id_map_.find(str); it != string_id_map_.end()) {
                return it->second;
            }

            // Not found - need to insert
            uint64_t id = static_cast<uint64_t>(strings_.size());
            strings_.emplace_back(str);
            string_id_map_.emplace(strings_.back(), id);  // Use the stored string
            return id;
        }

        const std::string& get_string(uint64_t id) const {
            const static std::string empty_string;
            return id < strings_.size() ? strings_[id] : empty_string;
        }

        // Get cached "this" ID (lazily initialized)
        uint64_t get_this_id() const {
            if (cached_this_id_ == UINT64_MAX) {
                cached_this_id_ = const_cast<string_symbolizer*>(this)->intern("this");
            }
            return cached_this_id_;
        }
    };

} // namespace jai

#endif // __JAISCRIPT_DETAIL_STRING_SYMBOLIZER_HPP__
