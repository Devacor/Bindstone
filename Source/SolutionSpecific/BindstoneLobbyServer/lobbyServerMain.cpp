#include "Game/gameEditor.h"
#include "MV/Utility/threadPool.hpp"

#include "MV/ArtificialIntelligence/pathfinding.h"
#include "MV/Serialization/serialize.h"
//#include "vld.h"

#include "Game/NetworkLayer/lobbyServer.h"

#include "MV/Utility/scopeGuard.hpp"

#include "webServer.h"

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

class TestWebConnection : public MV::WebConnectionStateBase {
public:
	using WebConnectionStateBase::WebConnectionStateBase;

	void processRequest(const MV::HttpRequest& a_recieve) override {
		std::ostringstream output;
		auto postParams = a_recieve.parsePostParameters();
		output << "<html><body>Hello World!<br/>I see your name is " << postParams["name"] << " and you enjoy " << postParams["food"] << "!</body></html>";
		connection()->send(MV::HttpResponse::make200(output.str()));
	}
};

int main(int, char *[]) {
	//auto result = MV::DownloadRequest::make({ MV::Url{"https://ptsv2.com/t/snapjaw" }, { {"param1", "value1"}, {"param2", "value2"} } });
	auto result = MV::DownloadRequest::make({ MV::Url{"http://www.snapjaw.net/test.php?g1=v1" }, {{"p1", "v1"}, {"p2", "v2"}} });

	//std::cout << "Response:[" <<result->response() << "]\n";


	try {
		auto webServer = std::make_shared<MV::WebServer>(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 80),
			[](const std::shared_ptr<MV::WebConnection>& a_connection) {
				return std::make_unique<TestWebConnection>(a_connection);
			});

		//auto result = MV::DownloadRequest::make({ MV::Url{"http://localhost:80/test.php?g1=v1" }, {{"p1", "v1"}, {"p2", "v2"}} });

		//std::cout << result->response();

		bool done = false;
		while (!done) {
			std::this_thread::yield();
		}
	}
	catch (std::exception& e) {
		MV::error("Exception Toasted the Server: ", e.what());
	}

	return 0;

    Managers managers({});
	managers.timer.start();
	bool done = false;

    try {
        auto server = std::make_shared<LobbyServer>(managers);
        std::cout << "Made server\n";
        while (!done) {
            managers.pool.run();
            auto tick = managers.timer.delta("tick");
            server->update(tick);
            std::this_thread::yield();
        }
    } catch (std::exception & e) {
        MV::error("Exception Toasted the Server: ", e.what());
    }

	return 0;
}