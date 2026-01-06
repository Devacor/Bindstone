#pragma once

#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <memory>
#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_schema.hpp>
#include <jaiscript/core/engine_bindable.hpp>
#include <jaiscript/signals/receiver_owner.hpp>

namespace jai {
	// Forward declarations
	class engine;

	// Helper to get the first type from a parameter pack
	namespace detail {
		template<typename First, typename...>
		struct first_type { using type = First; };

		template<typename... Ts>
		using first_type_t = typename first_type<Ts...>::type;
	}

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

		// Track a script-created receiver - keeps it alive for the lifetime of the property_manager
		// This is used by dynamic_binder to manage observable property callbacks from scripts
		template<typename T>
		std::shared_ptr<receiver<T>> track_receiver(std::shared_ptr<receiver<T>> recv) {
			return script_receivers_.track(std::move(recv));
		}

		receiver_owner& script_receivers() { return script_receivers_; }

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
		receiver_owner script_receivers_;  // Holds receivers created by script callbacks
	};

	// Note: property_owner is now a CRTP template class defined later in this file.
	// See "Property Owner (CRTP with optional inheritance tracking)" section.

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

	// ============================================================================
	// Property Owner (CRTP with optional inheritance tracking)
	// ============================================================================
	//
	// Unified property owner class. Use CRTP pattern by passing your class
	// as the first template parameter. Additional bases are optional.
	//
	// Example:
	//   // Simple class (no inheritance)
	//   class Entity : public property_owner<Entity> {
	//       JAI_PROPERTY((int), health);
	//   };
	//
	//   // With inheritance tracking
	//   class Player : public property_owner<Player, Entity> {
	//       JAI_PROPERTY((std::string), name);
	//   };
	//
	// The type registry will track inheritance and include base properties
	// when serializing derived classes.
	//
	// Signal receiver tracking:
	//   property_owner includes a receiver_owner for automatic signal cleanup.
	//   Use track() to tie receiver lifetime to the object:
	//
	//   class Player : public property_owner<Player> {
	//   public:
	//       Player(signal<void(int)>& damage_signal) {
	//           track(damage_signal.connect([this](int dmg) {
	//               // handle damage - auto-disconnects when Player dies
	//           }));
	//       }
	//   };

	template<typename Derived, typename... Bases>
	class property_owner : public Bases... {
	public:
		// Expose the derived type and base types for macros and dynamic_binder
		using _jai_owner_type = Derived;
		using _jai_base_types = std::tuple<Bases...>;
		static constexpr size_t _jai_base_count = sizeof...(Bases);

		// Per-instance property manager (for runtime property access)
		property_manager property_mgr;

		property_manager& reflection() { return property_mgr; }
		const property_manager& reflection() const { return property_mgr; }

		// Static access to type-level schema
		static const type_property_schema& schema() {
			return type_registry::instance().for_type<Derived>();
		}

		// Get all property names including inherited (from type registry)
		static std::vector<std::string> all_property_names() {
			return type_registry::instance().all_property_names<Derived>();
		}

		// Default constructor
		property_owner() {
			register_inheritance();
		}

		// Forwarding constructor for single base - forwards all arguments to that base
		// This allows derived classes to initialize their base properly:
		//   Sprite(args) : property_owner(args) { }
		template<typename... Args>
		explicit property_owner(Args&&... args) requires (sizeof...(Bases) == 1)
			: detail::first_type_t<Bases...>(std::forward<Args>(args)...) {
			register_inheritance();
		}

		// Forwarding constructor for multiple bases - forwards one arg per base
		// Requires number of args to match number of bases
		template<typename... Args>
		explicit property_owner(Args&&... args) requires (sizeof...(Bases) > 1 && sizeof...(Bases) == sizeof...(Args))
			: Bases(std::forward<Args>(args))... {
			register_inheritance();
		}

	private:
		void register_inheritance() {
			// Ensure inheritance is registered (runs once per type)
			static const auto _inheritance_registered = []() {
				if constexpr (sizeof...(Bases) > 0) {
					auto& schema = type_registry::instance().for_type<Derived>();
					(schema.add_base(std::type_index(typeid(Bases))), ...);
				}
				return true;
			}();
			(void)_inheritance_registered;
		}

	public:

		// Move operations
		property_owner(property_owner&&) = default;
		property_owner& operator=(property_owner&&) = default;

		virtual ~property_owner() = default;

		// Engine binding support
		void bind_to_engine(std::weak_ptr<engine> eng) {
			// Bind bases if they have bind_to_engine
			if constexpr (sizeof...(Bases) > 0) {
				(try_bind_base<Bases>(eng), ...);
			}
			property_mgr.bind_to_engine(eng);
		}

		// Post-deserialization hook
		virtual void post_deserialize(serialization::archive_reader& ar) {
			// Default: call base class hooks
			if constexpr (sizeof...(Bases) > 0) {
				(try_post_deserialize_base<Bases>(ar), ...);
			}
		}

		// Load with hook
		void load_with_hook(serialization::archive_reader& ar) {
			property_mgr.load(ar);
			post_deserialize(ar);
		}

	protected:
		// Receiver owner for automatic signal cleanup
		receiver_owner receivers_;

		// Track a receiver - ties its lifetime to this object
		template<typename T>
		std::shared_ptr<receiver<T>> track(std::shared_ptr<receiver<T>> recv) {
			return receivers_.track(std::move(recv));
		}

		// Access to receiver_owner for advanced use cases
		receiver_owner& receivers() { return receivers_; }
		const receiver_owner& receivers() const { return receivers_; }

		// Copy operations are protected
		property_owner(const property_owner& rhs) {
			*this = rhs;
		}

		property_owner& operator=(const property_owner& other) {
			if (this != &other) {
				other.property_mgr.clone_to_target(property_mgr);
				// Note: receivers_ are NOT copied - each instance manages its own connections
			}
			return *this;
		}

	private:
		// SFINAE helpers to call base class methods if they exist
		template<typename Base>
		auto try_bind_base(std::weak_ptr<engine> eng)
			-> decltype(static_cast<Base*>(this)->bind_to_engine(eng), void()) {
			static_cast<Base*>(this)->bind_to_engine(eng);
		}
		template<typename Base>
		void try_bind_base(...) {}

		template<typename Base>
		auto try_post_deserialize_base(serialization::archive_reader& ar)
			-> decltype(static_cast<Base*>(this)->post_deserialize(ar), void()) {
			static_cast<Base*>(this)->post_deserialize(ar);
		}
		template<typename Base>
		void try_post_deserialize_base(...) {}
	};

	// ============================================================================
	// Type traits for property_owner
	// ============================================================================

	// Check if T has _jai_owner_type (uses property_owner CRTP)
	template<typename T, typename = void>
	struct has_property_owner : std::false_type {};

	template<typename T>
	struct has_property_owner<T, std::void_t<typename T::_jai_owner_type>> : std::true_type {};

	template<typename T>
	inline constexpr bool has_property_owner_v = has_property_owner<T>::value;

	// Get base types from a property_owner
	template<typename T, typename = void>
	struct get_base_types {
		using type = std::tuple<>;
	};

	template<typename T>
	struct get_base_types<T, std::void_t<typename T::_jai_base_types>> {
		using type = typename T::_jai_base_types;
	};

	template<typename T>
	using get_base_types_t = typename get_base_types<T>::type;

	// Legacy alias for compatibility
	template<typename Derived, typename... Bases>
	using typed_property_owner = property_owner<Derived, Bases...>;

	template<typename T>
	inline constexpr bool has_typed_property_owner_v = has_property_owner_v<T>;

} // namespace jai
