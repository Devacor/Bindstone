#ifndef _MV_SERVICES_H_
#define _MV_SERVICES_H_

#include <typeindex>
#include <unordered_map>
#include "boost/any.hpp"

namespace MV {
	template <typename T>
	class Service {
	public:
		Service(T* a_service) :
			service(a_service) {
		}
		Service(const Service& a_rhs) :
			service(a_rhs.service) {
		}
		Service& operator=(const Service<T>& a_rhs) {
			service = a_rhs.service;
			return *this;
		}

		T* self() {
			return service;
		}
	private:
		T* service;
	};

	class Services {
	public:
		Services() {}

		template<typename T>
		T* connect(T* serviceObject) {
			types[typeid(T)] = Service<T>(serviceObject);
			return serviceObject;
		}

		template<typename T, typename V>
		T* connect(V* serviceObject) {
			types[typeid(T)] = Service<T>(serviceObject);
			return dynamic_cast<T*>(serviceObject);
		}

		template<typename T>
		void disconnect() {
			types.erase(typeid(T));
		}

		template<typename T>
		T* get() {
			auto i = types.find(typeid(T));
			if (i != types.end()) {
				try {
					return boost::any_cast<Service<T>>(i->second).self();
				} catch (boost::bad_any_cast&) {
					std::cerr << "Error: Tried to get service [" << i->second.type().name() << "] as [" << typeid(T).name() << "]\n";
				}
			}
			std::cerr << "Error: Failed to find service [" << typeid(T).name() << "]\n";
			return nullptr;
		}

		template<typename T, typename V>
		V* get() {
			auto i = types.find(typeid(T));
			if (i != types.end()) {
				try {
					if (V* result = dynamic_cast<V*>(boost::any_cast<Service<T>>(i->second).self())) {
						return result;
					}
				} catch (boost::bad_any_cast&) {
					std::cerr << "Error: Tried to get service [" << i->second.type().name() << "] as [" << typeid(T).name() << "]\n";
				}
			}
			std::cerr << "Error: Failed to find service [" << typeid(T).name() << "] -> [" << typeid(V).name() << "]\n";
			return nullptr;
		}
	private:
		Services(const Services&) = delete;
		Services(Services&&) = delete;
		Services& operator=(const Services&) = delete;

		std::unordered_map<std::type_index, boost::any> types;
	};
}

#endif
