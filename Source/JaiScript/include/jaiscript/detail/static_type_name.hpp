#pragma once

#ifndef JAISCRIPT_DETAIL_STATIC_TYPE_NAME_HPP
#define JAISCRIPT_DETAIL_STATIC_TYPE_NAME_HPP

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
template<typename T>
constexpr std::string_view static_type_name() {
	return extract_type_name(raw_type_signature<T>());
}

// Namespaces stripped ("Node"). Empty for template instantiations: their spellings
// differ across compilers, which would make saved data non-portable — register those
// explicitly via jai::registrar with a chosen name instead.
template<typename T>
constexpr std::string_view static_unqualified_type_name() {
	auto name = static_type_name<T>();
	if (name.find('<') != std::string_view::npos) { return {}; }
	if (auto pos = name.rfind("::"); pos != std::string_view::npos) { name.remove_prefix(pos + 2); }
	return name;
}

} // namespace detail
} // namespace jai

#endif // JAISCRIPT_DETAIL_STATIC_TYPE_NAME_HPP
