#ifndef _SYNCHRONIZE_ACTION_H
#define _SYNCHRONIZE_ACTION_H

#include "Game/building.h"
#include "Game/creature.h"
#include "Game/battleEffect.h"
#include "Game/NetworkLayer/networkAction.h"
#include "MV/Network/networkObject.h"

typedef MV::NetworkObjectPool<BuildingNetworkState, CreatureNetworkState, BattleEffectNetworkState> BindstoneNetworkObjectPool;

class SynchronizeAction : public NetworkAction {
public:
	SynchronizeAction(const std::vector<BindstoneNetworkObjectPool::VariantType> &a_objects) : objects(a_objects) {}
	SynchronizeAction() {}

	virtual void execute(Game&) override;
#ifdef BINDSTONE_SERVER
	virtual void execute(GameServer&) override;
	virtual void execute(GameUserConnectionState*, GameServer&) override;
	virtual void execute(LobbyUserConnectionState*) override;
	virtual void execute(LobbyGameConnectionState*) override;
#endif

	template<class Archive>
	void serialize(Archive& archive) {
		archive(JAI_NVP(objects));
		NetworkAction::serialize(archive);
	}

	//private:
	std::vector<BindstoneNetworkObjectPool::VariantType> objects;
};

inline std::string makeSynchronizeNetworkString(BindstoneNetworkObjectPool &a_networkObjectPool) {
	auto updated = a_networkObjectPool.updated();
	if (!updated.empty()) {
		return makeNetworkString<SynchronizeAction>(updated);
	} else {
		return "";
	}
}

#endif