#include "Game/gameEditor.h"
#include "MV/Utility/threadPool.hpp"

#include "MV/ArtificialIntelligence/pathfinding.h"
#include "MV/Utility/cerealUtility.h"
//#include "vld.h"


#include "MV/Utility/scopeGuard.hpp"
#include "chaiscript/chaiscript.hpp"
#include "Game/NetworkLayer/gameServer.h"

#include "MV/Utility/taskActions.hpp"

#include <fstream>

template<size_t SizeX, size_t SizeY, size_t ResultY>
inline MV::Matrix<SizeX, ResultY> shittyMultiply(const MV::Matrix<SizeX, SizeY>& a_left, const MV::Matrix<ResultY, SizeY>& a_right) {
	MV::Matrix<SizeX, ResultY> result;

	for (size_t x = 0; x < SizeX; ++x) {
		for (size_t common = 0; common < SizeX; ++common) {
			for (size_t y = 0; y < ResultY; ++y) {
				result.access(x, y) += a_left(common, y) * a_right(x, common);
			}
		}
	}

	return result;
}

void multiplyTest(std::string name, MV::Matrix<4, 4> &a, MV::Matrix<4, 4> & b) {
	MV::Stopwatch timer;
	timer.start();
	for (int i = 0; i < 1000000; ++i) {
		a = a * b;
	}
	auto elapsed = timer.stop();
	std::cout << "Test [" << name << "] == " << elapsed << "s\n";
}

void multiplyTestShit(std::string name, MV::Matrix<4, 4> & a, MV::Matrix<4, 4> & b) {
	MV::Stopwatch timer;
	timer.start();
	for (int i = 0; i < 1000000; ++i) {
		a = shittyMultiply(a, b);
	}
	auto elapsed = timer.stop();
	std::cout << "Test [" << name << "] == " << elapsed << "s\n";
}

// void multiplyTest(std::string name, boost::numeric::ublas::matrix<float>& a, boost::numeric::ublas::matrix<float>& b) {
// 	MV::Stopwatch timer;
// 	timer.start();
// 	for (int i = 0; i < 1000000; ++i) {
// 		boost::numeric::ublas::axpy_prod(a, b, a, true);
// 	}
// 	auto elapsed = timer.stop();
// 	std::cout << "Test [" << name << "] == " << elapsed << "s\n";
// }

int main(int, char *[]) {
	Managers managers;
	managers.timer.start();
	
	MV::Matrix<4, 4> a1, b1, a2, b2;

	for (int x = 0; x < 4; ++x) {
		for (int y = 0; y < 4; ++y) {
			a1(x, y) = a2(x, y) = MV::randomNumber(0.2f, 5.0f);
			b1(x, y) = b2(x, y) = MV::randomNumber(0.2f, 5.0f);
		}
	}

	//multiplyTestShit("Basic", a2, b2);
	//multiplyTest("StaticFor", a1, b1);

	MV::initializeSpineBindings();

	bool done = false;
	auto server = std::make_shared<GameServer>(managers);
	MV::Task statDisplay;
	statDisplay.also("PrintBandwidth", [&](MV::Task&, double) {
		if (server->server()) {
			auto sent = static_cast<double>(server->server()->bytesPerSecondSent()) / 1024.0;
			auto received = static_cast<double>(server->server()->bytesPerSecondReceived()) / 1024.0;
			if (sent > 0 || received > 0) {
				MV::info("Server Sent: [", sent, "]kbs Received: [", received, "]kbs");
			}
		}
		return true;
	}).recent()->localInterval(1.0);
	while (!done) {
		managers.pool.run();
		auto tick = managers.timer.delta("tick");
		server->update(tick);
		statDisplay.update(tick);
		std::this_thread::yield();
	}

	return 0;
}