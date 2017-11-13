#ifndef __NETWORK_OBJECT_H__
#define __NETWORK_OBJECT_H__
/*
#include "Utility/visitor.hpp"

#include "cereal/cereal.hpp"
#include "cereal/types/boost_variant.hpp"
#include "cereal/archives/portable_binary.hpp"
#include "cereal/archives/json.hpp"

template <typename T>
class NetworkObjectPool;

template <typename T>
class NetworkObject : std::enable_shared_from_this<NetworkObject<T>> {
	friend NetworkObjectPool<T>;
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
	}

	bool destroyed() const {
		return local;
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
	void serialize(Archive & archive, std::uint32_t const ) {
		archive(
			cereal::make_nvp("id", synchronizeId),
			cereal::make_nvp("local", local)
		);
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
	NetworkObjectPool(std::vector<) {
	}

	template <class V>
	NetworkObject<V> spawn(const std::shared_ptr<V> &a_newItem) {
		auto idToMake = ++currentId;
		NetworkObject<V> item{idToMake, a_newItem};
		objects.insert({ idToMake, item });
		NetworkObject<V>::spawned();
		return item;
	}

	void synchronize(std::vector<boost::variant<NetworkObject<T>...> a_incoming) {
		for (auto&& item : a_incoming) {
			objects[item.id()]->synchronize(item.local());
		}
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const ) {
		archive(
			cereal::make_nvp("objects", objects)
		);
	}

private:
	std::unordered_map<std::type_index, int>

	std::atomic<uint64_t> currentId = 0;
	std::map<uint64_t, boost::variant<NetworkObject<T>...>> objects;
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
		archive(CEREAL_NVP(objects), cereal::make_nvp("NetworkAction", cereal::base_class<NetworkAction>(this)));
	}

private:
	std::vector<boost::variant<NetworkObject<T>...> objects;
};*/

#endif
