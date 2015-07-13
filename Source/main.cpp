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
	auto world = MV::Map::make({ 20, 20 }, true);

	for (int i = 2; i < 18; ++i) {
		world->get({ i, 3 }).block();
	}


	MV::NavigationAgent agent(world, MV::Point<int>(2, 2)), agent2(world, MV::Point<int>(2, 2)), agent3(world, MV::Point<int>(2, 2));
	agent.goal(MV::Point<int>(17, 17), 3);
	agent2.goal(MV::Point<int>(17, 17));
	agent3.goal(MV::Point<int>(17, 17), 1);
	agent3.speed(2.0f);
	MV::Stopwatch timer;
	while (agent.pathfinding() || agent2.pathfinding()) {
		while (timer.frame(.5f)) {
			agent.update(1);
			agent2.update(1);
			agent3.update(1);
			auto pathNodes = agent.path();
			auto pathNoeds2 = agent2.path();
			pathNodes.insert(pathNodes.end(), pathNoeds2.begin(), pathNoeds2.end());

			for (int x = 0; x < world->size().width; ++x) {
				for (int y = 0; y < world->size().height; ++y) {
					if (MV::point(x, y) == agent.position()) {
						std::cout << "1";
					} else if (MV::point(x, y) == agent2.position()) {
						std::cout << "2";
					}
					else if (MV::point(x, y) == agent3.position()) {
						std::cout << "3";
					}
					else if (world->blocked({ x, y })) {
						std::cout << "O";
					} else if (std::find_if(pathNodes.begin(), pathNodes.end(), [&](const MV::PathNode &a_node) {return a_node.position().x == x && a_node.position().y == y; }) != pathNodes.end()) {
						std::cout << "*";
					} else {
						std::cout << "-";
					}
				}
				std::cout << std::endl;
			}

		}
	}

	return 0;
// 
// 	MV::Expression expression("(x + 5) * functest()",
// 	{ {"x", 5.0f} });
// 
// 	expression.function("functest", std::function<MV::PointPrecision()>([]() {
// 		return 2.0f;
// 	}));
// 
// 	std::cout << "Expression Result: " << expression.evaluate() << std::endl;
// 
// 	expression["y"] = 20.0f;
// 
// 	std::cout << "Expression Result: " << expression.evaluate("(x + 5) * functest() + y") << std::endl;
// 	
// 	expression["x"] = 15.0f;
// 
// 	expression["x"] = 20.0f;
// 
// 	std::cout << "Expression Result: " << expression.evaluate() << std::endl;
// 
// 	GameEditor menu;
// 
// 	menu.start();
// 
// 	return 0;
}