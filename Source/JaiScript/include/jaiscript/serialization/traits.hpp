#pragma once

#ifndef JAISCRIPT_SERIALIZATION_TRAITS_HPP
#define JAISCRIPT_SERIALIZATION_TRAITS_HPP

// Serialization type traits - pure type detection with minimal dependencies
// This header has NO dependencies on archive implementations

#include <type_traits>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <list>
#include <deque>
#include <forward_list>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <optional>
#include <variant>
#include <tuple>
#include <iterator>
#include <cstdint>

namespace jai {

// Forward declarations for construct types
namespace serialization {
	template<typename T> class construct;
	template<typename T> class construct_unique;
}

// THE friend class for serialization access — the single class every trait probe and
// dispatch routes through. Defined here (not construct.hpp) so it is complete before
// archive_impl.hpp's trait specializations name it. Private serialize/save/load and
// load_and_construct members need exactly one declaration: `friend jai::access;`
class access {
public:
	template<typename T, typename... Args>
	static std::shared_ptr<T> make_shared(Args&&... args) {
		return std::shared_ptr<T>(new T(std::forward<Args>(args)...));
	}
	template<typename T, typename... Args>
	static std::unique_ptr<T> make_unique(Args&&... args) {
		return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
	}

	// Forward to T::load_and_construct - allows access to private methods via friendship
	// Templated on Archive to work with CRTP-based archives
	template<typename Archive, typename T>
	static auto load_and_construct(Archive& ar, serialization::construct<T>& c)
		-> decltype(T::load_and_construct(ar, c)) {
		return T::load_and_construct(ar, c);
	}

	template<typename Archive, typename T>
	static auto load_and_construct(Archive& ar, serialization::construct_unique<T>& c)
		-> decltype(T::load_and_construct(ar, c)) {
		return T::load_and_construct(ar, c);
	}

	// Explicit-T forms for callers with custom construct wrappers
	template<typename T, typename Archive, typename Construct>
	static auto load_and_construct(Archive& ar, Construct& c)
		-> decltype(T::load_and_construct(ar, c)) {
		return T::load_and_construct(ar, c);
	}
	template<typename T, typename Archive, typename Construct>
	static auto load_and_construct(Archive& ar, Construct& c, std::uint32_t version)
		-> decltype(T::load_and_construct(ar, c, version)) {
		return T::load_and_construct(ar, c, version);
	}

	// Member save/load/serialize accessors — allows SFINAE detection of protected methods.
	// Types only need `friend jai::access;` for all serialization support.
	template<typename Archive, typename T>
	static auto member_save(Archive& ar, const T& obj, uint32_t v) -> decltype(obj.save(ar, v)) { return obj.save(ar, v); }
	template<typename Archive, typename T>
	static auto member_save(Archive& ar, const T& obj) -> decltype(obj.save(ar)) { return obj.save(ar); }
	template<typename Archive, typename T>
	static auto member_load(Archive& ar, T& obj, uint32_t v) -> decltype(obj.load(ar, v)) { return obj.load(ar, v); }
	template<typename Archive, typename T>
	static auto member_load(Archive& ar, T& obj) -> decltype(obj.load(ar)) { return obj.load(ar); }
	template<typename Archive, typename T>
	static auto member_serialize(Archive& ar, T& obj, uint32_t v) -> decltype(obj.serialize(ar, v)) { return obj.serialize(ar, v); }
	template<typename Archive, typename T>
	static auto member_serialize(Archive& ar, T& obj) -> decltype(obj.serialize(ar)) { return obj.serialize(ar); }
};

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

template<typename T> struct is_std_optional : std::false_type {};
template<typename T> struct is_std_optional<std::optional<T>> : std::true_type {};
template<typename T> inline constexpr bool is_std_optional_v = is_std_optional<T>::value;

template<typename T> struct is_std_variant : std::false_type {};
template<typename... Ts> struct is_std_variant<std::variant<Ts...>> : std::true_type {};
template<typename T> inline constexpr bool is_std_variant_v = is_std_variant<T>::value;

template<typename T> struct is_std_tuple : std::false_type {};
template<typename... Ts> struct is_std_tuple<std::tuple<Ts...>> : std::true_type {};
template<typename T> inline constexpr bool is_std_tuple_v = is_std_tuple<T>::value;

// Specific set/list/deque detection (for explicit handling if needed)
template<typename T> struct is_std_set : std::false_type {};
template<typename K, typename C, typename A> struct is_std_set<std::set<K, C, A>> : std::true_type {};
template<typename T> inline constexpr bool is_std_set_v = is_std_set<T>::value;

template<typename T> struct is_std_unordered_set : std::false_type {};
template<typename K, typename H, typename E, typename A>
struct is_std_unordered_set<std::unordered_set<K, H, E, A>> : std::true_type {};
template<typename T> inline constexpr bool is_std_unordered_set_v = is_std_unordered_set<T>::value;

template<typename T> struct is_std_list : std::false_type {};
template<typename T, typename A> struct is_std_list<std::list<T, A>> : std::true_type {};
template<typename T> inline constexpr bool is_std_list_v = is_std_list<T>::value;

template<typename T> struct is_std_deque : std::false_type {};
template<typename T, typename A> struct is_std_deque<std::deque<T, A>> : std::true_type {};
template<typename T> inline constexpr bool is_std_deque_v = is_std_deque<T>::value;

template<typename T> struct is_std_forward_list : std::false_type {};
template<typename T, typename A> struct is_std_forward_list<std::forward_list<T, A>> : std::true_type {};
template<typename T> inline constexpr bool is_std_forward_list_v = is_std_forward_list<T>::value;

// ============================================================================
// Generic container trait detection (elegant handling of all containers)
// ============================================================================

// Detect if type is iterable (has begin/end that return iterators)
template<typename T, typename = void>
struct is_iterable : std::false_type {};
template<typename T>
struct is_iterable<T, std::void_t<
    decltype(std::begin(std::declval<T&>())),
    decltype(std::end(std::declval<T&>()))
>> : std::true_type {};
template<typename T> inline constexpr bool is_iterable_v = is_iterable<T>::value;

// Detect if type has key_type (associative containers)
template<typename T, typename = void>
struct has_key_type : std::false_type {};
template<typename T>
struct has_key_type<T, std::void_t<typename T::key_type>> : std::true_type {};
template<typename T> inline constexpr bool has_key_type_v = has_key_type<T>::value;

// Detect if type has mapped_type (map-like containers)
template<typename T, typename = void>
struct has_mapped_type : std::false_type {};
template<typename T>
struct has_mapped_type<T, std::void_t<typename T::mapped_type>> : std::true_type {};
template<typename T> inline constexpr bool has_mapped_type_v = has_mapped_type<T>::value;

// Map-like: has both key_type and mapped_type (std::map, std::unordered_map, etc.)
template<typename T>
inline constexpr bool is_map_like_v = has_key_type_v<T> && has_mapped_type_v<T>;

// Set-like: has key_type but NOT mapped_type (std::set, std::unordered_set, etc.)
template<typename T>
inline constexpr bool is_set_like_v = has_key_type_v<T> && !has_mapped_type_v<T> && is_iterable_v<T>;

// Sequence container: iterable with value_type, but not associative and not string
// Covers: vector, list, deque, forward_list, array
template<typename T, typename = void>
struct has_value_type : std::false_type {};
template<typename T>
struct has_value_type<T, std::void_t<typename T::value_type>> : std::true_type {};
template<typename T> inline constexpr bool has_value_type_v = has_value_type<T>::value;

template<typename T>
inline constexpr bool is_sequence_container_v =
    is_iterable_v<T> &&
    has_value_type_v<T> &&
    !has_key_type_v<T> &&
    !std::is_same_v<T, std::string> &&
    !is_std_array_v<T>;  // std::array handled separately due to fixed size

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

template<typename T> struct optional_element { using type = void; };
template<typename T> struct optional_element<std::optional<T>> { using type = T; };
template<typename T> using optional_element_t = typename optional_element<T>::type;

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
// jai::property<T> type detection
// ============================================================================

// Forward declaration of jai::property template
} // namespace serialization_traits
} // namespace jai

namespace jai { template<typename T> class property; }

namespace jai {
namespace serialization_traits {

template<typename T> struct is_jai_property : std::false_type {};
template<typename T> struct is_jai_property<::jai::property<T>> : std::true_type {};
template<typename T> inline constexpr bool is_jai_property_v = is_jai_property<T>::value;

// Property element type extractor
template<typename T> struct property_element { using type = void; };
template<typename T> struct property_element<::jai::property<T>> { using type = T; };
template<typename T> using property_element_t = typename property_element<T>::type;

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
using serialization_traits::is_std_optional_v;
using serialization_traits::is_std_variant_v;
using serialization_traits::is_std_tuple_v;
using serialization_traits::is_std_weak_ptr_v;
using serialization_traits::is_std_shared_ptr_v;
using serialization_traits::is_std_unique_ptr_v;
using serialization_traits::is_smart_ptr_v;
using serialization_traits::is_direct_serializable_v;
using serialization_traits::is_property_owner_v;
using serialization_traits::is_jai_property_v;
using serialization_traits::property_element_t;

} // namespace jai

#endif // JAISCRIPT_SERIALIZATION_TRAITS_HPP
