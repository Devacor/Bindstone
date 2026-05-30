#ifndef _MV_PROPERTIES_H_
#define _MV_PROPERTIES_H_

#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include <utility>
#include <memory>
#include <compare>
#include <concepts>
#include <type_traits>
#include "require.hpp"
#include <jaiscript/signals/signal_decl.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/portable_binary.hpp>

namespace MV {

	// C++20 Concepts for cleaner constraints
	template<typename T>
	concept SmartPointer = requires(T t) {
		typename T::element_type;
		{ t.get() } -> std::convertible_to<typename T::element_type*>;
		{ t.operator->() } -> std::same_as<typename T::element_type*>;
		{ t.operator*() } -> std::same_as<std::add_lvalue_reference_t<typename T::element_type>>;
	};

	template<typename T>
	concept PointerLike = std::is_pointer_v<T> || SmartPointer<T>;

	template<typename T>
	concept BooleanTestable = requires(const T & t) {
		{ static_cast<bool>(t) };  // Can be explicitly converted to bool
	};

	class PropertyBase;
	template<typename T> class Property;
	template<typename T> class ObservableProperty;

	class PropertyManager;
	class PropertyOwner;

	class PropertyManager {
	public:
		PropertyManager() = default;
		PropertyManager(PropertyManager&&) = default;

		PropertyManager(const PropertyManager&) = delete;
		PropertyManager& operator=(const PropertyManager&) = delete;

		void add(PropertyBase* prop);
		const std::map<std::string, PropertyBase*>& all() const { return properties; }

		template<typename Fn>
		void visit(Fn&& func) const {
			for (const auto& [key, prop] : properties) {
				func(key, prop);
			}
		}

		inline PropertyBase* get(const std::string& key) const;

		template<typename T>
		inline Property<T>* get(const std::string& key) const;

		template<typename T>
		inline ObservableProperty<T>* getObservable(const std::string& key) const;

		template<typename T>
		inline T* getValue(const std::string& key) const;

		template<class Archive>
		void save(Archive& ar) const {
			std::vector<std::string> keys;
			for (const auto& [name, prop] : properties) {
				if (prop->allowSave()) {
					keys.push_back(name);
				}
			}
			ar(cereal::make_nvp("PropertyKeys", keys));
			for (const auto& name : keys) {
				properties.at(name)->save(ar);
			}
		}

		template<class Archive>
		void load(Archive& ar) {
			std::vector<std::string> savedKeys;
			ar(cereal::make_nvp("PropertyKeys", savedKeys));

			for (const auto& name : savedKeys) {
				auto it = properties.find(name);
				MV::require<ResourceException>(it != properties.end(), "Unknown Property: ", name,
					" If a property was deleted, you will need to either recreate this archive, or, add a DeletedProperty<TheDeletedPropertyType> with this name.");
				it->second->load(ar, false);
			}
		}

		template<class Archive>
		void load(Archive& ar, const std::vector<std::string>& a_optionalKeyOrderOverride,
			const std::unordered_map<std::string, std::string>& a_optionalKeyRenameBindings = {}) {
			std::vector<std::string> savedKeys;
			bool usingPropertyOverride = !a_optionalKeyOrderOverride.empty();
			if (!usingPropertyOverride) {
				ar(cereal::make_nvp("PropertyKeys", savedKeys));
			}
			else {
				savedKeys = a_optionalKeyOrderOverride;
			}

			for (const auto& name : savedKeys) {
				const auto found = a_optionalKeyRenameBindings.find(name);
				bool wasRenamed = found != a_optionalKeyRenameBindings.end();
				auto it = properties.find(wasRenamed ? found->second : name);
				MV::require<ResourceException>(it != properties.end(), "Unknown Property: ", name,
					" If a property was deleted, you will need to either recreate this archive, or, add a DeletedProperty<TheDeletedPropertyType> with this name.");
				it->second->load(ar, usingPropertyOverride);
			}
		}

		inline void cloneToTarget(PropertyManager& target) const;

	private:
		std::map<std::string, PropertyBase*> properties;
	};

	class PropertyOwner {
	public:
		PropertyManager propertyManager;
		
		PropertyManager& reflection() { return propertyManager; }
		const PropertyManager& reflection() const { return propertyManager; }

		// Default constructor and move operations
		PropertyOwner() = default;
		PropertyOwner(PropertyOwner&&) = default;
		PropertyOwner& operator=(PropertyOwner&&) = default;

		virtual ~PropertyOwner() = default;

	protected:
		// Copy operations are protected to prevent slicing
		// Derived classes must explicitly opt-in to copying
		PropertyOwner(const PropertyOwner& a_rhs) {
			*this = a_rhs;
		}

		PropertyOwner& operator=(const PropertyOwner& other) {
			if (this != &other) {
				other.propertyManager.cloneToTarget(propertyManager);
			}
			return *this;
		}
	};

	// PropertyBase - abstract base for all properties
	class PropertyBase {
	public:
		virtual ~PropertyBase() = default;

		bool operator<(const PropertyBase& rhs) const {
			return name() < rhs.name();
		}

		const std::string& name() const { return propertyName; }

		bool serializeEnabled() const { return allowSerialization; }
		void serializeEnabled(bool a_allowSerialization) const {
			allowSerialization = a_allowSerialization;
		}

		virtual bool allowSave() const { return allowSerialization; }

		virtual void save(cereal::JSONOutputArchive& ar) const = 0;
		virtual void load(cereal::JSONInputArchive& ar, bool a_usingPropertyOverride) = 0;
		virtual void save(cereal::BinaryOutputArchive& ar) const = 0;
		virtual void load(cereal::BinaryInputArchive& ar, bool a_usingPropertyOverride) = 0;
		virtual void save(cereal::PortableBinaryOutputArchive& ar) const = 0;
		virtual void load(cereal::PortableBinaryInputArchive& ar, bool a_usingPropertyOverride) = 0;

		virtual void cloneToTarget(PropertyBase& target) = 0;

	protected:
		inline PropertyBase(std::string a_name)
			: propertyName(std::move(a_name)) {
		}

		inline PropertyBase(PropertyManager& a_propertyRegister, std::string a_name)
			: PropertyBase(std::move(a_name)) {
			a_propertyRegister.add(this);
		}

		mutable bool allowSerialization = true;

	private:
		std::string propertyName;
	};

	template<typename T>
	class Property : public PropertyBase {
	protected:
		T value;
		std::function<void(Property<T>&, Property<T>&)> customClone;

		template<typename F>
		static constexpr bool is_clone_function_v =
			std::is_invocable_v<F, Property<T>&, Property<T>&>;

	public:
		using value_type = T;

		// Constructor 1: No default value, no clone function
		inline Property(PropertyManager& reg, std::string name)
			: PropertyBase(reg, std::move(name))
			, customClone()
			, value{} {
		}

		// Constructor 2: With value (but NOT a function type)
		template<typename U, std::enable_if_t<!is_clone_function_v<U>, int> = 0>
		inline Property(PropertyManager& reg, std::string name, U&& def)
			: PropertyBase(reg, std::move(name))
			, customClone()
			, value(std::forward<U>(def)) {
		}

		// Constructor 3: With clone function only
		template<typename Fn, std::enable_if_t<is_clone_function_v<Fn>, int> = 0>
		inline Property(PropertyManager& reg, std::string name, Fn&& cl)
			: PropertyBase(reg, std::move(name))
			, customClone(std::forward<Fn>(cl))
			, value{} {
		}

		// Constructor 4: With T constructed from initializer list
		inline Property(PropertyManager& reg, std::string name, T def)
			: PropertyBase(reg, std::move(name))
			, customClone()
			, value(std::move(def)) {
		}

		// Constructor 5: With value and clone function
		template<typename U, typename Fn>
		inline Property(PropertyManager& reg, std::string name, U&& def, Fn&& cl)
			: PropertyBase(reg, std::move(name))
			, customClone(std::forward<Fn>(cl))
			, value(std::forward<U>(def)) {
		}

		// Constructor 6: With T constructed from initializer list and clone function
		inline Property(PropertyManager& reg, std::string name, T def,
			std::function<void(Property<T>&, Property<T>&)> cl)
			: PropertyBase(reg, std::move(name))
			, customClone(std::move(cl))
			, value(std::move(def)) {
		}

		// Move constructor
		Property(Property&& other) noexcept
			: PropertyBase(std::move(other))
			, customClone(std::move(other.customClone))
			, value(std::move(other.value)) {
		}

		Property& operator=(Property&& other) noexcept {
			if (this != &other) {
				// Note: Don't move PropertyBase, we stay registered with our original manager
				customClone = std::move(other.customClone);
				value = std::move(other.value);
			}
			return *this;
		}

		// We cannot meaningfully copy construct a Property because they need to be tightly bound to their PropertyManager reference.
		Property(const Property&) = delete;

		// Basic assignment only copies the value. We do have a cloneToTarget method too, which can be used to invoke explicit deep copy semantics.
		Property& operator=(const Property& a_rhs) {
			value = a_rhs.value;
			return *this;
		}

		// Assignment operators
		Property& operator=(const T& v) { value = v; return *this; }
		Property& operator=(T&& v) { value = std::move(v); return *this; }

		// ===== CONVERSION OPERATORS =====
		operator const T& () const {
			return value;
		}

		// Mutable access  
		operator T& () {
			return value;
		}

		// Boolean conversion for all non-bool types
		operator bool() const requires (!std::is_same_v<T, bool>) && requires(const T& t) { static_cast<bool>(t); } {
			return static_cast<bool>(value);
		}

		// ===== ACCESSOR METHODS =====
		T& get() {
			return value;
		}
		const T& get() const {
			return value;
		}

		// ===== DEREFERENCE OPERATORS =====
		// For smart pointers: operator* returns the smart pointer itself
		T& operator*() requires SmartPointer<T> {
			return value;
		}
		const T& operator*() const requires SmartPointer<T> {
			return value;
		}

		// For raw pointers: operator* dereferences
		auto& operator*() requires std::is_pointer_v<T> {
			return *value;
		}
		const auto& operator*() const requires std::is_pointer_v<T> {
			return *value;
		}

		// For non-pointer types: operator* returns the value
		T& operator*() requires (!PointerLike<T>) {
			return value;
		}
		const T& operator*() const requires (!PointerLike<T>) {
			return value;
		}

		// ===== ARROW OPERATORS =====
		// For smart pointers: arrow goes through to element_type
		template<typename U = T>
		auto operator->() -> std::enable_if_t<SmartPointer<U>, typename U::element_type*> {
			return value.get();
		}

		template<typename U = T>
		auto operator->() const -> std::enable_if_t<SmartPointer<U>, typename U::element_type*> {
			return value.get();
		}

		// For raw pointers: just return the pointer
		T operator->() requires std::is_pointer_v<T> {
			return value;
		}
		T operator->() const requires std::is_pointer_v<T> {
			return value;
		}

		// For class types: return pointer to value
		T* operator->() requires std::is_class_v<T> && (!PointerLike<T>) {
			return &value;
		}
		const T* operator->() const requires std::is_class_v<T> && (!PointerLike<T>) {
			return &value;
		}

		// ===== COMPARISON OPERATORS =====
		// We provide both spaceship and traditional operators to avoid ambiguity

		// Three-way comparison with another Property
		auto operator<=>(const Property& other) const requires std::three_way_comparable<T> {
			return value <=> other.value;
		}

		// Equality with another Property
		bool operator==(const Property& other) const {
			return value == other.value;
		}

		// Traditional comparison operators with any type U
		template<typename U>
		bool operator<(const U& other) const {
			return value < other;
		}

		template<typename U>
		bool operator<=(const U& other) const {
			return value <= other;
		}

		template<typename U>
		bool operator>(const U& other) const {
			return value > other;
		}

		template<typename U>
		bool operator>=(const U& other) const {
			return value >= other;
		}

		template<typename U>
		bool operator==(const U& other) const {
			return value == other;
		}

		template<typename U>
		bool operator!=(const U& other) const {
			return value != other;
		}

		// Friend operators for reverse comparisons (e.g., 0 > property)
		template<typename U>
		friend bool operator<(const U& lhs, const Property& rhs) {
			return lhs < rhs.value;
		}

		template<typename U>
		friend bool operator<=(const U& lhs, const Property& rhs) {
			return lhs <= rhs.value;
		}

		template<typename U>
		friend bool operator>(const U& lhs, const Property& rhs) {
			return lhs > rhs.value;
		}

		template<typename U>
		friend bool operator>=(const U& lhs, const Property& rhs) {
			return lhs >= rhs.value;
		}

		template<typename U>
		friend bool operator==(const U& lhs, const Property& rhs) {
			return lhs == rhs.value;
		}

		template<typename U>
		friend bool operator!=(const U& lhs, const Property& rhs) {
			return lhs != rhs.value;
		}

		// ===== ARITHMETIC OPERATORS =====
		// Enable arithmetic operations for arithmetic types
		template<typename U>
		friend auto operator+(const Property& lhs, const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs.value + rhs;
		}

		template<typename U>
		friend auto operator+(const U& lhs, const Property& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs + rhs.value;
		}

		template<typename U>
		friend auto operator-(const Property& lhs, const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs.value - rhs;
		}

		template<typename U>
		friend auto operator-(const U& lhs, const Property& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs - rhs.value;
		}

		template<typename U>
		friend auto operator*(const Property& lhs, const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs.value * rhs;
		}

		template<typename U>
		friend auto operator*(const U& lhs, const Property& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs * rhs.value;
		}

		template<typename U>
		friend auto operator/(const Property& lhs, const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs.value / rhs;
		}

		template<typename U>
		friend auto operator/(const U& lhs, const Property& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			return lhs / rhs.value;
		}

		template<typename U>
		friend auto operator%(const Property& lhs, const U& rhs)
			requires std::is_integral_v<T>&& std::is_integral_v<U> {
			return lhs.value % rhs;
		}

		template<typename U>
		friend auto operator%(const U& lhs, const Property& rhs)
			requires std::is_integral_v<T>&& std::is_integral_v<U> {
			return lhs % rhs.value;
		}

		// ===== COMPOUND ASSIGNMENT OPERATORS =====
		template<typename U>
		Property& operator+=(const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			value += rhs;
			return *this;
		}

		template<typename U>
		Property& operator-=(const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			value -= rhs;
			return *this;
		}

		template<typename U>
		Property& operator*=(const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			value *= rhs;
			return *this;
		}

		template<typename U>
		Property& operator/=(const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			value /= rhs;
			return *this;
		}

		template<typename U>
		Property& operator%=(const U& rhs)
			requires std::is_integral_v<T>&& std::is_integral_v<U> {
			value %= rhs;
			return *this;
		}

		// Increment/decrement operators
		Property& operator++() requires std::is_arithmetic_v<T> {
			++value;
			return *this;
		}

		Property& operator--() requires std::is_arithmetic_v<T> {
			--value;
			return *this;
		}

		T operator++(int) requires std::is_arithmetic_v<T> {
			T old = value;
			++value;
			return old;
		}

		T operator--(int) requires std::is_arithmetic_v<T> {
			T old = value;
			--value;
			return old;
		}

		// ===== CONTAINER OPERATIONS =====
		// operator[] for container types
		template<typename I>
		auto& operator[](I&& i) requires requires { value[std::forward<I>(i)]; } {
			return value[std::forward<I>(i)];
		}

		template<typename I>
		const auto& operator[](I&& i) const requires requires { value[std::forward<I>(i)]; } {
			return value[std::forward<I>(i)];
		}

		// Function call operator
		template<typename... Args>
		auto operator()(Args&&... args)
			requires std::invocable<T, Args...> {
			return value(std::forward<Args>(args)...);
		}

		template<typename... Args>
		auto operator()(Args&&... args) const
			requires std::invocable<const T, Args...> {
			return value(std::forward<Args>(args)...);
		}

		// Iterator support
		auto begin() requires requires { value.begin(); } {
			return value.begin();
		}

		auto end() requires requires { value.end(); } {
			return value.end();
		}

		auto begin() const requires requires { value.begin(); } {
			return value.begin();
		}

		auto end() const requires requires { value.end(); } {
			return value.end();
		}

		// Serialization methods
		void save(cereal::JSONOutputArchive& ar) const override {
			if (allowSerialization) { 
				ar(cereal::make_nvp(name(), value));
			}
		}

		void load(cereal::JSONInputArchive& ar, bool a_usingPropertyOverride) override {
			if (!a_usingPropertyOverride || allowSerialization) {
				ar(cereal::make_nvp(name(), value));
			}
		}

		void save(cereal::BinaryOutputArchive& ar) const override {
			if (allowSerialization) { 
				ar(cereal::make_nvp(name(), value));
			}
		}

		void load(cereal::BinaryInputArchive& ar, bool a_usingPropertyOverride) override {
			if (!a_usingPropertyOverride || allowSerialization) {
				ar(cereal::make_nvp(name(), value));
			}
		}

		void save(cereal::PortableBinaryOutputArchive& ar) const override {
			if (allowSerialization) { 
				ar(cereal::make_nvp(name(), value));
			}
		}

		void load(cereal::PortableBinaryInputArchive& ar, bool a_usingPropertyOverride) override {
			if (!a_usingPropertyOverride || allowSerialization) {
				ar(cereal::make_nvp(name(), value));
			}
		}

		// Clone method
		void setCustomClone(std::function<void(Property<T>&, Property<T>&)> a_customClone) {
			customClone = a_customClone;
		}

		void cloneToTarget(PropertyBase& target) override {
			auto& t = static_cast<Property<T>&>(target);
			if (customClone){
				customClone(*this, t);
			} else {
				t.value = value;
			}
		}
	};

	// DeletedProperty - placeholder for removed properties in serialization
	template<typename T>
	class DeletedProperty : public PropertyBase {
	public:
		DeletedProperty(PropertyManager& a_list, std::string a_name)
			: PropertyBase(a_list, std::move(a_name)) {
		}

		DeletedProperty(const std::string& a_name)
			: PropertyBase(a_name) {
		}

		bool allowSave() const override { return false; }

		// No accessors, no value.
		const T& get() const = delete;
		T& get() = delete;

		void save(cereal::JSONOutputArchive&) const override {}
		void load(cereal::JSONInputArchive& ar, bool a_usingPropertyOverride) override {
			if (!a_usingPropertyOverride || allowSerialization) {
				T dummy;
				ar(cereal::make_nvp(name(), dummy));
			}
		}

		void save(cereal::BinaryOutputArchive&) const override {}
		void load(cereal::BinaryInputArchive& ar, bool a_usingPropertyOverride) override {
			if (!a_usingPropertyOverride || allowSerialization) {
				T dummy;
				ar(cereal::make_nvp(name(), dummy));
			}
		}

		void save(cereal::PortableBinaryOutputArchive&) const override {}
		void load(cereal::PortableBinaryInputArchive& ar, bool a_usingPropertyOverride) override {
			if (!a_usingPropertyOverride || allowSerialization) {
				T dummy;
				ar(cereal::make_nvp(name(), dummy));
			}
		}

		void cloneToTarget(PropertyBase&) override {
			// No-op
		}
	};

	// ObservableProperty - Property with change notifications
	template<typename T>
	class ObservableProperty : public Property<T> {
	private:
		template<typename F>
		static constexpr bool is_clone_function_v =
			std::is_invocable_v<F, Property<T>&, Property<T>&>;

	public:
		using ChangeSignature = void(const T& newValue, const T& oldValue, bool isFromLoad);

		// Constructor 1: No default value, no clone function
		inline ObservableProperty(PropertyManager& registry, std::string name)
			: Property<T>(registry, std::move(name))
			, onChanged(onChangedSignal) {
		}

		// Constructor 2: With value (but NOT a function type)
		template<typename U, std::enable_if_t<!is_clone_function_v<U>, int> = 0>
		inline ObservableProperty(PropertyManager& registry, std::string name, U&& defaultValue)
			: Property<T>(registry, std::move(name), std::forward<U>(defaultValue))
			, onChanged(onChangedSignal) {
		}

		// Constructor 3: With clone function only
		template<typename Fn, std::enable_if_t<is_clone_function_v<Fn>, int> = 0>
		inline ObservableProperty(PropertyManager& registry, std::string name, Fn&& cloneFn)
			: Property<T>(registry, std::move(name), std::forward<Fn>(cloneFn))
			, onChanged(onChangedSignal) {
		}

		// Constructor 4: With T constructed from initializer list
		inline ObservableProperty(PropertyManager& registry, std::string name, T defaultValue)
			: Property<T>(registry, std::move(name), std::move(defaultValue))
			, onChanged(onChangedSignal) {
		}

		// Constructor 5: With value and clone function
		template<typename U, typename Fn>
		inline ObservableProperty(PropertyManager& registry, std::string name, U&& defaultValue, Fn&& cloneFn)
			: Property<T>(registry, std::move(name), std::forward<U>(defaultValue), std::forward<Fn>(cloneFn))
			, onChanged(onChangedSignal) {
		}

		// Constructor 6: With T constructed from initializer list and clone function
		inline ObservableProperty(PropertyManager& registry, std::string name, T defaultValue,
			std::function<void(Property<T>&, Property<T>&)> cloneFn)
			: Property<T>(registry, std::move(name), std::move(defaultValue), std::move(cloneFn))
			, onChanged(onChangedSignal) {
		}

		// Move constructor
		ObservableProperty(ObservableProperty&& other) noexcept
			: Property<T>(std::move(other))
			, onChangedSignal(std::move(other.onChangedSignal))
			, onChanged(onChangedSignal) {  // Re-bind to our own signal
		}

		// Move assignment
		ObservableProperty& operator=(ObservableProperty&& other) noexcept {
			if (this != &other) {
				Property<T>::operator=(std::move(other));
				onChangedSignal = std::move(other.onChangedSignal);
				// Note: onChanged stays bound to our onChangedSignal
			}
			return *this;
		}

		// Override assignment operators to emit change signals
		inline ObservableProperty& operator=(const T& newVal) {
			if (this->value != newVal) {
				T oldVal = this->value;
				this->value = newVal;
				onChangedSignal(this->value, oldVal, false);
			}
			return *this;
		}

		ObservableProperty& operator=(T&& newVal) {
			if (this->value != newVal) {
				T oldVal = this->value;
				this->value = std::move(newVal);
				onChangedSignal(this->value, oldVal, false);
			}
			return *this;
		}

		// Override compound assignments to emit signals
		template<typename U>
		ObservableProperty& operator+=(const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			T oldVal = this->value;
			this->value += rhs;
			if (this->value != oldVal) {
				onChangedSignal(this->value, oldVal, false);
			}
			return *this;
		}

		template<typename U>
		ObservableProperty& operator-=(const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			T oldVal = this->value;
			this->value -= rhs;
			if (this->value != oldVal) {
				onChangedSignal(this->value, oldVal, false);
			}
			return *this;
		}

		template<typename U>
		ObservableProperty& operator*=(const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			T oldVal = this->value;
			this->value *= rhs;
			if (this->value != oldVal) {
				onChangedSignal(this->value, oldVal, false);
			}
			return *this;
		}

		template<typename U>
		ObservableProperty& operator/=(const U& rhs)
			requires std::is_arithmetic_v<T>&& std::is_arithmetic_v<U> {
			T oldVal = this->value;
			this->value /= rhs;
			if (this->value != oldVal) {
				onChangedSignal(this->value, oldVal, false);
			}
			return *this;
		}

		template<typename U>
		ObservableProperty& operator%=(const U& rhs)
			requires std::is_integral_v<T>&& std::is_integral_v<U> {
			T oldVal = this->value;
			this->value %= rhs;
			if (this->value != oldVal) {
				onChangedSignal(this->value, oldVal, false);
			}
			return *this;
		}

		ObservableProperty& operator++() requires std::is_arithmetic_v<T> {
			T oldVal = this->value;
			++this->value;
			onChangedSignal(this->value, oldVal, false);
			return *this;
		}

		ObservableProperty& operator--() requires std::is_arithmetic_v<T> {
			T oldVal = this->value;
			--this->value;
			onChangedSignal(this->value, oldVal, false);
			return *this;
		}

		T operator++(int) requires std::is_arithmetic_v<T> {
			T oldVal = this->value;
			++this->value;
			onChangedSignal(this->value, oldVal, false);
			return oldVal;
		}

		T operator--(int) requires std::is_arithmetic_v<T> {
			T oldVal = this->value;
			--this->value;
			onChangedSignal(this->value, oldVal, false);
			return oldVal;
		}

		// Override load methods to emit change signals
		void load(cereal::JSONInputArchive& ar, bool a_usingPropertyOverride) override {
			if (!a_usingPropertyOverride || this->allowSerialization) {
				T oldVal = this->value;
				Property<T>::load(ar, a_usingPropertyOverride);
				if (this->value != oldVal) {
					onChangedSignal(this->value, oldVal, true);
				}
			}
		}

		void load(cereal::BinaryInputArchive& ar, bool a_usingPropertyOverride) override {
			if (!a_usingPropertyOverride || this->allowSerialization) {
				T oldVal = this->value;
				Property<T>::load(ar, a_usingPropertyOverride);
				if (this->value != oldVal) {
					onChangedSignal(this->value, oldVal, true);
				}
			}
		}

		void load(cereal::PortableBinaryInputArchive& ar, bool a_usingPropertyOverride) override {
			if (!a_usingPropertyOverride || this->allowSerialization) {
				T oldVal = this->value;
				Property<T>::load(ar, a_usingPropertyOverride);
				if (this->value != oldVal) {
					onChangedSignal(this->value, oldVal, true);
				}
			}
		}

		jai::signal<ChangeSignature> onChanged;

	protected:
		jai::signal_emitter<ChangeSignature> onChangedSignal;
	};

	// PropertyRegistry implementation
	inline void PropertyManager::add(PropertyBase* prop) {
		properties[prop->name()] = prop;
	}

	inline void PropertyManager::cloneToTarget(PropertyManager& target) const {
		for (auto& [k, v] : properties) {
			auto it = target.properties.find(k);
			if (it != target.properties.end()) {
				v->cloneToTarget(*it->second);
			}
		}
	}

	inline PropertyBase* PropertyManager::get(const std::string& key) const {
		auto it = properties.find(key);
		return it != properties.end() ? it->second : nullptr;
	}

	template<typename T>
	inline Property<T>* PropertyManager::get(const std::string& key) const {
		auto base = get(key);
		return base ? dynamic_cast<Property<T>*>(base) : nullptr;
	}

	template<typename T>
	inline ObservableProperty<T>* PropertyManager::getObservable(const std::string& key) const {
		auto base = get(key);
		return base ? dynamic_cast<ObservableProperty<T>*>(base) : nullptr;
	}

	template<typename T>
	inline T* PropertyManager::getValue(const std::string& key) const {
		if (auto prop = get<T>(key)) {
			return &prop->get();
		}
		return nullptr;
	}

} // namespace MV

#endif // _MV_PROPERTIES_H_

// Helper macros
#define MV_EXPAND(x) x
#define MV_REMOVE_PARENS_IMPL(...) __VA_ARGS__
#define MV_REMOVE_PARENS(x) MV_EXPAND(MV_REMOVE_PARENS_IMPL x)

// Main property macros
#define MV_PROPERTY(type, name, ...) \
    MV::Property<MV_REMOVE_PARENS(type)> name{ propertyManager, #name, ##__VA_ARGS__ }

#define MV_OBSERVABLE_PROPERTY(type, name, ...) \
    MV::ObservableProperty<MV_REMOVE_PARENS(type)> name{ propertyManager, #name, ##__VA_ARGS__ }

#define MV_DELETED_PROPERTY(type, name) \
    MV::DeletedProperty<MV_REMOVE_PARENS(type)> name{ propertyManager, #name }

// Named property macros
#define MV_NAMED_PROPERTY(type, propName, varName, ...) \
    MV::Property<MV_REMOVE_PARENS(type)> varName{ propertyManager, propName, ##__VA_ARGS__ }

#define MV_NAMED_OBSERVABLE_PROPERTY(type, propName, varName, ...) \
    MV::ObservableProperty<MV_REMOVE_PARENS(type)> varName{ propertyManager, propName, ##__VA_ARGS__ }