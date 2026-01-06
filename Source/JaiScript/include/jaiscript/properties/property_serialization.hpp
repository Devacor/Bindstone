#pragma once

#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/static_binder.hpp>
#include <vector>
#include <map>
#include <unordered_map>
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

	// --- Member serialize() detection (cereal-style bidirectional) ---
	// Detects: t.serialize(archive_writer&) OR t.serialize(archive_reader&)
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

	// --- Free function serialize() detection (cereal-style bidirectional via ADL) ---
	// Detects: serialize(archive_writer&, T&) or serialize(archive_reader&, T&)
	template<typename T, typename = void>
	struct has_free_serialize_save : std::false_type {};

	template<typename T>
	struct has_free_serialize_save<T, std::void_t<
		decltype(serialize(std::declval<serialization::archive_writer&>(), std::declval<T&>()))
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
	// For save: member save() OR member serialize() OR free save() OR free serialize()
	template<typename T>
	inline constexpr bool has_any_save_v =
		has_member_save_v<T> || has_member_serialize_save_v<T> ||
		has_free_save_v<T> || has_free_serialize_save_v<T>;

	// For load: member load() OR member serialize() OR free load() OR free serialize()
	template<typename T>
	inline constexpr bool has_any_load_v =
		has_member_load_v<T> || has_member_serialize_load_v<T> ||
		has_free_load_v<T> || has_free_serialize_load_v<T>;

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
		if constexpr (has_member_save_v<T>) {
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
			// cereal-style member serialize()
			const_cast<T&>(value).serialize(ar);
		} else if constexpr (has_free_serialize_save_v<T>) {
			// cereal-style free serialize()
			serialize(ar, const_cast<T&>(value));
		} else {
			// No serialization support - compile-time error
			static_assert(
				has_member_save_v<T> || has_free_save_v<T> ||
				has_static_type_v<T> || is_property_owner_v<T> ||
				has_member_serialize_save_v<T> || has_free_serialize_save_v<T>,
				"Type has no serialization support. Provide one of: "
				"T::save(archive_writer&), "
				"save(archive_writer&, const T&), "
				"JAI_STATIC_BINDER(T, ...), "
				"inherit from property_owner<T>, "
				"T::serialize(Archive&), or "
				"serialize(Archive&, T&)");
		}
	}

	// Load dispatcher
	template<typename T>
	void dispatch_load(serialization::archive_reader& ar, T& value) {
		if constexpr (has_member_load_v<T>) {
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
			// cereal-style member serialize()
			value.serialize(ar);
		} else if constexpr (has_free_serialize_load_v<T>) {
			// cereal-style free serialize()
			serialize(ar, value);
		} else {
			// No deserialization support - compile-time error
			static_assert(
				has_member_load_v<T> || has_free_load_v<T> ||
				has_static_type_v<T> || is_property_owner_v<T> ||
				has_member_serialize_load_v<T> || has_free_serialize_load_v<T>,
				"Type has no deserialization support. Provide one of: "
				"T::load(archive_reader&), "
				"load(archive_reader&, T&), "
				"JAI_STATIC_BINDER(T, ...), "
				"inherit from property_owner<T>, "
				"T::serialize(Archive&), or "
				"serialize(Archive&, T&)");
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

	// Check if a type is a primitive that we can directly serialize
	template<typename T>
	constexpr bool is_direct_serializable_v =
		std::is_arithmetic_v<T> ||
		std::is_same_v<T, std::string> ||
		std::is_same_v<T, char> ||
		std::is_same_v<T, unsigned char>;

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

} // namespace property_serialization
} // namespace jai

// Now implement property<T>::save() and load() using the helpers
namespace jai {

	// Compile-time check: is this type serializable?
	template<typename T>
	inline constexpr bool is_type_serializable_v =
		property_serialization::is_direct_serializable_v<T> ||
		property_serialization::is_std_vector_v<T> ||
		property_serialization::is_std_map_v<T> ||
		property_serialization::has_any_save_v<T>;

	template<typename T>
	inline void property<T>::save(serialization::archive_writer& ar) const {
		if (!m_allow_serialization) {
			return;
		}

		// Handle different type categories
		if constexpr (property_serialization::is_direct_serializable_v<T>) {
			// Direct primitives and strings
			ar.write_property_name(name());
			property_serialization::write_primitive(ar, m_value);
		}
		else if constexpr (property_serialization::is_std_vector_v<T>) {
			// Vectors
			ar.write_property_name(name());
			property_serialization::write_container(ar, m_value);
		}
		else if constexpr (property_serialization::is_std_map_v<T>) {
			// Maps
			ar.write_property_name(name());
			property_serialization::write_map(ar, m_value);
		}
		else if constexpr (property_serialization::has_any_save_v<T>) {
			// Custom types with save support (member or free function)
			ar.write_property_name(name());
			property_serialization::dispatch_save(ar, m_value);
		}
		// else: Type has no serialization support - silently skip
		// (This allows properties of non-serializable types to exist without breaking compilation)
	}

	template<typename T>
	inline void property<T>::load(serialization::archive_reader& ar) {
		// Note: property_manager::load() has already read the property name
		// We just need to read the value from the archive

		// Handle different type categories
		if constexpr (property_serialization::is_direct_serializable_v<T>) {
			// Direct primitives and strings
			property_serialization::read_primitive(ar, m_value);
		}
		else if constexpr (property_serialization::is_std_vector_v<T>) {
			// Vectors
			property_serialization::read_vector(ar, m_value);
		}
		else if constexpr (property_serialization::is_std_map_v<T>) {
			// Maps
			property_serialization::read_map(ar, m_value);
		}
		else if constexpr (property_serialization::has_any_load_v<T>) {
			// Custom types with load support (member or free function)
			property_serialization::dispatch_load(ar, m_value);
		}
		else {
			// Type has no deserialization support - skip the value in the archive
			ar.read_value();
		}
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

		// Read and discard the value based on type
		if constexpr (property_serialization::is_direct_serializable_v<T>) {
			T dummy;
			property_serialization::read_primitive(ar, dummy);
		}
		else if constexpr (property_serialization::is_std_vector_v<T>) {
			std::vector<property_serialization::vector_element_t<T>> dummy;
			property_serialization::read_vector(ar, dummy);
		}
		else if constexpr (property_serialization::is_std_map_v<T>) {
			std::map<property_serialization::map_key_t<T>, property_serialization::map_value_t<T>> dummy;
			property_serialization::read_map(ar, dummy);
		}
		else if constexpr (property_serialization::has_any_load_v<T>) {
			// Custom types with load support - read and discard
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
