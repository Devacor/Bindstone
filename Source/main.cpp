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
// 	MV::Path path(world, MV::Point<int>(2, 2), MV::Point<int>(17, 17));
// 
// 	for (int i = 2; i < 18; ++i) {
// 		world->get({ i, 3 }).block();
// 	}
// 
// 
// 	auto pathNodes = path.path();
// 
// 	for (int x = 0; x < world->size().width; ++x) {
// 		for (int y = 0; y < world->size().height; ++y) {
// 			if (std::find_if(pathNodes.begin(), pathNodes.end(), [&](const MV::PathNode &a_node) {return a_node.position().x == x && a_node.position().y == y; }) != pathNodes.end()) {
// 				std::cout << "*";
// 			} else if (world->blocked({ x, y })) {
// 				std::cout << "O";
// 			} else {
// 				std::cout << "-";
// 			}
// 		}
// 		std::cout << std::endl;
// 	}

	MV::Variant<int, std::string> test(std::string("hey"));
	MV::visit(test,
		[](int val) {std::cout << "INT: " << val; },
		[](const std::string &val) {std::cout << "STRING: " << val; }
	);
	MV::Variant<int, std::string> test2(102);
	MV::visit(test2,
		[](int val) {std::cout << "INT: " << val; },
		[](const std::string &val) {std::cout << "STRING: " << val; }
	);

	GameEditor menu;

	menu.start();

	return 0;
}