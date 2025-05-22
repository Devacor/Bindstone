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

		game.initializeBuilding(buildingNode->attach<Building>(i, i - startIndex, player, game).self());
	}
}

std::vector<std::shared_ptr<ServerCreature>> Team::creaturesInRange(const MV::Point<> &a_location, float a_radius) {
        std::vector<std::shared_ptr<ServerCreature>> result;
        std::unordered_map<ServerCreature*, double> distances;
        for (auto&& kv : game.creatures) {
                auto a_creature = std::dynamic_pointer_cast<ServerCreature>(kv.second);
                if (a_creature && (game.teamForPlayer(a_creature->player()).side() == ourSide) && a_creature->alive()) {
                        auto ourDistance = MV::distance(a_location, a_creature->agent()->gridPosition());
                        distances[a_creature.get()] = ourDistance;
                        if (ourDistance <= a_radius) {
                                MV::insertSorted(result, a_creature, [&](const std::shared_ptr<ServerCreature> &a_lhs, const std::shared_ptr<ServerCreature> &a_rhs) {
                                        return distances[a_lhs.get()] < distances[a_rhs.get()];
                                });
                        }
                }
        }
        return result;
}