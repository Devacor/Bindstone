#ifndef _SYNCHRONIZE_ACTION_H
#define _SYNCHRONIZE_ACTION_H

#include "Game/building.h"
#include "Game/NetworkLayer/networkAction.h"
#include "MV/Network/networkObject.h"

typedef MV::NetworkObjectPool<Creature::NetworkState> BindstoneNetworkObjectPool;

class SynchronizeAction : public NetworkAction {
public:
	SynchronizeAction(const std::vector<BindstoneNetworkObjectPool::VariantType> &a_objects) : objects(a_objects) {}
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
	std::vector<BindstoneNetworkObjectPool::VariantType> objects;
};

inline std::string makeSynchronizeNetworkString(BindstoneNetworkObjectPool &a_networkObjectPool) {
	makeNetworkString<SynchronizeAction>(a_networkObjectPool.updated());
}

CEREAL_FORCE_DYNAMIC_INIT(mv_synchronizeaction);

#endif