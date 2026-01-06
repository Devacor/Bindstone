#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <typeindex>
#include <functional>
#include <memory>
#include <tuple>

namespace jai {

// Forward declarations
class property_base;

namespace serialization {
	class archive_writer;
	class archive_reader;
}

// ============================================================================
// Property Metadata (stored once per type, not per instance)
// ============================================================================

struct property_meta {
	std::string name;
	std::type_index type_id;           // typeid(property<T>)
	std::type_index value_type_id;     // typeid(T) - the actual value type
	size_t offset_from_owner;          // Byte offset from owner to property<T>*
	bool default_allow_serialization = true;
	bool is_observable = false;        // True if this is an observable_property

	// Type-erased accessors using offset (use void* for flexibility)
	property_base* get_property(void* owner) const;
	const property_base* get_property(const void* owner) const;

	// Type-erased getter: returns pointer to the underlying value (T*)
	// Uses property_mgr to find property by name
	std::function<void*(void* owner, const std::string& prop_name)> get_value_ptr;

	// Type-erased setter: sets the value from a void* source
	// For observable properties, this triggers the on_change signal
	std::function<void(void* owner, const std::string& prop_name, const void* value_ptr)> set_value;

	// Default constructor for containers
	property_meta()
		: type_id(typeid(void))
		, value_type_id(typeid(void))
		, offset_from_owner(0) {}

	// Full constructor with all fields
	property_meta(std::string n, std::type_index ti, std::type_index value_ti,
	              size_t offset, bool allow_ser, bool observable,
	              std::function<void*(void*, const std::string&)> getter,
	              std::function<void(void*, const std::string&, const void*)> setter)
		: name(std::move(n))
		, type_id(ti)
		, value_type_id(value_ti)
		, offset_from_owner(offset)
		, default_allow_serialization(allow_ser)
		, is_observable(observable)
		, get_value_ptr(std::move(getter))
		, set_value(std::move(setter)) {
	}

	// Legacy constructor for backwards compatibility
	property_meta(std::string n, std::type_index ti, size_t offset, bool allow_ser = true)
		: name(std::move(n))
		, type_id(ti)
		, value_type_id(typeid(void))
		, offset_from_owner(offset)
		, default_allow_serialization(allow_ser)
		, is_observable(false) {
	}

	// Constructor without lambdas - used by JAI_PROPERTY macros
	// The getter/setter are generated later in dynamic_binder when the type is complete
	property_meta(std::string n, std::type_index ti, std::type_index value_ti,
	              size_t offset, bool allow_ser, bool observable)
		: name(std::move(n))
		, type_id(ti)
		, value_type_id(value_ti)
		, offset_from_owner(offset)
		, default_allow_serialization(allow_ser)
		, is_observable(observable) {
	}
};

// ============================================================================
// Type Metadata (properties + inheritance info)
// ============================================================================

class type_property_schema {
public:
	type_property_schema() = default;

	// Register a property with this schema (properties declared directly on this type)
	void add(property_meta meta) {
		name_to_index_[meta.name] = own_properties_.size();
		own_properties_.push_back(std::move(meta));
	}

	// Register base class types
	void add_base(std::type_index base_type) {
		base_types_.push_back(base_type);
	}

	// Get properties declared directly on this type (not inherited)
	const std::vector<property_meta>& own_properties() const { return own_properties_; }

	// Get direct base class type indices
	const std::vector<std::type_index>& base_types() const { return base_types_; }

	// Find property by name (in own properties only)
	const property_meta* find(const std::string& name) const {
		auto it = name_to_index_.find(name);
		return it != name_to_index_.end() ? &own_properties_[it->second] : nullptr;
	}

	// Get own property names
	std::vector<std::string> property_names() const {
		std::vector<std::string> names;
		names.reserve(own_properties_.size());
		for (const auto& prop : own_properties_) {
			names.push_back(prop.name);
		}
		return names;
	}

	bool empty() const { return own_properties_.empty(); }
	size_t size() const { return own_properties_.size(); }
	bool has_bases() const { return !base_types_.empty(); }

private:
	std::vector<property_meta> own_properties_;
	std::vector<std::type_index> base_types_;
	std::unordered_map<std::string, size_t> name_to_index_;
};

// ============================================================================
// Global Type Registry
// ============================================================================

class type_registry {
public:
	static type_registry& instance() {
		static type_registry registry;
		return registry;
	}

	// Get or create schema for a type
	type_property_schema& for_type(std::type_index ti) {
		return schemas_[ti];
	}

	// Get schema if it exists
	const type_property_schema* try_get(std::type_index ti) const {
		auto it = schemas_.find(ti);
		return it != schemas_.end() ? &it->second : nullptr;
	}

	// Convenience template versions
	template<typename T>
	type_property_schema& for_type() {
		return for_type(std::type_index(typeid(T)));
	}

	template<typename T>
	const type_property_schema* try_get() const {
		return try_get(std::type_index(typeid(T)));
	}

	// Check if a type has a registered schema
	template<typename T>
	bool has_schema() const {
		return try_get<T>() != nullptr;
	}

	// Get all properties including inherited (walks base class chain)
	std::vector<const property_meta*> all_properties(std::type_index ti) const {
		std::vector<const property_meta*> result;
		collect_properties_recursive(ti, result);
		return result;
	}

	template<typename T>
	std::vector<const property_meta*> all_properties() const {
		return all_properties(std::type_index(typeid(T)));
	}

	// Get all property names including inherited
	std::vector<std::string> all_property_names(std::type_index ti) const {
		std::vector<std::string> result;
		auto props = all_properties(ti);
		result.reserve(props.size());
		for (const auto* p : props) {
			result.push_back(p->name);
		}
		return result;
	}

	template<typename T>
	std::vector<std::string> all_property_names() const {
		return all_property_names(std::type_index(typeid(T)));
	}

	// Get direct base classes
	std::vector<std::type_index> base_classes(std::type_index ti) const {
		if (auto* schema = try_get(ti)) {
			return schema->base_types();
		}
		return {};
	}

	template<typename T>
	std::vector<std::type_index> base_classes() const {
		return base_classes(std::type_index(typeid(T)));
	}

	// Check if Derived inherits from Base (directly or indirectly)
	bool inherits_from(std::type_index derived, std::type_index base) const {
		if (derived == base) return true;

		const auto* schema = try_get(derived);
		if (!schema) return false;

		for (const auto& direct_base : schema->base_types()) {
			if (inherits_from(direct_base, base)) {
				return true;
			}
		}
		return false;
	}

	template<typename Derived, typename Base>
	bool inherits_from() const {
		return inherits_from(std::type_index(typeid(Derived)), std::type_index(typeid(Base)));
	}

private:
	type_registry() = default;

	void collect_properties_recursive(std::type_index ti, std::vector<const property_meta*>& result) const {
		const auto* schema = try_get(ti);
		if (!schema) return;

		// First collect base class properties (so they come first in order)
		for (const auto& base_ti : schema->base_types()) {
			collect_properties_recursive(base_ti, result);
		}

		// Then add own properties
		for (const auto& prop : schema->own_properties()) {
			result.push_back(&prop);
		}
	}

	std::map<std::type_index, type_property_schema> schemas_;
};

// ============================================================================
// Helper to compute offset from owner to property member
// ============================================================================

template<typename OwnerT, typename PropT>
constexpr size_t compute_property_offset(PropT OwnerT::* member_ptr) {
	// Use offsetof-like calculation
	return reinterpret_cast<size_t>(&(static_cast<OwnerT*>(nullptr)->*member_ptr));
}

// ============================================================================
// Static Registration Helpers
// ============================================================================
//
// Note: property_owner CRTP template and related type traits are defined in
// property_manager.hpp. Use has_property_owner_v<T> to detect if a type uses
// the property system.

// Registers inheritance info during static initialization
template<typename Derived, typename... Bases>
struct type_inheritance_registrar {
	type_inheritance_registrar() {
		auto& schema = type_registry::instance().for_type<Derived>();
		(schema.add_base(std::type_index(typeid(Bases))), ...);
	}
};

// Registers a single property during static initialization
template<typename OwnerT>
struct property_registrar {
	template<typename PropT>
	property_registrar(const char* name, size_t offset, bool allow_serialization = true) {
		auto& schema = type_registry::instance().for_type<OwnerT>();
		schema.add(property_meta{
			name,
			std::type_index(typeid(PropT)),
			offset,
			allow_serialization
		});
	}
};

// ============================================================================
// Implementation
// ============================================================================

inline property_base* property_meta::get_property(void* owner) const {
	return reinterpret_cast<property_base*>(
		reinterpret_cast<char*>(owner) + offset_from_owner
	);
}

inline const property_base* property_meta::get_property(const void* owner) const {
	return reinterpret_cast<const property_base*>(
		reinterpret_cast<const char*>(owner) + offset_from_owner
	);
}

} // namespace jai
