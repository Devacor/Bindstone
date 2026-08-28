#ifndef _SYNCHRONIZE_ACTION_H
#define _SYNCHRONIZE_ACTION_H

#include "Game/building.h"
#include "Game/creature.h"
#include "Game/battleEffect.h"
#include "Game/NetworkLayer/networkAction.h"
#include "MV/Network/networkObject.h"
#include <fstream>
#include <cstdlib>

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
	if (updated.empty()) {
		return "";
	}
	auto wire = makeNetworkString<SynchronizeAction>(updated);
	static const bool selfCheck = std::getenv("MV_WIRE_SELFCHECK") != nullptr;
	if (selfCheck) {
		try {
			MV::fromBinaryString<std::shared_ptr<NetworkAction>>(wire, MV::Services::instance());
		} catch (const std::exception& e) {
			std::string kinds;
			for (auto&& object : updated) { kinds += std::to_string(object.index()); }
			MV::error("WIRE SELFCHECK FAILED: ", e.what(), " objects=", updated.size(), " kinds=", kinds, " bytes=", wire.size());
			std::ofstream dump("wire_fail.b64", std::ios::trunc);
			dump << MV::toBase64(wire);
		}
	}
	return wire;
}

#endif