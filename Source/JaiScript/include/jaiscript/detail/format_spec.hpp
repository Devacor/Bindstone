#pragma once

#include <string>
#include <string_view>

namespace jai {

class script_value;

namespace detail {

// Format-spec mini-language for template-string splices (`${hp:.1f}`) and the
// stdlib format()/print spec placeholders ({:.1f} / {0:>6}). Deliberate
// std::format subset — full grammar documented in docs/grammar.md "Template
// Strings":
//
//   spec      = [[fill] align] [width] ["." precision] [type]
//   align     = "<" | ">" | "^"
//   fill      = any char except "{" and "}" (default " "; requires align)
//   width     = [1-9][0-9]*          (max 1024; no zero-padding flag)
//   precision = "." [0-9]+           (numeric values only; max 1024)
//   type      = "f" | "x" | "X" | "b"
//
// Everything else (sign chars, '#', '0' pad, 'e'/'g', braces) is rejected.
struct format_spec {
    char fill = ' ';
    char align = 0;      // 0 = default: '>' for int/float, '<' for everything else
    int width = 0;
    int precision = -1;  // -1 = unset
    char type = 0;       // 0, 'f', 'x', 'X', 'b'
};

inline constexpr int format_spec_max_width = 1024;
inline constexpr int format_spec_max_precision = 1024;

inline bool parse_format_spec(std::string_view s, format_spec& out) {
    if (s.empty()) return false;
    size_t i = 0;
    auto is_align = [](char c) { return c == '<' || c == '>' || c == '^'; };
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    if (s.size() >= 2 && is_align(s[1]) && s[0] != '{' && s[0] != '}') {
        out.fill = s[0];
        out.align = s[1];
        i = 2;
    } else if (is_align(s[0])) {
        out.align = s[0];
        i = 1;
    }
    if (i < s.size() && is_digit(s[i])) {
        if (s[i] == '0') return false;   // zero-padding flag unsupported
        long w = 0;
        while (i < s.size() && is_digit(s[i])) {
            w = w * 10 + (s[i] - '0');
            if (w > format_spec_max_width) return false;
            ++i;
        }
        out.width = static_cast<int>(w);
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        if (i >= s.size() || !is_digit(s[i])) return false;
        long p = 0;
        while (i < s.size() && is_digit(s[i])) {
            p = p * 10 + (s[i] - '0');
            if (p > format_spec_max_precision) return false;
            ++i;
        }
        out.precision = static_cast<int>(p);
    }
    if (i < s.size()) {
        char t = s[i];
        if (t != 'f' && t != 'x' && t != 'X' && t != 'b') return false;
        out.type = t;
        ++i;
    }
    return i == s.size();
}

// ONE runtime formatter shared by the template-string desugar (the engine-core
// `format_value` builtin) and the stdlib format()/print spec placeholders, so
// both execution backends produce byte-identical output and error text by
// construction. Defined in engine.cpp to keep the <charconv> numeric work off
// the universal header path. Throws jai::runtime_error on a type-vs-spec
// mismatch (e.g. `${str:.2f}`).
std::string format_value_with_spec(const script_value& value, const format_spec& spec, std::string_view raw_spec);

} // namespace detail
} // namespace jai
