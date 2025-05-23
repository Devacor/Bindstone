#ifndef _MV_PROPERTIES_H_
#define _MV_PROPERTIES_H_

#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include <utility>
#include <memory>
#include "require.hpp"
#include "signal.hpp"
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/portable_binary.hpp>

namespace MV {

	class PropertyBase;
	template<typename T> class Property;
	template<typename T> class ObservableProperty;

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
				keys.push_back(name);
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
				MV::require<ResourceException>(it != properties.end(), "Unknown Property: ", name, " If a property was deleted, you will need to either recreate this archive, or, add a DeletedProperty<TheDeletedPropertyType> with this name.");
				it->second->load(ar);
			}
		}

		template <class Archive>
		void load(Archive& ar, const std::vector<std::string>& a_optionalKeyOrderOverride, const std::unordered_map<std::string, std::string>& a_optionalKeyRenameBindings = {}) {
			std::vector<std::string> savedKeys;
			if (a_optionalKeyOrderOverride.empty())
			{
				ar(cereal::make_nvp("PropertyKeys", savedKeys));
			} else {
				savedKeys = a_optionalKeyOrderOverride;
			}

			for (const auto& name : savedKeys) {
				const auto found = a_optionalKeyRenameBindings.find(name);
				bool wasRenamed = found != a_optionalKeyRenameBindings.end();
				auto it = properties.find(wasRenamed ? found->second : name);
				MV::require<ResourceException>(it != properties.end(), "Unknown Property: ", (wasRenamed ? found->second : name), " If a property was deleted, you will need to either recreate this archive, or, add a DeletedProperty<TheDeletedPropertyType> with this name.");
				it->second->load(ar);
			}
		}

		inline void cloneToTarget(PropertyRegistry& target) const;

	private:
		std::map<std::string, PropertyBase*> properties;
	};

	class PropertyBase {
	public:
		virtual ~PropertyBase() = default;

		bool operator<(const PropertyBase& rhs) const {
			return name() < rhs.name();
		}

		const std::string& name() const { return propertyName; }

		bool serializeEnabled() const { return allowSerialization; }
		void serializeEnabled(bool a_allowSerialization) const { allowSerialization = a_allowSerialization; } //must be callable from within a const save method to skip serialize.

		virtual void save(cereal::JSONOutputArchive& ar) const = 0;
		virtual void load(cereal::JSONInputArchive& ar) = 0;
		virtual void save(cereal::BinaryOutputArchive& ar) const = 0;
		virtual void load(cereal::BinaryInputArchive& ar) = 0;
		virtual void save(cereal::PortableBinaryOutputArchive& ar) const = 0;
		virtual void load(cereal::PortableBinaryInputArchive& ar) = 0;

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

	inline void PropertyRegistry::add(PropertyBase* prop) { properties[prop->name()] = prop; }

	inline void PropertyRegistry::cloneToTarget(PropertyRegistry& target) const {
		for (auto& [k, v] : properties) {
			auto it = target.properties.find(k);
			if (it != target.properties.end()) {
				v->cloneToTarget(*it->second);
			}
		}
	}

	// Type-traits for operators
	namespace details {
		template<typename, typename = void> struct has_arrow : std::false_type {};
		template<typename T> struct has_arrow<T, std::void_t<decltype(std::declval<T&>().operator->())>> : std::true_type {};

		template<typename, typename = void> struct has_deref : std::false_type {};
		template<typename T> struct has_deref<T, std::void_t<decltype(std::declval<T&>().operator*())>> : std::true_type {};
		template<> struct has_deref<std::string> : std::false_type {};
		template<> struct has_deref<const std::string> : std::false_type {};

		template<typename T, typename = void> struct has_begin : std::false_type {};
		template<typename T> struct has_begin<T, std::void_t<decltype(std::declval<T&>().begin())>> : std::true_type {};

		template<typename T, typename = void> struct has_end : std::false_type {};
		template<typename T> struct has_end<T, std::void_t<decltype(std::declval<T&>().end())>> : std::true_type {};

		// detect operator[]
		template<typename, typename = void> struct has_subscript : std::false_type {};
		template<typename T> struct has_subscript< T, std::void_t<decltype(std::declval<T&>()[std::declval<std::size_t>()])> > : std::true_type {};

		template<typename T, typename = void> struct deref_ret {};
		template<typename T> struct deref_ret<T, std::enable_if_t<std::is_class_v<T>&& has_deref<T>::value>> {
			using type = decltype(std::declval<T&>().operator*());
		};

		template<typename T, typename = void> struct arrow_ret {};
		template<typename T> struct arrow_ret<T, std::enable_if_t<std::is_class_v<T>&& has_arrow<T>::value>> {
			using type = decltype(std::declval<T&>().operator->());
		};
	}

	template<typename T>
	class Property : public PropertyBase {
	protected:
		T value;
		std::function<void(Property<T>&, Property<T>&)> customClone;

	public:
		using value_type = T;

		inline Property(PropertyRegistry& reg, std::string name, T def = T{}, std::function<void(Property<T>&, Property<T>&)> cl = {})
			: PropertyBase(reg, std::move(name))
			, customClone(std::move(cl))
			, value(std::move(def)) {
		}

		Property(const Property&) = delete;
		Property& operator=(const Property&) = delete;
		Property& operator=(const T& v) { value = v; return *this; }
		Property& operator=(T&& v) {
			value = std::move(v);
			return *this;
		}
		operator const T& () const noexcept { return value; }

		template<typename U = T, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
		operator U() const noexcept {
			return value;
		}

		T& get() { return value; }
		const T& get() const { return value; }

		// operator*
		template<typename U = T>
		auto operator*() -> std::enable_if_t<details::has_deref<U>::value, typename details::deref_ret<U>::type> {
			return value.operator*();
		}
		template<typename U = T>
		auto operator*() const -> std::enable_if_t<details::has_deref<const U>::value, typename details::deref_ret<const U>::type> {
			return value.operator*();
		}
		template<typename U = T>
		auto operator*() -> std::enable_if_t<std::is_class_v<U> && !details::has_deref<U>::value, U&> { return value; }
		template<typename U = T>
		auto operator*() const -> std::enable_if_t<std::is_class_v<const U> && !details::has_deref<const U>::value, const U&> { return value; }
		template<typename U = T>
		auto operator*()->std::enable_if_t<!std::is_class_v<U>, void> = delete;

		// operator-> for class types without arrow
		template<typename U = T>
		auto operator->() -> std::enable_if_t<std::is_class_v<U> && !details::has_arrow<U>::value, U*> { return &value; }
		template<typename U = T>
		auto operator->() const -> std::enable_if_t<std::is_class_v<const U> && !details::has_arrow<const U>::value, const U*> { return &value; }

		// operator-> for types that define operator->
		template<typename U = T>
		auto operator->() -> std::enable_if_t<details::has_arrow<U>::value, typename details::arrow_ret<U>::type> { return value.operator->(); }
		template<typename U = T>
		auto operator->() const -> std::enable_if_t<details::has_arrow<const U>::value, typename details::arrow_ret<const U>::type> { return value.operator->(); }

		// --- raw-pointer support ---
		template<typename U = T>
		auto operator->() -> std::enable_if_t<std::is_pointer_v<U>, U> { return value; }
		template<typename U = T>
		auto operator->() const -> std::enable_if_t<std::is_pointer_v<U>, U> { return value; }

		template<typename U = T>
		auto operator*() -> std::enable_if_t<std::is_pointer_v<U>, U> { return value; }
		template<typename U = T>
		auto operator*() const -> std::enable_if_t<std::is_pointer_v<U>, U> { return value; }

		// operator[] only for container-like types (has_subscript and has_begin)
		template<typename I, typename U = T>
		auto operator[](I&& i)
			-> std::enable_if_t<
			details::has_subscript<U>::value&&
			details::has_begin<U>::value,
			decltype(value[std::forward<I>(i)])
			>
		{
			return value[std::forward<I>(i)];
		}
		template<typename I, typename U = T>
		auto operator[](I&& i) const
			-> std::enable_if_t<
			details::has_subscript<const U>::value&&
			details::has_begin<const U>::value,
			decltype(std::declval<const U&>()[std::forward<I>(i)])
			>
		{
			return value[std::forward<I>(i)];
		}

		// operator()
		template<typename... A>
		auto operator()(A&&... a) -> decltype(value(std::forward<A>(a)...)) {
			return value(std::forward<A>(a)...);
		}
		template<typename... A>
		auto operator()(A&&... a) const -> decltype(std::declval<const T&>()(std::forward<A>(a)...)) {
			return value(std::forward<A>(a)...);
		}

		// iteration
		template<typename U = T>
		auto begin() -> std::enable_if_t<details::has_begin<U>::value, decltype(std::declval<U&>().begin())> { return value.begin(); }
		template<typename U = T>
		auto end() -> std::enable_if_t<details::has_end<U>::value, decltype(std::declval<U&>().end())> { return value.end(); }
		template<typename U = T>
		auto begin() const -> std::enable_if_t<details::has_begin<const U>::value, decltype(std::declval<const U&>().begin())> { return value.begin(); }
		template<typename U = T>
		auto end() const -> std::enable_if_t<details::has_end<const U>::value, decltype(std::declval<const U&>().end())> { return value.end(); }

		// serialization
		void save(cereal::JSONOutputArchive& ar) const override { if (allowSerialization) ar(cereal::make_nvp(name(), value)); }
		void load(cereal::JSONInputArchive& ar) override { if (allowSerialization) ar(cereal::make_nvp(name(), value)); }
		void save(cereal::BinaryOutputArchive& ar) const override { if (allowSerialization) ar(cereal::make_nvp(name(), value)); }
		void load(cereal::BinaryInputArchive& ar) override { if (allowSerialization) ar(cereal::make_nvp(name(), value)); }
		void save(cereal::PortableBinaryOutputArchive& ar) const override { if (allowSerialization) ar(cereal::make_nvp(name(), value)); }
		void load(cereal::PortableBinaryInputArchive& ar) override { if (allowSerialization) ar(cereal::make_nvp(name(), value)); }

		// clone
		void setCustomClone(std::function<void(Property<T>&, Property<T>&)> a_customClone) {
			customClone = a_customClone;
		}

		void cloneToTarget(PropertyBase& target) override {
			auto& t = static_cast<Property<T>&>(target);
			if (customClone) customClone(*this, t);
			else t.value = value;
		}
	};

	template<typename T>
	bool operator==(const Property<T>& lhs, const Property<T>& rhs) {
		return lhs.get() == rhs.get();
	}

	template<typename T>
	bool operator==(const T& lhs, const Property<T>& rhs) {
		return lhs == rhs.get();
	}

	template<typename T>
	bool operator==(const Property<T>& lhs, const T& rhs) {
		return lhs.get() == rhs;
	}

	template<typename T>
	bool operator!=(const Property<T>& lhs, const Property<T>& rhs) {
		return !(lhs == rhs);
	}

	template<typename T>
	bool operator!=(const T& lhs, const Property<T>& rhs) {
		return !(lhs == rhs);
	}

	template<typename T>
	bool operator!=(const Property<T>& lhs, const T& rhs) {
		return !(lhs == rhs);
	}

	template<typename T>
	class DeletedProperty : public PropertyBase {
	public:
		DeletedProperty(PropertyRegistry& a_list,  std::string a_name)
			: PropertyBase(a_list, std::move(a_name)) {
		}

		DeletedProperty(const std::string& a_name)
			: PropertyBase(a_name) {
		}

		// No accessors, no value.
		const T& get() const = delete;
		T& get() = delete;

		//allowSerialization in this context only blocks the consumption and disregarding of the parameter being loaded (for supporting conditionally serialized data). DeletedProperties do not serialize.
		void save(cereal::JSONOutputArchive&) const override {}
		void load(cereal::JSONInputArchive& ar) override { if (allowSerialization) { T dummy; ar(cereal::make_nvp(name(), dummy)); } }
		void save(cereal::BinaryOutputArchive&) const override {}
		void load(cereal::BinaryInputArchive& ar) override { if (allowSerialization) { T dummy; ar(cereal::make_nvp(name(), dummy)); } }
		void save(cereal::PortableBinaryOutputArchive&) const override {}
		void load(cereal::PortableBinaryInputArchive& ar) override { if (allowSerialization) { T dummy; ar(cereal::make_nvp(name(), dummy)); } }

		void cloneToTarget(PropertyBase&) override {
			// No-op
		}
	};

	template<typename T>
	class ObservableProperty : public Property<T> {
	public:
		using ChangeSignature = void(const T& newValue, const T& oldValue, bool isFromLoad);

		inline ObservableProperty(PropertyRegistry& registry, std::string name, T defaultValue = T{},
			std::function<void(Property<T>&, Property<T>&)> cloneFn = {})
			: Property<T>(registry, std::move(name), std::move(defaultValue), std::move(cloneFn)),
			onChanged(onChangedSignal) {
		}

		// same as Property<T>::operator=(T), but also emits onChanged
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

		void load(cereal::JSONInputArchive& ar) override {
			if (this->allowSerialization) {
				T oldVal = this->value;
				Property<T>::load(ar);
				if (this->value != oldVal) {
					onChangedSignal(this->value, oldVal, true);
				}
			}
		}

		void load(cereal::BinaryInputArchive& ar) override {
			if (this->allowSerialization) {
				T oldVal = this->value;
				Property<T>::load(ar);
				if (this->value != oldVal) {
					onChangedSignal(this->value, oldVal, true);
				}
			}
		}

		void load(cereal::PortableBinaryInputArchive& ar) override {
			if (this->allowSerialization) {
				T oldVal = this->value;
				Property<T>::load(ar);
				if (this->value != oldVal) {
					onChangedSignal(this->value, oldVal, true);
				}
			}
		}

		MV::SignalRegister<ChangeSignature> onChanged;

	protected:
		MV::Signal<ChangeSignature> onChangedSignal;
	};

	inline PropertyBase* PropertyRegistry::get(const std::string& key) const {
		auto it = properties.find(key);
		return it != properties.end() ? it->second : nullptr;
	}

	template<typename T>
	Property<T>* PropertyRegistry::get(const std::string& key) const {
		auto base = get(key);
		return base ? dynamic_cast<Property<T>*>(base) : nullptr;
	}

	template<typename T>
	ObservableProperty<T>* PropertyRegistry::getObservable(const std::string& key) const {
		auto base = get(key);
		return base ? dynamic_cast<ObservableProperty<T>*>(base) : nullptr;
	}

	template<typename T>
	T* PropertyRegistry::getValue(const std::string& key) const {
		if (auto prop = get<T>(key)) {
			return &prop->get();
		}
		return nullptr;
	}

} // namespace MV

#endif // _MV_PROPERTIES_H_


#define MV_PROPERTY(type, name, ...) \
	MV::Property<type> name{ properties, #name, __VA_ARGS__ }

#define MV_OBSERVABLE_PROPERTY(type, name, ...) \
	MV::ObservableProperty<type> name{ properties, #name, __VA_ARGS__ }

#define MV_DELETED_PROPERTY(type, name) \
	MV::DeletedProperty<type> name{ properties, #name }