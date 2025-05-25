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
#include "signal.hpp"
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

	// Forward declarations
	class PropertyBase;
	template<typename T> class Property;
	template<typename T> class ObservableProperty;

	// PropertyRegistry - manages all properties
	class PropertyRegistry {
	public:
		PropertyRegistry() = default;
		PropertyRegistry(PropertyRegistry&&) = default;

		PropertyRegistry(const PropertyRegistry&) = delete;
		PropertyRegistry& operator=(const PropertyRegistry&) = delete;

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

		template <class Archive>
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

		template <class Archive>
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

		template <class Archive>
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
				MV::require<ResourceException>(it != properties.end(), "Unknown Property: ",
					(wasRenamed ? found->second : name),
					" If a property was deleted, you will need to either recreate this archive, or, add a DeletedProperty<TheDeletedPropertyType> with this name.");
				it->second->load(ar, usingPropertyOverride);
			}
		}

		inline void cloneToTarget(PropertyRegistry& target) const;

	private:
		std::map<std::string, PropertyBase*> properties;
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

		inline PropertyBase(PropertyRegistry& a_propertyRegister, std::string a_name)
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

	public:
		using value_type = T;

		// Constructor
		inline Property(PropertyRegistry& reg, std::string name, T def = T{},
			std::function<void(Property<T>&, Property<T>&)> cl = {})
			: PropertyBase(reg, std::move(name))
			, customClone(std::move(cl))
			, value(std::move(def)) {
		}

		Property(const Property&) = delete;
		Property& operator=(const Property&) = delete;

		// Assignment operators
		Property& operator=(const T& v) { value = v; return *this; }
		Property& operator=(T&& v) { value = std::move(v); return *this; }

		// ===== CONVERSION OPERATORS =====
		// Implicit conversion to T (works for all types)
		operator T() const noexcept requires std::is_arithmetic_v<T> {
			return value;  // Return by value for arithmetic types
		}

		operator const T& () const noexcept requires (!std::is_arithmetic_v<T>) {
			return value;  // Return by reference for non-arithmetic types
		}

		// Mutable conversion for non-const contexts
		operator T& () noexcept requires (!std::is_arithmetic_v<T>) {
			return value;
		}

		// Boolean conversion for all non-bool types
		operator bool() const noexcept requires (!std::is_same_v<T, bool>) && requires(const T& t) { static_cast<bool>(t); } {
			return static_cast<bool>(value);
		}

		// ===== ACCESSOR METHODS =====
		T& get() noexcept { return value; }
		const T& get() const noexcept { return value; }

		// ===== DEREFERENCE OPERATORS =====
		// For smart pointers: operator* returns the smart pointer itself
		T& operator*() noexcept requires SmartPointer<T> {
			return value;
		}
		const T& operator*() const noexcept requires SmartPointer<T> {
			return value;
		}

		// For raw pointers: operator* dereferences
		auto& operator*() noexcept requires std::is_pointer_v<T> {
			return *value;
		}
		const auto& operator*() const noexcept requires std::is_pointer_v<T> {
			return *value;
		}

		// For non-pointer types: operator* returns the value
		T& operator*() noexcept requires (!PointerLike<T>) {
			return value;
		}
		const T& operator*() const noexcept requires (!PointerLike<T>) {
			return value;
		}

		// ===== ARROW OPERATORS =====
		// For smart pointers: arrow goes through to element_type
		auto operator->() noexcept requires SmartPointer<T> {
			return value.get();
		}
		auto operator->() const noexcept requires SmartPointer<T> {
			return value.get();
		}

		// For raw pointers: just return the pointer
		T operator->() noexcept requires std::is_pointer_v<T> {
			return value;
		}
		T operator->() const noexcept requires std::is_pointer_v<T> {
			return value;
		}

		// For class types: return pointer to value
		T* operator->() noexcept requires std::is_class_v<T> && (!PointerLike<T>) {
			return &value;
		}
		const T* operator->() const noexcept requires std::is_class_v<T> && (!PointerLike<T>) {
			return &value;
		}

		// ===== COMPARISON OPERATORS =====
		// We provide both spaceship and traditional operators to avoid ambiguity

		// Three-way comparison with another Property
		auto operator<=>(const Property& other) const noexcept
			requires std::three_way_comparable<T> {
			return value <=> other.value;
		}

		// Equality with another Property
		bool operator==(const Property& other) const noexcept {
			return value == other.value;
		}

		// Traditional comparison operators with any type U
		template<typename U>
		bool operator<(const U& other) const noexcept {
			return value < other;
		}

		template<typename U>
		bool operator<=(const U& other) const noexcept {
			return value <= other;
		}

		template<typename U>
		bool operator>(const U& other) const noexcept {
			return value > other;
		}

		template<typename U>
		bool operator>=(const U& other) const noexcept {
			return value >= other;
		}

		template<typename U>
		bool operator==(const U& other) const noexcept {
			return value == other;
		}

		template<typename U>
		bool operator!=(const U& other) const noexcept {
			return value != other;
		}

		// Friend operators for reverse comparisons (e.g., 0 > property)
		template<typename U>
		friend bool operator<(const U& lhs, const Property& rhs) noexcept {
			return lhs < rhs.value;
		}

		template<typename U>
		friend bool operator<=(const U& lhs, const Property& rhs) noexcept {
			return lhs <= rhs.value;
		}

		template<typename U>
		friend bool operator>(const U& lhs, const Property& rhs) noexcept {
			return lhs > rhs.value;
		}

		template<typename U>
		friend bool operator>=(const U& lhs, const Property& rhs) noexcept {
			return lhs >= rhs.value;
		}

		template<typename U>
		friend bool operator==(const U& lhs, const Property& rhs) noexcept {
			return lhs == rhs.value;
		}

		template<typename U>
		friend bool operator!=(const U& lhs, const Property& rhs) noexcept {
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
			if (allowSerialization) { ar(cereal::make_nvp(name(), value)); }
		}

		void load(cereal::JSONInputArchive& ar, bool a_usingPropertyOverride) override {
			if (!a_usingPropertyOverride || allowSerialization) {
				ar(cereal::make_nvp(name(), value));
			}
		}

		void save(cereal::BinaryOutputArchive& ar) const override {
			if (allowSerialization) { ar(cereal::make_nvp(name(), value)); }
		}

		void load(cereal::BinaryInputArchive& ar, bool a_usingPropertyOverride) override {
			if (!a_usingPropertyOverride || allowSerialization) {
				ar(cereal::make_nvp(name(), value));
			}
		}

		void save(cereal::PortableBinaryOutputArchive& ar) const override {
			if (allowSerialization) { ar(cereal::make_nvp(name(), value)); }
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
			if (customClone) customClone(*this, t);
			else t.value = value;
		}
	};

	// DeletedProperty - placeholder for removed properties in serialization
	template<typename T>
	class DeletedProperty : public PropertyBase {
	public:
		DeletedProperty(PropertyRegistry& a_list, std::string a_name)
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
	public:
		using ChangeSignature = void(const T& newValue, const T& oldValue, bool isFromLoad);

		inline ObservableProperty(PropertyRegistry& registry, std::string name, T defaultValue = T{},
			std::function<void(Property<T>&, Property<T>&)> cloneFn = {})
			: Property<T>(registry, std::move(name), std::move(defaultValue), std::move(cloneFn)),
			onChanged(onChangedSignal) {
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

		MV::SignalRegister<ChangeSignature> onChanged;

	protected:
		MV::Signal<ChangeSignature> onChangedSignal;
	};

	// PropertyRegistry implementation
	inline void PropertyRegistry::add(PropertyBase* prop) {
		properties[prop->name()] = prop;
	}

	inline void PropertyRegistry::cloneToTarget(PropertyRegistry& target) const {
		for (auto& [k, v] : properties) {
			auto it = target.properties.find(k);
			if (it != target.properties.end()) {
				v->cloneToTarget(*it->second);
			}
		}
	}

	inline PropertyBase* PropertyRegistry::get(const std::string& key) const {
		auto it = properties.find(key);
		return it != properties.end() ? it->second : nullptr;
	}

	template<typename T>
	inline Property<T>* PropertyRegistry::get(const std::string& key) const {
		auto base = get(key);
		return base ? dynamic_cast<Property<T>*>(base) : nullptr;
	}

	template<typename T>
	inline ObservableProperty<T>* PropertyRegistry::getObservable(const std::string& key) const {
		auto base = get(key);
		return base ? dynamic_cast<ObservableProperty<T>*>(base) : nullptr;
	}

	template<typename T>
	inline T* PropertyRegistry::getValue(const std::string& key) const {
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

// Main MV_PROPERTY macro (Always parentheses required)
#define MV_PROPERTY(type, name, ...) \
    MV::Property<MV_REMOVE_PARENS(type)> name{ properties, #name, __VA_ARGS__ }

#define MV_OBSERVABLE_PROPERTY(type, name, ...) \
    MV::ObservableProperty<MV_REMOVE_PARENS(type)> name{ properties, #name, __VA_ARGS__ }

#define MV_DELETED_PROPERTY(type, name) \
    MV::DeletedProperty<MV_REMOVE_PARENS(type)> name{ properties, #name }