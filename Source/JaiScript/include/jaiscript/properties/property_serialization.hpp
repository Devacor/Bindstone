#pragma once

#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/core/value.hpp>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <memory>
#include <stdexcept>

namespace jai {
namespace property_serialization {

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

	template<typename T>
	inline void property<T>::save(serialization::archive_writer& ar) const {
		if (!m_allow_serialization) {
			return;
		}

		ar.write_property_name(name());

		// Handle different type categories
		if constexpr (property_serialization::is_direct_serializable_v<T>) {
			// Direct primitives and strings
			property_serialization::write_primitive(ar, m_value);
		}
		else if constexpr (std::is_same_v<T, std::vector<typename T::value_type>>) {
			// Vectors
			property_serialization::write_container(ar, m_value);
		}
		else if constexpr (std::is_same_v<T, std::map<typename T::key_type, typename T::mapped_type>>) {
			// Maps
			property_serialization::write_map(ar, m_value);
		}
		else {
			// For custom types, they must provide their own serialization
			// This will be a compile error with a helpful message
			static_assert(sizeof(T) == 0,
				"Property type must be directly serializable (primitive/string) or a container of such types. "
				"For custom types, you must implement custom serialization logic.");
		}
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
		else if constexpr (std::is_same_v<T, std::vector<typename T::value_type>>) {
			// Vectors
			property_serialization::read_vector(ar, m_value);
		}
		else if constexpr (std::is_same_v<T, std::map<typename T::key_type, typename T::mapped_type>>) {
			// Maps
			property_serialization::read_map(ar, m_value);
		}
		else {
			static_assert(sizeof(T) == 0,
				"Property type must be directly serializable (primitive/string) or a container of such types. "
				"For custom types, you must implement custom serialization logic.");
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
		else if constexpr (std::is_same_v<T, std::vector<typename T::value_type>>) {
			std::vector<typename T::value_type> dummy;
			property_serialization::read_vector(ar, dummy);
		}
		else if constexpr (std::is_same_v<T, std::map<typename T::key_type, typename T::mapped_type>>) {
			std::map<typename T::key_type, typename T::mapped_type> dummy;
			property_serialization::read_map(ar, dummy);
		}
		else {
			// For custom types, just skip reading
			// The archive should handle skipping unknown types
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
