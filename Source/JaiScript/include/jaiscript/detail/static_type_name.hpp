#pragma once

#ifndef JAISCRIPT_DETAIL_STATIC_TYPE_NAME_HPP
#define JAISCRIPT_DETAIL_STATIC_TYPE_NAME_HPP

#include <string>
#include <string_view>

// Compile-time C++ type names via the compiler's signature macro (the nameof/ctti
// technique). Used by property_owner's implicit polymorphic registration so a plain
// class gets a stable, human-readable serialization name with no explicit registrar.

namespace jai {
namespace detail {

template<typename T>
constexpr std::string_view raw_type_signature() {
#if defined(_MSC_VER) && !defined(__clang__)
	return __FUNCSIG__;
#elif defined(__clang__) || defined(__GNUC__)
	return __PRETTY_FUNCTION__;
#else
	return "";
#endif
}

constexpr std::string_view extract_type_name(std::string_view sig) {
#if defined(_MSC_VER) && !defined(__clang__)
	// "... raw_type_signature<class MV::Foo>(void)"
	constexpr std::string_view marker = "raw_type_signature<";
	auto start = sig.find(marker);
	if (start == std::string_view::npos) { return {}; }
	start += marker.size();
	auto end = sig.rfind(">(");
	if (end == std::string_view::npos || end <= start) { return {}; }
	auto name = sig.substr(start, end - start);
	if (name.substr(0, 6) == "class ") { name.remove_prefix(6); }
	else if (name.substr(0, 7) == "struct ") { name.remove_prefix(7); }
	else if (name.substr(0, 5) == "enum ") { name.remove_prefix(5); }
	return name;
#else
	// gcc: "... [with T = MV::Foo; std::string_view = ...]"   clang: "... [T = MV::Foo]"
	constexpr std::string_view marker = "T = ";
	auto start = sig.find(marker);
	if (start == std::string_view::npos) { return {}; }
	start += marker.size();
	auto end = sig.find_first_of(";]", start);
	if (end == std::string_view::npos) { return {}; }
	return sig.substr(start, end - start);
#endif
}

// Fully qualified ("MV::Scene::Node"); empty if the compiler is unsupported.
// Best-effort spelling for diagnostics — for names that go into saved data, use
// static_unqualified_type_name, which validates portability.
template<typename T>
constexpr std::string_view static_type_name() {
	return extract_type_name(raw_type_signature<T>());
}

// True only for names spelled identically by every supported compiler: plain
// (possibly namespace-qualified) identifiers. Template instantiations ('<'),
// local classes, and lambdas contain characters outside this set.
constexpr bool is_portable_type_name(std::string_view name) {
	if (name.empty()) { return false; }
	for (char c : name) {
		const bool identifierChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':';
		if (!identifierChar) { return false; }
	}
	return true;
}

// Namespaces stripped ("Node"). Empty whenever the spelling is not provably
// identical across compilers — those types must supply a name explicitly
// (jai_type_name member or jai::registrar).
template<typename T>
constexpr std::string_view static_unqualified_type_name() {
	auto name = static_type_name<T>();
	if (!is_portable_type_name(name)) { return {}; }
	if (auto pos = name.rfind("::"); pos != std::string_view::npos) { name.remove_prefix(pos + 2); }
	return name;
}

// Anonymous-namespace segments are the one compiler-variant spelling worth
// canonicalizing rather than rejecting: each compiler marks them differently, but the
// surrounding identifiers are portable. They normalize to the stable token "AN"
// (foo::{anonymous}::Baz -> "foo::AN::Baz"). Note such types are per-TU distinct; two
// TUs deriving the same canonical name trip the registry's collision warning.
inline constexpr std::string_view anonymous_namespace_markers[] = {
	"`anonymous namespace'",  //MSVC
	"`anonymous-namespace'",  //MSVC (alternate spelling)
	"(anonymous namespace)",  //Clang, newer GCC
	"{anonymous}",            //GCC
	"(anonymous)",            //GCC (alternate spelling)
};

constexpr std::string_view matched_anonymous_marker(std::string_view name, size_t pos) {
	for (auto marker : anonymous_namespace_markers) {
		if (name.substr(pos, marker.size()) == marker) { return marker; }
	}
	return {};
}

// Identifier chars and '::' with anonymous-namespace markers allowed as whole segments.
constexpr bool is_canonicalizable_type_name(std::string_view name) {
	if (name.empty()) { return false; }
	size_t i = 0;
	while (i < name.size()) {
		if (auto marker = matched_anonymous_marker(name, i); !marker.empty()) {
			i += marker.size();
			continue;
		}
		const char c = name[i];
		const bool identifierChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':';
		if (!identifierChar) { return false; }
		++i;
	}
	return true;
}

// The implicit registration name: plain types give the bare identifier ("Baz"),
// anonymous-namespace types keep their qualified path with canonical AN segments
// ("foo::AN::Baz" — the qualification is what disambiguates them from a public Baz).
// Empty for spellings that cannot be made portable (templates, locals, lambdas).
inline std::string canonical_registration_name(std::string_view name) {
	if (!is_canonicalizable_type_name(name)) { return {}; }
	std::string result;
	result.reserve(name.size());
	bool hasAnonymousSegment = false;
	size_t i = 0;
	while (i < name.size()) {
		if (auto marker = matched_anonymous_marker(name, i); !marker.empty()) {
			result += "AN";
			hasAnonymousSegment = true;
			i += marker.size();
			continue;
		}
		result += name[i];
		++i;
	}
	if (!hasAnonymousSegment) {
		if (auto pos = result.rfind("::"); pos != std::string::npos) { result.erase(0, pos + 2); }
	}
	return result;
}

template<typename T>
std::string registration_type_name() {
	return canonical_registration_name(static_type_name<T>());
}

// Compile-time self-test: locks the signature parse against compiler/format drift
// (new compiler versions, clang-cl, gcc -fno-pretty-templates, ...). If a toolchain
// renders the signature differently, the build fails HERE instead of silently
// deriving wrong serialization names.
struct static_type_name_probe {};
static_assert(static_type_name<static_type_name_probe>() == std::string_view("jai::detail::static_type_name_probe"),
	"static_type_name: signature parse failed for this compiler — implicit registration names would be wrong. Fix extract_type_name for this compiler's signature format.");
static_assert(static_unqualified_type_name<static_type_name_probe>() == std::string_view("static_type_name_probe"),
	"static_unqualified_type_name: namespace stripping failed for this compiler.");

} // namespace detail
} // namespace jai

#endif // JAISCRIPT_DETAIL_STATIC_TYPE_NAME_HPP
