#pragma once

#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/serialization/archive_impl.hpp>
#include <jaiscript/serialization/serialization_metadata.hpp>
#include <jaiscript/serialization/construct.hpp>
#include <jaiscript/serialization/archive_dispatch.hpp>
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
		// Priority 7: shared_ptr - uses ID-based deduplication
		else if constexpr (is_std_shared_ptr_v<T>) {
			auto [id, is_new] = ar.get_or_assign_shared_id(value.get());

			// Write ID first
			ar.write_uint32(id);

			// If new and non-null, serialize the element
			if (is_new && value) {
				dispatch_save_erased(ar, *value);
			}
		}
		// Priority 8: unique_ptr
		else if constexpr (is_std_unique_ptr_v<T>) {
			ar.write_bool(value != nullptr);
			if (value) {
				dispatch_save_erased(ar, *value);
			}
		}
		// Priority 9: weak_ptr - just writes whether it's valid (actual tracking done elsewhere)
		else if constexpr (is_std_weak_ptr_v<T>) {
			// weak_ptr serialization is tricky in type-erased context
			// For now, just mark as expired/valid
			ar.write_bool(!value.expired());
		}
		// Priority 10: property_owner types - serialize via their property_mgr
		else if constexpr (is_property_owner_v<T>) {
			ar.begin_object("", 0);  // Type name not needed in type-erased context
			for (const auto& [name, prop] : value.property_mgr.all()) {
				if (prop->allow_save()) {
					prop->serialize(ar);  // Calls virtual serialize(any_archive_writer&)
				}
			}
			ar.end_object();
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
		// Priority 7: shared_ptr - uses ID-based deduplication
		else if constexpr (is_std_shared_ptr_v<T>) {
			using elem_type = shared_ptr_element_t<T>;
			uint32_t id = ar.read_uint32();

			if (id == 0) {
				value.reset();
				return;
			}

			// Check if already deserialized
			if (ar.has_deserialized_shared(id)) {
				value = ar.get_deserialized_shared<elem_type>(id);
				return;
			}

			// New - construct and deserialize
			if constexpr (std::is_default_constructible_v<elem_type>) {
				value = std::make_shared<elem_type>();
				dispatch_load_erased(ar, *value);
				ar.register_deserialized_shared(id, value);
			} else {
				throw std::runtime_error("Cannot deserialize shared_ptr<T>: element type is not default constructible");
			}
		}
		// Priority 8: unique_ptr
		else if constexpr (is_std_unique_ptr_v<T>) {
			using elem_type = unique_ptr_element_t<T>;
			bool has_value = ar.read_bool();
			if (has_value) {
				if constexpr (std::is_default_constructible_v<elem_type>) {
					value = std::make_unique<elem_type>();
					dispatch_load_erased(ar, *value);
				} else {
					throw std::runtime_error("Cannot deserialize unique_ptr<T>: element type is not default constructible");
				}
			} else {
				value.reset();
			}
		}
		// Priority 9: weak_ptr - just reads validity (actual resolution done elsewhere)
		else if constexpr (is_std_weak_ptr_v<T>) {
			// Read and discard the validity flag - weak_ptr resolution is complex in type-erased context
			ar.read_bool();
			value.reset();
		}
		// Priority 10: property_owner types - deserialize via their property_mgr
		else if constexpr (is_property_owner_v<T>) {
			std::string type_name;
			uint32_t version;
			ar.begin_object(type_name, version);
			// Read properties - the archive will iterate through them
			std::string property_name;
			while (ar.read_property_name(property_name)) {
				auto* prop = value.property_mgr.get(property_name);
				if (prop) {
					prop->serialize(ar);  // Calls virtual serialize(any_archive_reader&)
				}
				// Unknown properties are skipped
			}
			ar.end_object();
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
	//
	// Uses ar(*ptr) uniformly - the archive handles type detection via write_custom()
	// which checks for custom save()/serialize() methods before falling back to property_mgr.
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
				bool did_poly = false;
				if constexpr (std::is_polymorphic_v<T>) {
					const auto& actual_type = typeid(*ptr);
					if (actual_type != typeid(T)) {
						auto* entry = serialization::polymorphic_registry::instance().find(std::type_index(actual_type));
						if (entry && entry->save_fn) {
							ar.serialize("$type", entry->name);
							ar.write_property_name("$val");
							ar.begin_object();
							serialization::any_archive_writer wrapper(ar);
							entry->save_fn(wrapper, ptr.get());
							ar.end_object();
							did_poly = true;
						}
					}
				}
				if (!did_poly) {
					ar.serialize("$val", *ptr);
				}
			}
			ar.end_object();
		} else {
			// Binary: compact format - just ID and optional object
			ar.write_uint32(id);

			if (is_new && ptr) {
				ar(*ptr);
			}
		}
	}

	// Read shared_ptr with ID-based de-duplication
	// Binary format: [id:uint32][object data if new and non-null] - compact
	// JSON format: {"_type_": "ptr", "$id": id, "$val": object} - verbose
	//
	// Uses ar(*ptr) uniformly for default-constructible types.
	// For non-default-constructible types, uses load_and_construct.
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
				ptr = ar.template get_deserialized_shared<T>(id);
				ar.end_object();
				return;
			}

			// Check for polymorphic type discriminator
			bool did_poly = false;
			if constexpr (std::is_polymorphic_v<T>) {
				std::string poly_type;
				if (ar.has_property("$type")) {
					ar.serialize("$type", poly_type);
				}
				if (!poly_type.empty()) {
					auto* entry = serialization::polymorphic_registry::instance().find(poly_type);
					if (entry && entry->load_fn) {
						if (!ar.seek_property("$val")) {
							throw std::runtime_error("Expected '$val' for polymorphic shared_ptr");
						}
						ar.begin_object();
						serialization::any_archive_reader wrapper(ar);
						// Pass id so load_fn registers the object eagerly (before its
						// properties load), enabling subtree back-references to resolve.
						auto void_ptr = entry->load_fn(wrapper, id);
						ptr = std::static_pointer_cast<T>(void_ptr);
						ar.end_object();
						did_poly = true;
					} else {
						throw std::runtime_error("Unknown polymorphic type: " + poly_type);
					}
				}
			}

			if (!did_poly) {
				// Non-polymorphic: existing behavior
				if constexpr (has_member_load_and_construct_v<T>) {
					std::string prop_name;
					ar.read_property_name(prop_name);
					std::string lac_type_name;
					uint32_t lac_version;
					ar.begin_object(lac_type_name, lac_version);
					serialization::construct<T> c(ptr);
					access::load_and_construct(ar, c);
					ar.end_object();
				} else if constexpr (std::is_default_constructible_v<T>) {
					ptr = std::make_shared<T>();
					ar.serialize("$val", *ptr);
				} else {
					throw std::runtime_error("Cannot deserialize shared_ptr<T>: type is not default constructible and has no load_and_construct");
				}
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
				ptr = ar.template get_deserialized_shared<T>(id);
				return;
			}

			// First time - read object directly
			if constexpr (has_member_load_and_construct_v<T>) {
				serialization::construct<T> c(ptr);
				access::load_and_construct(ar, c);
			} else if constexpr (std::is_default_constructible_v<T>) {
				ptr = std::make_shared<T>();
				ar(*ptr);
			} else {
				throw std::runtime_error("Cannot deserialize shared_ptr<T>: type is not default constructible and has no load_and_construct");
			}

			ar.register_deserialized_shared(id, ptr);
		}
	}

	// Write unique_ptr - format: {"$val": {...}} or null (matches archive.hpp)
	// Binary: compact bool + object format
	template<typename Archive, typename T>
	inline void write_unique_ptr(Archive& ar, const std::unique_ptr<T>& ptr) {
		constexpr bool text_format = std::remove_reference_t<Archive>::is_text_format;

		if constexpr (text_format) {
			if (!ptr) {
				ar.write_null();
			} else {
				ar.begin_object();
				ar.serialize("$val", *ptr);
				ar.end_object();
			}
		} else {
			if (ptr) {
				ar.write_bool(true);
				ar(*ptr);
			} else {
				ar.write_bool(false);
			}
		}
	}

	// Read unique_ptr - format: {"$val": {...}} or null (matches archive.hpp)
	// Binary: compact bool + object format
	template<typename Archive, typename T>
	inline void read_unique_ptr(Archive& ar, std::unique_ptr<T>& ptr) {
		constexpr bool text_format = std::remove_reference_t<Archive>::is_text_format;

		if constexpr (text_format) {
			if (ar.peek_null()) {
				ar.read_null();
				ptr.reset();
				return;
			}
			ar.begin_object();
			if constexpr (has_load_and_construct_unique_v<T>) {
				serialization::construct_unique<T> c(ptr);
				access::load_and_construct(ar, c);
			} else if constexpr (std::is_default_constructible_v<T>) {
				ptr = std::make_unique<T>();
				ar.serialize("$val", *ptr);
			} else {
				throw std::runtime_error("Cannot deserialize unique_ptr<T>: type is not default constructible and has no load_and_construct");
			}
			ar.end_object();
		} else {
			bool has_value = ar.read_bool();
			if (has_value) {
				if constexpr (has_load_and_construct_unique_v<T>) {
					serialization::construct_unique<T> c(ptr);
					access::load_and_construct(ar, c);
				} else if constexpr (std::is_default_constructible_v<T>) {
					ptr = std::make_unique<T>();
					ar(*ptr);
				} else {
					throw std::runtime_error("Cannot deserialize unique_ptr<T>: type is not default constructible and has no load_and_construct");
				}
			} else {
				ptr.reset();
			}
		}
	}

	// weak_ptr: Saves the ID of the shared_ptr it references
	// Binary format: [id:uint32] - just the ID, very compact
	// JSON format: {"$ref": id} or null - matches archive.hpp format
	// NOTE: The shared_ptr must be serialized BEFORE the weak_ptr for this to work.
	//       If the shared_ptr hasn't been seen yet, the ID will be 0 (not found).
	template<typename Archive, typename T>
	inline void write_weak_ptr(Archive& ar, const std::weak_ptr<T>& ptr) {
		constexpr bool text_format = std::remove_reference_t<Archive>::is_text_format;

		if constexpr (text_format) {
			if (auto shared = ptr.lock()) {
				uint32_t id = ar.lookup_shared_id(shared.get());
				ar.begin_object();
				ar.serialize("$ref", id);
				ar.end_object();
			} else {
				ar.write_null();
			}
		} else {
			uint32_t id = 0;
			if (auto shared = ptr.lock()) {
				id = ar.lookup_shared_id(shared.get());
			}
			ar.write_uint32(id);
		}
	}

	template<typename Archive, typename T>
	inline void read_weak_ptr(Archive& ar, std::weak_ptr<T>& ptr) {
		constexpr bool text_format = std::remove_reference_t<Archive>::is_text_format;

		uint32_t id = 0;

		if constexpr (text_format) {
			if (ar.peek_null()) {
				ar.read_null();
				ptr.reset();
				return;
			}
			ar.begin_object();
			ar.serialize("$ref", id);
			ar.end_object();
		} else {
			id = ar.read_uint32();
		}

		if (id == 0) {
			ptr.reset();
			return;
		}

		// Look up the shared_ptr by ID and create weak_ptr from it
		if (auto shared = ar.template get_deserialized_shared<T>(id)) {
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

	// property<T>::serialize(any_archive_writer&) - save via dispatch pattern
	// Uses dispatch() to recover concrete archive type for full-fidelity serialization
	// This enables shared_ptr, load_and_construct, and custom save methods to work properly
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

		// Use concrete archive's serialize(name, value) which properly manages context:
		// - Writes property name
		// - Pushes PropertyValue context
		// - Calls write_value() which wraps types with serialize methods in begin_object/end_object
		// - Pops context after writing
		ar.dispatch([this](auto& concrete_ar) {
			concrete_ar.serialize(name().c_str(), m_value);
		});
	}

	// property<T>::serialize(any_archive_reader&) - load via dispatch pattern
	// Uses dispatch() to recover concrete archive type for full-fidelity deserialization
	template<typename T>
	inline void property<T>::serialize(serialization::any_archive_reader& ar) {
		// Note: property_manager has already positioned to this property (read the name).
		// We're now at the VALUE, which for complex types is wrapped in {}.
		// Use read_custom() which properly handles:
		// - Types with serialize() methods: calls begin_object/end_object
		// - STL containers and primitives: delegates to operator()
		ar.dispatch([&](auto& concrete_ar) {
			concrete_ar.read_custom(m_value);
		});
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
		} else {
			// For non-default-constructible types, read as a generic script_value to
			// advance the stream past this property's data and prevent corruption
			ar.dispatch([](auto& concrete_ar) {
				script_value dummy_val;
				concrete_ar.read_custom(dummy_val);
			});
		}
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
			// Binary format: iterate through pre-read property names. COPY, not reference:
			// loading a nested property pushes onto the reader's object stack (a vector),
			// and reallocation would invalidate a reference into the current top.
			const std::vector<std::string> prop_names = ar.get_object_property_names();
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
			// JSON format: seek each known property by name (map lookup).
			// This avoids consuming the sequential iterator, which is critical
			// when multiple property_mgrs at different inheritance levels
			// (Sprite → Drawable → Component) read from the same flat object.
			for (const auto& [name, prop] : m_properties) {
				if (type_erased.seek_property(name)) {
					prop->serialize(type_erased);
				}
			}
			type_erased.clear_property_value();
		}
	}

} // namespace jai

// ============================================================================
// Smart pointer serialization is handled internally by the archive's operator()
// No ADL free functions needed - this avoids collisions with Cereal.
// See write_shared_ptr/read_shared_ptr and write_weak_ptr/read_weak_ptr above.
// ============================================================================
