#pragma once

#ifndef __JAISCRIPT_SIGNALS_SIGNAL_IMPL_HPP__
#define __JAISCRIPT_SIGNALS_SIGNAL_IMPL_HPP__

#include <jaiscript/signals/signal.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/engine_impl.hpp>
#include <jaiscript/core/value.hpp>
#include <iostream>
#include <sstream>

namespace jai {

// ============================================================================
// receiver<T> script execution implementations
// ============================================================================

namespace detail {

// Helper to convert arguments to script_values and build local variable map
template<typename... Args>
inline instance_variables build_script_locals(
	engine* eng,
	const std::shared_ptr<std::vector<std::string>>& param_names,
	Args&&... args
) {
	instance_variables locals;

	// Convert each argument to script_value
	std::vector<script_value> values;
	values.reserve(sizeof...(Args));
	(values.push_back(eng->make_value(std::forward<Args>(args))), ...);

	// Map to parameter names
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
		} catch (const std::exception& e) {
			std::cerr << "Signal script error: " << e.what() << "\n";
		}
	} else if (!script_callback_.empty()) {
		std::cerr << "Failed to run script in receiver: no JaiScript engine handle\n";
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
		} catch (const std::exception& e) {
			std::cerr << "Signal script error: " << e.what() << "\n";
			return false;
		}
	} else if (!script_callback_.empty()) {
		std::cerr << "Failed to run script in receiver: no JaiScript engine handle\n";
	}
	return false;
}

template<typename T>
void receiver<T>::call_script() {
	if (script_engine_ && !script_callback_.empty()) {
		try {
			script_engine_->execute(script_callback_);
		} catch (const std::exception& e) {
			std::cerr << "Signal script error: " << e.what() << "\n";
		}
	} else if (!script_callback_.empty()) {
		std::cerr << "Failed to run script in receiver: no JaiScript engine handle\n";
	}
}

template<typename T>
bool receiver<T>::call_script_predicate() {
	if (script_engine_ && !script_callback_.empty()) {
		try {
			auto result = script_engine_->execute(script_callback_);
			return result.template as<bool>();
		} catch (const std::exception& e) {
			std::cerr << "Signal script error: " << e.what() << "\n";
			return false;
		}
	} else if (!script_callback_.empty()) {
		std::cerr << "Failed to run script in receiver: no JaiScript engine handle\n";
	}
	return false;
}

} // namespace jai

#endif // __JAISCRIPT_SIGNALS_SIGNAL_IMPL_HPP__
