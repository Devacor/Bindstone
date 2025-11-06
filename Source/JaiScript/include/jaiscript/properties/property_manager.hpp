#pragma once

#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <memory>
#include <jaiscript/properties/property.hpp>
#include <jaiscript/core/engine_bindable.hpp>

namespace jai {
	// Forward declarations
	class engine;

	namespace serialization {
		class archive_writer;
		class archive_reader;
	}

	class property_manager {
	public:
		property_manager() = default;
		property_manager(property_manager&&) = default;

		property_manager(const property_manager&) = delete;
		property_manager& operator=(const property_manager&) = delete;

		void add(property_base* prop);
		const std::map<std::string, property_base*>& all() const { return m_properties; }

		// Engine binding support
		void bind_to_engine(std::weak_ptr<engine> eng) {
			engine_ref_ = eng;
		}

		std::weak_ptr<engine> get_engine() const {
			return engine_ref_;
		}

		bool has_engine() const {
			return !engine_ref_.expired();
		}

		template<typename Fn>
		void visit(Fn&& func) const {
			for (const auto& [key, prop] : m_properties) {
				func(key, prop);
			}
		}

		inline property_base* get(const std::string& key) const;

		template<typename T>
		inline property<T>* get(const std::string& key) const;

		template<typename T>
		inline T* get_value(const std::string& key) const;

		// Serialization methods - implemented in property_serialization.hpp
		// Forward declared here to avoid circular dependency
		void save(serialization::archive_writer& ar) const;
		void load(serialization::archive_reader& ar);

		inline void clone_to_target(property_manager& target) const;

	private:
		std::map<std::string, property_base*> m_properties;
		std::weak_ptr<engine> engine_ref_;
	};

	class property_owner : public engine_bindable {
	public:
		property_manager property_mgr;

		property_manager& reflection() { return property_mgr; }
		const property_manager& reflection() const { return property_mgr; }

		// Default constructor and move operations
		property_owner() = default;
		property_owner(property_owner&&) = default;
		property_owner& operator=(property_owner&&) = default;

		virtual ~property_owner() = default;

		// Override bind_to_engine to propagate to property_mgr
		void bind_to_engine(std::weak_ptr<engine> eng) override {
			engine_bindable::bind_to_engine(eng);
			property_mgr.bind_to_engine(eng);
		}

		// Post-deserialization hook for migration and data transformation
		// Called after all properties have been loaded from the archive
		// Override this to implement custom migration logic
		//
		// The archive parameter provides access to:
		// - ar.get_version() - the version that was serialized
		// - ar.get_user_context<T>() - custom context for dependency injection
		//
		// Example:
		//   void post_deserialize(archive_reader& ar) override {
		//       if (ar.get_version() < 2) {
		//           // Migrate from v1 to v2
		//           new_field = compute_from_old_fields();
		//       }
		//   }
		virtual void post_deserialize(serialization::archive_reader& ar) {
			// Default implementation does nothing
		}

		// Convenience method: Load properties and call post_deserialize hook
		// This is the recommended way to deserialize objects
		void load_with_hook(serialization::archive_reader& ar) {
			property_mgr.load(ar);
			post_deserialize(ar);
		}

	protected:
		// Copy operations are protected to prevent slicing
		// Derived classes must explicitly opt-in to copying
		property_owner(const property_owner& rhs) {
			*this = rhs;
		}

		property_owner& operator=(const property_owner& other) {
			if (this != &other) {
				other.property_mgr.clone_to_target(property_mgr);
			}
			return *this;
		}
	};

	// ===== IMPLEMENTATION =====

	// property_base constructor implementation (depends on property_manager)
	inline property_base::property_base(property_manager& property_register, std::string name)
		: property_base(std::move(name)) {
		property_register.add(this);
	}

	// property_manager implementation
	inline void property_manager::add(property_base* prop) {
		m_properties[prop->name()] = prop;
	}

	inline void property_manager::clone_to_target(property_manager& target) const {
		for (auto& [k, v] : m_properties) {
			auto it = target.m_properties.find(k);
			if (it != target.m_properties.end()) {
				v->clone_to_target(*it->second);
			}
		}
	}

	inline property_base* property_manager::get(const std::string& key) const {
		auto it = m_properties.find(key);
		return it != m_properties.end() ? it->second : nullptr;
	}

	template<typename T>
	inline property<T>* property_manager::get(const std::string& key) const {
		auto base = get(key);
		return base ? dynamic_cast<property<T>*>(base) : nullptr;
	}

	template<typename T>
	inline T* property_manager::get_value(const std::string& key) const {
		if (auto prop = get<T>(key)) {
			return &prop->get();
		}
		return nullptr;
	}

} // namespace jai
