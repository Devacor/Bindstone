#include "team.h"
#include "gameInstance.h"

Team::Team(std::shared_ptr<Player> a_player, TeamSide a_side, GameInstance& a_game) :
	player(a_player),
	game(a_game),
	ourSide(a_side),
	health(a_game.data().constants().startHealth) {

	auto sideString = sideToString(ourSide);
	for (int i = 0; i < 8; ++i) {
		auto buildingNode = game.worldScene->get(sideString + "_" + std::to_string(i));

		buildings.push_back(buildingNode->attach<Building>(game.data().buildings().data(a_player->loadout.buildings[i]), a_player->loadout.skins[i], i, a_player, game).self());
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
		return a_self.buildings;
	}), "buildings");

	a_script.add(chaiscript::fun([](Team &a_self) {
		return a_self.creatures;
	}), "creatures");

	a_script.add(chaiscript::fun([](Team &a_self, const MV::Point<> &a_location, float a_radius) {
		return a_self.creaturesInRange(a_location, a_radius);
	}), "creaturesInRange");

	return a_script;
}

std::vector<std::shared_ptr<Creature>> Team::creaturesInRange(const MV::Point<> &a_location, float a_radius) {
	std::vector<std::shared_ptr<Creature>> result;
	std::copy_if(creatures.begin(), creatures.end(), std::back_inserter(result), [&](std::shared_ptr<Creature> &a_creature) {
		return a_creature->alive() && MV::distance(a_location, a_creature->agent()->gridPosition()) <= a_radius;
	});
	std::sort(result.begin(), result.end(), [&](std::shared_ptr<Creature> &a_lhs, std::shared_ptr<Creature> &a_rhs) {
		return MV::distance(a_location, a_lhs->agent()->gridPosition()) < MV::distance(a_location, a_rhs->agent()->gridPosition());
	});
	return result;
}

void Team::spawn(std::shared_ptr<Creature> &a_registerCreature) {
	if (a_registerCreature->alive()) {
		creatures.push_back(a_registerCreature);
		a_registerCreature->onDeath.connect("_RemoveFromTeam", [&](std::shared_ptr<Creature> a_creature) {
			creatures.erase(std::remove(creatures.begin(), creatures.end(), a_creature), creatures.end());
		});
	}
}