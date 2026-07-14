#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <typeindex>
#include <mutex>

namespace jai {

// Forward declarations
class property_base;

// ============================================================================
// Property Metadata (stored once per type, not per instance)
// ============================================================================
// Identity + flags only: typed value access goes through the live property_base
// erased bridge (value_type_id/value_as/assign_value), never through the schema.

struct property_meta {
	std::string name;
	std::type_index value_type_id;     // typeid(T) - the actual value type
	bool default_allow_serialization = true;
	bool is_observable = false;        // True if this is an observable_property
	bool is_signal = false;            // True for JAI_SIGNAL_PROPERTY (binds the generic Signal view)

	// Default constructor for containers
	property_meta()
		: value_type_id(typeid(void)) {}

	property_meta(std::string n, std::type_index value_ti, bool allow_ser, bool observable, bool signal_prop = false)
		: name(std::move(n))
		, value_type_id(value_ti)
		, default_allow_serialization(allow_ser)
		, is_observable(observable)
		, is_signal(signal_prop) {
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
// Sanctioned process-wide static (like the polymorphic registry): type identity is
// per-binary, not per-engine. Guarded — first-touch inserts can race across engine
// threads constructing property_owners concurrently.

class type_registry {
public:
	static type_registry& instance() {
		static type_registry registry;
		return registry;
	}

	// Get or create schema for a type
	type_property_schema& for_type(std::type_index ti) {
		std::scoped_lock guard(mutex_);
		return schemas_[ti];
	}

	// Get schema if it exists
	const type_property_schema* try_get(std::type_index ti) const {
		std::scoped_lock guard(mutex_);
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
		std::scoped_lock guard(mutex_);
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
		std::scoped_lock guard(mutex_);
		auto it = schemas_.find(ti);
		if (it != schemas_.end()) {
			return it->second.base_types();
		}
		return {};
	}

	template<typename T>
	std::vector<std::type_index> base_classes() const {
		return base_classes(std::type_index(typeid(T)));
	}

	// Check if Derived inherits from Base (directly or indirectly)
	bool inherits_from(std::type_index derived, std::type_index base) const {
		std::scoped_lock guard(mutex_);
		return inherits_from_locked(derived, base);
	}

	template<typename Derived, typename Base>
	bool inherits_from() const {
		return inherits_from(std::type_index(typeid(Derived)), std::type_index(typeid(Base)));
	}

private:
	type_registry() = default;

	bool inherits_from_locked(std::type_index derived, std::type_index base) const {
		if (derived == base) return true;

		auto it = schemas_.find(derived);
		if (it == schemas_.end()) return false;

		for (const auto& direct_base : it->second.base_types()) {
			if (inherits_from_locked(direct_base, base)) {
				return true;
			}
		}
		return false;
	}

	void collect_properties_recursive(std::type_index ti, std::vector<const property_meta*>& result) const {
		auto it = schemas_.find(ti);
		if (it == schemas_.end()) return;

		// First collect base class properties (so they come first in order)
		for (const auto& base_ti : it->second.base_types()) {
			collect_properties_recursive(base_ti, result);
		}

		// Then add own properties
		for (const auto& prop : it->second.own_properties()) {
			result.push_back(&prop);
		}
	}

	mutable std::mutex mutex_;
	std::map<std::type_index, type_property_schema> schemas_;
};

} // namespace jai
