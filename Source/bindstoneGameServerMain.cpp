#include "Game/gameEditor.h"
#include "Utility/threadPool.h"

#include "ArtificialIntelligence/pathfinding.h"
#include "Utility/cerealUtility.h"
//#include "vld.h"


#include "Utility/scopeGuard.hpp"
#include "chaiscript/chaiscript.hpp"
#include "Game/NetworkLayer/gameServer.h"

struct TestObject {
	TestObject() { std::cout << "\nConstructor\n"; }
	~TestObject() { std::cout << "\nDestructor\n"; }
	TestObject(TestObject&);
	TestObject(TestObject&&) { std::cout << "\nMove\n"; }
	TestObject& operator=(const TestObject&) { std::cout << "\nAssign\n"; }

	int payload = 0;
};

TestObject::TestObject(TestObject&) {
	std::cout << "\nCopy\n";
	payload++;
}

#include <fstream>

int main(int, char *[]) {
	Managers managers;
	managers.timer.start();
	bool done = false;
	auto server = std::make_shared<GameServer>(managers, 22333);
	while (!done) {
		managers.pool.run();
		auto tick = managers.timer.delta("tick");
		server->update(tick);
		std::this_thread::yield();
	}

	return 0;
}