#include "Game/gameEditor.h"
#include "ClickerGame/clickerGame.h"
#include "Utility/threadPool.h"

#include "ArtificialIntelligence/pathfinding.h"
#include "vld.h"

#include <memory>

bool isDone() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			return true;
		}
	}
	return false;
}

struct TestClass {

	template <class Archive>
	void serialize(Archive & archive) {
		archive(CEREAL_NVP(test));
	}

	int test;
};

struct TestClassVersioned {
	
	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const version) {
		archive(CEREAL_NVP(test));
		std::cout << "VERSION: " << version << std::endl;
	}

	int test;
};

#include "cereal/archives/json.hpp"

int main(int argc, char *argv[]){
	{
		std::stringstream stream;

		{
			cereal::JSONOutputArchive archive(stream);
			TestClass test;
			archive(CEREAL_NVP(test));
		}

		{
			TestClassVersioned test;
			cereal::JSONInputArchive archive(stream);
			archive(CEREAL_NVP(test));
		}
	}

// 	auto world = MV::Map::make({ 20, 20 }, false);
// 
// 	for (int i = 0; i < 20; ++i) {
// 		world->get({ 8, i }).staticBlock();
// 	}
// 
// 	world->get({ 8, 6 }).staticUnblock();
// 	world->get({ 8, 7 }).staticUnblock();
// 
// 
// 	std::vector<std::shared_ptr<MV::NavigationAgent>> agents{ 
// 		MV::NavigationAgent::make(world, MV::Point<int>(2, 2), 2),
// 		MV::NavigationAgent::make(world, MV::Point<int>(0, 2), 2),
// 		MV::NavigationAgent::make(world, MV::Point<int>(2, 0), 2),
// 		MV::NavigationAgent::make(world, MV::Point<int>(0, 0), 2) };
// 	MV::Stopwatch timer;
// 
// 	for (int i = 0; i < agents.size(); ++i) {
// 		agents[i]->onStart.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
// 			std::cout << (i + 1) << ": START" << std::endl;
// 		});
// 		agents[i]->onStop.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
// 			std::cout << (i + 1) << ": STOP" << std::endl;
// 		});
// 		agents[i]->onBlocked.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
// 			std::cout << (i + 1) << ": BLOCKED" << std::endl;
// 		});
// 		agents[i]->onArrive.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
// 			std::cout << (i + 1) << ": ARRIVED" << std::endl;
// 		});
// 		agents[i]->goal(MV::Point<int>(10, 17), 0);
// 	}
// 
// 
// 	int i = 0;
// 
// 	while (std::find_if(agents.begin(), agents.end(), [](auto&& agent) {
// 		return agent->pathfinding();
// 	}) != agents.end() ) {
// 		char a;
// 		while (true) {
// 			for (auto&& agent : agents) {
// 				agent->update(1.0f);
// 			}
// 			std::vector<MV::PathNode> pathNodes;
// 			for (int y = 0; y < world->size().height; ++y) {
// 				for (int x = 0; x < world->size().width; ++x) {
// 					bool wasAgent = false;
// 					for (int i = 0; i < agents.size(); ++i) {
// 						if (agents[i]->overlaps({ x, y })) {
// 							std::cout << "[" << (char)(i + 65) << "]";
// 							wasAgent = true;
// 							break;
// 						}
// 					}
// 					if (!wasAgent) {
// 						if (world->blocked({ x, y })) {
// 							std::cout << " " << "X" << " ";
// 						} else {
// 							std::cout << " " << world->get({ x, y }).clearance() << " ";
// 						}
// 					}
// 				}
// 				std::cout << std::endl;
// 			}
// 			std::cout << "\n\n\n" << std::endl;
// 			++i;
// 			std::cin >> a;
// 		}
// 	}
// 	std::cout << "YO" << std::endl;
// 	return 0;

	GameEditor menu;

	menu.start();
	
	return 0;
}