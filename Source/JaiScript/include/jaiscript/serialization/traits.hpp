#pragma once

#ifndef JAISCRIPT_SERIALIZATION_TRAITS_HPP
#define JAISCRIPT_SERIALIZATION_TRAITS_HPP

// Serialization type traits - pure type detection with minimal dependencies
// This header has NO dependencies on archive implementations

#include <type_traits>
#include <vector>
#include <map>
#include <unordered_map>
#include <array>
#include <memory>
#include <string>
#include <utility>

namespace jai {

// Forward declarations for construct types
namespace serialization {
	template<typename T> class construct;
	template<typename T> class construct_unique;
}

// Forward declaration for jai::access (defined in construct.hpp)
class access;

namespace serialization_traits {

// ============================================================================
// Container type detection
// ============================================================================

template<typename T> struct is_std_vector : std::false_type {};
template<typename T, typename A> struct is_std_vector<std::vector<T, A>> : std::true_type {};
template<typename T> inline constexpr bool is_std_vector_v = is_std_vector<T>::value;

template<typename T> struct is_std_map : std::false_type {};
template<typename K, typename V, typename C, typename A> struct is_std_map<std::map<K, V, C, A>> : std::true_type {};
template<typename T> inline constexpr bool is_std_map_v = is_std_map<T>::value;

template<typename T> struct is_std_unordered_map : std::false_type {};
template<typename K, typename V, typename H, typename E, typename A>
struct is_std_unordered_map<std::unordered_map<K, V, H, E, A>> : std::true_type {};
template<typename T> inline constexpr bool is_std_unordered_map_v = is_std_unordered_map<T>::value;

template<typename T> struct is_std_array : std::false_type {};
template<typename T, std::size_t N> struct is_std_array<std::array<T, N>> : std::true_type {};
template<typename T> inline constexpr bool is_std_array_v = is_std_array<T>::value;

template<typename T> struct is_std_pair : std::false_type {};
template<typename T1, typename T2> struct is_std_pair<std::pair<T1, T2>> : std::true_type {};
template<typename T> inline constexpr bool is_std_pair_v = is_std_pair<T>::value;

// ============================================================================
// Container element type extractors
// ============================================================================

template<typename T> struct vector_element { using type = void; };
template<typename T, typename A> struct vector_element<std::vector<T, A>> { using type = T; };
template<typename T> using vector_element_t = typename vector_element<T>::type;

template<typename T> struct map_key { using type = void; };
template<typename K, typename V, typename C, typename A> struct map_key<std::map<K, V, C, A>> { using type = K; };
template<typename T> using map_key_t = typename map_key<T>::type;

template<typename T> struct map_value { using type = void; };
template<typename K, typename V, typename C, typename A> struct map_value<std::map<K, V, C, A>> { using type = V; };
template<typename T> using map_value_t = typename map_value<T>::type;

template<typename T> struct unordered_map_key { using type = void; };
template<typename K, typename V, typename H, typename E, typename A>
struct unordered_map_key<std::unordered_map<K, V, H, E, A>> { using type = K; };
template<typename T> using unordered_map_key_t = typename unordered_map_key<T>::type;

template<typename T> struct unordered_map_value { using type = void; };
template<typename K, typename V, typename H, typename E, typename A>
struct unordered_map_value<std::unordered_map<K, V, H, E, A>> { using type = V; };
template<typename T> using unordered_map_value_t = typename unordered_map_value<T>::type;

template<typename T> struct array_element { using type = void; };
template<typename T, std::size_t N> struct array_element<std::array<T, N>> { using type = T; };
template<typename T> using array_element_t = typename array_element<T>::type;

template<typename T> struct array_size { static constexpr std::size_t value = 0; };
template<typename T, std::size_t N> struct array_size<std::array<T, N>> { static constexpr std::size_t value = N; };
template<typename T> inline constexpr std::size_t array_size_v = array_size<T>::value;

template<typename T> struct pair_first { using type = void; };
template<typename T1, typename T2> struct pair_first<std::pair<T1, T2>> { using type = T1; };
template<typename T> using pair_first_t = typename pair_first<T>::type;

template<typename T> struct pair_second { using type = void; };
template<typename T1, typename T2> struct pair_second<std::pair<T1, T2>> { using type = T2; };
template<typename T> using pair_second_t = typename pair_second<T>::type;

// ============================================================================
// Smart pointer type detection
// ============================================================================

template<typename T> struct is_std_weak_ptr : std::false_type {};
template<typename T> struct is_std_weak_ptr<std::weak_ptr<T>> : std::true_type {};
template<typename T> inline constexpr bool is_std_weak_ptr_v = is_std_weak_ptr<T>::value;

template<typename T> struct is_std_shared_ptr : std::false_type {};
template<typename T> struct is_std_shared_ptr<std::shared_ptr<T>> : std::true_type {};
template<typename T> inline constexpr bool is_std_shared_ptr_v = is_std_shared_ptr<T>::value;

template<typename T> struct is_std_unique_ptr : std::false_type {};
template<typename T, typename D> struct is_std_unique_ptr<std::unique_ptr<T, D>> : std::true_type {};
template<typename T> inline constexpr bool is_std_unique_ptr_v = is_std_unique_ptr<T>::value;

template<typename T>
inline constexpr bool is_smart_ptr_v = is_std_weak_ptr_v<T> || is_std_shared_ptr_v<T> || is_std_unique_ptr_v<T>;

// Smart pointer element type extractors
template<typename T> struct weak_ptr_element { using type = void; };
template<typename T> struct weak_ptr_element<std::weak_ptr<T>> { using type = T; };
template<typename T> using weak_ptr_element_t = typename weak_ptr_element<T>::type;

template<typename T> struct shared_ptr_element { using type = void; };
template<typename T> struct shared_ptr_element<std::shared_ptr<T>> { using type = T; };
template<typename T> using shared_ptr_element_t = typename shared_ptr_element<T>::type;

template<typename T> struct unique_ptr_element { using type = void; };
template<typename T, typename D> struct unique_ptr_element<std::unique_ptr<T, D>> { using type = T; };
template<typename T> using unique_ptr_element_t = typename unique_ptr_element<T>::type;

// ============================================================================
// Primitive type detection
// ============================================================================

template<typename T>
inline constexpr bool is_direct_serializable_v =
	std::is_arithmetic_v<T> ||
	std::is_enum_v<T> ||
	std::is_same_v<T, std::string> ||
	std::is_same_v<T, char> ||
	std::is_same_v<T, unsigned char>;

// ============================================================================
// Method detection traits - LEGACY PLACEHOLDERS
// ============================================================================
//
// NOTE: These traits previously detected methods taking archive_writer& or
// archive_reader& base class references. Since the CRTP-based devirtualization,
// archives are now concrete template types (e.g., binary_archive_writer).
//
// These traits are kept for API compatibility but always return false.
// The actual serialization detection now uses:
//   - is_direct_serializable_v<T> for primitives
//   - has_static_type_v<T> for types with JAI_STATIC_BINDER
//   - is_property_owner_v<T> for property_owner derived types
//
// See JAI_ARCHIVE_DEVIRTUALIZATION.md for the migration details.

template<typename T>
inline constexpr bool has_member_save_v = false;

template<typename T>
inline constexpr bool has_member_load_v = false;

template<typename T>
inline constexpr bool has_member_serialize_save_v = false;

template<typename T>
inline constexpr bool has_member_serialize_load_v = false;

template<typename T>
inline constexpr bool has_free_save_v = false;

template<typename T>
inline constexpr bool has_free_load_v = false;

template<typename T>
inline constexpr bool has_free_serialize_save_v = false;

template<typename T>
inline constexpr bool has_free_serialize_load_v = false;

// ============================================================================
// Combined checks
// ============================================================================

template<typename T>
inline constexpr bool has_any_save_v =
	has_member_save_v<T> || has_free_save_v<T> ||
	has_member_serialize_save_v<T> || has_free_serialize_save_v<T>;

template<typename T>
inline constexpr bool has_any_load_v =
	has_member_load_v<T> || has_free_load_v<T> ||
	has_member_serialize_load_v<T> || has_free_serialize_load_v<T>;

template<typename T>
inline constexpr bool has_custom_serialization_v = has_any_save_v<T> && has_any_load_v<T>;

// ============================================================================
// property_owner detection
// ============================================================================

template<typename T, typename = void>
struct is_property_owner : std::false_type {};

template<typename T>
struct is_property_owner<T, std::void_t<
	decltype(std::declval<T&>().property_mgr)
>> : std::true_type {};

template<typename T>
inline constexpr bool is_property_owner_v = is_property_owner<T>::value;

// ============================================================================
// load_and_construct detection (for types without default constructors)
// ============================================================================

// Note: These require jai::access to be complete, so they're declared here
// but the actual detection happens when construct.hpp is included

// Member load_and_construct for shared_ptr
template<typename T, typename = void>
struct has_member_load_and_construct : std::false_type {};

// Free function load_and_construct for shared_ptr
template<typename T, typename = void>
struct has_free_load_and_construct : std::false_type {};

// Combined for shared_ptr
template<typename T>
inline constexpr bool has_load_and_construct_v =
	has_member_load_and_construct<T>::value || has_free_load_and_construct<T>::value;

// Member load_and_construct for unique_ptr
template<typename T, typename = void>
struct has_member_load_and_construct_unique : std::false_type {};

// Free function load_and_construct for unique_ptr
template<typename T, typename = void>
struct has_free_load_and_construct_unique : std::false_type {};

// Combined for unique_ptr
template<typename T>
inline constexpr bool has_load_and_construct_unique_v =
	has_member_load_and_construct_unique<T>::value || has_free_load_and_construct_unique<T>::value;

} // namespace serialization_traits

// Bring commonly used traits into jai namespace for convenience
using serialization_traits::is_std_vector_v;
using serialization_traits::is_std_map_v;
using serialization_traits::is_std_unordered_map_v;
using serialization_traits::is_std_array_v;
using serialization_traits::is_std_pair_v;
using serialization_traits::is_std_weak_ptr_v;
using serialization_traits::is_std_shared_ptr_v;
using serialization_traits::is_std_unique_ptr_v;
using serialization_traits::is_smart_ptr_v;
using serialization_traits::is_direct_serializable_v;
using serialization_traits::has_member_save_v;
using serialization_traits::has_member_load_v;
using serialization_traits::has_member_serialize_save_v;
using serialization_traits::has_member_serialize_load_v;
using serialization_traits::has_free_save_v;
using serialization_traits::has_free_load_v;
using serialization_traits::has_free_serialize_save_v;
using serialization_traits::has_free_serialize_load_v;
using serialization_traits::has_any_save_v;
using serialization_traits::has_any_load_v;
using serialization_traits::is_property_owner_v;

} // namespace jai

#endif // JAISCRIPT_SERIALIZATION_TRAITS_HPP
