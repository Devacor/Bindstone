#pragma once

#include <string>
#include <functional>
#include <memory>
#include <compare>
#include <concepts>
#include <type_traits>

// Forward declarations
namespace jai {
	class property_manager;
	class property_base;

	template<typename T> class property;
	template<typename T> class deleted_property;

	namespace serialization {
		class archive_writer;
		class archive_reader;
	}
}

namespace jai {

	// C++20 Concepts for cleaner constraints
	template<typename T>
	concept smart_pointer = requires(T t) {
		typename T::element_type;
		{ t.get() } -> std::convertible_to<typename T::element_type*>;
		{ t.operator->() } -> std::same_as<typename T::element_type*>;
		{ t.operator*() } -> std::same_as<std::add_lvalue_reference_t<typename T::element_type>>;
	};

	template<typename T>
	concept pointer_like = std::is_pointer_v<T> || smart_pointer<T>;

	template<typename T>
	concept boolean_testable = requires(const T & t) {
		{ static_cast<bool>(t) };  // Can be explicitly converted to bool
	};

	// property_base - abstract base for all properties
	class property_base {
	public:
		virtual ~property_base() = default;

		bool operator<(const property_base& rhs) const {
			return name() < rhs.name();
		}

		const std::string& name() const { return m_property_name; }

		bool serialize_enabled() const { return m_allow_serialization; }
		void serialize_enabled(bool allow_serialization) const {
			m_allow_serialization = allow_serialization;
		}

		virtual bool allow_save() const { return m_allow_serialization; }

		// Serialization interface - uses JaiScript archives
		// Forward declared to avoid circular dependency
		virtual void save(class jai::serialization::archive_writer& ar) const = 0;
		virtual void load(class jai::serialization::archive_reader& ar) = 0;

		virtual void clone_to_target(property_base& target) = 0;

	protected:
		inline property_base(std::string name)
			: m_property_name(std::move(name)) {
		}

		inline property_base(property_manager& property_register, std::string name);

		mutable bool m_allow_serialization = true;

	private:
		std::string m_property_name;
	};

	template<typename T>
	class property : public property_base {
	protected:
		T m_value;
		std::function<void(property<T>&, property<T>&)> m_custom_clone;

		template<typename F>
		static constexpr bool is_clone_function_v =
			std::is_invocable_v<F, property<T>&, property<T>&>;

	public:
		using value_type = T;

		// Constructor 1: No default value, no clone function
		inline property(property_manager& reg, std::string name)
			: property_base(reg, std::move(name))
			, m_custom_clone()
			, m_value{} {
		}

		// Constructor 2: With value (but NOT a function type)
		template<typename U, std::enable_if_t<!is_clone_function_v<U>, int> = 0>
		inline property(property_manager& reg, std::string name, U&& def)
			: property_base(reg, std::move(name))
			, m_custom_clone()
			, m_value(std::forward<U>(def)) {
		}

		// Constructor 3: With clone function only
		template<typename Fn, std::enable_if_t<is_clone_function_v<Fn>, int> = 0>
		inline property(property_manager& reg, std::string name, Fn&& cl)
			: property_base(reg, std::move(name))
			, m_custom_clone(std::forward<Fn>(cl))
			, m_value{} {
		}

		// Constructor 4: With T constructed from initializer list
		inline property(property_manager& reg, std::string name, T def)
			: property_base(reg, std::move(name))
			, m_custom_clone()
			, m_value(std::move(def)) {
		}

		// Constructor 5: With value and clone function
		template<typename U, typename Fn>
		inline property(property_manager& reg, std::string name, U&& def, Fn&& cl)
			: property_base(reg, std::move(name))
			, m_custom_clone(std::forward<Fn>(cl))
			, m_value(std::forward<U>(def)) {
		}

		// Constructor 6: With T constructed from initializer list and clone function
		inline property(property_manager& reg, std::string name, T def,
			std::function<void(property<T>&, property<T>&)> cl)
			: property_base(reg, std::move(name))
			, m_custom_clone(std::move(cl))
			, m_value(std::move(def)) {
		}

		// Move constructor
		property(property&& other) noexcept
			: property_base(std::move(other))
			, m_custom_clone(std::move(other.m_custom_clone))
			, m_value(std::move(other.m_value)) {
		}

		property& operator=(property&& other) noexcept {
			if (this != &other) {
				// Note: Don't move property_base, we stay registered with our original manager
				m_custom_clone = std::move(other.m_custom_clone);
				m_value = std::move(other.m_value);
			}
			return *this;
		}

		// We cannot meaningfully copy construct a property because they need to be tightly bound to their property_manager reference.
		property(const property&) = delete;

		// Basic assignment only copies the value. We do have a clone_to_target method too, which can be used to invoke explicit deep copy semantics.
		property& operator=(const property& rhs) {
			m_value = rhs.m_value;
			return *this;
		}

		// Assignment operators
		property& operator=(const T& v) { m_value = v; return *this; }
		property& operator=(T&& v) { m_value = std::move(v); return *this; }

		// ===== CONVERSION OPERATORS =====
		operator const T& () const {
			return m_value;
		}

		// Mutable access
		operator T& () {
			return m_value;
		}

		// Boolean conversion for all non-bool types
		operator bool() const requires (!std::is_same_v<T, bool>) && requires(const T& t) { static_cast<bool>(t); } {
			return static_cast<bool>(m_value);
		}

		// ===== ACCESSOR METHODS =====
		T& get() {
			return m_value;
		}
		const T& get() const {
			return m_value;
		}

		// ===== DEREFERENCE OPERATORS =====
		// For smart pointers: operator* returns the smart pointer itself
		T& operator*() requires smart_pointer<T> {
			return m_value;
		}
		const T& operator*() const requires smart_pointer<T> {
			return m_value;
		}

		// For raw pointers: operator* dereferences
		auto& operator*() requires std::is_pointer_v<T> {
			return *m_value;
		}
		const auto& operator*() const requires std::is_pointer_v<T> {
			return *m_value;
		}

		// For non-pointer types: operator* returns the value
		T& operator*() requires (!pointer_like<T>) {
			return m_value;
		}
		const T& operator*() const requires (!pointer_like<T>) {
			return m_value;
		}

		// ===== ARROW OPERATORS =====
		// For smart pointers: arrow goes through to element_type
		template<typename U = T>
		auto operator->() -> std::enable_if_t<smart_pointer<U>, typename U::element_type*> {
			return m_value.get();
		}

		template<typename U = T>
		auto operator->() const -> std::enable_if_t<smart_pointer<U>, typename U::element_type*> {
			return m_value.get();
		}

		// For raw pointers: just return the pointer
		T operator->() requires std::is_pointer_v<T> {
			return m_value;
		}
		T operator->() const requires std::is_pointer_v<T> {
			return m_value;
		}

		// For class types: return pointer to value
		T* operator->() requires std::is_class_v<T> && (!pointer_like<T>) {
			return &m_value;
		}
		const T* operator->() const requires std::is_class_v<T> && (!pointer_like<T>) {
			return &m_value;
		}

		// ===== COMPARISON OPERATORS =====
		// We provide both spaceship and traditional operators to avoid ambiguity

		// Three-way comparison with another property
		auto operator<=>(const property& other) const requires std::three_way_comparable<T> {
			return m_value <=> other.m_value;
		}

		// Equality with another property
		bool operator==(const property& other) const {
			return m_value == other.m_value;
		}

		// Traditional comparison operators with any type U
		template<typename U>
		bool operator<(const U& other) const {
			return m_value < other;
		}

		template<typename U>
		bool operator<=(const U& other) const {
			return m_value <= other;
		}

		template<typename U>
		bool operator>(const U& other) const {
			return m_value > other;
		}

		template<typename U>
		bool operator>=(const U& other) const {
			return m_value >= other;
		}

		template<typename U>
		bool operator==(const U& other) const {
			return m_value == other;
		}

		template<typename U>
		bool operator!=(const U& other) const {
			return m_value != other;
		}

		// Friend operators for reverse comparisons (e.g., 0 > property)
		template<typename U>
		friend bool operator<(const U& lhs, const property& rhs) {
			return lhs < rhs.m_value;
		}

		template<typename U>
		friend bool operator<=(const U& lhs, const property& rhs) {
			return lhs <= rhs.m_value;
		}

		template<typename U>
		friend bool operator>(const U& lhs, const property& rhs) {
			return lhs > rhs.m_value;
		}

		template<typename U>
		friend bool operator>=(const U& lhs, const property& rhs) {
			return lhs >= rhs.m_value;
		}

		template<typename U>
		friend bool operator==(const U& lhs, const property& rhs) {
			return lhs == rhs.m_value;
		}

		template<typename U>
		friend bool operator!=(const U& lhs, const property& rhs) {
			return lhs != rhs.m_value;
		}

		// ===== ARITHMETIC OPERATORS =====
		// Enable arithmetic operations for arithmetic types
		template<typename U>
		friend auto operator+(const property& lhs, const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs.m_value + rhs;
		}

		template<typename U>
		friend auto operator+(const U& lhs, const property& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs + rhs.m_value;
		}

		template<typename U>
		friend auto operator-(const property& lhs, const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs.m_value - rhs;
		}

		template<typename U>
		friend auto operator-(const U& lhs, const property& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs - rhs.m_value;
		}

		template<typename U>
		friend auto operator*(const property& lhs, const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs.m_value * rhs;
		}

		template<typename U>
		friend auto operator*(const U& lhs, const property& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs * rhs.m_value;
		}

		template<typename U>
		friend auto operator/(const property& lhs, const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs.m_value / rhs;
		}

		template<typename U>
		friend auto operator/(const U& lhs, const property& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs / rhs.m_value;
		}

		template<typename U>
		friend auto operator%(const property& lhs, const U& rhs)
			requires std::is_integral_v<T>&& std::is_integral_v<U> {
			return lhs.m_value % rhs;
		}

		template<typename U>
		friend auto operator%(const U& lhs, const property& rhs)
			requires std::is_integral_v<T>&& std::is_integral_v<U> {
			return lhs % rhs.m_value;
		}

		// ===== COMPOUND ASSIGNMENT OPERATORS =====
		template<typename U>
		property& operator+=(const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			m_value += rhs;
			return *this;
		}

		template<typename U>
		property& operator-=(const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			m_value -= rhs;
			return *this;
		}

		template<typename U>
		property& operator*=(const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			m_value *= rhs;
			return *this;
		}

		template<typename U>
		property& operator/=(const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			m_value /= rhs;
			return *this;
		}

		template<typename U>
		property& operator%=(const U& rhs)
			requires std::is_integral_v<T>&& std::is_integral_v<U> {
			m_value %= rhs;
			return *this;
		}

		// Increment/decrement operators
		property& operator++() requires std::is_arithmetic_v<T> {
			++m_value;
			return *this;
		}

		property& operator--() requires std::is_arithmetic_v<T> {
			--m_value;
			return *this;
		}

		T operator++(int) requires std::is_arithmetic_v<T> {
			T old = m_value;
			++m_value;
			return old;
		}

		T operator--(int) requires std::is_arithmetic_v<T> {
			T old = m_value;
			--m_value;
			return old;
		}

		// ===== CONTAINER OPERATIONS =====
		// operator[] for container types
		template<typename I>
		auto& operator[](I&& i) requires requires { m_value[std::forward<I>(i)]; } {
			return m_value[std::forward<I>(i)];
		}

		template<typename I>
		const auto& operator[](I&& i) const requires requires { m_value[std::forward<I>(i)]; } {
			return m_value[std::forward<I>(i)];
		}

		// Function call operator
		template<typename... Args>
		auto operator()(Args&&... args)
			requires std::invocable<T, Args...> {
			return m_value(std::forward<Args>(args)...);
		}

		template<typename... Args>
		auto operator()(Args&&... args) const
			requires std::invocable<const T, Args...> {
			return m_value(std::forward<Args>(args)...);
		}

		// Iterator support
		auto begin() requires requires { m_value.begin(); } {
			return m_value.begin();
		}

		auto end() requires requires { m_value.end(); } {
			return m_value.end();
		}

		auto begin() const requires requires { m_value.begin(); } {
			return m_value.begin();
		}

		auto end() const requires requires { m_value.end(); } {
			return m_value.end();
		}

		// Serialization methods - implemented in property_serialization.hpp
		void save(serialization::archive_writer& ar) const override;
		void load(serialization::archive_reader& ar) override;

		// Clone method
		void set_custom_clone(std::function<void(property<T>&, property<T>&)> custom_clone) {
			m_custom_clone = custom_clone;
		}

		void clone_to_target(property_base& target) override {
			auto& t = static_cast<property<T>&>(target);
			if (m_custom_clone){
				m_custom_clone(*this, t);
			} else {
				t.m_value = m_value;
			}
		}
	};

	// deleted_property - placeholder for removed properties in serialization
	template<typename T>
	class deleted_property : public property_base {
	public:
		deleted_property(property_manager& list, std::string name)
			: property_base(list, std::move(name)) {
		}

		deleted_property(const std::string& name)
			: property_base(name) {
		}

		bool allow_save() const override { return false; }

		// No accessors, no value.
		const T& get() const = delete;
		T& get() = delete;

		// Serialization methods - implemented in property_serialization.hpp
		void save(serialization::archive_writer& ar) const override;
		void load(serialization::archive_reader& ar) override;

		void clone_to_target(property_base&) override {
			// No-op
		}
	};

} // namespace jai
