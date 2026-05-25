#pragma once

#ifndef __JAISCRIPT_SIGNALS_SIGNAL_IMPL_HPP__
#define __JAISCRIPT_SIGNALS_SIGNAL_IMPL_HPP__

#include <jaiscript/signals/signal.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>

namespace jai {

// ============================================================================
// receiver<T> script execution implementations
// ============================================================================

namespace detail {

template<typename... Args>
inline instance_variables build_script_locals(
	engine* eng,
	const std::shared_ptr<std::vector<std::string>>& param_names,
	Args&&... args
) {
	instance_variables locals;

	std::vector<script_value> values;
	values.reserve(sizeof...(Args));
	(values.push_back(eng->make_value(std::forward<Args>(args))), ...);

	for (size_t i = 0; i < values.size(); ++i) {
		std::string name = (param_names && i < param_names->size())
			? (*param_names)[i]
			: "arg_" + std::to_string(i);
		locals[name] = std::move(values[i]);
	}

	return locals;
}

} // namespace detail

template<typename T>
template<typename... Args>
void receiver<T>::call_script(Args&&... args) {
	if (script_engine_ && !script_callback_.empty()) {
		try {
			auto locals = detail::build_script_locals(
				script_engine_,
				ordered_parameter_names_,
				std::forward<Args>(args)...
			);
			script_engine_->execute(script_callback_, locals);
		} catch (...) {
		}
	}
}

template<typename T>
template<typename... Args>
bool receiver<T>::call_script_predicate(Args&&... args) {
	if (script_engine_ && !script_callback_.empty()) {
		try {
			auto locals = detail::build_script_locals(
				script_engine_,
				ordered_parameter_names_,
				std::forward<Args>(args)...
			);
			auto result = script_engine_->execute(script_callback_, locals);
			return result.template as<bool>();
		} catch (...) {
			return false;
		}
	}
	return false;
}

template<typename T>
void receiver<T>::call_script() {
	if (script_engine_ && !script_callback_.empty()) {
		try {
			script_engine_->execute(script_callback_);
		} catch (...) {
		}
	}
}

template<typename T>
bool receiver<T>::call_script_predicate() {
	if (script_engine_ && !script_callback_.empty()) {
		try {
			auto result = script_engine_->execute(script_callback_);
			return result.template as<bool>();
		} catch (...) {
			return false;
		}
	}
	return false;
}

} // namespace jai

#endif // __JAISCRIPT_SIGNALS_SIGNAL_IMPL_HPP__
