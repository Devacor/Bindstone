#ifndef __NETWORK_OBJECT_H__
#define __NETWORK_OBJECT_H__

#include "Utility/visitor.hpp"

#include <unordered_map>
#include <vector>
#include <memory>
#include <atomic>
#include <type_traits>

#include "cereal/cereal.hpp"
#include "cereal/types/boost_variant.hpp"
#include "cereal/archives/portable_binary.hpp"
#include "cereal/archives/json.hpp"

template <typename ...T>
class NetworkObjectPool;

template <typename T>
class NetworkObject : public std::enable_shared_from_this<NetworkObject<T>> {
	template <typename...>
	friend class NetworkObjectPool;

public:

	NetworkObject(){}

	NetworkObject(uint64_t a_id) :
		synchronizeId(a_id) {
	}

	NetworkObject(uint64_t a_id, const std::shared_ptr<T> &a_local):
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

	uint64_t id() const {
		return synchronizeId;
	}

	void destroy() {
		local.reset();
		dirty = true;
	}

	bool destroyed() const {
		return local == nullptr;
	}

	void markDirty() {
		dirty = true;
	}

	bool undirty() {
		if (dirty) {
			dirty = false;
			return true;
		}
		return false;
	}

	std::shared_ptr<T> self() const {
		return local;
	}

	void synchronize(const std::shared_ptr<NetworkObject<T>> &a_other) {
		if (!local && a_other->local) {
			local = a_other->local;
		} else if(local) {
			local->synchronize(a_other->local);
		}
		dirty = false;
	}

	template <class Archive>
	void save(Archive & archive, std::uint32_t const ) const {
		archive(
			cereal::make_nvp("id", synchronizeId),
			cereal::make_nvp("local", local)
		);
	}

	template <class Archive>
	void load(Archive & archive, std::uint32_t const) {
		archive(
			cereal::make_nvp("id", synchronizeId),
			cereal::make_nvp("local", local)
		);
		dirty = false;
	}

	std::shared_ptr<NetworkObject<T>> localCopy() const {
		return std::make_shared<NetworkObject<T>>(synchronizeId, nullptr);
	}

	std::shared_ptr<NetworkObject<T>> remoteCopy() const {
		return std::make_shared<NetworkObject<T>>(synchronizeId, local);
	}

	//hookup to observe new spawned objects, called on main thread
	static std::function<void (std::shared_ptr<NetworkObject<T>>)> spawned;
private:
	bool dirty = true;
	uint64_t synchronizeId = 0;
	std::shared_ptr<T> local;
};

template <typename T>
std::function<void(std::shared_ptr<NetworkObject<T>>)> NetworkObject<T>::spawned = std::function<void(std::shared_ptr<NetworkObject<T>>)>();

template <typename ...T>
class SynchronizeAction : public NetworkAction {
public:
	SynchronizeAction(const std::vector<boost::variant<std::shared_ptr<NetworkObject<T>>...>> &a_objects) : objects(a_objects) {}
	SynchronizeAction() {}

	virtual void execute(Game&) override;
	virtual void execute(GameServer&) override;
	virtual void execute(GameUserConnectionState*, GameServer&) override;
	virtual void execute(LobbyUserConnectionState*) override;
	virtual void execute(LobbyGameConnectionState*) override;

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const) {
		archive(CEREAL_NVP(objects), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

//private:
	std::vector<boost::variant<std::shared_ptr<NetworkObject<T>>...>> objects;
};

template <typename... T>
struct TypePack {};

template <typename ...T>
class NetworkObjectPool {
public:
	using SynchronizeAction = SynchronizeAction<T...>;

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

	void synchronize(std::vector<boost::variant<std::shared_ptr<NetworkObject<T>>...>> a_incoming) {
		std::vector<uint64_t> removeList;
		for (auto&& item : a_incoming) {
			MV::visit(item, [&](auto &a_item) {
				auto found = objects.find(a_item->id());
				if (found == objects.end()) {
					objects.insert({ a_item->id(), { a_item } });
					callSpawnCallback(a_item);
				} else {
					MV::visit(found->second, [&a_item](auto &a_found){
						if constexpr(std::is_same<decltype(a_found), decltype(a_item)>::value) {
							a_found->synchronize(a_item);
						}
					});
				}
				if (a_item->destroyed()) {
					removeList.push_back(a_item->id());
				}
			});
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

	std::vector<boost::variant<std::shared_ptr<NetworkObject<T>>...>> updated() {
		std::vector<boost::variant<std::shared_ptr<NetworkObject<T>>...>> results;
		for (auto&& object : objects) {
			if (MV::visit(object.second, [](const auto &a_object) { return a_object->undirty(); })) {
				results.push_back(object.second);
			}
		}
		return results;
	}

	std::string makeNetworkString() {
		return ::makeNetworkString<SynchronizeAction>(updated());
	}

private:
	template <typename T>
	void callSpawnCallback(T& a_item) {
		auto onSpawnCallable = a_item->spawnCallbackCast(spawnCallbacks[a_item->typeIndex()]);
		if (onSpawnCallable) {
			onSpawnCallable(a_item);
		}
	}

	std::unordered_map<std::type_index, boost::any> spawnCallbacks;

	std::atomic<uint64_t> currentId;
	std::unordered_map<uint64_t, boost::variant<std::shared_ptr<NetworkObject<T>>...>> objects;
};

template <typename ...T>
void SynchronizeAction<T...>::execute(Game&) {

}
template <typename ...T>
void SynchronizeAction<T...>::execute(GameServer&) {

}
template <typename ...T>
void SynchronizeAction<T...>::execute(GameUserConnectionState*, GameServer&) {

}
template <typename ...T>
void SynchronizeAction<T...>::execute(LobbyUserConnectionState*) {

}
template <typename ...T>
void SynchronizeAction<T...>::execute(LobbyGameConnectionState*) {

}

#endif
