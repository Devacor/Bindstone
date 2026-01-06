#pragma once

#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/static_binder.hpp>
#include <vector>
#include <map>
#include <unordered_map>
#include <array>
#include <string>
#include <memory>
#include <stdexcept>

namespace jai {
namespace property_serialization {

	// Type traits for detecting containers without accessing nested types directly
	template<typename T> struct is_std_vector : std::false_type {};
	template<typename T, typename A> struct is_std_vector<std::vector<T, A>> : std::true_type {};
	template<typename T> inline constexpr bool is_std_vector_v = is_std_vector<T>::value;

	template<typename T> struct is_std_map : std::false_type {};
	template<typename K, typename V, typename C, typename A> struct is_std_map<std::map<K, V, C, A>> : std::true_type {};
	template<typename T> inline constexpr bool is_std_map_v = is_std_map<T>::value;

	// Element type extractors (only valid when the type IS a container)
	template<typename T> struct vector_element { using type = void; };
	template<typename T, typename A> struct vector_element<std::vector<T, A>> { using type = T; };
	template<typename T> using vector_element_t = typename vector_element<T>::type;

	template<typename T> struct map_key { using type = void; };
	template<typename K, typename V, typename C, typename A> struct map_key<std::map<K, V, C, A>> { using type = K; };
	template<typename T> using map_key_t = typename map_key<T>::type;

	template<typename T> struct map_value { using type = void; };
	template<typename K, typename V, typename C, typename A> struct map_value<std::map<K, V, C, A>> { using type = V; };
	template<typename T> using map_value_t = typename map_value<T>::type;

	// Smart pointer type detection
	template<typename T> struct is_std_weak_ptr : std::false_type {};
	template<typename T> struct is_std_weak_ptr<std::weak_ptr<T>> : std::true_type {};
	template<typename T> inline constexpr bool is_std_weak_ptr_v = is_std_weak_ptr<T>::value;

	template<typename T> struct is_std_shared_ptr : std::false_type {};
	template<typename T> struct is_std_shared_ptr<std::shared_ptr<T>> : std::true_type {};
	template<typename T> inline constexpr bool is_std_shared_ptr_v = is_std_shared_ptr<T>::value;

	template<typename T> struct is_std_unique_ptr : std::false_type {};
	template<typename T, typename D> struct is_std_unique_ptr<std::unique_ptr<T, D>> : std::true_type {};
	template<typename T> inline constexpr bool is_std_unique_ptr_v = is_std_unique_ptr<T>::value;

	// Combined smart pointer detection
	template<typename T>
	inline constexpr bool is_smart_ptr_v = is_std_weak_ptr_v<T> || is_std_shared_ptr_v<T> || is_std_unique_ptr_v<T>;

	// Unordered map detection
	template<typename T> struct is_std_unordered_map : std::false_type {};
	template<typename K, typename V, typename H, typename E, typename A>
	struct is_std_unordered_map<std::unordered_map<K, V, H, E, A>> : std::true_type {};
	template<typename T> inline constexpr bool is_std_unordered_map_v = is_std_unordered_map<T>::value;

	// std::array detection
	template<typename T> struct is_std_array : std::false_type {};
	template<typename T, std::size_t N> struct is_std_array<std::array<T, N>> : std::true_type {};
	template<typename T> inline constexpr bool is_std_array_v = is_std_array<T>::value;

	// std::pair detection
	template<typename T> struct is_std_pair : std::false_type {};
	template<typename T1, typename T2> struct is_std_pair<std::pair<T1, T2>> : std::true_type {};
	template<typename T> inline constexpr bool is_std_pair_v = is_std_pair<T>::value;

	// std::pair element type extractors
	template<typename T> struct pair_first { using type = void; };
	template<typename T1, typename T2> struct pair_first<std::pair<T1, T2>> { using type = T1; };
	template<typename T> using pair_first_t = typename pair_first<T>::type;

	template<typename T> struct pair_second { using type = void; };
	template<typename T1, typename T2> struct pair_second<std::pair<T1, T2>> { using type = T2; };
	template<typename T> using pair_second_t = typename pair_second<T>::type;

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

	// Unordered map element type extractors
	template<typename T> struct unordered_map_key { using type = void; };
	template<typename K, typename V, typename H, typename E, typename A>
	struct unordered_map_key<std::unordered_map<K, V, H, E, A>> { using type = K; };
	template<typename T> using unordered_map_key_t = typename unordered_map_key<T>::type;

	template<typename T> struct unordered_map_value { using type = void; };
	template<typename K, typename V, typename H, typename E, typename A>
	struct unordered_map_value<std::unordered_map<K, V, H, E, A>> { using type = V; };
	template<typename T> using unordered_map_value_t = typename unordered_map_value<T>::type;

	// std::array element type extractors
	template<typename T> struct array_element { using type = void; };
	template<typename T, std::size_t N> struct array_element<std::array<T, N>> { using type = T; };
	template<typename T> using array_element_t = typename array_element<T>::type;

	template<typename T> struct array_size { static constexpr std::size_t value = 0; };
	template<typename T, std::size_t N> struct array_size<std::array<T, N>> { static constexpr std::size_t value = N; };
	template<typename T> inline constexpr std::size_t array_size_v = array_size<T>::value;

	// ============================================================================
	// Detection traits for custom serialization support (cereal-style)
	// ============================================================================
	//
	// Supports multiple patterns (checked in this priority order):
	//   1. Member functions: t.save(archive_writer&) const / t.load(archive_reader&)
	//   2. Free functions (ADL): save(archive_writer&, const T&) / load(archive_reader&, T&)
	//
	// If neither exists, the type is not serializable (serialization will be skipped).

	// --- Member function detection ---
	// Detects: t.save(archive_writer&) const
	template<typename T, typename = void>
	struct has_member_save : std::false_type {};

	template<typename T>
	struct has_member_save<T, std::void_t<
		decltype(std::declval<const T&>().save(std::declval<serialization::archive_writer&>()))
	>> : std::true_type {};

	template<typename T>
	inline constexpr bool has_member_save_v = has_member_save<T>::value;

	// Detects: t.load(archive_reader&)
	template<typename T, typename = void>
	struct has_member_load : std::false_type {};

	template<typename T>
	struct has_member_load<T, std::void_t<
		decltype(std::declval<T&>().load(std::declval<serialization::archive_reader&>()))
	>> : std::true_type {};

	template<typename T>
	inline constexpr bool has_member_load_v = has_member_load<T>::value;

	// --- Free function detection (ADL) ---
	// Detects: save(archive_writer&, const T&) via ADL
	template<typename T, typename = void>
	struct has_free_save : std::false_type {};

	template<typename T>
	struct has_free_save<T, std::void_t<
		decltype(save(std::declval<serialization::archive_writer&>(), std::declval<const T&>()))
	>> : std::true_type {};

	template<typename T>
	inline constexpr bool has_free_save_v = has_free_save<T>::value;

	// Detects: load(archive_reader&, T&) via ADL
	template<typename T, typename = void>
	struct has_free_load : std::false_type {};

	template<typename T>
	struct has_free_load<T, std::void_t<
		decltype(load(std::declval<serialization::archive_reader&>(), std::declval<T&>()))
	>> : std::true_type {};

	template<typename T>
	inline constexpr bool has_free_load_v = has_free_load<T>::value;

	// --- JaiScript-style serialize() detection ---
	// Detects: t.serialize(archive_writer&) for save
	// Detects: t.serialize(archive_reader&) for load
	// Uses SFINAE to specifically match JaiScript archives, not cereal archives
	template<typename T, typename = void>
	struct has_member_serialize_save : std::false_type {};

	template<typename T>
	struct has_member_serialize_save<T, std::void_t<
		decltype(std::declval<T&>().serialize(std::declval<serialization::archive_writer&>()))
	>> : std::true_type {};

	template<typename T>
	inline constexpr bool has_member_serialize_save_v = has_member_serialize_save<T>::value;

	template<typename T, typename = void>
	struct has_member_serialize_load : std::false_type {};

	template<typename T>
	struct has_member_serialize_load<T, std::void_t<
		decltype(std::declval<T&>().serialize(std::declval<serialization::archive_reader&>()))
	>> : std::true_type {};

	template<typename T>
	inline constexpr bool has_member_serialize_load_v = has_member_serialize_load<T>::value;

	// Free function serialize() detection
	template<typename T, typename = void>
	struct has_free_serialize_save : std::false_type {};

	template<typename T>
	struct has_free_serialize_save<T, std::void_t<
		decltype(serialize(std::declval<serialization::archive_writer&>(), std::declval<const T&>()))
	>> : std::true_type {};

	template<typename T>
	inline constexpr bool has_free_serialize_save_v = has_free_serialize_save<T>::value;

	template<typename T, typename = void>
	struct has_free_serialize_load : std::false_type {};

	template<typename T>
	struct has_free_serialize_load<T, std::void_t<
		decltype(serialize(std::declval<serialization::archive_reader&>(), std::declval<T&>()))
	>> : std::true_type {};

	template<typename T>
	inline constexpr bool has_free_serialize_load_v = has_free_serialize_load<T>::value;

	// --- Combined checks ---
	// For save: member save() OR free save() OR (cereal-style if archive supports it)
	template<typename T>
	inline constexpr bool has_any_save_v =
		has_member_save_v<T> || has_free_save_v<T> ||
		has_member_serialize_save_v<T> || has_free_serialize_save_v<T>;

	// For load: member load() OR free load() OR (cereal-style if archive supports it)
	template<typename T>
	inline constexpr bool has_any_load_v =
		has_member_load_v<T> || has_free_load_v<T> ||
		has_member_serialize_load_v<T> || has_free_serialize_load_v<T>;

	template<typename T>
	inline constexpr bool has_custom_serialization_v = has_any_save_v<T> && has_any_load_v<T>;

	// Legacy aliases for compatibility
	template<typename T>
	inline constexpr bool has_jai_save_v = has_any_save_v<T>;

	template<typename T>
	inline constexpr bool has_jai_load_v = has_any_load_v<T>;

	// --- property_owner detection ---
	// Detects: T has a property_mgr member (inherits from property_owner<T>)
	// This enables automatic serialization via JAI_PROPERTY definitions
	template<typename T, typename = void>
	struct is_property_owner : std::false_type {};

	template<typename T>
	struct is_property_owner<T, std::void_t<
		decltype(std::declval<T&>().property_mgr)
	>> : std::true_type {};

	template<typename T>
	inline constexpr bool is_property_owner_v = is_property_owner<T>::value;

	// ============================================================================
	// Forward declarations for serialization helpers
	// ============================================================================
	template<typename T> inline void write_primitive(serialization::archive_writer& ar, const T& value);
	template<typename T> inline void read_primitive(serialization::archive_reader& ar, T& value);
	template<typename T> inline void write_container(serialization::archive_writer& ar, const T& container);
	template<typename T> inline void read_vector(serialization::archive_reader& ar, std::vector<T>& vec);
	template<typename K, typename V> inline void write_map(serialization::archive_writer& ar, const std::map<K, V>& map);
	template<typename K, typename V> inline void read_map(serialization::archive_reader& ar, std::map<K, V>& map);
	template<typename K, typename V> inline void write_unordered_map(serialization::archive_writer& ar, const std::unordered_map<K, V>& map);
	template<typename K, typename V> inline void read_unordered_map(serialization::archive_reader& ar, std::unordered_map<K, V>& map);
	template<typename T1, typename T2> inline void write_pair(serialization::archive_writer& ar, const std::pair<T1, T2>& p);
	template<typename T1, typename T2> inline void read_pair(serialization::archive_reader& ar, std::pair<T1, T2>& p);
	template<typename T, std::size_t N> inline void write_array(serialization::archive_writer& ar, const std::array<T, N>& arr);
	template<typename T, std::size_t N> inline void read_array(serialization::archive_reader& ar, std::array<T, N>& arr);
	template<typename T> inline void write_shared_ptr(serialization::archive_writer& ar, const std::shared_ptr<T>& ptr);
	template<typename T> inline void read_shared_ptr(serialization::archive_reader& ar, std::shared_ptr<T>& ptr);
	template<typename T> inline void write_unique_ptr(serialization::archive_writer& ar, const std::unique_ptr<T>& ptr);
	template<typename T> inline void read_unique_ptr(serialization::archive_reader& ar, std::unique_ptr<T>& ptr);
	template<typename T> inline void write_weak_ptr(serialization::archive_writer& ar, const std::weak_ptr<T>& ptr);
	template<typename T> inline void read_weak_ptr(serialization::archive_reader& ar, std::weak_ptr<T>& ptr);

	// Check if a type is a primitive that we can directly serialize
	// (moved here so dispatch functions can use it)
	template<typename T>
	constexpr bool is_direct_serializable_v =
		std::is_arithmetic_v<T> ||
		std::is_enum_v<T> ||  // Enum classes serialize as their underlying type
		std::is_same_v<T, std::string> ||
		std::is_same_v<T, char> ||
		std::is_same_v<T, unsigned char>;

	// ============================================================================
	// Dispatch helpers - call the appropriate serialization function
	// ============================================================================
	// Priority order (composable - explicit functions can call others internally):
	//   1. Member save()/load() - explicit custom functions (can compose others)
	//   2. Free save()/load() - explicit custom functions via ADL (can compose others)
	//   3. JAI_STATIC_BINDER - compile-time property serialization
	//   4. property_owner<T> - auto-serialize via property_mgr (JAI_PROPERTY)
	//   5. Member serialize() - cereal-style bidirectional
	//   6. Free serialize() - cereal-style bidirectional via ADL
	//   7. static_assert - compile-time error if no serialization support
	//
	// COMPOSABILITY: Explicit save/load functions have highest priority so users
	// can implement custom logic that internally calls:
	//   - jai_static_type<T>::save(ar, obj) for static binder properties
	//   - obj.property_mgr.save(ar) for property_owner properties
	// This allows adding computed fields, version migration, etc.

	// Save dispatcher
	template<typename T>
	void dispatch_save(serialization::archive_writer& ar, const T& value) {
		// FIRST: Handle STL containers and smart pointers natively
		// This prevents ADL from finding cereal template declarations
		if constexpr (is_std_weak_ptr_v<T>) {
			// weak_ptr: Save the ID of the shared_ptr it references
			write_weak_ptr(ar, value);
		} else if constexpr (is_std_shared_ptr_v<T>) {
			write_shared_ptr(ar, value);
		} else if constexpr (is_std_unique_ptr_v<T>) {
			write_unique_ptr(ar, value);
		} else if constexpr (is_std_vector_v<T>) {
			write_container(ar, value);
		} else if constexpr (is_std_map_v<T>) {
			write_map(ar, value);
		} else if constexpr (is_std_unordered_map_v<T>) {
			write_unordered_map(ar, value);
		} else if constexpr (is_std_array_v<T>) {
			write_array(ar, value);
		} else if constexpr (is_std_pair_v<T>) {
			write_pair(ar, value);
		} else if constexpr (is_direct_serializable_v<T>) {
			write_primitive(ar, value);
		}
		// THEN: Custom serialization methods
		else if constexpr (has_member_save_v<T>) {
			// Explicit member save() - user has full control, can compose others
			value.save(ar);
		} else if constexpr (has_free_save_v<T>) {
			// Explicit free save() - user has full control, can compose others
			save(ar, value);
		} else if constexpr (has_static_type_v<T>) {
			// JAI_STATIC_BINDER - compile-time property serialization
			jai_static_type<T>::save(ar, value);
		} else if constexpr (is_property_owner_v<T>) {
			// property_owner<T> - auto-serialize via property_mgr
			value.property_mgr.save(ar);
		} else if constexpr (has_member_serialize_save_v<T>) {
			// cereal-style member serialize() - only when archive supports operator()
			const_cast<T&>(value).serialize(ar);
		} else if constexpr (has_free_serialize_save_v<T>) {
			// cereal-style free serialize() - only when archive supports operator()
			serialize(ar, const_cast<T&>(value));
		} else {
			// No serialization support - compile-time error
			static_assert(
				is_smart_ptr_v<T> || is_std_vector_v<T> || is_std_map_v<T> ||
				is_std_unordered_map_v<T> || is_std_array_v<T> || is_std_pair_v<T> ||
				is_direct_serializable_v<T> ||
				has_member_save_v<T> || has_free_save_v<T> ||
				has_static_type_v<T> || is_property_owner_v<T> ||
				has_member_serialize_save_v<T> || has_free_serialize_save_v<T>,
				"Type has no serialization support. Provide one of: "
				"T::save(archive_writer&), "
				"save(archive_writer&, const T&), "
				"JAI_STATIC_BINDER(T, ...), or "
				"inherit from property_owner<T>");
		}
	}

	// Load dispatcher
	template<typename T>
	void dispatch_load(serialization::archive_reader& ar, T& value) {
		// FIRST: Handle STL containers and smart pointers natively
		// This prevents ADL from finding cereal template declarations
		if constexpr (is_std_weak_ptr_v<T>) {
			// weak_ptr: Look up the ID and reconstruct from shared_ptr
			read_weak_ptr(ar, value);
		} else if constexpr (is_std_shared_ptr_v<T>) {
			read_shared_ptr(ar, value);
		} else if constexpr (is_std_unique_ptr_v<T>) {
			read_unique_ptr(ar, value);
		} else if constexpr (is_std_vector_v<T>) {
			read_vector(ar, value);
		} else if constexpr (is_std_map_v<T>) {
			read_map(ar, value);
		} else if constexpr (is_std_unordered_map_v<T>) {
			read_unordered_map(ar, value);
		} else if constexpr (is_std_array_v<T>) {
			read_array(ar, value);
		} else if constexpr (is_std_pair_v<T>) {
			read_pair(ar, value);
		} else if constexpr (is_direct_serializable_v<T>) {
			read_primitive(ar, value);
		}
		// THEN: Custom serialization methods
		else if constexpr (has_member_load_v<T>) {
			// Explicit member load() - user has full control, can compose others
			value.load(ar);
		} else if constexpr (has_free_load_v<T>) {
			// Explicit free load() - user has full control, can compose others
			load(ar, value);
		} else if constexpr (has_static_type_v<T>) {
			// JAI_STATIC_BINDER - compile-time property deserialization
			jai_static_type<T>::load(ar, value);
		} else if constexpr (is_property_owner_v<T>) {
			// property_owner<T> - auto-deserialize via property_mgr
			value.property_mgr.load(ar);
		} else if constexpr (has_member_serialize_load_v<T>) {
			// cereal-style member serialize() - only when archive supports operator()
			value.serialize(ar);
		} else if constexpr (has_free_serialize_load_v<T>) {
			// cereal-style free serialize() - only when archive supports operator()
			serialize(ar, value);
		} else {
			// No deserialization support - compile-time error
			static_assert(
				is_smart_ptr_v<T> || is_std_vector_v<T> || is_std_map_v<T> ||
				is_std_unordered_map_v<T> || is_std_array_v<T> || is_std_pair_v<T> ||
				is_direct_serializable_v<T> ||
				has_member_load_v<T> || has_free_load_v<T> ||
				has_static_type_v<T> || is_property_owner_v<T> ||
				has_member_serialize_load_v<T> || has_free_serialize_load_v<T>,
				"Type has no deserialization support. Provide one of: "
				"T::load(archive_reader&), "
				"load(archive_reader&, T&), "
				"JAI_STATIC_BINDER(T, ...), or "
				"inherit from property_owner<T>");
		}
	}

	// Legacy ADL helpers (kept for compatibility, now use dispatch functions internally)
	template<typename T>
	void adl_save(serialization::archive_writer& ar, const T& value) {
		dispatch_save(ar, value);
	}

	template<typename T>
	void adl_load(serialization::archive_reader& ar, T& value) {
		dispatch_load(ar, value);
	}

	// Helper to serialize primitives
	template<typename T>
	inline void write_primitive(serialization::archive_writer& ar, const T& value) {
		if constexpr (std::is_same_v<T, int8_t>) {
			ar.write_int8(value);
		} else if constexpr (std::is_same_v<T, int16_t>) {
			ar.write_int16(value);
		} else if constexpr (std::is_same_v<T, int32_t>) {
			ar.write_int32(value);
		} else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, long long>) {
			ar.write_int64(value);
		} else if constexpr (std::is_same_v<T, uint8_t>) {
			ar.write_uint8(value);
		} else if constexpr (std::is_same_v<T, uint16_t>) {
			ar.write_uint16(value);
		} else if constexpr (std::is_same_v<T, uint32_t>) {
			ar.write_uint32(value);
		} else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, unsigned long long>) {
			ar.write_uint64(value);
		} else if constexpr (std::is_same_v<T, float>) {
			ar.write_float32(value);
		} else if constexpr (std::is_same_v<T, double>) {
			ar.write_float64(value);
		} else if constexpr (std::is_same_v<T, bool>) {
			ar.write_bool(value);
		} else if constexpr (std::is_same_v<T, std::string>) {
			ar.write_string(value);
		} else if constexpr (std::is_same_v<T, char>) {
			ar.write_int8(static_cast<int8_t>(value));
		} else if constexpr (std::is_same_v<T, unsigned char>) {
			ar.write_uint8(value);
		} else if constexpr (std::is_enum_v<T>) {
			// Enum classes: serialize as underlying type
			using underlying = std::underlying_type_t<T>;
			write_primitive(ar, static_cast<underlying>(value));
		} else if constexpr (std::is_integral_v<T>) {
			// Fallback for int, long, etc.
			ar.write_int64(static_cast<int64_t>(value));
		} else if constexpr (std::is_floating_point_v<T>) {
			// Fallback for other floating point
			ar.write_float64(static_cast<double>(value));
		} else {
			static_assert(sizeof(T) == 0, "Unsupported primitive type for serialization");
		}
	}

	// Helper to deserialize primitives
	template<typename T>
	inline void read_primitive(serialization::archive_reader& ar, T& value) {
		if constexpr (std::is_same_v<T, int8_t>) {
			value = ar.read_int8();
		} else if constexpr (std::is_same_v<T, int16_t>) {
			value = ar.read_int16();
		} else if constexpr (std::is_same_v<T, int32_t>) {
			value = ar.read_int32();
		} else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, long long>) {
			value = ar.read_int64();
		} else if constexpr (std::is_same_v<T, uint8_t>) {
			value = ar.read_uint8();
		} else if constexpr (std::is_same_v<T, uint16_t>) {
			value = ar.read_uint16();
		} else if constexpr (std::is_same_v<T, uint32_t>) {
			value = ar.read_uint32();
		} else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, unsigned long long>) {
			value = ar.read_uint64();
		} else if constexpr (std::is_same_v<T, float>) {
			value = ar.read_float32();
		} else if constexpr (std::is_same_v<T, double>) {
			value = ar.read_float64();
		} else if constexpr (std::is_same_v<T, bool>) {
			value = ar.read_bool();
		} else if constexpr (std::is_same_v<T, std::string>) {
			value = ar.read_string();
		} else if constexpr (std::is_same_v<T, char>) {
			value = static_cast<char>(ar.read_int8());
		} else if constexpr (std::is_same_v<T, unsigned char>) {
			value = ar.read_uint8();
		} else if constexpr (std::is_enum_v<T>) {
			// Enum classes: deserialize from underlying type
			using underlying = std::underlying_type_t<T>;
			underlying temp;
			read_primitive(ar, temp);
			value = static_cast<T>(temp);
		} else if constexpr (std::is_integral_v<T>) {
			// Fallback for int, long, etc.
			value = static_cast<T>(ar.read_int64());
		} else if constexpr (std::is_floating_point_v<T>) {
			// Fallback for other floating point
			value = static_cast<T>(ar.read_float64());
		} else {
			static_assert(sizeof(T) == 0, "Unsupported primitive type for deserialization");
		}
	}

	// Serialize containers (vector, map, etc.)
	template<typename T>
	inline void write_container(serialization::archive_writer& ar, const T& container) {
		ar.begin_array(container.size());
		for (const auto& elem : container) {
			using elem_type = std::decay_t<decltype(elem)>;
			if constexpr (is_direct_serializable_v<elem_type>) {
				write_primitive(ar, elem);
			} else {
				// For complex types, they must provide their own serialization
				throw serialization_error("Container elements must be directly serializable or provide custom serialization");
			}
		}
		ar.end_array();
	}

	// Deserialize containers (vector)
	template<typename T>
	inline void read_vector(serialization::archive_reader& ar, std::vector<T>& vec) {
		size_t size = ar.begin_array();
		vec.clear();
		vec.reserve(size);

		for (size_t i = 0; i < size; ++i) {
			T elem;
			if constexpr (is_direct_serializable_v<T>) {
				read_primitive(ar, elem);
			} else {
				throw serialization_error("Vector elements must be directly serializable or provide custom serialization");
			}
			vec.push_back(std::move(elem));
		}

		ar.end_array();
	}

	// Serialize map
	template<typename K, typename V>
	inline void write_map(serialization::archive_writer& ar, const std::map<K, V>& map) {
		// Use native map format (JSON: object, Binary: array of pairs)
		ar.begin_map(map.size());
		for (const auto& [key, value] : map) {
			// Write the key (must be convertible to string for JSON compatibility)
			std::string key_str;
			if constexpr (std::is_same_v<K, std::string>) {
				key_str = key;
			} else if constexpr (std::is_arithmetic_v<K>) {
				key_str = std::to_string(key);
			} else {
				static_assert(sizeof(K) == 0, "Map keys must be strings or arithmetic types");
			}
			ar.write_map_key(key_str);

			// Write the value
			if constexpr (is_direct_serializable_v<V>) {
				write_primitive(ar, value);
			} else {
				throw serialization_error("Map values must be directly serializable or provide custom serialization");
			}
		}
		ar.end_map();
	}

	// Deserialize map
	template<typename K, typename V>
	inline void read_map(serialization::archive_reader& ar, std::map<K, V>& map) {
		size_t size = ar.begin_map();
		map.clear();

		for (size_t i = 0; i < size; ++i) {
			std::string key_str;
			if (!ar.read_map_key(key_str)) {
				break;
			}

			// Convert key from string
			K key;
			if constexpr (std::is_same_v<K, std::string>) {
				key = key_str;
			} else if constexpr (std::is_integral_v<K>) {
				key = static_cast<K>(std::stoll(key_str));
			} else if constexpr (std::is_floating_point_v<K>) {
				key = static_cast<K>(std::stod(key_str));
			} else {
				static_assert(sizeof(K) == 0, "Map keys must be strings or arithmetic types");
			}

			// Read the value
			V value;
			if constexpr (is_direct_serializable_v<V>) {
				read_primitive(ar, value);
			} else {
				throw serialization_error("Map values must be directly serializable or provide custom serialization");
			}

			map[std::move(key)] = std::move(value);
		}
		ar.end_map();
	}

	// Serialize unordered_map (same as map)
	template<typename K, typename V>
	inline void write_unordered_map(serialization::archive_writer& ar, const std::unordered_map<K, V>& map) {
		ar.begin_map(map.size());
		for (const auto& [key, value] : map) {
			std::string key_str;
			if constexpr (std::is_same_v<K, std::string>) {
				key_str = key;
			} else if constexpr (std::is_arithmetic_v<K>) {
				key_str = std::to_string(key);
			} else {
				static_assert(sizeof(K) == 0, "Map keys must be strings or arithmetic types");
			}
			ar.write_map_key(key_str);

			if constexpr (is_direct_serializable_v<V>) {
				write_primitive(ar, value);
			} else {
				throw serialization_error("Map values must be directly serializable or provide custom serialization");
			}
		}
		ar.end_map();
	}

	// Deserialize unordered_map
	template<typename K, typename V>
	inline void read_unordered_map(serialization::archive_reader& ar, std::unordered_map<K, V>& map) {
		size_t size = ar.begin_map();
		map.clear();
		map.reserve(size);

		for (size_t i = 0; i < size; ++i) {
			std::string key_str;
			if (!ar.read_map_key(key_str)) {
				break;
			}

			K key;
			if constexpr (std::is_same_v<K, std::string>) {
				key = key_str;
			} else if constexpr (std::is_integral_v<K>) {
				key = static_cast<K>(std::stoll(key_str));
			} else if constexpr (std::is_floating_point_v<K>) {
				key = static_cast<K>(std::stod(key_str));
			} else {
				static_assert(sizeof(K) == 0, "Map keys must be strings or arithmetic types");
			}

			V value;
			if constexpr (is_direct_serializable_v<V>) {
				read_primitive(ar, value);
			} else {
				throw serialization_error("Map values must be directly serializable or provide custom serialization");
			}

			map[std::move(key)] = std::move(value);
		}
		ar.end_map();
	}

	// Serialize std::array
	template<typename T, std::size_t N>
	inline void write_array(serialization::archive_writer& ar, const std::array<T, N>& arr) {
		ar.begin_array(N);
		for (const auto& elem : arr) {
			if constexpr (is_direct_serializable_v<T>) {
				write_primitive(ar, elem);
			} else {
				throw serialization_error("Array elements must be directly serializable or provide custom serialization");
			}
		}
		ar.end_array();
	}

	// Deserialize std::array
	template<typename T, std::size_t N>
	inline void read_array(serialization::archive_reader& ar, std::array<T, N>& arr) {
		size_t size = ar.begin_array();
		// std::array has fixed size - read up to N elements
		for (std::size_t i = 0; i < N && i < size; ++i) {
			if constexpr (is_direct_serializable_v<T>) {
				read_primitive(ar, arr[i]);
			} else {
				throw serialization_error("Array elements must be directly serializable or provide custom serialization");
			}
		}
		// Skip any extra elements if size > N
		for (std::size_t i = N; i < size; ++i) {
			T dummy;
			if constexpr (is_direct_serializable_v<T>) {
				read_primitive(ar, dummy);
			}
		}
		ar.end_array();
	}

	// Serialize std::pair
	template<typename T1, typename T2>
	inline void write_pair(serialization::archive_writer& ar, const std::pair<T1, T2>& p) {
		ar.begin_array(2);
		dispatch_save(ar, p.first);
		dispatch_save(ar, p.second);
		ar.end_array();
	}

	// Deserialize std::pair
	template<typename T1, typename T2>
	inline void read_pair(serialization::archive_reader& ar, std::pair<T1, T2>& p) {
		size_t size = ar.begin_array();
		if (size >= 1) {
			dispatch_load(ar, p.first);
		}
		if (size >= 2) {
			dispatch_load(ar, p.second);
		}
		ar.end_array();
	}

	// ============================================================================
	// Smart pointer serialization (with ID-based de-duplication)
	// ============================================================================
	// shared_ptr: Uses ID tracking - same object serialized once, referenced by ID
	// weak_ptr: Saves the ID of the shared_ptr it references (for reconstruction)
	// unique_ptr: Always serializes the object (no sharing possible)

	// Helper: check if a type can be serialized (for smart pointer element checks)
	template<typename T>
	inline constexpr bool is_element_serializable_v =
		is_direct_serializable_v<T> ||
		has_static_type_v<T> ||
		is_property_owner_v<T> ||
		has_member_save_v<T> ||
		has_free_save_v<T> ||
		has_member_serialize_save_v<T> ||
		has_free_serialize_save_v<T>;

	// Helper: check if a type can be deserialized (for smart pointer element checks on load)
	template<typename T>
	inline constexpr bool is_element_deserializable_v =
		is_direct_serializable_v<T> ||
		has_static_type_v<T> ||
		is_property_owner_v<T> ||
		has_member_load_v<T> ||
		has_free_load_v<T> ||
		has_member_serialize_load_v<T> ||
		has_free_serialize_load_v<T>;

	// Write shared_ptr with ID-based de-duplication
	template<typename T>
	inline void write_shared_ptr(serialization::archive_writer& ar, const std::shared_ptr<T>& ptr) {
		// If element type isn't serializable, write as null (allows transient properties to compile)
		if constexpr (!is_element_serializable_v<T>) {
			ar.write_uint32(0);  // null
			return;
		} else {
			// Get or assign ID for this pointer
			auto [id, is_new] = ar.get_or_assign_shared_id(ptr.get());
			ar.write_uint32(id);  // ID 0 means null

			// Only serialize the object if this is the first time we've seen it
			if (is_new && ptr) {
				if constexpr (is_direct_serializable_v<T>) {
					write_primitive(ar, *ptr);
				} else if constexpr (has_static_type_v<T>) {
					jai_static_type<T>::save(ar, *ptr);
				} else if constexpr (is_property_owner_v<T>) {
					ptr->property_mgr.save(ar);
				} else if constexpr (has_member_save_v<T>) {
					ptr->save(ar);
				} else if constexpr (has_free_save_v<T>) {
					save(ar, *ptr);
				} else if constexpr (has_member_serialize_save_v<T>) {
					ptr->serialize(ar);
				} else if constexpr (has_free_serialize_save_v<T>) {
					serialize(ar, *ptr);
				}
			}
			// If !is_new, the object was already serialized - just the ID is enough
		}
	}

	// Read shared_ptr with ID-based de-duplication
	template<typename T>
	inline void read_shared_ptr(serialization::archive_reader& ar, std::shared_ptr<T>& ptr) {
		uint32_t id = ar.read_uint32();

		if (id == 0) {
			// Null pointer
			ptr.reset();
			return;
		}

		// If element type isn't deserializable at compile time, we can only
		// skip the data. This happens for deleted properties with non-serializable
		// element types - we need to consume old data from the stream.
		if constexpr (!is_element_deserializable_v<T>) {
			// Check if we've already seen this ID (data was already skipped)
			if (!ar.has_deserialized_shared(id)) {
				// First time seeing this ID - skip the object data
				// read_value() consumes any value from the stream
				ar.read_value();
			}
			ptr.reset();  // Can't reconstruct - return null
			return;
		} else {
			// Check if we've already deserialized this object
			if (ar.has_deserialized_shared(id)) {
				ptr = ar.get_deserialized_shared<T>(id);
				return;
			}

			// First time seeing this ID - deserialize the object
			ptr = std::make_shared<T>();
			if constexpr (is_direct_serializable_v<T>) {
				read_primitive(ar, *ptr);
			} else if constexpr (has_static_type_v<T>) {
				jai_static_type<T>::load(ar, *ptr);
			} else if constexpr (is_property_owner_v<T>) {
				ptr->property_mgr.load(ar);
			} else if constexpr (has_member_load_v<T>) {
				ptr->load(ar);
			} else if constexpr (has_free_load_v<T>) {
				load(ar, *ptr);
			} else if constexpr (has_member_serialize_load_v<T>) {
				ptr->serialize(ar);
			} else if constexpr (has_free_serialize_load_v<T>) {
				serialize(ar, *ptr);
			}

			// Register for weak_ptr reconstruction
			ar.register_deserialized_shared(id, ptr);
		}
	}

	// Write unique_ptr
	template<typename T>
	inline void write_unique_ptr(serialization::archive_writer& ar, const std::unique_ptr<T>& ptr) {
		// If element type isn't serializable, write as null (allows transient properties to compile)
		if constexpr (!is_element_serializable_v<T>) {
			ar.write_bool(false);  // null
			return;
		} else {
			if (ptr) {
				ar.write_bool(true); // has value
				if constexpr (is_direct_serializable_v<T>) {
					write_primitive(ar, *ptr);
				} else if constexpr (has_static_type_v<T>) {
					jai_static_type<T>::save(ar, *ptr);
				} else if constexpr (is_property_owner_v<T>) {
					ptr->property_mgr.save(ar);
				} else if constexpr (has_member_save_v<T>) {
					ptr->save(ar);
				} else if constexpr (has_free_save_v<T>) {
					save(ar, *ptr);
				} else if constexpr (has_member_serialize_save_v<T>) {
					ptr->serialize(ar);
				} else if constexpr (has_free_serialize_save_v<T>) {
					serialize(ar, *ptr);
				}
			} else {
				ar.write_bool(false); // null
			}
		}
	}

	// Read unique_ptr
	template<typename T>
	inline void read_unique_ptr(serialization::archive_reader& ar, std::unique_ptr<T>& ptr) {
		bool has_value = ar.read_bool();
		if (has_value) {
			// If element type isn't deserializable, skip the data and return null
			if constexpr (!is_element_deserializable_v<T>) {
				ar.read_value();  // Skip the object data
				ptr.reset();
				return;
			} else {
				ptr = std::make_unique<T>();
				if constexpr (is_direct_serializable_v<T>) {
					read_primitive(ar, *ptr);
				} else if constexpr (has_static_type_v<T>) {
					jai_static_type<T>::load(ar, *ptr);
				} else if constexpr (is_property_owner_v<T>) {
					ptr->property_mgr.load(ar);
				} else if constexpr (has_member_load_v<T>) {
					ptr->load(ar);
				} else if constexpr (has_free_load_v<T>) {
					load(ar, *ptr);
				} else if constexpr (has_member_serialize_load_v<T>) {
					ptr->serialize(ar);
				} else if constexpr (has_free_serialize_load_v<T>) {
					serialize(ar, *ptr);
				}
			}
		} else {
			ptr.reset();
		}
	}

	// weak_ptr: Saves the ID of the shared_ptr it references
	// On load, looks up the ID in the archive's registered shared_ptrs
	// NOTE: The shared_ptr must be serialized BEFORE the weak_ptr for this to work.
	//       If the shared_ptr hasn't been seen yet, the ID will be 0 (not found).
	template<typename T>
	inline void write_weak_ptr(serialization::archive_writer& ar, const std::weak_ptr<T>& ptr) {
		// Try to lock the weak_ptr to get the raw pointer
		if (auto shared = ptr.lock()) {
			// Look up the ID assigned to this pointer (must have been serialized already)
			uint32_t id = ar.lookup_shared_id(shared.get());
			ar.write_uint32(id);  // 0 if not found (shared_ptr wasn't serialized yet)
		} else {
			// Expired weak_ptr
			ar.write_uint32(0);
		}
	}

	template<typename T>
	inline void read_weak_ptr(serialization::archive_reader& ar, std::weak_ptr<T>& ptr) {
		uint32_t id = ar.read_uint32();

		if (id == 0) {
			// Null or expired
			ptr.reset();
			return;
		}

		// Look up the shared_ptr by ID and create weak_ptr from it
		if (auto shared = ar.get_deserialized_shared<T>(id)) {
			ptr = shared;
		} else {
			// ID not found - the shared_ptr wasn't deserialized yet or doesn't exist
			// This can happen if serialization order is wrong
			ptr.reset();
		}
	}

} // namespace property_serialization
} // namespace jai

// Now implement property<T>::save() and load() using the helpers
namespace jai {

	// Compile-time check: is this type serializable?
	// Note: This is provided for users to check types, but properties now always try
	// to serialize unless marked as transient. Use serialize_mode::transient for
	// properties that should not be serialized.
	template<typename T>
	inline constexpr bool is_type_serializable_v =
		property_serialization::is_direct_serializable_v<T> ||
		property_serialization::is_std_vector_v<T> ||
		property_serialization::is_std_map_v<T> ||
		property_serialization::is_std_unordered_map_v<T> ||
		property_serialization::is_std_array_v<T> ||
		property_serialization::is_std_pair_v<T> ||
		property_serialization::is_smart_ptr_v<T> ||
		property_serialization::has_any_save_v<T> ||
		has_static_type_v<T> ||  // In jai namespace (from static_binder.hpp)
		property_serialization::is_property_owner_v<T>;

	template<typename T>
	inline void property<T>::save(serialization::archive_writer& ar) const {
		// Skip if marked as transient (compile-time decision)
		if (m_serialize_mode == serialize_mode::transient) {
			return;
		}

		// Skip if dynamically disabled (runtime decision)
		if (!m_allow_serialization) {
			return;
		}

		// Always try to serialize - will hit static_assert in dispatch_save if type
		// has no serialization support. Mark property as transient to skip instead.
		ar.write_property_name(name());
		property_serialization::dispatch_save(ar, m_value);
	}

	template<typename T>
	inline void property<T>::load(serialization::archive_reader& ar) {
		// Note: property_manager::load() has already read the property name
		// Always try to deserialize - will hit static_assert in dispatch_load if type
		// has no serialization support.
		property_serialization::dispatch_load(ar, m_value);
	}

	// deleted_property serialization
	template<typename T>
	inline void deleted_property<T>::save(serialization::archive_writer& ar) const {
		// No-op: deleted properties don't save
	}

	template<typename T>
	inline void deleted_property<T>::load(serialization::archive_reader& ar) {
		// Skip the deleted property by reading and discarding the value
		// Note: property_manager::load() has already read the property name

		if constexpr (std::is_default_constructible_v<T>) {
			// Use dispatch_load with a dummy value to read and discard
			T dummy{};
			property_serialization::dispatch_load(ar, dummy);
		}
		else {
			// Type has no deserialization support - skip the value in the archive
			ar.read_value();
		}
	}

	// property_manager serialization implementation
	inline void property_manager::save(serialization::archive_writer& ar) const {
		// Collect properties to save
		std::vector<std::string> keys;
		for (const auto& [name, prop] : m_properties) {
			if (prop->allow_save()) {
				keys.push_back(name);
			}
		}

		// Binary format needs property count; JSON has self-describing objects
		if (ar.needs_property_keys()) {
			ar.write_uint32(static_cast<uint32_t>(keys.size()));
		}

		// Write each property
		for (const auto& name : keys) {
			m_properties.at(name)->save(ar);
		}
	}

	inline void property_manager::load(serialization::archive_reader& ar) {
		// Binary format has property count; JSON reads until no more properties
		std::string property_name;

		if (ar.needs_property_keys()) {
			// Binary: read count, then read that many properties
			uint32_t num_props = ar.read_uint32();
			for (uint32_t i = 0; i < num_props; ++i) {
				if (!ar.read_property_name(property_name)) {
					break;
				}

				auto it = m_properties.find(property_name);
				if (it != m_properties.end()) {
					it->second->load(ar);
				} else {
					ar.read_value(); // Skip unknown property
				}
			}
		} else {
			// JSON: loop until read_property_name returns false (end of object)
			while (ar.read_property_name(property_name)) {
				auto it = m_properties.find(property_name);
				if (it != m_properties.end()) {
					it->second->load(ar);
				} else {
					ar.read_value(); // Skip unknown property
				}
			}
		}

		// Note: Engine binding must be done explicitly before serialization
		// via bind_to_engine(eng->get_weak_engine()) - cannot auto-inject from raw pointer
	}

} // namespace jai
