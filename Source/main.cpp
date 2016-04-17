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

	MV::NetworkMessage a("12345"), b("123456789"), c("The quick brown dog jumped over the lazy fucking mother asshole of a fox."), 
		d("Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed eget massa accumsan, faucibus sapien sit amet, venenatis mi. Etiam laoreet massa a mauris laoreet dapibus. In hac habitasse platea dictumst. Lorem ipsum dolor sit amet, consectetur adipiscing elit. In hac habitasse platea dictumst. Praesent vitae magna et leo convallis ullamcorper id nec velit. Aenean libero felis, pharetra vel augue ut, sagittis posuere mauris. Morbi nisi mauris, elementum vel egestas in, pharetra id nunc. Maecenas feugiat augue nec nisi lobortis gravida non consequat tellus. Proin eget sem sed arcu imperdiet aliquam vitae id eros. Vivamus blandit tortor vitae nunc elementum, id porttitor tellus rhoncus.Ut ac pulvinar purus.Maecenas porta commodo mi vel consequat.Donec varius ligula sodales, cursus ligula id, mattis tortor.Fusce vitae faucibus ipsum, quis commodo sem.Cras fringilla accumsan eros, in blandit ligula auctor mattis.Maecenas iaculis quam ut pharetra volutpat.Sed posuere augue eu neque lacinia fringilla.");

	a.pushSizeToHeaderBuffer();
	b.pushSizeToHeaderBuffer();
	c.pushSizeToHeaderBuffer();
	d.pushSizeToHeaderBuffer();

	std::cout << a.content.size() << ", " << a.sizeFromHeaderBuffer() << std::endl;
	std::cout << b.content.size() << ", " << b.sizeFromHeaderBuffer() << std::endl;
	std::cout << c.content.size() << ", " << c.sizeFromHeaderBuffer() << std::endl;
	std::cout << d.content.size() << ", " << d.sizeFromHeaderBuffer() << std::endl;
	std::cout << "_________" << std::endl;

	GameEditor menu;

	menu.start();

	return 0;
}