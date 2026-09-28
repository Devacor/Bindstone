#pragma once

#ifndef __JAISCRIPT_STDLIB_STRINGS_HPP__
#define __JAISCRIPT_STDLIB_STRINGS_HPP__

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/detail/utf8.hpp>
#include <jaiscript/detail/builtin_methods.hpp>

#include <cctype>
#include <string>
#include <vector>

namespace jai {
namespace stdlib {

    // string:: namespace — NON-MUTATING transforms. The method forms
    // (s.to_lower() / s.to_upper()) mutate the receiver in place; these take the
    // string as an argument and return a new one, leaving the argument untouched.
    // ASCII byte semantics (chars are bytes 0..255 under the promotion rules).
    inline void register_string_functions(engine& eng_ref) {
        auto& ns = eng_ref.script_namespaces()[eng_ref.symbolize("string")];
        if (!ns) { ns = std::make_shared<script_namespace_data>(); }
        engine* eng = &eng_ref;
        constexpr auto text = script_value_type::jai_string_type;
        constexpr auto integer = script_value_type::jai_int_type;
        constexpr auto any = script_value_type::jai_null_type;
        auto sign = [&](const char* name, host_entry_signature sig) { ns->signatures[eng_ref.symbolize(name)] = std::move(sig); };
        ns->variables[eng_ref.symbolize("whitespace")] = script_value(std::string(k_whitespace_text), eng);
        sign("to_lower", {1, 1, {text}, any, true});
        sign("to_upper", {1, 1, {text}, any, true});
        sign("from_code", {1, 1, {integer}, text, false});
        sign("is_valid_utf8", {1, 1, {text}, script_value_type::jai_bool_type, false});
        sign("join", {1, 2, {script_value_type::jai_array_type, text}, any, false});
        sign("trim", {1, 1, {text}, any, true});
        sign("replace_all", {3, 3, {text, text, text}, any, true});
        sign("repeat", {2, 2, {text, integer}, any, true});
        sign("pad_left", {2, 3, {text, integer, any}, any, true});
        sign("pad_right", {2, 3, {text, integer, any}, any, true});

        ns->variables[eng_ref.symbolize("to_lower")] = script_value::make_function(
            [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
                if (args.size() != 1) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                        "string::to_lower expects exactly 1 argument");
                }
                const script_value& arg = args[0].deref();
                if (!arg.is_text()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch),
                        "string::to_lower expects a string argument");
                }
                std::string out = arg.is_utf8_string() ? arg.unchecked_as_utf8().bytes : arg.as<std::string>();
                for (auto& c : out) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                return arg.is_utf8_string() ? script_value::make_utf8(std::move(out), eng) : script_value(std::move(out), eng);
            }, &eng_ref);

        ns->variables[eng_ref.symbolize("to_upper")] = script_value::make_function(
            [eng](const std::vector<script_value>& args) -> checked_result<script_value> {
                if (args.size() != 1) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                        "string::to_upper expects exactly 1 argument");
                }
                const script_value& arg = args[0].deref();
                if (!arg.is_text()) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch),
                        "string::to_upper expects a string argument");
                }
                std::string out = arg.is_utf8_string() ? arg.unchecked_as_utf8().bytes : arg.as<std::string>();
                for (auto& c : out) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
                return arg.is_utf8_string() ? script_value::make_utf8(std::move(out), eng) : script_value(std::move(out), eng);
            }, &eng_ref);

        // the rest of the namespace's free functions: each checks its arguments and returns a new value
        using R = checked_result<script_value>;
        auto def = [&](const char* name, auto fn) {
            ns->variables[eng_ref.symbolize(name)] = script_value::make_function(
                [eng, fn](const std::vector<script_value>& raw) -> R {
                    std::vector<const script_value*> args;
                    for (const auto& a : raw) { args.push_back(&a.deref()); }
                    return fn(eng, args);
                }, &eng_ref);
        };
        auto count_error = [](const char* text) { return R(make_error_code(runtime_error_code::argument_count_mismatch), text); };   // static texts: the result holds a view
        auto type_error = [](const char* text) { return R(make_error_code(runtime_error_code::type_mismatch), text); };
        auto pad_char = [](const script_value* a, char& out) {
            if (a->is_char()) { out = a->unchecked_as_char(); return true; }
            if (a->is_text() && !a->unchecked_text().empty()) { out = a->unchecked_text()[0]; return true; }
            return false;
        };

        def("from_code", [=](engine* e, const std::vector<const script_value*>& a) -> R {
            if (a.size() != 1) return count_error("string::from_code expects exactly 1 argument");
            if (!a[0]->is_int()) return type_error("string::from_code expects an int code point");
            const script_int cp = a[0]->unchecked_as_int();
            std::string out;
            if (cp < 0 || !detail::utf8::encode(static_cast<uint32_t>(cp), out)) {
                return R(make_error_code(runtime_error_code::invalid_operation), "string::from_code expects a Unicode code point (0 to 0x10FFFF, no surrogate)");
            }
            return script_value(std::move(out), e);
        });
        def("is_valid_utf8", [=](engine* e, const std::vector<const script_value*>& a) -> R {
            if (a.size() != 1) return count_error("string::is_valid_utf8 expects exactly 1 argument");
            if (!a[0]->is_text()) return type_error("string::is_valid_utf8 expects a string argument");
            const std::string& s = a[0]->unchecked_text();
            for (size_t at = 0; at < s.size();) { const auto st = detail::utf8::decode(s, at); if (!st.valid) return script_value(false, e); at += st.length; }
            return script_value(true, e);
        });
        def("join", [=](engine* e, const std::vector<const script_value*>& a) -> R {
            if (a.size() < 1 || a.size() > 2) return count_error("string::join expects 1 or 2 arguments (parts, [separator])");
            if (!a[0]->is_array()) return type_error("string::join expects an array of strings");
            std::string sep;
            if (a.size() == 2) { if (!a[1]->is_text()) return type_error("string::join separator must be a string"); sep = a[1]->unchecked_text(); }
            std::string out; bool first = true, bad = false, utf8 = a.size() == 2 && a[1]->is_utf8_string();
            a[0]->unchecked_array_node()->for_each(e, [&](const script_value& el) {
                const script_value& v = el.deref();
                if (!v.is_text()) { bad = true; return; }
                if (!first) out += sep;
                out += v.unchecked_text(); first = false; utf8 = utf8 || v.is_utf8_string();
            });
            if (bad) return type_error("string::join expects an array of strings");
            return utf8 ? script_value::make_utf8(std::move(out), e) : script_value(std::move(out), e);
        });
        def("trim", [=](engine* e, const std::vector<const script_value*>& a) -> R {
            if (a.size() != 1) return count_error("string::trim expects exactly 1 argument");
            if (!a[0]->is_text()) return type_error("string::trim expects a string argument");
            const std::string& s = a[0]->unchecked_text();
            const auto space = [](char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
            size_t b = 0, n = s.size();
            while (b < n && space(s[b])) ++b;
            while (n > b && space(s[n - 1])) --n;
            return a[0]->is_utf8_string() ? script_value::make_utf8(s.substr(b, n - b), e) : script_value(s.substr(b, n - b), e);
        });
        def("replace_all", [=](engine* e, const std::vector<const script_value*>& a) -> R {
            if (a.size() != 3) return count_error("string::replace_all expects exactly 3 arguments (s, from, to)");
            if (!a[0]->is_text() || !a[1]->is_text() || !a[2]->is_text()) return type_error("string::replace_all expects string arguments");
            const std::string& s = a[0]->unchecked_text(); const std::string& from = a[1]->unchecked_text(); const std::string& to = a[2]->unchecked_text();
            if (from.empty()) return a[0]->is_utf8_string() ? script_value::make_utf8(s, e) : script_value(s, e);
            std::string out; size_t at = 0, hit;
            while ((hit = s.find(from, at)) != std::string::npos) { out.append(s, at, hit - at); out += to; at = hit + from.size(); }
            out.append(s, at, std::string::npos);
            return a[0]->is_utf8_string() ? script_value::make_utf8(std::move(out), e) : script_value(std::move(out), e);
        });
        def("repeat", [=](engine* e, const std::vector<const script_value*>& a) -> R {
            if (a.size() != 2) return count_error("string::repeat expects exactly 2 arguments (s, count)");
            if (!a[0]->is_text()) return type_error("string::repeat expects a string first argument");
            if (!a[1]->is_int()) return type_error("string::repeat count must be an integer");
            const std::string& s = a[0]->unchecked_text(); const script_int n = a[1]->unchecked_as_int();
            if (n < 0) return R(make_error_code(runtime_error_code::invalid_operation), "string::repeat count cannot be negative");
            std::string out; out.reserve(s.size() * static_cast<size_t>(n));
            for (script_int k = 0; k < n; ++k) out += s;
            return a[0]->is_utf8_string() ? script_value::make_utf8(std::move(out), e) : script_value(std::move(out), e);
        });
        struct pad_texts { const char* name; const char* count; const char* first; const char* width; const char* fill; bool left; };
        for (const pad_texts& pt : {
                pad_texts{"pad_left", "string::pad_left expects 2 or 3 arguments (s, width, [fill])", "string::pad_left expects a string first argument", "string::pad_left width must be an integer", "string::pad_left fill must be a char or a non-empty string", true},
                pad_texts{"pad_right", "string::pad_right expects 2 or 3 arguments (s, width, [fill])", "string::pad_right expects a string first argument", "string::pad_right width must be an integer", "string::pad_right fill must be a char or a non-empty string", false}}) {
            def(pt.name, [=](engine* e, const std::vector<const script_value*>& a) -> R {
                if (a.size() < 2 || a.size() > 3) return count_error(pt.count);
                if (!a[0]->is_text()) return type_error(pt.first);
                if (!a[1]->is_int()) return type_error(pt.width);
                char fill = ' ';
                if (a.size() == 3 && !pad_char(a[2], fill)) return type_error(pt.fill);
                std::string s = a[0]->unchecked_text(); const script_int w = a[1]->unchecked_as_int();
                size_t len = s.size();
                if (a[0]->is_utf8_string()) { len = 0; for (size_t at = 0; at < s.size(); ++len) { at += detail::utf8::decode(s, at).length; } }   // a utf8_string pads to a width in characters
                if (w > static_cast<script_int>(len)) {
                    const size_t pad = static_cast<size_t>(w) - len;
                    if (pt.left) s.insert(0, pad, fill); else s.append(pad, fill);
                }
                return a[0]->is_utf8_string() ? script_value::make_utf8(std::move(s), e) : script_value(std::move(s), e);
            });
        }
    }

} // namespace stdlib
} // namespace jai

#endif // __JAISCRIPT_STDLIB_STRINGS_HPP__
