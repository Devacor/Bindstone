#ifndef _MV_SERVICES_H_
#define _MV_SERVICES_H_

#include <typeindex>
#include <unordered_map>
#include "boost/any.hpp"
#include "Utility/log.h"

namespace MV {
	template <typename T>
	class Service {
	public:
		Service(T* a_service) :
			service(a_service) {
		}

		T* self() {
			return service;
		}
	private:
		T* service;
	};

	class Services {
	public:
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
		T* get(bool a_throwOnFail = true) {
			auto i = types.find(typeid(T));
			if (i != types.end()) {
				try {
					return boost::any_cast<Service<T>>(i->second).self();
				} catch (boost::bad_any_cast&) {
					auto message = std::string("Error: Tried to get service [") + i->second.type().name() + "] as [" + typeid(T).name() + "]\n";
					if (a_throwOnFail) {
						throw MV::ResourceException(message);
					} else {
						MV::warning(message);
					}
				}
			}
			auto message = std::string("Error: Failed to find service [") + typeid(T).name() + "]\n";
			if (a_throwOnFail) {
				throw MV::ResourceException(message);
			} else {
				MV::warning(message);
			}
			return nullptr;
		}

		template<typename T, typename V>
		V* get(bool a_throwOnFail = true) {
			auto i = types.find(typeid(T));
			if (i != types.end()) {
				try {
					if (V* result = dynamic_cast<V*>(boost::any_cast<Service<T>>(i->second).self())) {
						return result;
					}
				} catch (boost::bad_any_cast&) {
					auto message = std::string("Error: Tried to get service [") + i->second.type().name() + "] as [" + typeid(T).name() + "]\n";
					if (a_throwOnFail) {
						throw MV::ResourceException(message);
					} else {
						MV::warning(message);
					}
				}
			}

			auto message = std::string("Error: Failed to find service [") + i->second.type().name() + "] as [" + typeid(T).name() + "]\n";
			if (a_throwOnFail) {
				throw MV::ResourceException(message);
			} else {
				MV::warning(message);
			}
			return nullptr;
		}

		template<typename T>
		T* tryGet() {
			auto i = types.find(typeid(T));
			if (i != types.end()) {
				try {
					return boost::any_cast<Service<T>>(i->second).self();
				} catch (boost::bad_any_cast&) {
				}
			}
			return nullptr;
		}

		template<typename T, typename V>
		V* tryGet() {
			auto i = types.find(typeid(T));
			if (i != types.end()) {
				try {
					if (V* result = dynamic_cast<V*>(boost::any_cast<Service<T>>(i->second).self())) {
						return result;
					}
				} catch (boost::bad_any_cast&) {
				}
			}
			return nullptr;
		}
	private:

		std::unordered_map<std::type_index, boost::any> types;
	};
}

#endif
