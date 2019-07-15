#ifndef _NETWORK_OBJECT_H_
#define _NETWORK_OBJECT_H_

#include "MV/Utility/visitor.hpp"

#include <unordered_map>
#include <vector>
#include <memory>
#include <atomic>
#include <type_traits>

#include "cereal/cereal.hpp"
#include "cereal/types/variant.hpp"
#include "cereal/archives/portable_binary.hpp"
#include "cereal/archives/json.hpp"

namespace MV {

	namespace NetworkDetail {
		template<class T>
		constexpr auto supportsPostSend(T* x) -> decltype(x->postSend(), std::true_type{}) {
			return {};
		}
		template<class T>
		constexpr auto supportsPostSend(...) -> std::false_type {
			return {};
		}
	}

	template <typename ...T>
	class NetworkObjectPool;

	template <typename T>
	class NetworkObject : public std::enable_shared_from_this<NetworkObject<T>> {
		template <typename...>
		friend class NetworkObjectPool;

	public:

		NetworkObject(){}

		NetworkObject(int64_t a_id) :
			synchronizeId(a_id) {
		}

		NetworkObject(int64_t a_id, const std::shared_ptr<T> &a_local):
			synchronizeId(a_id),
			local(a_local){
		}

		std::type_index typeIndex() const {
			return typeid(T);
		}

		std::function<void(std::shared_ptr<NetworkObject<T>>)> spawnCallbackCast(const boost::any &a_any) {
			try {
				return boost::any_cast<std::function<void(std::shared_ptr<NetworkObject<T>>)>>(a_any);
			} catch (...) {
				return std::function<void(std::shared_ptr<NetworkObject<T>>)>();
			}
		}

		int64_t id() const {
			return synchronizeId;
		}

		void destroy() {
			destroying = true;
			dirty = true;
		}

		bool destroyed() const {
			return destroying;
		}

		T* operator*() const {
			return *local;
		}

		const std::shared_ptr<T> &self() const {
			return local;
		}

		const std::shared_ptr<T> & modify() {
			dirty = true;
			return local;
		}

		void synchronize(const std::shared_ptr<NetworkObject<T>> &a_other) {
			destroying = a_other->destroying;
			if (!local && a_other->local) {
				local = a_other->local;
			} else if(local) {
				if (!a_other->destroying) {
					local->synchronize(a_other->local);
				}
			}
			if (a_other->destroying) {
				local->destroy(a_other->local);
			}
			dirty = false;
		}

		template <class Archive>
		void save(Archive & archive, std::uint32_t const ) const {
			archive(
				cereal::make_nvp("id", synchronizeId),
				cereal::make_nvp("local", local),
				cereal::make_nvp("destroy", destroying)
			);

			if constexpr (NetworkDetail::supportsPostSend<T>(nullptr)) {
				if (local) {
					const_cast<T*>(local.get())->postSend();
				}
			}
		}

		template <class Archive>
		void load(Archive & archive, std::uint32_t const) {
			archive(
				cereal::make_nvp("id", synchronizeId),
				cereal::make_nvp("local", local),
				cereal::make_nvp("destroy", destroying)
			);
			dirty = false;
		}

		//hookup to observe new spawned objects, called on main thread
		static std::function<void (std::shared_ptr<NetworkObject<T>>)> spawned;
	private:
		bool undirty() {
			if (dirty) {
				dirty = false;
				return true;
			}
			return false;
		}

		bool dirty = true;
		bool destroying = false;
		int64_t synchronizeId = 0;
		std::shared_ptr<T> local;
	};

	template <typename T>
	std::function<void(std::shared_ptr<NetworkObject<T>>)> NetworkObject<T>::spawned = std::function<void(std::shared_ptr<NetworkObject<T>>)>();

	template <typename ...T>
	class NetworkObjectPool {
	public:
		typedef std::variant<std::shared_ptr<NetworkObject<T>>...> VariantType;

		template <class V>
		std::shared_ptr<NetworkObject<V>> spawn(const std::shared_ptr<V> &a_newItem) {
			auto idToMake = ++currentId;
			auto networkObject = std::make_shared<NetworkObject<V>>(idToMake, a_newItem);
			objects.insert({idToMake, { networkObject } });
			callSpawnCallback(networkObject);
			return networkObject;
		}

		template <class V>
		NetworkObjectPool& onSpawn(std::function<void(std::shared_ptr<NetworkObject<V>>)> a_newItem) {
			spawnCallbacks[typeid(V)] = a_newItem;
			return *this;
		}

		void synchronize(std::vector<VariantType> a_incoming) {
			std::vector<int64_t> removeList;
			for (auto&& item : a_incoming) {
				std::visit([&](auto &a_item) {
					auto found = objects.find(a_item->id());
					if (found == objects.end()) {
						found = objects.insert({ a_item->id(), { a_item } }).first;
						callSpawnCallback(a_item);
						//Handle same frame creation and destruction:
						if (a_item->destroyed()) {
							synchronizeItem(a_item, found->second);
						}
					} else {
						synchronizeItem(a_item, found->second);
					}
					if (a_item->destroyed()) {
						removeList.push_back(a_item->id());
					}
				}, item);
			}
			for (auto&& key : removeList) {
				objects.erase(key);
			}
		}

		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const ) {
			archive(
				cereal::make_nvp("objects", objects)
			);
		}

		std::vector<VariantType> updated() {
			std::vector<VariantType> results;
			for (auto object = objects.begin(); object != objects.end();) {
				if (std::visit([](const auto &a_object) { return a_object->undirty(); }, object->second)) {
					results.push_back(object->second);
				}
				if (std::visit([](const auto &a_object) { return a_object->destroyed(); }, object->second)) {
					object = objects.erase(object);
				} else {
					++object;
				}
			}
			return results;
		}

		//Used for a full synchronize of alive objects
		std::vector<VariantType> all() {
			std::vector<VariantType> results;
			for (auto object = objects.begin(); object != objects.end();++object) {
				if (!std::visit([](const auto &a_object) { return a_object->destroyed(); }, object->second)) {
					results.push_back(object->second);
				}
			}
			return results;
		}

	private:
		template <typename T>
		void callSpawnCallback(T& a_item) {
			auto onSpawnCallable = a_item->spawnCallbackCast(spawnCallbacks[a_item->typeIndex()]);
			if (onSpawnCallable) {
				onSpawnCallable(a_item);
			}
		}

		template <typename T>
		void synchronizeItem(T& a_item, VariantType& found) {
			std::visit([&](auto &a_found) {
				synchronizeItemImplementation(a_item, a_found);
			}, found);
		}

		template <typename T, typename V,
			std::enable_if_t<std::is_same<T, V>::value>* = nullptr>
		void synchronizeItemImplementation(T & a_item, V & a_found) {
			a_found->synchronize(a_item);
		}

		template <typename T, typename V,
			std::enable_if_t<!std::is_same<T, V>::value>* = nullptr>
		void synchronizeItemImplementation(T & a_item, V & a_found) {
		}

		std::unordered_map<std::type_index, boost::any> spawnCallbacks;

		std::atomic<int64_t> currentId;
		std::unordered_map<int64_t, VariantType> objects;
	};

}

#endif
