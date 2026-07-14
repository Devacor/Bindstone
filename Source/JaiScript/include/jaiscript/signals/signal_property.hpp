#pragma once

#ifndef __JAISCRIPT_SIGNAL_PROPERTY_HPP__
#define __JAISCRIPT_SIGNAL_PROPERTY_HPP__

#include <jaiscript/signals/signal_serialization.hpp>
#include <jaiscript/signals/signal_view.hpp>
#include <jaiscript/properties/property.hpp>
#include <jaiscript/properties/property_manager.hpp>
#include <jaiscript/serialization/archive_impl.hpp>

namespace jai {

	// A property that serializes an externally-owned signal_emitter under a given name, routing
	// through the ADL save/load hooks. The emitter stays a plain signal_emitter on the owning
	// class (so emission via operator() is unchanged); this just lets it ride property_mgr's
	// save/load like any other property. Only script receivers serialize (C++ callbacks don't).
	template<typename T>
	class signal_property : public property_base {
		signal_emitter<T>& m_emitter;
	public:
		signal_property(property_manager& reg, std::string name, signal_emitter<T>& emitter)
			: property_base(reg, std::move(name)), m_emitter(emitter) {}

		void serialize(serialization::any_archive_writer& ar) const override {
			ar.dispatch([this](auto& concrete_ar) {
				concrete_ar.serialize(name().c_str(), m_emitter);
			});
		}
		void serialize(serialization::any_archive_reader& ar) override {
			ar.dispatch([this](auto& concrete_ar) {
				concrete_ar.read_custom(m_emitter);
			});
		}
		void clone_to_target(property_base&) override {}

		// The generic "Signal" script view connects through this erased table,
		// so auto_bind exposes the property with no per-signature registration.
		bool signal_ops(signal_script_ops& ops) override {
			ops = make_signal_script_ops(m_emitter);
			return true;
		}
	};
}

// Declares a serializable, script-connectable signal as the established private-emitter /
// public-view pair, plus a hidden property that serializes the emitter under "name":
//
//   private: signal_emitter<Sig> nameSignal;   // emit source + serialized state
//   public:  signal<Sig>         name;         // connect target (scripts / external code)
//
// Emit through the emitter as before: nameSignal(args). Requires the enclosing class to be a
// property_owner (have a property_mgr). Leaves member access at public:. The schema entry
// (is_signal) lets dynamic_binder's auto_bind expose the generic Signal view — declaring the
// signal is the whole registration.
#define JAI_SIGNAL_PROPERTY(signature, name) \
	private: \
		inline static const int _jai_schema_reg_##name = []{ \
			using OwnerT = _jai_owner_type; \
			jai::type_registry::instance().for_type<OwnerT>().add( \
				jai::property_meta{ #name, std::type_index(typeid(jai::signal_emitter<signature>)), true, false, true }); \
			return 0; \
		}(); \
		jai::signal_emitter<signature> name##Signal; \
		jai::signal_property<signature> _jai_signalprop_##name{ property_mgr, #name, name##Signal }; \
	public: \
		jai::signal<signature> name{ name##Signal }

#endif
