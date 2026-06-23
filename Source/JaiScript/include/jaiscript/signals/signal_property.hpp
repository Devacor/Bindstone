#pragma once

#ifndef __JAISCRIPT_SIGNAL_PROPERTY_HPP__
#define __JAISCRIPT_SIGNAL_PROPERTY_HPP__

#include <jaiscript/signals/signal_serialization.hpp>
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
	};
}

// Declares a serializable, script-connectable signal as the established private-emitter /
// public-view pair, plus a hidden property that serializes the emitter under "name":
//
//   private: signal_emitter<Sig> nameSignal;   // emit source + serialized state
//   public:  signal<Sig>         name;         // connect target (scripts / external code)
//
// Emit through the emitter as before: nameSignal(args). Requires the enclosing class to be a
// property_owner (have a property_mgr). Leaves member access at public:.
#define JAI_SIGNAL_PROPERTY(signature, name) \
	private: \
		jai::signal_emitter<signature> name##Signal; \
		jai::signal_property<signature> _jai_signalprop_##name{ property_mgr, #name, name##Signal }; \
	public: \
		jai::signal<signature> name{ name##Signal }

#endif
