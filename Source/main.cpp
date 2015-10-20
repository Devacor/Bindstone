#include "Game/gameEditor.h"
#include "ClickerGame/clickerGame.h"
#include "Utility/threadPool.h"

#include "ArtificialIntelligence/pathfinding.h"
#include "vld.h"

bool isDone() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			return true;
		}
	}
	return false;
}

int main(int argc, char *argv[]){
// 	auto world = MV::Map::make({ 20, 20 }, true);
// 
// 	for (int i = 2; i < 18; ++i) {
// 		world->get({ i, 3 }).staticBlock();
// 	}
// 
// 
// 	std::vector<std::shared_ptr<MV::NavigationAgent>> agents{ MV::NavigationAgent::make(world, MV::Point<int>(2, 2)),
// 		MV::NavigationAgent::make(world, MV::Point<int>(2, 2)) };
// 	agents[0]->goal(MV::Point<int>(17, 17), 0);
// 	agents[1]->goal(MV::Point<int>(17, 17), 0);
// 	MV::Stopwatch timer;
// 
// 	for (int i = 0; i < 2; ++i) {
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
// 	}
// 
// 	int i = 0;
// 
// 	while (std::find_if(agents.begin(), agents.end(), [](auto&& agent) {
// 		return agent->pathfinding();
// 	}) != agents.end() ) {
// 		while (timer.frame(.5f)) {
// 			for (auto&& agent : agents) {
// 				agent->update(1.0f);
// 			}
// 			std::vector<MV::PathNode> pathNodes;
// 			for (int x = 0; x < world->size().width; ++x) {
// 				for (int y = 0; y < world->size().height; ++y) {
// 					bool wasAgent = false;
// 					for (int i = 0; i < agents.size(); ++i) {
// 						auto pathNodes2 = agents[i]->path();
// 						pathNodes.insert(pathNodes.end(), pathNodes2.begin(), pathNodes2.end());
// 						if (MV::point(x, y) == MV::cast<int>(agents[i]->position())) {
// 							std::cout << (i + 1);
// 							wasAgent = true;
// 							break;
// 						}
// 					}
// 					if (!wasAgent) {
// 						if (world->blocked({ x, y })) {
// 							std::cout << "O";
// 						}
// 						else if (std::find_if(pathNodes.begin(), pathNodes.end(), [&](const MV::PathNode &a_node) {return a_node.position().x == x && a_node.position().y == y; }) != pathNodes.end()) {
// 							std::cout << "*";
// 						}
// 						else {
// 							std::cout << "-";
// 						}
// 					}
// 				}
// 				std::cout << std::endl;
// 			}
// 			++i;
// 		}
// 	}
// 	std::cout << "YO" << std::endl;
// 	return 0;
// 	std::stringstream stream;
// 	{
// 		cereal::JSONOutputArchive archive(stream);
// 		archive(
// 			cereal::make_nvp("world", world),
// 			cereal::make_nvp("agents", agents)
// 			);
// 	}
// 	agents.clear();
// 	world.reset();
// 
// 	cereal::JSONInputArchive archive2(stream);
// 	archive2(
// 		cereal::make_nvp("world", world),
// 		cereal::make_nvp("agents", agents)
// 	);
// 
// 
// 	std::cout << "\n\n\n______________" << "HOLYFUCK" << "________________\n\n\n" << std::endl;
// 
// 	for (int i = 0; i < 2; ++i) {
// 		agents[i]->onStart.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
// 			std::cout << (i+1) << ": START" << std::endl;
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
// 	}
// 
// 	while (std::find_if(agents.begin(), agents.end(), [](auto&& agent) {
// 		return agent->pathfinding();
// 	}) != agents.end()) {
// 		while (timer.frame(.5f)) {
// 			for (auto&& agent : agents) {
// 				agent->update(1.0f);
// 			}
// 			std::vector<MV::PathNode> pathNodes;
// 			for (int x = 0; x < world->size().width; ++x) {
// 				for (int y = 0; y < world->size().height; ++y) {
// 					bool wasAgent = false;
// 					for (int i = 0; i < agents.size(); ++i) {
// 						auto pathNodes2 = agents[i]->path();
// 						pathNodes.insert(pathNodes.end(), pathNodes2.begin(), pathNodes2.end());
// 						if (MV::point(x, y) == MV::cast<int>(agents[i]->position())) {
// 							std::cout << (i + 1);
// 							wasAgent = true;
// 							break;
// 						}
// 					}
// 					if (!wasAgent) {
// 						if (world->blocked({ x, y })) {
// 							std::cout << "O";
// 						}
// 						else if (std::find_if(pathNodes.begin(), pathNodes.end(), [&](const MV::PathNode &a_node) {return a_node.position().x == x && a_node.position().y == y; }) != pathNodes.end()) {
// 							std::cout << "*";
// 						}
// 						else {
// 							std::cout << "-";
// 						}
// 					}
// 				}
// 				std::cout << std::endl;
// 			}
// 
// 		}
// 	}
// 
// 	return 0;
	GameEditor menu;

	menu.start();

	return 0;
}