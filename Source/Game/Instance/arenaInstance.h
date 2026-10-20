#ifndef __MV_ARENA_INSTANCE_H__
#define __MV_ARENA_INSTANCE_H__

#include "Game/Instance/gameInstance.h"
#include "Game/battleEffect.h"
#include <random>

// A ServerGameInstance without the server: the authoritative simulation driven headless at
// whatever speed the host loops it, for balance sampling (Bindstone.exe -arena) and tooling.
class ArenaGameInstance : public GameInstance {
	friend MV::Script;
	ArenaGameInstance(const std::shared_ptr<MV::Scene::Node> &a_root, GameData& a_data, MV::TapDevice& a_mouse) :
		GameInstance(a_root, a_data, a_mouse, 1.0f / 10.0f, 1) {
		onTeamDefeated.connect("arenaResult", [this](Team& a_team) {
			if (!finished) {
				finished = true;
				winner = !a_team.side();
			}
		});
		synchronizedObjects.onSpawn<CreatureNetworkState>([](std::shared_ptr<MV::NetworkObject<CreatureNetworkState>> a_newItem) {
			a_newItem->self()->netId = a_newItem->id();
		});
		synchronizedObjects.onSpawn<BattleEffectNetworkState>([](std::shared_ptr<MV::NetworkObject<BattleEffectNetworkState>> a_newItem) {
			a_newItem->self()->netId = a_newItem->id();
		});
		script().engine().add_function("spawnOnNetwork", [this](std::shared_ptr<BattleEffectNetworkState> a_effect) {
			auto networkBattleEffect = networkPool().spawn(a_effect);
			auto effectNode = gameObjectContainer()->make("E_" + std::to_string(networkBattleEffect->id()));
			effectNode->position(networkBattleEffect->self()->position);
			return effectNode->attach<ServerBattleEffect>(networkBattleEffect, *this).self();
		});
		script().engine().serial_function("spawnOnNetwork");   // the network pool and the scene graph
	}
public:
	static std::unique_ptr<ArenaGameInstance> make(const std::shared_ptr<MV::Scene::Node> &a_root, GameData& a_data, MV::TapDevice& a_mouse, const std::shared_ptr<InGamePlayer> &a_leftPlayer, const std::shared_ptr<InGamePlayer> &a_rightPlayer) {
		auto result = std::unique_ptr<ArenaGameInstance>(new ArenaGameInstance(a_root, a_data, a_mouse));
		result->leftPlayer = a_leftPlayer;
		result->rightPlayer = a_rightPlayer;
		result->initialize(a_leftPlayer, a_rightPlayer);
		return result;
	}

	virtual void initializeBuilding(std::shared_ptr<Building> a_building) override {
		a_building->initializeNetworkState(networkPool().spawn(std::make_shared<BuildingNetworkState>(static_cast<int32_t>(buildings.size()))));
		buildings.push_back(a_building);
	}

	virtual void spawnCreature(int a_buildingSlot, const std::string& a_creatureId) override {
		spawnCreatureAt(a_buildingSlot, a_creatureId, building(a_buildingSlot)->spawnPositionWorld());
	}

	virtual int64_t spawnCreatureAt(int a_buildingSlot, const std::string& a_creatureId, const MV::Point<> &a_worldPosition) override {
		auto creatureNode = gameObjectContainer()->make(MV::guid(a_creatureId));
		creatureNode->worldPosition(a_worldPosition);
		auto spawnedId = creatureNode->attach<ServerCreature>(a_creatureId, building(a_buildingSlot)->slotIndex(), *this)->netId();
		++spawned[a_buildingSlot < 8 ? 0 : 1][a_creatureId];
		return spawnedId;
	}

	virtual bool canUpgradeBuildingFor(const std::shared_ptr<InGamePlayer> &) const override {
		return true;
	}

	void requestUpgrade(int a_slot, int a_upgrade) override {
		performUpgrade(a_slot, a_upgrade);
	}

	// Every building takes its first upgrade at a seeded moment in the first seconds, so two runs
	// of the same matchup differ in spawn phase - the randomness the sampling averages over.
	void scheduleOpeningUpgrades(uint32_t a_seed, double a_spread) {
		std::mt19937 rng(a_seed);
		std::uniform_real_distribution<double> when(0.0, a_spread);
		for (int slot = 0; slot < 16; ++slot) {
			pendingUpgrades.push_back({ when(rng), slot });
		}
	}

	// One simulated step; returns false once the match has a winner
	bool step(double a_dt) {
		elapsed += a_dt;
		for (auto it = pendingUpgrades.begin(); it != pendingUpgrades.end();) {
			if (it->first <= elapsed) { performUpgrade(it->second, 0); it = pendingUpgrades.erase(it); }
			else { ++it; }
		}
		update(a_dt);
		return !finished;
	}

	int leaksAgainst(TeamSide a_side) {
		auto& team = teamForPlayer(a_side == TeamSide::LEFT ? leftPlayer : rightPlayer);
		return data().constants().startHealth - team.health();
	}

	bool finished = false;
	TeamSide winner = TeamSide::NEUTRAL;
	double elapsed = 0.0;
	std::map<std::string, int> spawned[2];

protected:
	void updateImplementation(double) override {}

private:
	std::shared_ptr<InGamePlayer> leftPlayer;
	std::shared_ptr<InGamePlayer> rightPlayer;
	std::vector<std::pair<double, int>> pendingUpgrades;
};

#endif
