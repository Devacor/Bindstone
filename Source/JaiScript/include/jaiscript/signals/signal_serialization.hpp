#pragma once

#ifndef __JAISCRIPT_SIGNALS_SIGNAL_SERIALIZATION_HPP__
#define __JAISCRIPT_SIGNALS_SIGNAL_SERIALIZATION_HPP__

#include <jaiscript/signals/signal.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace jai {

// ============================================================================
// Receiver serialization
// ============================================================================
//
// Only script-based receivers can be serialized (C++ callbacks cannot).
// The serialized format stores:
//   - parameter_names: array of string
//   - script: string
//
// On deserialization, the engine pointer is retrieved from user context.

template<typename Archive, typename T>
void save_receiver(Archive& ar, const receiver<T>& recv) {
	ar.begin_object("receiver", 1);

	// Parameter names
	ar.write_property_name("parameter_names");
	const auto& names = recv.parameter_names();
	if (names) {
		ar.begin_array(names->size());
		for (const auto& name : *names) {
			ar.write_string(name);
		}
		ar.end_array();
	} else {
		ar.begin_array(0);
		ar.end_array();
	}

	// Script callback
	ar.write_property_name("script");
	ar.write_string(recv.script());

	ar.end_object();
}

template<typename T, typename Archive>
std::shared_ptr<receiver<T>> load_receiver(Archive& ar) {
	std::string type_name;
	uint32_t version;
	ar.begin_object(type_name, version);

	// Read parameter names
	std::string prop_name;
	ar.read_property_name(prop_name);  // "parameter_names"
	size_t count = ar.begin_array();
	auto param_names = std::make_shared<std::vector<std::string>>();
	param_names->reserve(count);
	for (size_t i = 0; i < count; ++i) {
		param_names->push_back(ar.read_string());
	}
	ar.end_array();

	// Read script
	ar.read_property_name(prop_name);  // "script"
	std::string script = ar.read_string();

	ar.end_object();

	// Get engine from user context
	engine* eng = ar.get_user_context<engine>();

	if (param_names->empty()) {
		param_names = nullptr;
	}

	return receiver<T>::make(script, eng, param_names);
}


// ============================================================================
// signal_emitter serialization
// ============================================================================
//
// Only script-based receivers are serialized (C++ callbacks are runtime-only).
// The format stores:
//   - parameter_names: array of string (signal-level defaults)
//   - observers: array of receiver objects
//   - owned_observers: map of id -> receiver objects

template<typename Archive, typename T>
void save_signal_emitter(Archive& ar, const signal_emitter<T>& sig) {
	ar.begin_object("signal_emitter", 1);

	// Parameter names (signal-level)
	ar.write_property_name("parameter_names");
	auto names = sig.parameter_names();
	ar.begin_array(names.size());
	for (const auto& name : names) {
		ar.write_string(name);
	}
	ar.end_array();

	// Collect script-only observers
	std::vector<std::shared_ptr<receiver<T>>> script_observers;
	for (const auto& weak_obs : sig.observers_) {
		if (auto obs = weak_obs.lock()) {
			if (obs->has_script()) {
				script_observers.push_back(obs);
			}
		}
	}

	// Write observers
	ar.write_property_name("observers");
	ar.begin_array(script_observers.size());
	for (const auto& obs : script_observers) {
		save_receiver(ar, *obs);
	}
	ar.end_array();

	// Collect script-only owned observers
	std::map<std::string, std::shared_ptr<receiver<T>>> script_owned;
	for (const auto& [id, obs] : sig.owned_connections_) {
		if (obs->has_script()) {
			script_owned[id] = obs;
		}
	}

	// Write owned observers as map
	ar.write_property_name("owned_observers");
	ar.begin_map(script_owned.size());
	for (const auto& [id, obs] : script_owned) {
		ar.write_map_key(id);
		save_receiver(ar, *obs);
	}
	ar.end_map();

	ar.end_object();
}

template<typename Archive, typename T>
void load_signal_emitter(Archive& ar, signal_emitter<T>& sig) {
	std::string type_name;
	uint32_t version;
	ar.begin_object(type_name, version);

	std::string prop_name;

	// Read parameter names
	ar.read_property_name(prop_name);  // "parameter_names"
	size_t name_count = ar.begin_array();
	std::vector<std::string> param_names;
	param_names.reserve(name_count);
	for (size_t i = 0; i < name_count; ++i) {
		param_names.push_back(ar.read_string());
	}
	ar.end_array();
	sig.parameter_names(param_names);

	// Read observers
	ar.read_property_name(prop_name);  // "observers"
	size_t obs_count = ar.begin_array();
	for (size_t i = 0; i < obs_count; ++i) {
		auto recv = load_receiver<T>(ar);
		sig.connect(recv);
	}
	ar.end_array();

	// Read owned observers
	ar.read_property_name(prop_name);  // "owned_observers"
	size_t owned_count = ar.begin_map();
	for (size_t i = 0; i < owned_count; ++i) {
		std::string id;
		ar.read_map_key(id);
		auto recv = load_receiver<T>(ar);
		sig.owned_connections_[id] = recv;
		sig.connect(recv);
	}
	ar.end_map();

	// Get engine from user context
	engine* eng = ar.get_user_context<engine>();
	if (eng) {
		sig.script_engine(eng);
	}

	ar.end_object();
}

} // namespace jai

#endif // __JAISCRIPT_SIGNALS_SIGNAL_SERIALIZATION_HPP__
