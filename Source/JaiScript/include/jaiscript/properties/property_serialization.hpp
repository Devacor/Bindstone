#pragma once

#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/serialization/serialization_metadata.hpp>
#include <jaiscript/serialization/construct.hpp>
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
	// Serialization support detection (CRTP-compatible)
	// ============================================================================
	// After CRTP migration, we no longer detect save/load methods that take
	// the deleted archive_writer/archive_reader base classes.
	//
	// Supported serialization patterns:
	//   1. JAI_STATIC_BINDER(T, ...) - compile-time property serialization
	//   2. property_owner<T> - runtime property serialization via property_mgr
	//   3. is_direct_serializable_v<T> - primitives, strings, enums
	//
	// For smart pointers: Element types must use one of the above patterns.
	// Custom save/load methods should be templated on Archive type (Cereal-style).
	// ============================================================================

	// Placeholder traits for legacy compatibility (always false)
	// These existed for deleted archive_writer/archive_reader - now dead code paths
	template<typename T>
	inline constexpr bool has_member_save_v = false;
	template<typename T>
	inline constexpr bool has_member_load_v = false;
	template<typename T>
	inline constexpr bool has_free_save_v = false;
	template<typename T>
	inline constexpr bool has_free_load_v = false;
	template<typename T>
	inline constexpr bool has_member_serialize_save_v = false;
	template<typename T>
	inline constexpr bool has_member_serialize_load_v = false;
	template<typename T>
	inline constexpr bool has_free_serialize_save_v = false;
	template<typename T>
	inline constexpr bool has_free_serialize_load_v = false;
	template<typename T>
	inline constexpr bool has_any_save_v = false;
	template<typename T>
	inline constexpr bool has_any_load_v = false;
	template<typename T>
	inline constexpr bool has_custom_serialization_v = false;
	template<typename T>
	inline constexpr bool has_jai_save_v = false;
	template<typename T>
	inline constexpr bool has_jai_load_v = false;
	// --- load_and_construct detection ---
	// Detects: T has a static load_and_construct(Archive&, construct<T>&) member
	// This pattern is used for types without default constructors
	namespace detail {
		// Dummy archive for trait detection
		// Must have enough stub methods for load_and_construct decltype to work
		struct detection_archive {
			static constexpr bool is_text_format = false;
			static constexpr bool is_jai_archive = true;

			// Stub methods to allow trait detection of load_and_construct
			// These allow the decltype expression to succeed, even though
			// they aren't actually called
			template<typename T>
			T* get_user_context() const { return nullptr; }

			template<typename T>
			void serialize(const char*, T&) {}

			// Basic read/write stubs
			int32_t read_int32() { return 0; }
			std::string read_string() { return {}; }
			void begin_object(std::string&, uint32_t&) {}
			void end_object() {}
		};
	}

	template<typename T, typename = void>
	struct has_member_load_and_construct_impl : std::false_type {};

	template<typename T>
	struct has_member_load_and_construct_impl<T, std::void_t<
		decltype(T::load_and_construct(
			std::declval<detail::detection_archive&>(),
			std::declval<serialization::construct<T>&>()
		))
	>> : std::true_type {};

	template<typename T>
	inline constexpr bool has_member_load_and_construct_v = has_member_load_and_construct_impl<T>::value;

	template<typename T>
	inline constexpr bool has_load_and_construct_v = has_member_load_and_construct_v<T>;

	// Free function version (not commonly used, placeholder)
	template<typename T>
	inline constexpr bool has_free_load_and_construct_v = false;

	// unique_ptr versions
	template<typename T, typename = void>
	struct has_member_load_and_construct_unique_impl : std::false_type {};

	template<typename T>
	struct has_member_load_and_construct_unique_impl<T, std::void_t<
		decltype(T::load_and_construct(
			std::declval<detail::detection_archive&>(),
			std::declval<serialization::construct_unique<T>&>()
		))
	>> : std::true_type {};

	template<typename T>
	inline constexpr bool has_member_load_and_construct_unique_v = has_member_load_and_construct_unique_impl<T>::value;

	template<typename T>
	inline constexpr bool has_load_and_construct_unique_v = has_member_load_and_construct_unique_v<T>;

	template<typename T>
	inline constexpr bool has_free_load_and_construct_unique_v = false;

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
	// Forward declarations for serialization helpers (templated on Archive)
	// ============================================================================
	template<typename Archive, typename T> inline void write_shared_ptr(Archive& ar, const std::shared_ptr<T>& ptr);
	template<typename Archive, typename T> inline void read_shared_ptr(Archive& ar, std::shared_ptr<T>& ptr);
	template<typename Archive, typename T> inline void write_unique_ptr(Archive& ar, const std::unique_ptr<T>& ptr);
	template<typename Archive, typename T> inline void read_unique_ptr(Archive& ar, std::unique_ptr<T>& ptr);
	template<typename Archive, typename T> inline void write_weak_ptr(Archive& ar, const std::weak_ptr<T>& ptr);
	template<typename Archive, typename T> inline void read_weak_ptr(Archive& ar, std::weak_ptr<T>& ptr);

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
	// Type-erased dispatch for property serialization
	// ============================================================================
	// These work with any_archive_writer/reader for the new property system.
	// Properties are "binding glue" - they provide (name, value) to the archive.
	// The dispatch handles type-specific serialization using the type-erased interface.

	// Helper to serialize primitives via type-erased archive
	template<typename T>
	inline void write_primitive_erased(serialization::any_archive_writer& ar, const T& value) {
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
			using underlying = std::underlying_type_t<T>;
			write_primitive_erased(ar, static_cast<underlying>(value));
		} else if constexpr (std::is_integral_v<T>) {
			ar.write_int64(static_cast<int64_t>(value));
		} else if constexpr (std::is_floating_point_v<T>) {
			ar.write_float64(static_cast<double>(value));
		} else {
			static_assert(sizeof(T) == 0, "Unsupported primitive type for serialization");
		}
	}

	// Helper to deserialize primitives via type-erased archive
	template<typename T>
	inline void read_primitive_erased(serialization::any_archive_reader& ar, T& value) {
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
			using underlying = std::underlying_type_t<T>;
			underlying temp;
			read_primitive_erased(ar, temp);
			value = static_cast<T>(temp);
		} else if constexpr (std::is_integral_v<T>) {
			value = static_cast<T>(ar.read_int64());
		} else if constexpr (std::is_floating_point_v<T>) {
			value = static_cast<T>(ar.read_float64());
		} else {
			static_assert(sizeof(T) == 0, "Unsupported primitive type for deserialization");
		}
	}

	// Type-erased save dispatch
	// Handles all serializable types via the any_archive_writer interface
	template<typename T>
	void dispatch_save_erased(serialization::any_archive_writer& ar, const T& value) {
		// Priority 1: Direct primitives
		if constexpr (is_direct_serializable_v<T>) {
			write_primitive_erased(ar, value);
		}
		// Priority 2: Vectors - write as array
		else if constexpr (is_std_vector_v<T>) {
			using elem_type = vector_element_t<T>;
			ar.begin_array(value.size());
			for (const auto& elem : value) {
				dispatch_save_erased(ar, elem);
			}
			ar.end_array();
		}
		// Priority 3: Maps - write as map
		else if constexpr (is_std_map_v<T>) {
			using key_type = map_key_t<T>;
			using val_type = map_value_t<T>;
			ar.begin_map(value.size());
			for (const auto& [k, v] : value) {
				std::string key_str;
				if constexpr (std::is_same_v<key_type, std::string>) {
					key_str = k;
				} else if constexpr (std::is_arithmetic_v<key_type>) {
					key_str = std::to_string(k);
				}
				ar.write_map_key(key_str);
				dispatch_save_erased(ar, v);
			}
			ar.end_map();
		}
		// Priority 4: Unordered maps
		else if constexpr (is_std_unordered_map_v<T>) {
			using key_type = unordered_map_key_t<T>;
			using val_type = unordered_map_value_t<T>;
			ar.begin_map(value.size());
			for (const auto& [k, v] : value) {
				std::string key_str;
				if constexpr (std::is_same_v<key_type, std::string>) {
					key_str = k;
				} else if constexpr (std::is_arithmetic_v<key_type>) {
					key_str = std::to_string(k);
				}
				ar.write_map_key(key_str);
				dispatch_save_erased(ar, v);
			}
			ar.end_map();
		}
		// Priority 5: Arrays
		else if constexpr (is_std_array_v<T>) {
			constexpr size_t N = array_size_v<T>;
			ar.begin_array(N);
			for (const auto& elem : value) {
				dispatch_save_erased(ar, elem);
			}
			ar.end_array();
		}
		// Priority 6: Pairs
		else if constexpr (is_std_pair_v<T>) {
			ar.begin_array(2);
			dispatch_save_erased(ar, value.first);
			dispatch_save_erased(ar, value.second);
			ar.end_array();
		}
		// Priority 7: Smart pointers - not supported in type-erased context
		// Smart pointer serialization requires concrete archive type for ID tracking.
		// Mark these properties as transient, or use templated serialization for full support.
		else if constexpr (is_smart_ptr_v<T>) {
			throw std::runtime_error("Smart pointer serialization requires concrete archive type (use templated serialization or mark property as transient)");
		}
		// Fallback: throw for unsupported types - fail explicitly instead of silently
		else {
			throw std::runtime_error("Type is not serializable via type-erased archive (use JAI_STATIC_BINDER, property_owner, or mark property as transient)");
		}
	}

	// Type-erased load dispatch
	template<typename T>
	void dispatch_load_erased(serialization::any_archive_reader& ar, T& value) {
		// Priority 1: Direct primitives
		if constexpr (is_direct_serializable_v<T>) {
			read_primitive_erased(ar, value);
		}
		// Priority 2: Vectors
		else if constexpr (is_std_vector_v<T>) {
			using elem_type = vector_element_t<T>;
			size_t size = ar.begin_array();
			value.clear();
			value.reserve(size);
			for (size_t i = 0; i < size; ++i) {
				elem_type elem;
				dispatch_load_erased(ar, elem);
				value.push_back(std::move(elem));
			}
			ar.end_array();
		}
		// Priority 3: Maps
		else if constexpr (is_std_map_v<T>) {
			using key_type = map_key_t<T>;
			using val_type = map_value_t<T>;
			size_t size = ar.begin_map();
			value.clear();
			for (size_t i = 0; i < size; ++i) {
				std::string key_str;
				if (!ar.read_map_key(key_str)) break;
				key_type key;
				if constexpr (std::is_same_v<key_type, std::string>) {
					key = key_str;
				} else if constexpr (std::is_integral_v<key_type>) {
					key = static_cast<key_type>(std::stoll(key_str));
				} else if constexpr (std::is_floating_point_v<key_type>) {
					key = static_cast<key_type>(std::stod(key_str));
				}
				val_type val;
				dispatch_load_erased(ar, val);
				value[std::move(key)] = std::move(val);
			}
			ar.end_map();
		}
		// Priority 4: Unordered maps
		else if constexpr (is_std_unordered_map_v<T>) {
			using key_type = unordered_map_key_t<T>;
			using val_type = unordered_map_value_t<T>;
			size_t size = ar.begin_map();
			value.clear();
			value.reserve(size);
			for (size_t i = 0; i < size; ++i) {
				std::string key_str;
				if (!ar.read_map_key(key_str)) break;
				key_type key;
				if constexpr (std::is_same_v<key_type, std::string>) {
					key = key_str;
				} else if constexpr (std::is_integral_v<key_type>) {
					key = static_cast<key_type>(std::stoll(key_str));
				} else if constexpr (std::is_floating_point_v<key_type>) {
					key = static_cast<key_type>(std::stod(key_str));
				}
				val_type val;
				dispatch_load_erased(ar, val);
				value[std::move(key)] = std::move(val);
			}
			ar.end_map();
		}
		// Priority 5: Arrays
		else if constexpr (is_std_array_v<T>) {
			constexpr size_t N = array_size_v<T>;
			size_t size = ar.begin_array();
			for (size_t i = 0; i < N && i < size; ++i) {
				dispatch_load_erased(ar, value[i]);
			}
			ar.end_array();
		}
		// Priority 6: Pairs
		else if constexpr (is_std_pair_v<T>) {
			size_t size = ar.begin_array();
			if (size >= 1) dispatch_load_erased(ar, value.first);
			if (size >= 2) dispatch_load_erased(ar, value.second);
			ar.end_array();
		}
		// Priority 7: Smart pointers - not supported in type-erased context
		// Smart pointer deserialization requires concrete archive type for ID tracking.
		// Mark these properties as transient, or use templated serialization for full support.
		else if constexpr (is_smart_ptr_v<T>) {
			throw std::runtime_error("Smart pointer deserialization requires concrete archive type (use templated serialization or mark property as transient)");
		}
		// Fallback: throw for unsupported types - fail explicitly instead of silently
		else {
			throw std::runtime_error("Type is not deserializable via type-erased archive (use JAI_STATIC_BINDER, property_owner, or mark property as transient)");
		}
	}

	// ============================================================================
	// Templated primitive write/read helpers (for CRTP archives)
	// ============================================================================
	// These take any Archive type and call the appropriate write/read method.
	// Used by smart pointer serialization which is templated on Archive.

	template<typename Archive, typename T>
	inline void write_primitive_templated(Archive& ar, const T& value) {
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
			using underlying = std::underlying_type_t<T>;
			write_primitive_templated(ar, static_cast<underlying>(value));
		} else if constexpr (std::is_integral_v<T>) {
			ar.write_int64(static_cast<int64_t>(value));
		} else if constexpr (std::is_floating_point_v<T>) {
			ar.write_float64(static_cast<double>(value));
		} else {
			static_assert(sizeof(T) == 0, "Unsupported primitive type for serialization");
		}
	}

	template<typename Archive, typename T>
	inline void read_primitive_templated(Archive& ar, T& value) {
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
			using underlying = std::underlying_type_t<T>;
			underlying temp;
			read_primitive_templated(ar, temp);
			value = static_cast<T>(temp);
		} else if constexpr (std::is_integral_v<T>) {
			value = static_cast<T>(ar.read_int64());
		} else if constexpr (std::is_floating_point_v<T>) {
			value = static_cast<T>(ar.read_float64());
		} else {
			static_assert(sizeof(T) == 0, "Unsupported primitive type for deserialization");
		}
	}

	// ============================================================================
	// Smart pointer serialization (with ID-based de-duplication)
	// ============================================================================
	// shared_ptr: Uses ID tracking - same object serialized once, referenced by ID
	// weak_ptr: Saves the ID of the shared_ptr it references (for reconstruction)
	// unique_ptr: Always serializes the object (no sharing possible)

	// Helper: check if a type can be serialized via CRTP archives
	// Supported: primitives, JAI_STATIC_BINDER types, property_owner types
	template<typename T>
	inline constexpr bool is_element_serializable_v =
		is_direct_serializable_v<T> ||
		has_static_type_v<T> ||
		is_property_owner_v<T>;

	// Helper: check if a type can be deserialized via CRTP archives
	// Supported: primitives, JAI_STATIC_BINDER types, property_owner types
	template<typename T>
	inline constexpr bool is_element_deserializable_v =
		is_direct_serializable_v<T> ||
		has_static_type_v<T> ||
		is_property_owner_v<T>;

	// Write shared_ptr with ID-based de-duplication
	// Binary format: [id:uint32][object data if new and non-null] - compact, no overhead
	// JSON format: {"_type_": "ptr", "$id": id, "$val": object} - verbose for readability
	template<typename Archive, typename T>
	inline void write_shared_ptr(Archive& ar, const std::shared_ptr<T>& ptr) {
		constexpr bool text_format = std::remove_reference_t<Archive>::is_text_format;

		// Get or assign ID for this pointer
		auto [id, is_new] = ar.get_or_assign_shared_id(ptr.get());

		if constexpr (text_format) {
			// JSON: verbose object format for readability
			ar.begin_object("ptr", 0);
			ar(serialization::make_nvp("$id", id));

			if (is_new && ptr) {
				ar.write_property_name("$val");
				if constexpr (is_direct_serializable_v<T>) {
					write_primitive_templated(ar, *ptr);
				} else if constexpr (has_static_type_v<T>) {
					jai_static_type<T>::save(ar, *ptr);
				} else if constexpr (is_property_owner_v<T>) {
					ar.begin_object("", 0);
					ptr->property_mgr.save(ar);
					ar.end_object();
				} else {
					// Fallback: use write_custom which handles both save() and serialize() types
					// and wraps in begin_object/end_object consistently
					ar.write_custom(*ptr);
				}
			}
			ar.end_object();
		} else {
			// Binary: compact format - just ID and optional object
			ar.write_uint32(id);

			if (is_new && ptr) {
				if constexpr (is_direct_serializable_v<T>) {
					write_primitive_templated(ar, *ptr);
				} else if constexpr (has_static_type_v<T>) {
					jai_static_type<T>::save(ar, *ptr);
				} else if constexpr (is_property_owner_v<T>) {
					ar.begin_object("", 0);
					ptr->property_mgr.save(ar);
					ar.end_object();
				} else {
					// Fallback: use write_custom which handles both save() and serialize() types
					// and wraps in begin_object/end_object consistently
					ar.write_custom(*ptr);
				}
			}
		}
	}

	// Read shared_ptr with ID-based de-duplication
	// Binary format: [id:uint32][object data if new and non-null] - compact
	// JSON format: {"_type_": "ptr", "$id": id, "$val": object} - verbose
	template<typename Archive, typename T>
	inline void read_shared_ptr(Archive& ar, std::shared_ptr<T>& ptr) {
		constexpr bool text_format = std::remove_reference_t<Archive>::is_text_format;

		if constexpr (text_format) {
			// JSON: verbose object format
			std::string type_name;
			uint32_t version;
			ar.begin_object(type_name, version);

			uint32_t id = 0;
			ar(serialization::make_nvp("$id", id));

			if (id == 0) {
				ptr.reset();
				ar.end_object();
				return;
			}

			if (ar.has_deserialized_shared(id)) {
				ptr = ar.get_deserialized_shared<T>(id);
				ar.end_object();
				return;
			}

			// First time - read "$val" property
			std::string prop_name;
			ar.read_property_name(prop_name);

			// Use default construction if available, otherwise use load_and_construct
			if constexpr (std::is_default_constructible_v<T>) {
				ptr = std::make_shared<T>();
				if constexpr (is_direct_serializable_v<T>) {
					read_primitive_templated(ar, *ptr);
				} else if constexpr (has_static_type_v<T>) {
					jai_static_type<T>::load(ar, *ptr);
				} else if constexpr (is_property_owner_v<T>) {
					std::string inner_type_name;
					uint32_t inner_version;
					ar.begin_object(inner_type_name, inner_version);
					ptr->property_mgr.load(ar);
					ar.end_object();
				} else {
					// Fallback: use read_custom which handles both load() and serialize() types
					// and unwraps begin_object/end_object consistently
					ar.read_custom(*ptr);
				}
			} else if constexpr (has_member_load_and_construct_v<T>) {
				// Type has JaiScript-style load_and_construct
				serialization::construct<T> c(ptr);
				access::load_and_construct(ar, c);
			} else {
				// Type is NOT default constructible and has no load_and_construct
				throw std::runtime_error("Cannot deserialize shared_ptr<T>: type is not default constructible and has no load_and_construct");
			}

			ar.register_deserialized_shared(id, ptr);
			ar.end_object();
		} else {
			// Binary: compact format - just ID and optional object
			uint32_t id = ar.read_uint32();

			if (id == 0) {
				ptr.reset();
				return;
			}

			if (ar.has_deserialized_shared(id)) {
				ptr = ar.get_deserialized_shared<T>(id);
				return;
			}

			// First time - read object directly
			// Use default construction if available, otherwise use load_and_construct
			if constexpr (std::is_default_constructible_v<T>) {
				ptr = std::make_shared<T>();
				if constexpr (is_direct_serializable_v<T>) {
					read_primitive_templated(ar, *ptr);
				} else if constexpr (has_static_type_v<T>) {
					jai_static_type<T>::load(ar, *ptr);
				} else if constexpr (is_property_owner_v<T>) {
					std::string inner_type_name;
					uint32_t inner_version;
					ar.begin_object(inner_type_name, inner_version);
					ptr->property_mgr.load(ar);
					ar.end_object();
				} else {
					// Fallback: use read_custom which handles both load() and serialize() types
					// and unwraps begin_object/end_object consistently
					ar.read_custom(*ptr);
				}
			} else if constexpr (has_member_load_and_construct_v<T>) {
				// Type has JaiScript-style load_and_construct
				serialization::construct<T> c(ptr);
				access::load_and_construct(ar, c);
			} else {
				// Type is NOT default constructible and has no load_and_construct
				throw std::runtime_error("Cannot deserialize shared_ptr<T>: type is not default constructible and has no load_and_construct");
			}

			ar.register_deserialized_shared(id, ptr);
		}
	}

	// Write unique_ptr (templated on Archive for CRTP support)
	template<typename Archive, typename T>
	inline void write_unique_ptr(Archive& ar, const std::unique_ptr<T>& ptr) {
		if (ptr) {
			ar.write_bool(true); // has value
			if constexpr (is_direct_serializable_v<T>) {
				write_primitive_templated(ar, *ptr);
			} else if constexpr (has_static_type_v<T>) {
				jai_static_type<T>::save(ar, *ptr);
			} else if constexpr (is_property_owner_v<T>) {
				ar.begin_object("", 0);
				ptr->property_mgr.save(ar);
				ar.end_object();
			} else {
				// Fallback: use write_custom which handles both save() and serialize() types
				// and wraps in begin_object/end_object consistently
				ar.write_custom(*ptr);
			}
		} else {
			ar.write_bool(false); // null
		}
	}

	// Read unique_ptr (templated on Archive for CRTP support)
	template<typename Archive, typename T>
	inline void read_unique_ptr(Archive& ar, std::unique_ptr<T>& ptr) {
		bool has_value = ar.read_bool();
		if (has_value) {
			// Use default construction if available, otherwise use load_and_construct
			if constexpr (std::is_default_constructible_v<T>) {
				ptr = std::make_unique<T>();
				if constexpr (is_direct_serializable_v<T>) {
					read_primitive_templated(ar, *ptr);
				} else if constexpr (has_static_type_v<T>) {
					jai_static_type<T>::load(ar, *ptr);
				} else if constexpr (is_property_owner_v<T>) {
					std::string type_name;
					uint32_t version;
					ar.begin_object(type_name, version);
					ptr->property_mgr.load(ar);
					ar.end_object();
				} else {
					// Fallback: use read_custom which handles both load() and serialize() types
					// and unwraps begin_object/end_object consistently
					ar.read_custom(*ptr);
				}
			} else if constexpr (has_load_and_construct_unique_v<T>) {
				// Type is NOT default constructible - use load_and_construct pattern
				serialization::construct_unique<T> c(ptr);
				access::load_and_construct(ar, c);
			} else {
				// Type is NOT default constructible and has no load_and_construct
				throw std::runtime_error("Cannot deserialize unique_ptr<T>: type is not default constructible and has no load_and_construct");
			}
		} else {
			ptr.reset();
		}
	}

	// weak_ptr: Saves the ID of the shared_ptr it references
	// Binary format: [id:uint32] - just the ID, very compact
	// JSON format: {"_type_": "weak_ptr", "$id": id} - verbose for readability
	// NOTE: The shared_ptr must be serialized BEFORE the weak_ptr for this to work.
	//       If the shared_ptr hasn't been seen yet, the ID will be 0 (not found).
	template<typename Archive, typename T>
	inline void write_weak_ptr(Archive& ar, const std::weak_ptr<T>& ptr) {
		constexpr bool text_format = std::remove_reference_t<Archive>::is_text_format;

		uint32_t id = 0;
		if (auto shared = ptr.lock()) {
			id = ar.lookup_shared_id(shared.get());
		}

		if constexpr (text_format) {
			// JSON: verbose object format
			ar.begin_object("weak_ptr", 0);
			ar(serialization::make_nvp("$id", id));
			ar.end_object();
		} else {
			// Binary: just the ID
			ar.write_uint32(id);
		}
	}

	template<typename Archive, typename T>
	inline void read_weak_ptr(Archive& ar, std::weak_ptr<T>& ptr) {
		constexpr bool text_format = std::remove_reference_t<Archive>::is_text_format;

		uint32_t id = 0;

		if constexpr (text_format) {
			// JSON: verbose object format
			std::string type_name;
			uint32_t version;
			ar.begin_object(type_name, version);
			ar(serialization::make_nvp("$id", id));
			ar.end_object();
		} else {
			// Binary: just the ID
			id = ar.read_uint32();
		}

		if (id == 0) {
			ptr.reset();
			return;
		}

		// Look up the shared_ptr by ID and create weak_ptr from it
		if (auto shared = ar.get_deserialized_shared<T>(id)) {
			ptr = shared;
		} else {
			// ID not found - the shared_ptr wasn't deserialized yet or doesn't exist
			ptr.reset();
		}
	}

} // namespace property_serialization
} // namespace jai

// ============================================================================
// Property serialization implementations using type-erased archives
// ============================================================================
// The virtual serialize() overloads use any_archive_writer/reader for polymorphic
// dispatch through property_base*. The concrete archive type is wrapped at the
// call site (property_manager) to enable compile-time dispatch in user types.

namespace jai {

	// Compile-time check: is this type serializable?
	// Supported: primitives, containers, smart pointers, JAI_STATIC_BINDER, property_owner
	template<typename T>
	inline constexpr bool is_type_serializable_v =
		property_serialization::is_direct_serializable_v<T> ||
		property_serialization::is_std_vector_v<T> ||
		property_serialization::is_std_map_v<T> ||
		property_serialization::is_std_unordered_map_v<T> ||
		property_serialization::is_std_array_v<T> ||
		property_serialization::is_std_pair_v<T> ||
		property_serialization::is_smart_ptr_v<T> ||
		has_static_type_v<T> ||
		property_serialization::is_property_owner_v<T>;

	// property<T>::serialize(any_archive_writer&) - save via type-erased interface
	template<typename T>
	inline void property<T>::serialize(serialization::any_archive_writer& ar) const {
		// Skip if marked as transient (compile-time decision)
		if (m_serialize_mode == serialize_mode::transient) {
			return;
		}

		// Skip if dynamically disabled (runtime decision)
		if (!m_allow_serialization) {
			return;
		}

		// Write property name then value via type-erased dispatch
		ar.write_property_name(name());
		property_serialization::dispatch_save_erased(ar, m_value);
	}

	// property<T>::serialize(any_archive_reader&) - load via type-erased interface
	template<typename T>
	inline void property<T>::serialize(serialization::any_archive_reader& ar) {
		// Note: property_manager has already positioned to this property
		// Just read the value via type-erased dispatch
		property_serialization::dispatch_load_erased(ar, m_value);
	}

	// deleted_property<T>::serialize(any_archive_writer&) - no-op
	template<typename T>
	inline void deleted_property<T>::serialize(serialization::any_archive_writer& ar) const {
		// No-op: deleted properties don't save
	}

	// deleted_property<T>::serialize(any_archive_reader&) - skip value
	template<typename T>
	inline void deleted_property<T>::serialize(serialization::any_archive_reader& ar) {
		// Skip the deleted property by reading and discarding
		if constexpr (std::is_default_constructible_v<T>) {
			T dummy{};
			property_serialization::dispatch_load_erased(ar, dummy);
		}
		// If not default constructible, we can't skip properly with type-erased interface
		// The archive should handle this at a higher level
	}

	// ============================================================================
	// property_manager templated serialization
	// ============================================================================
	// Archive is the concrete CRTP type. We wrap it in any_archive_writer/reader
	// for polymorphic dispatch through property_base* while maintaining compile-time
	// optimization in the concrete archive.

	template<typename Archive>
	void property_manager::save(Archive& ar) const {
		// Create type-erased wrapper for polymorphic dispatch
		serialization::any_archive_writer type_erased(ar);

		// Collect and write properties that should be saved
		for (const auto& [name, prop] : m_properties) {
			if (prop->allow_save()) {
				prop->serialize(type_erased);
			}
		}
	}

	template<typename Archive>
	void property_manager::load(Archive& ar) {
		// Create type-erased wrapper for polymorphic dispatch
		serialization::any_archive_reader type_erased(ar);

		// Read properties based on archive format (compile-time branch)
		if constexpr (Archive::needs_property_keys) {
			// Binary format: iterate through pre-read property names
			const auto& prop_names = ar.get_object_property_names();
			for (size_t i = 0; i < prop_names.size(); ++i) {
				const std::string& prop_name = prop_names[i];
				if (ar.seek_property_by_index(i)) {
					auto it = m_properties.find(prop_name);
					if (it != m_properties.end()) {
						it->second->serialize(type_erased);
					}
					// Unknown properties are skipped by end_object
				}
			}
		} else {
			// JSON format: loop until no more properties
			std::string property_name;
			while (type_erased.read_property_name(property_name)) {
				auto it = m_properties.find(property_name);
				if (it != m_properties.end()) {
					it->second->serialize(type_erased);
				}
				// Unknown properties should be skipped - but type-erased reader
				// doesn't have read_value(). For now, JSON must match properties.
			}
		}
	}

} // namespace jai

// ============================================================================
// ADL-findable serialization functions for smart pointers
// Templated on Archive to preserve concrete type for compile-time format detection.
// See JAI_ARCHIVE_DEVIRTUALIZATION.md for design rationale.
//
// IMPORTANT: All functions use JAI_ONLY_ARCHIVE constraint to ensure they only
// match JaiScript archives. This prevents conflicts with Cereal which also uses
// ADL to find free save/load functions.
// ============================================================================
namespace jai {
namespace serialization {

	// shared_ptr save - ADL wrapper for property_serialization::write_shared_ptr
	template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
	void save(Archive& ar, const std::shared_ptr<T>& ptr) {
		property_serialization::write_shared_ptr(ar, ptr);
	}

	// shared_ptr load - ADL wrapper for property_serialization::read_shared_ptr
	template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
	void load(Archive& ar, std::shared_ptr<T>& ptr) {
		property_serialization::read_shared_ptr(ar, ptr);
	}

	// shared_ptr serialize (write)
	template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
	void serialize(Archive& ar, const std::shared_ptr<T>& ptr) {
		property_serialization::write_shared_ptr(ar, ptr);
	}

	// shared_ptr serialize (read)
	template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
	void serialize(Archive& ar, std::shared_ptr<T>& ptr) {
		property_serialization::read_shared_ptr(ar, ptr);
	}

	// weak_ptr save - ADL wrapper for property_serialization::write_weak_ptr
	template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
	void save(Archive& ar, const std::weak_ptr<T>& ptr) {
		property_serialization::write_weak_ptr(ar, ptr);
	}

	// weak_ptr load - ADL wrapper for property_serialization::read_weak_ptr
	template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
	void load(Archive& ar, std::weak_ptr<T>& ptr) {
		property_serialization::read_weak_ptr(ar, ptr);
	}

	// weak_ptr serialize (write)
	template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
	void serialize(Archive& ar, const std::weak_ptr<T>& ptr) {
		property_serialization::write_weak_ptr(ar, ptr);
	}

	// weak_ptr serialize (read)
	template<typename Archive, typename T, JAI_ONLY_ARCHIVE>
	void serialize(Archive& ar, std::weak_ptr<T>& ptr) {
		property_serialization::read_weak_ptr(ar, ptr);
	}

} // namespace serialization
} // namespace jai
