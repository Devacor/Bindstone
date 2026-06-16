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

	namespace detail {
		template<typename First, typename...>
		struct first_type { using type = First; };

		template<typename... Ts>
		using first_type_t = typename first_type<Ts...>::type;
	}

	namespace serialization {
		class any_archive_writer;
		class any_archive_reader;
		// Defined in serialization/polymorphic.hpp, included at the bottom of this header
		// (same pattern as registrar.hpp): implicit polymorphic registration for
		// property_owner types, and the base-pointer adjuster registration for MI.
		template<typename T> void try_auto_register_implicit();
		template<typename Derived, typename Base> void register_polymorphic_base_relation();
	}

	class property_manager {
	public:
		property_manager() = default;

		property_manager(property_manager&&) = delete;
		property_manager& operator=(property_manager&&) = delete;
		property_manager(const property_manager&) = delete;
		property_manager& operator=(const property_manager&) = delete;

		void add(property_base* prop);
		const std::map<std::string, property_base*>& all() const { return m_properties; }

		// Track a script-created receiver - keeps it alive for the lifetime of the property_manager
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

		// Internally wraps in any_archive_writer/reader for polymorphic property dispatch
		template<typename Archive>
		void save(Archive& ar) const;

		template<typename Archive>
		void load(Archive& ar);

		inline void clone_to_target(property_manager& target) const;

	private:
		std::map<std::string, property_base*> m_properties;
		std::weak_ptr<engine> engine_ref_;
		receiver_owner script_receivers_;  // Holds receivers created by script callbacks
	};

	inline property_base::property_base(property_manager& property_register, std::string name)
		: property_base(std::move(name)) {
		property_register.add(this);
	}

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
		using _jai_owner_type = Derived;
		using _jai_base_types = std::tuple<Bases...>;
		static constexpr size_t _jai_base_count = sizeof...(Bases);

		property_manager property_mgr;

		property_manager& reflection() { return property_mgr; }
		const property_manager& reflection() const { return property_mgr; }

		static const type_property_schema& schema() {
			return type_registry::instance().for_type<Derived>();
		}

		static std::vector<std::string> all_property_names() {
			return type_registry::instance().all_property_names<Derived>();
		}

		property_owner() {
			register_inheritance();
		}

		template<typename... Args>
		explicit property_owner(Args&&... args) requires (sizeof...(Bases) == 1)
			: detail::first_type_t<Bases...>(std::forward<Args>(args)...) {
			register_inheritance();
		}

		template<typename... Args>
		explicit property_owner(Args&&... args) requires (sizeof...(Bases) > 1 && sizeof...(Bases) == sizeof...(Args))
			: Bases(std::forward<Args>(args))... {
			register_inheritance();
		}

	private:
		// Serialization identity, implicitly: every property_owner type registers its
		// class name with the polymorphic registry at program start (odr-used below, so
		// the entry exists before main — load factories must be ready before the first
		// construction). An explicit jai::registrar<T, Ctx>("Name") always takes
		// precedence; use it for custom names, template types, or a binary that loads a
		// type it never constructs (only construction odr-uses this member).
		static inline const bool _jai_poly_registered = [] {
			serialization::try_auto_register_implicit<Derived>();
			// Record Derived->Base pointer adjusters so a shared_ptr<Base> whose dynamic
			// type is Derived reconstructs the correct Base subobject under multiple
			// inheritance (each base is also a property_owner and records its own bases,
			// so adjust_to_base composes them transitively).
			if constexpr (sizeof...(Bases) > 0) {
				(serialization::register_polymorphic_base_relation<Derived, Bases>(), ...);
			}
			return true;
		}();

		void register_inheritance() {
			(void)_jai_poly_registered;
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

		// Move operations deleted: property_manager holds raw pointers to
		// member properties that would be invalidated by a move.
		property_owner(property_owner&&) = delete;
		property_owner& operator=(property_owner&&) = delete;

		virtual ~property_owner() = default;

		void bind_to_engine(std::weak_ptr<engine> eng) {
			if constexpr (sizeof...(Bases) > 0) {
				(try_bind_base<Bases>(eng), ...);
			}
			property_mgr.bind_to_engine(eng);
		}

		// Post-deserialization hook (called after all objects are deserialized)
		// Override in derived classes to resolve cross-references, update caches, etc.
		// Uses type-erased archive for polymorphic dispatch
		virtual void post_load(serialization::any_archive_reader& ar) {
			if constexpr (sizeof...(Bases) > 0) {
				(try_post_load_base<Bases>(ar), ...);
			}
		}

		template<typename Archive>
		void load_with_hook(Archive& ar) {
			load_owned_properties(ar);
			serialization::any_archive_reader type_erased(ar);
			post_load(type_erased);
		}

		// --- Inheritance-aware property serialization -----------------------------------------
		// property_owner<Derived, Bases...> holds ONE property_mgr per inheritance level, and the
		// derived level name-hides the bases'. These walk the whole chain (each base is itself a
		// property_owner, so it recurses — mirrors bind_to_engine/post_load) so a derived object's
		// INHERITED properties are serialized too; without them only the most-derived level survives.

		// Visit every level's properties (bases first, then own) — the single save walk.
		template<typename Fn>
		void visit_owned_properties(Fn&& fn) const {
			if constexpr (sizeof...(Bases) > 0) {
				(try_visit_base<Bases>(fn), ...);
			}
			for (const auto& [name, prop] : property_mgr.all()) {
				fn(name, prop);
			}
		}

		// Save: one type-erased wrap, then write every level's savable properties into the current
		// (already-open, flat) object.
		template<typename Archive>
		void save_owned_properties(Archive& ar) const {
			serialization::any_archive_writer type_erased(ar);
			visit_owned_properties([&type_erased](const std::string&, property_base* prop) {
				if (prop->allow_save()) {
					prop->serialize(type_erased);
				}
			});
		}

		// Load (concrete archive): each level seeks its own properties by name from the same flat
		// object — property_manager::load is re-entrant by design (see its comment), so call it per level.
		template<typename Archive>
		void load_owned_properties(Archive& ar) {
			if constexpr (sizeof...(Bases) > 0) {
				(try_load_base<Bases>(ar), ...);
			}
			property_mgr.load(ar);
		}

		// Find a property by name anywhere in the chain (the type-erased load reads names
		// sequentially and must resolve base-level properties too).
		property_base* find_owned_property(const std::string& name) {
			if (auto* p = property_mgr.get(name)) {
				return p;
			}
			property_base* found = nullptr;
			if constexpr (sizeof...(Bases) > 0) {
				((found = found ? found : try_find_base<Bases>(name)), ...);
			}
			return found;
		}

	protected:
		receiver_owner receivers_;

		template<typename T>
		std::shared_ptr<receiver<T>> track(std::shared_ptr<receiver<T>> recv) {
			return receivers_.track(std::move(recv));
		}

		receiver_owner& receivers() { return receivers_; }
		const receiver_owner& receivers() const { return receivers_; }

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
		template<typename Base>
		auto try_bind_base(std::weak_ptr<engine> eng)
			-> decltype(static_cast<Base*>(this)->bind_to_engine(eng), void()) {
			static_cast<Base*>(this)->bind_to_engine(eng);
		}
		template<typename Base>
		void try_bind_base(...) {}

		template<typename Base>
		auto try_post_load_base(serialization::any_archive_reader& ar)
			-> decltype(static_cast<Base*>(this)->post_load(ar), void()) {
			static_cast<Base*>(this)->post_load(ar);
		}
		template<typename Base>
		void try_post_load_base(...) {}

		template<typename Base, typename Fn>
		auto try_visit_base(Fn&& fn) const
			-> decltype(static_cast<const Base*>(this)->visit_owned_properties(fn), void()) {
			static_cast<const Base*>(this)->visit_owned_properties(fn);
		}
		template<typename Base>
		void try_visit_base(...) const {}

		template<typename Base, typename Archive>
		auto try_load_base(Archive& ar)
			-> decltype(static_cast<Base*>(this)->load_owned_properties(ar), void()) {
			static_cast<Base*>(this)->load_owned_properties(ar);
		}
		template<typename Base>
		void try_load_base(...) {}

		template<typename Base>
		auto try_find_base(const std::string& name)
			-> decltype(static_cast<Base*>(this)->find_owned_property(name)) {
			return static_cast<Base*>(this)->find_owned_property(name);
		}
		template<typename Base>
		property_base* try_find_base(...) { return nullptr; }
	};

	template<typename T, typename = void>
	struct has_property_owner : std::false_type {};

	template<typename T>
	struct has_property_owner<T, std::void_t<typename T::_jai_owner_type>> : std::true_type {};

	template<typename T>
	inline constexpr bool has_property_owner_v = has_property_owner<T>::value;

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

// Implicit polymorphic registration rides in with the registry definition (same pattern
// as registrar.hpp): forward-declared above, defined here. archive_dispatch.hpp supplies
// the any_archive dispatch() bodies the registration factories instantiate (see the note
// at the bottom of polymorphic.hpp).
#include <jaiscript/serialization/polymorphic.hpp>
#include <jaiscript/serialization/archive_dispatch.hpp>
