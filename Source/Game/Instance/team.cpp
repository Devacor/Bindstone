#include "team.h"
#include "gameInstance.h"

#include <jaiscript/core/registrar.hpp>
#include <algorithm>

static jai::registrar<Team, MV::Services> _hookTeam("Team",
	[](jai::dynamic_binder<Team>& builder, const MV::Services&) {
	builder.method("ourWell", &Team::ourWell);
	builder.method("enemyWell", &Team::enemyWell);
	builder.method("health", &Team::health);
	builder.method("damage", &Team::damage);
	builder.method("creaturesInRange", [](Team& a_self, const MV::Point<>& a_location, double a_radius) {
		return a_self.creaturesInRange(a_location, static_cast<float>(a_radius));
	});
});

Team::Team(std::shared_ptr<InGamePlayer> a_player, TeamSide a_side, GameInstance& a_game) :
	player(a_player),
	game(a_game),
	ourSide(a_side),
	ourHealth(a_game.data().constants().startHealth) {
}

void Team::damage(int a_amount) {
	if (ourHealth <= 0) {
		return;
	}
	ourHealth = std::max(0, ourHealth - a_amount);
	if (ourHealth == 0) {
		game.onTeamDefeatedSignal(*this);
	}
}

void Team::initialize() {
	int startIndex = (static_cast<int>(ourSide) - 1) * 8;
	auto sideString = sideToString(ourSide);
	for (int i = startIndex; i < (startIndex + 8); ++i) {
		auto buildingNode = game.worldScene->get("1v1_" + std::to_string(i));

		game.initializeBuilding(buildingNode->attach<Building>(i, i - startIndex, player, game).self());
	}
}

std::vector<std::shared_ptr<ServerCreature>> Team::creaturesInRange(const MV::Point<> &a_location, float a_radius) {
	std::vector<std::pair<float, std::shared_ptr<ServerCreature>>> found;
	auto radiusSquared = a_radius * a_radius;
	for (auto& creature : members) {
		if (!creature->alive() || creature->untargetable()) { continue; }
		auto offset = creature->agent()->gridPosition() - a_location;
		auto distanceSquared = offset.x * offset.x + offset.y * offset.y;
		if (distanceSquared <= radiusSquared) { found.emplace_back(distanceSquared, creature); }
	}
	std::sort(found.begin(), found.end(), [](const auto& a_lhs, const auto& a_rhs) { return a_lhs.first < a_rhs.first; });
	std::vector<std::shared_ptr<ServerCreature>> result;
	result.reserve(found.size());
	for (auto& entry : found) { result.push_back(entry.second); }
	return result;
}

std::shared_ptr<ServerCreature> Team::nearestCreature(const MV::Point<> &a_location, float a_radius) {
	std::shared_ptr<ServerCreature> best;
	auto bestSquared = a_radius * a_radius;
	for (auto& creature : members) {
		if (!creature->alive() || creature->untargetable()) { continue; }
		auto offset = creature->agent()->gridPosition() - a_location;
		auto distanceSquared = offset.x * offset.x + offset.y * offset.y;
		if (distanceSquared <= bestSquared) { bestSquared = distanceSquared; best = creature; }
	}
	return best;
}

void Team::removeMember(int64_t a_netId) {
	members.erase(std::remove_if(members.begin(), members.end(), [&](const std::shared_ptr<ServerCreature>& a_creature) { return a_creature->netId() == a_netId; }), members.end());
}
