#pragma once

#ifndef __JAISCRIPT_PROPERTIES_OBSERVABLE_PROPERTY_HPP__
#define __JAISCRIPT_PROPERTIES_OBSERVABLE_PROPERTY_HPP__

#include <jaiscript/properties/property.hpp>
#include <jaiscript/signals/signal.hpp>

namespace jai {

// ============================================================================
// observable_property<T> - Property with change notification signal
// ============================================================================
//
// Extends property<T> to emit a signal whenever the value changes.
// The signal provides both old and new values.
//
// Usage:
//   class Player : public jai::property_owner<Player> {
//   public:
//       JAI_OBSERVABLE_PROPERTY((int), health, 100);
//
//       Player() {
//           health.on_change.connect([](int old_val, int new_val) {
//               std::cout << "Health changed: " << old_val << " -> " << new_val << "\n";
//           });
//       }
//   };

template<typename T>
class observable_property : public property<T> {
public:
	using base_type = property<T>;
	using value_type = T;

	// Signal signature: void(const T& old_value, const T& new_value)
	using change_signal_type = signal_emitter<void(const T&, const T&)>;

	// Constructor: No default value
	observable_property(property_manager& reg, std::string name)
		: base_type(reg, std::move(name)) {}

	// Constructor: With default value
	template<typename U, std::enable_if_t<!base_type::template is_clone_function_v<U>, int> = 0>
	observable_property(property_manager& reg, std::string name, U&& def)
		: base_type(reg, std::move(name), std::forward<U>(def)) {}

	// Constructor: With T constructed from initializer list
	observable_property(property_manager& reg, std::string name, T def)
		: base_type(reg, std::move(name), std::move(def)) {}

	// Move constructor
	observable_property(observable_property&& other) noexcept
		: base_type(std::move(other))
		, on_change_(std::move(other.on_change_)) {}

	observable_property& operator=(observable_property&& other) noexcept {
		if (this != &other) {
			base_type::operator=(std::move(other));
			on_change_ = std::move(other.on_change_);
		}
		return *this;
	}

	// Delete copy constructor (properties are bound to managers)
	observable_property(const observable_property&) = delete;

	// Assignment from another observable_property - copies value and emits signal
	observable_property& operator=(const observable_property& rhs) {
		if (this->m_value != rhs.m_value) {
			T old_value = this->m_value;
			this->m_value = rhs.m_value;
			on_change_.emit(old_value, this->m_value);
		}
		return *this;
	}

	// Assignment operators - emit signal on change
	observable_property& operator=(const T& v) {
		if (this->m_value != v) {
			T old_value = this->m_value;
			this->m_value = v;
			on_change_.emit(old_value, this->m_value);
		}
		return *this;
	}

	observable_property& operator=(T&& v) {
		if (&this->m_value != &v && this->m_value != v) {
			T old_value = std::move(this->m_value);
			this->m_value = std::move(v);
			on_change_.emit(old_value, this->m_value);
		}
		return *this;
	}

	// Set value without triggering signal (for loading/initialization)
	void set_silent(const T& v) {
		this->m_value = v;
	}

	void set_silent(T&& v) {
		this->m_value = std::move(v);
	}

	// Compound assignment operators - emit signal
	template<typename U>
	observable_property& operator+=(const U& rhs)
		requires std::is_arithmetic_v<T> && std::is_arithmetic_v<U> {
		T old_value = this->m_value;
		this->m_value += rhs;
		if (old_value != this->m_value) {
			on_change_.emit(old_value, this->m_value);
		}
		return *this;
	}

	template<typename U>
	observable_property& operator-=(const U& rhs)
		requires std::is_arithmetic_v<T> && std::is_arithmetic_v<U> {
		T old_value = this->m_value;
		this->m_value -= rhs;
		if (old_value != this->m_value) {
			on_change_.emit(old_value, this->m_value);
		}
		return *this;
	}

	template<typename U>
	observable_property& operator*=(const U& rhs)
		requires std::is_arithmetic_v<T> && std::is_arithmetic_v<U> {
		T old_value = this->m_value;
		this->m_value *= rhs;
		if (old_value != this->m_value) {
			on_change_.emit(old_value, this->m_value);
		}
		return *this;
	}

	template<typename U>
	observable_property& operator/=(const U& rhs)
		requires std::is_arithmetic_v<T> && std::is_arithmetic_v<U> {
		T old_value = this->m_value;
		this->m_value /= rhs;
		if (old_value != this->m_value) {
			on_change_.emit(old_value, this->m_value);
		}
		return *this;
	}

	// Increment/decrement - emit signal
	observable_property& operator++() requires std::is_arithmetic_v<T> {
		T old_value = this->m_value;
		++this->m_value;
		on_change_.emit(old_value, this->m_value);
		return *this;
	}

	observable_property& operator--() requires std::is_arithmetic_v<T> {
		T old_value = this->m_value;
		--this->m_value;
		on_change_.emit(old_value, this->m_value);
		return *this;
	}

	T operator++(int) requires std::is_arithmetic_v<T> {
		T old_value = this->m_value;
		++this->m_value;
		on_change_.emit(old_value, this->m_value);
		return old_value;
	}

	T operator--(int) requires std::is_arithmetic_v<T> {
		T old_value = this->m_value;
		--this->m_value;
		on_change_.emit(old_value, this->m_value);
		return old_value;
	}

	// Public signal for connecting observers
	signal<void(const T&, const T&)> on_change{on_change_};

private:
	change_signal_type on_change_;
};


} // namespace jai

// Note: JAI_OBSERVABLE_PROPERTY macro is defined in macros.hpp
// to ensure the helper macros (JAI_REMOVE_PARENS) are available.

#endif // __JAISCRIPT_PROPERTIES_OBSERVABLE_PROPERTY_HPP__
