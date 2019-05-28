#include "team.h"
#include "gameInstance.h"

Team::Team(std::shared_ptr<InGamePlayer> a_player, TeamSide a_side, GameInstance& a_game) :
	player(a_player),
	game(a_game),
	ourSide(a_side),
	health(a_game.data().constants().startHealth) {
}

void Team::initialize() {
	int startIndex = (static_cast<int>(ourSide) - 1) * 8;
	auto sideString = sideToString(ourSide);
	for (int i = startIndex; i < (startIndex + 8); ++i) {
		auto buildingNode = game.worldScene->get("1v1_" + std::to_string(i));

		game.buildings.push_back(buildingNode->attach<Building>(i, i - startIndex, player, game).self());
	}
}

chaiscript::ChaiScript& Team::hook(chaiscript::ChaiScript &a_script) {
	a_script.add(chaiscript::user_type<Team>(), "Team");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return &a_self.game;
	}), "game");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return a_self.enemyWellPosition;
	}), "enemyWell");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return a_self.ourWellPosition;
	}), "ourWell");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return a_self.health;
	}), "health");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return a_self.game.buildings;
	}), "buildings");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return a_self.game.creatures;
	}), "creatures");

	a_script.add(chaiscript::fun([](Team &a_self, const MV::Point<> &a_location, float a_radius) {
		return a_self.creaturesInRange(a_location, a_radius);
	}), "creaturesInRange");

	return a_script;
}

std::vector<std::shared_ptr<ServerCreature>> Team::creaturesInRange(const MV::Point<> &a_location, float a_radius) {
	std::vector<std::shared_ptr<ServerCreature>> result;
	std::unordered_map<ServerCreature*, double> distances;
	for (auto&& kv : game.creatures) {
		auto a_creature = kv.second;
		if ((game.teamForPlayer(a_creature->player()).side() == ourSide) && a_creature->alive()){
			auto ourDistance = MV::preSquareDistance(a_location, a_creature->agent()->gridPosition());
			distances[kv.second.get()] = ourDistance;
			if (ourDistance <= a_radius) {
				MV::insertSorted(result, a_creature, [&](const std::shared_ptr<ServerCreature> &a_lhs, const std::shared_ptr<ServerCreature> &a_rhs) {
					return distances[a_lhs.get()] < distances[a_rhs.get()];
				});
			}
		}
	}
	return result;
}