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
// 	auto world = MV::Map::make({ 20, 20 }, false);
// 
// 	for (int i = 2; i < 18; ++i) {
// 		world->get({ i, 2 }).staticBlock();
// 		world->get({ 3, (i / 2) + 4 }).staticBlock();
// 	}
// 
// 
// 	std::vector<std::shared_ptr<MV::NavigationAgent>> agents{ MV::NavigationAgent::make(world, MV::Point<int>(2, 2)),
// 		MV::NavigationAgent::make(world, MV::Point<int>(2, 2)) };
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
// 	agents[0]->goal(MV::Point<int>(8, 17), 0);
// 	agents[1]->goal(MV::Point<int>(8, 17), 0);
// 
// 	int i = 0;
// 
// 	while (std::find_if(agents.begin(), agents.end(), [](auto&& agent) {
// 		return agent->pathfinding();
// 	}) != agents.end() ) {
// 		while (timer.frame(2.5f)) {
// 			for (auto&& agent : agents) {
// 				agent->update(1.0f);
// 			}
// 			std::vector<MV::PathNode> pathNodes;
// 			for (int y = 0; y < world->size().height; ++y) {
// 				for (int x = 0; x < world->size().width; ++x) {
// 					bool wasAgent = false;
// 					for (int i = 0; i < agents.size(); ++i) {
// 						if (MV::point(x, y) == MV::cast<int>(agents[i]->position())) {
// 							std::cout << " " << (char)(i + 65) << " ";
// 							wasAgent = true;
// 							break;
// 						}
// 					}
// 					if (!wasAgent) {
// 						auto agent0path = agents[0]->path();
// 						auto agent1path = agents[1]->path();
// 						if (world->blocked({ x, y })) {
// 							std::cout << " O ";
// 						} else if (std::find_if(agent0path.begin(), agent0path.end(), [&](const MV::PathNode &a_node) {return a_node.position().x == x && a_node.position().y == y; }) != agent0path.end()) {
// 							std::cout << "[" << world->get({ x, y }).totalCost() << "]";
// 						} else if (std::find_if(agent1path.begin(), agent1path.end(), [&](const MV::PathNode &a_node) {return a_node.position().x == x && a_node.position().y == y; }) != agent1path.end()) {
// 							std::cout << "{" << world->get({ x, y }).totalCost() << "}";
// 						} else {
// 							std::cout << " " << world->get({ x, y }).totalCost() << " ";
// 						}
// 					}
// 				}
// 				std::cout << std::endl;
// 			}
// 			std::cout << "\n\n\n" << std::endl;
// 			++i;
// 		}
// 	}
// 	std::cout << "YO" << std::endl;
// 	return 0;

	GameEditor menu;

	menu.start();

	return 0;
}