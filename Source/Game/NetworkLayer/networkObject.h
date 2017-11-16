#ifndef __NETWORK_OBJECT_H__
#define __NETWORK_OBJECT_H__

#include "Utility/visitor.hpp"

#include <unordered_map>
#include <vector>
#include <memory>
#include <atomic>

#include "cereal/cereal.hpp"
#include "cereal/types/boost_variant.hpp"
#include "cereal/archives/portable_binary.hpp"
#include "cereal/archives/json.hpp"

template <typename ...T>
class NetworkObjectPool;

template <typename T>
class NetworkObject : std::enable_shared_from_this<NetworkObject<T>> {
	template <typename...>
	friend class NetworkObjectPool;

public:
	NetworkObject(){}

	NetworkObject(uint64_t a_id, std::shared_ptr<T> a_local):
		synchronizeId(a_id),
		local(a_local){
	}

	uint64_t id() const {
		return synchronizeId;
	}

	void destroy() {
		local.reset();
		dirty = true;
	}

	bool destroyed() const {
		return local;
	}

	void makeDirty() {
		dirty = true;
	}

	bool undirty() {
		if (dirty) {
			dirty = false;
			return true;
		}
		return false;
	}

	void synchronize(const std::shared_ptr<T> &a_other) {
		if (!local && a_other) {
			local = a_other;
			spawned(shared_from_this());
		} else if(local) {
			local->synchronize(a_other);
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

	//hookup to observe new spawned objects, called on main thread
	static std::function<void (std::shared_ptr<NetworkObject<T>>)> spawned;
private:
	bool dirty = true;
	uint64_t synchronizeId = 0;
	std::shared_ptr<T> local;
};

template <typename ...T>
class NetworkObjectPool {
public:
	NetworkObjectPool() {
	}

	template <class V>
	NetworkObject<V> spawn(const std::shared_ptr<V> &a_newItem) {
		auto idToMake = ++currentId;
		NetworkObject<V> item{idToMake, a_newItem};
		objects.insert({ std::make_shared<NetworkObject<V>>(idToMake, item) });
		NetworkObject<V>::spawned();
		return item;
	}

	void synchronize(std::vector<boost::variant<std::shared_ptr<NetworkObject<T>>...>> a_incoming) {
		for (auto&& item : a_incoming) {
			auto& object{ objects[item.id()] };
			boost::apply_visitor([&item](auto const& a_object) { a_object->synchronize(item.local); }, object);
		}
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const ) {
		archive(
			cereal::make_nvp("objects", objects)
		);
	}

	std::vector<boost::variant<std::shared_ptr<NetworkObject<T>>...>> updated() {
		std::vector<boost::variant<NetworkObject<T>...> results;
		for (auto&& object : objects) {
			if (object.second->undirty()) {
				results.push_back(object);
			}
		}
	}

private:

	std::atomic<uint64_t> currentId;
	std::unordered_map<uint64_t, boost::variant<std::shared_ptr<NetworkObject<T>>...>> objects;
};

template <typename ...T>
class SynchronizeAction : public NetworkAction {
public:
	SynchronizeAction() {}

	virtual void execute(Game& ) override {
		std::cout << "Message Got: " << message << std::endl;
	}
	virtual void execute(GameServer&) override {
		std::cout << "Message Got: " << message << std::endl;
	}
	virtual void execute(GameUserConnectionState*, GameServer&) override {
		std::cout << "Message Got: " << message << std::endl;
	}
	virtual void execute(LobbyUserConnectionState* ) override {
		std::cout << "Message Got: " << message << std::endl;
	}
	virtual void execute(LobbyGameConnectionState*) override {
		std::cout << "Message Got: " << message << std::endl;
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const ) {
		//archive(CEREAL_NVP(objects), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

private:
	//std::vector<boost::variant<NetworkObject<T>...> objects;
};

#endif
