#ifndef _MV_PROPERTIES_H_
#define _MV_PROPERTIES_H_

#include <string>
#include <map>
#include <vector>
#include <functional>
#include "require.hpp"
#include "signal.hpp"
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/binary.hpp>

namespace MV {

	class PropertyBase;

	class PropertyRegistry {
	public:
		PropertyRegistry() = default;
		PropertyRegistry(PropertyRegistry&&) = default;

		PropertyRegistry(const PropertyRegistry&) = delete;
		PropertyRegistry& operator=(const PropertyRegistry&) = delete;

		inline void add(PropertyBase* a_property);

		const std::map<std::string, PropertyBase*>& all() const { return properties; }

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
		void load(Archive& ar, const std::vector<std::string>& a_optionalKeyOrderOverride = {}) {
			std::vector<std::string> savedKeys;
			if (a_optionalKeyOrderOverride.empty())
			{
				ar(cereal::make_nvp("PropertyKeys", savedKeys));
			} else {
				savedKeys = a_optionalKeyOrderOverride;
			}

			std::map<std::string, bool> loaded;
			for (const auto& name : savedKeys) {
				auto it = properties.find(name);
				MV::require<ResourceException>(it != properties.end(), "Unknown Property: ", name, " If a property was deleted, you will need to either recreate this archive, or, add a DeletedProperty<TheDeletedPropertyType> with this name.");
				it->second->load(ar);
				loaded[name] = true;
			}
		}

		inline void cloneTo(PropertyRegistry& target) const;

	private:
		std::map<std::string, PropertyBase*> properties;
	};


	class PropertyBase {
	public:
		bool operator<(const PropertyBase& rhs) const {
			return name() < rhs.name();
		}

		const std::string& name() const { return propertyName; }

		bool serializeEnabled() const { return allowSerialization; }
		void serializeEnabled(bool a_allowSerialization) { allowSerialization = a_allowSerialization; }

		virtual void save(cereal::JSONOutputArchive& ar) const = 0;
		virtual void load(cereal::JSONInputArchive& ar) = 0;
		virtual void save(cereal::BinaryOutputArchive& ar) const = 0;
		virtual void load(cereal::BinaryInputArchive& ar) = 0;
		virtual void save(cereal::PortableBinaryOutputArchive& ar) const = 0;
		virtual void load(cereal::PortableBinaryInputArchive& ar) = 0;

		virtual void cloneInto(PropertyBase& target) = 0;

	protected:
		PropertyBase(const std::string& a_name)
			: propertyName(a_name) {
		}

		PropertyBase(PropertyRegistry& a_list, const std::string& a_name)
			: PropertyBase(a_name) {
			a_list.add(this);
		}

		bool allowSerialization = true;
	private:
		std::string propertyName;
	};

	inline void PropertyRegistry::add(PropertyBase* a_property) {
		properties[a_property->name()] = a_property;
	}

	inline void PropertyRegistry::cloneTo(PropertyRegistry& target) const {
		for (const auto& [name, prop] : properties) {
			auto it = target.properties.find(name);
			if (it != target.properties.end()) {
				prop->cloneInto(*it->second);
			}
		}
	}

	template<typename T>
	class Property : public PropertyBase {
	public:
		using value_type = T;

		Property(PropertyRegistry& a_registry, const std::string& a_name, const T& a_defaultVal = T{}, std::function<void(Property<T>&, Property<T>&)> a_customClone = {})
			: PropertyBase(a_registry, a_name),
			cloneIntoImplementation(std::move(a_customClone)),
			value(a_defaultVal) {
		}

		Property(const Property<T>&) = delete;

		//Assignment copies the value.
		Property& operator=(const Property<T>& a_rhs) = delete;

		Property& operator=(const T& a_rhs) {
			value = a_rhs;
			return *this;
		}

		operator const T& () const { return value; }

		[[nodiscard]]
		const T& get() const { return value; }

		[[nodiscard]]
		T& get() { return value; }

		template<typename U = T>
		std::enable_if_t<std::is_class<U>::value, const U*> operator->() const { return &value; }

		template<typename U = T>
		std::enable_if_t<std::is_class<U>::value, U*> operator->() { return &value; }

		template<typename Index>
		auto operator[](Index&& index)
			-> decltype(std::declval<T&>()[std::forward<Index>(index)]) {
			return value[std::forward<Index>(index)];
		}

		template<typename Index>
		auto operator[](Index&& index) const
			-> decltype(std::declval<const T&>()[std::forward<Index>(index)]) {
			return value[std::forward<Index>(index)];
		}

		template<typename U = T>
		auto begin() -> decltype(std::declval<U&>().begin()) {
			return value.begin();
		}

		template<typename U = T>
		auto end() -> decltype(std::declval<U&>().end()) {
			return value.end();
		}

		template<typename U = T>
		auto begin() const -> decltype(std::declval<const U&>().begin()) {
			return value.begin();
		}

		template<typename U = T>
		auto end() const -> decltype(std::declval<const U&>().end()) {
			return value.end();
		}

		void save(cereal::JSONOutputArchive& ar) const override {
			if(allowSerialization) {
				ar(cereal::make_nvp(name(), value));
			}
		}

		void load(cereal::JSONInputArchive& ar) override {
			if (allowSerialization) {
				ar(cereal::make_nvp(name(), value));
			}
		}

		void save(cereal::BinaryOutputArchive& ar) const override {
			if (allowSerialization) {
				ar(cereal::make_nvp(name(), value));
			}
		}

		void load(cereal::BinaryInputArchive& ar) override {
			if (allowSerialization) {
				ar(cereal::make_nvp(name(), value));
			}
		}

		void save(cereal::PortableBinaryOutputArchive& ar) const override {
			if (allowSerialization) {
				ar(cereal::make_nvp(name(), value));
			}
		}
		void load(cereal::PortableBinaryInputArchive& ar) override {
			if (allowSerialization) {
				ar(cereal::make_nvp(name(), value));
			}
		}

		void cloneInto(PropertyBase& destination) override {
			if(cloneIntoImplementation)
			{
				cloneIntoImplementation(static_cast<Property<T>&>(*this), static_cast<Property<T>&>(destination));
			}else{
				static_cast<Property<T>&>(destination).value = value;
			}
		}

	protected:
		T value{};
		std::function<void(Property<T> &self, Property<T>& destination)> cloneIntoImplementation;
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
	class DeletedProperty : public PropertyBase {
	public:
		DeletedProperty(PropertyRegistry& a_list, const std::string& a_name)
			: PropertyBase(a_list, a_name) {
		}

		DeletedProperty(const std::string& a_name)
			: PropertyBase(a_name) {
		}

		// No accessors, no value.
		const T& get() const = delete;
		T& get() = delete;

		void save(cereal::JSONOutputArchive& ar) const override {}
		void load(cereal::JSONInputArchive& ar) override {
			if(allowSerialization){ //counter intuitively we never allow serialization of deleted properties, but we need to load and ignore them if this is true.
				T ignored;
				ar(cereal::make_nvp(name(), ignored));
			}
		}

		void save(cereal::BinaryOutputArchive& ar) const override {}
		void load(cereal::BinaryInputArchive& ar) override {
			if (allowSerialization) { //counter intuitively we never allow serialization of deleted properties, but we need to load and ignore them if this is true.
				T ignored;
				ar(cereal::make_nvp(name(), ignored));
			}
		}

		void save(cereal::PortableBinaryOutputArchive& ar) const override {}
		void load(cereal::PortableBinaryInputArchive& ar) override {
			if (allowSerialization) { //counter intuitively we never allow serialization of deleted properties, but we need to load and ignore them if this is true.
				T ignored;
				ar(cereal::make_nvp(name(), ignored));
			}
		}

		void cloneInto(PropertyBase&) override {
			// No-op
		}
	};

	template<typename T>
	class ObservableProperty : public Property<T> {
	public:
		using ChangeSignature = void(const T& newValue, const T& oldValue, bool isFromLoad);

		ObservableProperty(PropertyRegistry& registry, const std::string& name, const T& defaultValue = T{},
			std::function<void(Property<T>&, Property<T>&)> cloneFn = {})
			: Property<T>(registry, name, defaultValue, std::move(cloneFn)),
			onChanged(onChangedSignal) {
		}

		// same as Property<T>::operator=(T), but also emits onChanged
		ObservableProperty& operator=(const T& newVal) {
			if (this->value != newVal) {
				T oldVal = this->value;
				this->value = newVal;
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

} // namespace MV

#endif // _MV_PROPERTIES_H_


#define MV_PROPERTY(type, name, ...) \
	MV::Property<type> name{ properties, #name, __VA_ARGS__ }

#define MV_OBSERVABLE_PROPERTY(type, name, ...) \
	MV::ObservableProperty<type> name{ properties, #name, __VA_ARGS__ }

#define MV_DELETED_PROPERTY(type, name) \
	MV::DeletedProperty<type> name{ properties, #name }