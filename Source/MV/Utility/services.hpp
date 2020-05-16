#ifndef _MV_SERVICES_H_
#define _MV_SERVICES_H_
#include <typeindex>
#include <unordered_map>
#include <any>
#include "log.h"
#include "require.hpp"

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
		T * service;
	};
	class Services {
	public:
		static Services& instance() {
			static Services ourInstance;
			return ourInstance;
		}
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
		T* get(bool a_throwOnFail = true) const {
			auto i = types.find(typeid(T));
			if (i != types.end()) {
				try {
					return std::any_cast<Service<T>>(i->second).self();
				} catch (std::bad_any_cast&) {
					auto message = std::string("Error: Tried to get service [") + i->second.type().name() + "] as [" + typeid(T).name() + "]\n";
					if (a_throwOnFail) {
						throw MV::ResourceException(message);
					} else {
						MV::warning(message);
					}
				}
			}
			if (a_throwOnFail) {
				throw MV::ResourceException(std::string("Error: Failed to find service [") + typeid(T).name() + "]\n");
			}
			return nullptr;
		}
		template<typename T, typename V>
		V* get(bool a_throwOnFail = true) const {
			auto i = types.find(typeid(T));
			if (i != types.end()) {
				try {
					if (V* result = dynamic_cast<V*>(std::any_cast<Service<T>>(i->second).self())) {
						return result;
					} else {
						auto message = std::string("Error [DynamicCast]: Tried to get service [") + i->second.type().name() + "] as [" + typeid(V).name() + "]\n";
						if (a_throwOnFail) {
							throw MV::ResourceException(message);
						} else {
							MV::warning(message);
						}
					}
				} catch (std::bad_any_cast&) {
					auto message = std::string("Error [AnyCast]: Tried to get service [") + i->second.type().name() + "] as [" + typeid(V).name() + "]\n";
					if (a_throwOnFail) {
						throw MV::ResourceException(message);
					} else {
						MV::warning(message);
					}
				}
			}
			if (a_throwOnFail) {
				throw MV::ResourceException(std::string("Error: Failed to find service [") + i->second.type().name() + "] as [" + typeid(V).name() + "]\n");
			}
			return nullptr;
		}
	private:
		std::unordered_map<std::type_index, std::any> types;
	};
}
#endif