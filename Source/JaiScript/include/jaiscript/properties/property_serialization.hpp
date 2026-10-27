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

	// The container and smart-pointer traits (serialization/traits.hpp)
	using serialization_traits::is_std_vector;
	using serialization_traits::is_std_vector_v;
	using serialization_traits::is_std_map;
	using serialization_traits::is_std_map_v;
	using serialization_traits::vector_element;
	using serialization_traits::vector_element_t;
	using serialization_traits::is_std_weak_ptr;
	using serialization_traits::is_std_weak_ptr_v;
	using serialization_traits::is_std_shared_ptr;
	using serialization_traits::is_std_shared_ptr_v;
	using serialization_traits::is_std_unique_ptr;
	using serialization_traits::is_std_unique_ptr_v;
	using serialization_traits::is_smart_ptr_v;
	using serialization_traits::is_std_unordered_map;
	using serialization_traits::is_std_unordered_map_v;
	using serialization_traits::is_std_array;
	using serialization_traits::is_std_array_v;
	using serialization_traits::is_std_pair;
	using serialization_traits::is_std_pair_v;
	using serialization_traits::pair_first;
	using serialization_traits::pair_first_t;
	using serialization_traits::pair_second;
	using serialization_traits::pair_second_t;
	using serialization_traits::weak_ptr_element;
	using serialization_traits::weak_ptr_element_t;
	using serialization_traits::shared_ptr_element;
	using serialization_traits::shared_ptr_element_t;
	using serialization_traits::unique_ptr_element;
	using serialization_traits::unique_ptr_element_t;
	using serialization_traits::array_element;
	using serialization_traits::array_element_t;
	using serialization_traits::array_size;
	using serialization_traits::array_size_v;

	// ============================================================================
	// Serialization support detection (CRTP-compatible)
	// ============================================================================

	// --- load_and_construct detection ---
	// Detects: T has a static load_and_construct(Archive&, construct<T>&) member
	// This pattern is used for types without default constructors
	namespace detail {
		// Dummy archive for trait detection (stubs let load_and_construct decltype succeed)
		struct detection_archive {
			static constexpr bool is_text_format = false;
			static constexpr bool is_jai_archive = true;

			template<typename T>
			T* get_user_context() const { return nullptr; }

			template<typename T>
			void serialize(const char*, T&) {}

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

	using serialization_traits::is_property_owner;
	using serialization_traits::is_property_owner_v;
	using serialization_traits::is_direct_serializable_v;

	// ============================================================================
	// Type-erased dispatch for property serialization
	// ============================================================================

	// Primitive write/read, for the type-erased archives and the CRTP ones alike

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

	template<typename T>
	void dispatch_save_erased(serialization::any_archive_writer& ar, const T& value) {
		if constexpr (is_direct_serializable_v<T>) {
			write_primitive_templated(ar, value);
		}
		else if constexpr (is_std_vector_v<T>) {
			using elem_type = vector_element_t<T>;
			ar.begin_array(value.size());
			for (const auto& elem : value) {
				dispatch_save_erased(ar, elem);
			}
			ar.end_array();
		}
		else if constexpr (is_std_map_v<T> || is_std_unordered_map_v<T>) {
			using key_type = typename T::key_type;
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
		else if constexpr (is_std_array_v<T>) {
			constexpr size_t N = array_size_v<T>;
			ar.begin_array(N);
			for (const auto& elem : value) {
				dispatch_save_erased(ar, elem);
			}
			ar.end_array();
		}
		else if constexpr (is_std_pair_v<T>) {
			ar.begin_array(2);
			dispatch_save_erased(ar, value.first);
			dispatch_save_erased(ar, value.second);
			ar.end_array();
		}
		else if constexpr (is_std_shared_ptr_v<T>) {
			auto [id, is_new] = ar.get_or_assign_shared_id(value.get());
			ar.write_uint32(id);
			if (is_new && value) {
				dispatch_save_erased(ar, *value);
			}
		}
		else if constexpr (is_std_unique_ptr_v<T>) {
			ar.write_bool(value != nullptr);
			if (value) {
				dispatch_save_erased(ar, *value);
			}
		}
		else if constexpr (is_std_weak_ptr_v<T>) {
			ar.write_bool(!value.expired());
		}
		else if constexpr (is_property_owner_v<T>) {
			ar.begin_object("", 0);  // Type name not needed in type-erased context
			value.visit_owned_properties([&ar](const std::string&, property_base* prop) {
				if (prop->allow_save()) {
					prop->serialize(ar);
				}
			});
			ar.end_object();
		}
		else {
			throw std::runtime_error("Type is not serializable via type-erased archive (use JAI_STATIC_BINDER, property_owner, or mark property as transient)");
		}
	}

	template<typename T>
	void dispatch_load_erased(serialization::any_archive_reader& ar, T& value) {
		if constexpr (is_direct_serializable_v<T>) {
			read_primitive_templated(ar, value);
		}
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
		else if constexpr (is_std_map_v<T> || is_std_unordered_map_v<T>) {
			using key_type = typename T::key_type;
			using val_type = typename T::mapped_type;
			size_t size = ar.begin_map();
			value.clear();
			if constexpr (is_std_unordered_map_v<T>) { value.reserve(size); }
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
		else if constexpr (is_std_array_v<T>) {
			constexpr size_t N = array_size_v<T>;
			size_t size = ar.begin_array();
			for (size_t i = 0; i < N && i < size; ++i) {
				dispatch_load_erased(ar, value[i]);
			}
			ar.end_array();
		}
		else if constexpr (is_std_pair_v<T>) {
			size_t size = ar.begin_array();
			if (size >= 1) dispatch_load_erased(ar, value.first);
			if (size >= 2) dispatch_load_erased(ar, value.second);
			ar.end_array();
		}
		else if constexpr (is_std_shared_ptr_v<T>) {
			using elem_type = shared_ptr_element_t<T>;
			uint32_t id = ar.read_uint32();

			if (id == 0) {
				value.reset();
				return;
			}

			if (ar.has_deserialized_shared(id)) {
				value = ar.get_deserialized_shared<elem_type>(id);
				return;
			}

			if constexpr (::jai::serialization::load_default_constructible<elem_type>) {
				value = ::jai::serialization::make_default_for_load<elem_type>();
				dispatch_load_erased(ar, *value);
				ar.register_deserialized_shared(id, value);
			} else {
				throw std::runtime_error("Cannot deserialize shared_ptr<T>: element type is not default constructible");
			}
		}
		else if constexpr (is_std_unique_ptr_v<T>) {
			using elem_type = unique_ptr_element_t<T>;
			bool has_value = ar.read_bool();
			if (has_value) {
				if constexpr (::jai::serialization::load_default_constructible<elem_type>) {
					value = ::jai::serialization::make_unique_default_for_load<elem_type>();
					dispatch_load_erased(ar, *value);
				} else {
					throw std::runtime_error("Cannot deserialize unique_ptr<T>: element type is not default constructible");
				}
			} else {
				value.reset();
			}
		}
		else if constexpr (is_std_weak_ptr_v<T>) {
			ar.read_bool();
			value.reset();
		}
		else if constexpr (is_property_owner_v<T>) {
			std::string type_name;
			uint32_t version;
			ar.begin_object(type_name, version);
			std::string property_name;
			while (ar.read_property_name(property_name)) {
				auto* prop = value.find_owned_property(property_name);
				if (prop) {
					prop->serialize(ar);
				}
			}
			ar.end_object();
		}
		else {
			throw std::runtime_error("Type is not deserializable via type-erased archive (use JAI_STATIC_BINDER, property_owner, or mark property as transient)");
		}
	}

	// ============================================================================
	// Smart pointer serialization (with ID-based de-duplication)
	// ============================================================================
	// shared_ptr: Uses ID tracking - same object serialized once, referenced by ID
	// weak_ptr: Saves the ID of the shared_ptr it references (for reconstruction)
	// unique_ptr: Always serializes the object (no sharing possible)

	template<typename T>
	inline constexpr bool is_element_serializable_v =
		is_direct_serializable_v<T> ||
		has_static_type_v<T> ||
		is_property_owner_v<T>;

	template<typename T>
	inline constexpr bool is_element_deserializable_v =
		is_direct_serializable_v<T> ||
		has_static_type_v<T> ||
		is_property_owner_v<T>;

} // namespace property_serialization
} // namespace jai

namespace jai {

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

	template<typename T>
	inline void property<T>::serialize(serialization::any_archive_writer& ar) const {
		if (m_serialize_mode == serialize_mode::transient) {
			return;
		}

		if (!m_allow_serialization) {
			return;
		}

		ar.dispatch([this](auto& concrete_ar) {
			concrete_ar.serialize(name().c_str(), m_value);
		});
	}

	template<typename T>
	inline void property<T>::serialize(serialization::any_archive_reader& ar) {
		// property_manager has already positioned to this property (read the name);
		// we're now at the VALUE
		ar.dispatch([&](auto& concrete_ar) {
			concrete_ar.read_custom(m_value);
		});
	}

	template<typename T>
	inline void deleted_property<T>::serialize(serialization::any_archive_writer& ar) const {
	}

	template<typename T>
	inline void deleted_property<T>::serialize(serialization::any_archive_reader& ar) {
		if constexpr (std::is_default_constructible_v<T>) {
			T dummy{};
			property_serialization::dispatch_load_erased(ar, dummy);
		} else {
			// For non-default-constructible types, read as a generic script_value to
			// advance the stream past this property's data and prevent corruption
			// (null-engine monostate spelling: the default ctor is private, and this
			// non-dependent statement is checked at template definition time)
			ar.dispatch([](auto& concrete_ar) {
				script_value dummy_val(std::monostate{}, static_cast<engine*>(nullptr));
				concrete_ar.read_custom(dummy_val);
			});
		}
	}

	// ============================================================================
	// property_manager templated serialization
	// ============================================================================

	template<typename Archive>
	void property_manager::save(Archive& ar) const {
		serialization::any_archive_writer type_erased(ar);

		for (const auto& [name, prop] : m_properties) {
			if (prop->allow_save()) {
				prop->serialize(type_erased);
			}
		}
	}

	template<typename Archive>
	void property_manager::load(Archive& ar) {
		serialization::any_archive_reader type_erased(ar);

		if constexpr (Archive::needs_property_keys) {
			// Binary: COPY, not reference — loading a nested property pushes onto the
			// reader's object stack (a vector); reallocation would invalidate a reference.
			const std::vector<std::string> prop_names = ar.get_object_property_names();
			for (size_t i = 0; i < prop_names.size(); ++i) {
				const std::string& prop_name = prop_names[i];
				if (ar.seek_property_by_index(i)) {
					auto it = m_properties.find(prop_name);
					if (it != m_properties.end()) {
						it->second->serialize(type_erased);
					}
				}
			}
		} else {
			// JSON: seek each known property by name — critical when multiple property_mgrs
			// at different inheritance levels (Sprite → Drawable → Component) read the same
			// flat object; consuming the sequential iterator would lose position.
			for (const auto& [name, prop] : m_properties) {
				if (type_erased.seek_property(name)) {
					prop->serialize(type_erased);
				}
			}
			type_erased.clear_property_value();
		}
	}

	// Instantiated once in the library (schema_binding.cpp) for the four archives, not in every TU with a save/load ladder
	extern template void property_manager::save<serialization::json_archive_writer>(serialization::json_archive_writer&) const;
	extern template void property_manager::save<serialization::binary_archive_writer>(serialization::binary_archive_writer&) const;
	extern template void property_manager::load<serialization::json_archive_reader>(serialization::json_archive_reader&);
	extern template void property_manager::load<serialization::binary_archive_reader>(serialization::binary_archive_reader&);

} // namespace jai
