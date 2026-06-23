#pragma once

#ifndef __JAISCRIPT_SIGNALS_SIGNAL_SERIALIZATION_HPP__
#define __JAISCRIPT_SIGNALS_SIGNAL_SERIALIZATION_HPP__

#include <jaiscript/signals/signal.hpp>
// signal.hpp no longer pulls in the engine-coupled script glue; serialization needs a
// complete engine type (get_user_context<engine>) and the script-receiver execution path.
#include <jaiscript/signals/signal_impl.hpp>
#include <jaiscript/serialization/archive_impl.hpp>

#include <vector>
#include <map>
#include <string>

namespace jai {

// A serializable snapshot of a script receiver. Only script-based receivers persist (C++
// callbacks are runtime-only). Everything routes through the archive's format-agnostic
// ar(make_nvp(...)) path so signals round-trip in BOTH json and binary — raw begin_array /
// write_property_name framing is json-only and corrupts the binary stream.
struct serialized_receiver {
	std::vector<std::string> parameter_names;
	std::string script;
};

template<typename Archive>
void save(Archive& ar, const serialized_receiver& r) {
	ar(serialization::make_nvp("parameter_names", r.parameter_names));
	ar(serialization::make_nvp("script", r.script));
}

template<typename Archive>
void load(Archive& ar, serialized_receiver& r) {
	ar(serialization::make_nvp("parameter_names", r.parameter_names));
	ar(serialization::make_nvp("script", r.script));
}

// ADL hooks (named save/load so the archive's write_element/read_element find them). The format
// stores parameter_names (signal-level), observers (array), and owned_observers (id -> receiver
// map). A named connection lives in both observers_ and owned_connections_, so owned receivers
// are excluded from the observers list to avoid serializing them twice.
template<typename Archive, typename T>
void save(Archive& ar, const signal_emitter<T>& sig) {
	std::vector<std::string> param_names = sig.parameter_names();
	ar(serialization::make_nvp("parameter_names", param_names));

	std::vector<serialized_receiver> observers;
	for (const auto& weak_obs : sig.observers_) {
		if (auto obs = weak_obs.lock()) {
			if (!obs->has_script()) { continue; }
			bool owned = false;
			for (const auto& [id, owned_obs] : sig.owned_connections_) {
				if (owned_obs == obs) { owned = true; break; }
			}
			if (owned) { continue; }
			serialized_receiver r;
			if (obs->parameter_names()) { r.parameter_names = *obs->parameter_names(); }
			r.script = obs->script();
			observers.push_back(std::move(r));
		}
	}
	ar(serialization::make_nvp("observers", observers));

	std::map<std::string, serialized_receiver> owned_observers;
	for (const auto& [id, obs] : sig.owned_connections_) {
		if (!obs->has_script()) { continue; }
		serialized_receiver r;
		if (obs->parameter_names()) { r.parameter_names = *obs->parameter_names(); }
		r.script = obs->script();
		owned_observers[id] = std::move(r);
	}
	ar(serialization::make_nvp("owned_observers", owned_observers));
}

template<typename Archive, typename T>
void load(Archive& ar, signal_emitter<T>& sig) {
	engine* eng = ar.template get_user_context<engine>();

	std::vector<std::string> param_names;
	ar(serialization::make_nvp("parameter_names", param_names));
	sig.parameter_names(param_names);

	auto rebuild = [&](const serialized_receiver& r) {
		std::shared_ptr<std::vector<std::string>> names;
		if (!r.parameter_names.empty()) {
			names = std::make_shared<std::vector<std::string>>(r.parameter_names);
		}
		return receiver<T>::make(r.script, eng, names);
	};

	std::vector<serialized_receiver> observers;
	ar(serialization::make_nvp("observers", observers));
	for (const auto& r : observers) {
		sig.connect(rebuild(r));
	}

	std::map<std::string, serialized_receiver> owned_observers;
	ar(serialization::make_nvp("owned_observers", owned_observers));
	for (const auto& [id, r] : owned_observers) {
		auto recv = rebuild(r);
		sig.owned_connections_[id] = recv;
		sig.connect(recv);
	}

	if (eng) {
		sig.script_engine(eng);
	}
}

} // namespace jai

#endif // __JAISCRIPT_SIGNALS_SIGNAL_SERIALIZATION_HPP__
