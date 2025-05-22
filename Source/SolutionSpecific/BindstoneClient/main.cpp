#if 1

#include "Game/gameEditor.h"
#include "MV/Utility/threadPool.hpp"
#include "MV/Utility/services.hpp"

#include "MV/ArtificialIntelligence/pathfinding.h"
#include "MV/Serialization/serialize.h"
//#include "vld.h"


#include "MV/Utility/scopeGuard.hpp"
#include "chaiscript/chaiscript.hpp"
#include "Game/NetworkLayer/synchronizeAction.h"
#include "MV/Network/networkObject.h"

#include <fstream>

#include "glm/mat4x4.hpp"

struct Base {
	virtual ~Base() {}

	template <class Archive>
	void save(Archive & archive, std::uint32_t const) const {
		archive(CEREAL_NVP(baseMember));
	}
	template <class Archive>
	void load(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(baseMember));
	}
	template <class Archive>
	static void load_and_construct(Archive & archive, cereal::construct<Base> &construct, std::uint32_t const version) {
		construct();
		construct->load(archive, version);
	}

	int baseMember = 1;
};

struct Derived1 : public Base {
	template <class Archive>
	void save(Archive & archive, std::uint32_t const) const {
		archive(CEREAL_NVP(derived1Member),
			cereal::make_nvp("Base", cereal::base_class<Base>(this))
		);
	}
	template <class Archive>
	void load(Archive & archive, std::uint32_t const /*version*/) {
		archive(CEREAL_NVP(derived1Member),
			cereal::make_nvp("Base", cereal::base_class<Base>(this))
		);
	}
	template <class Archive>
	static void load_and_construct(Archive & archive, cereal::construct<Base> &construct, std::uint32_t const version) {
		construct();
		construct->load(archive, version);
	}

	int derived1Member = 1;
};

CEREAL_REGISTER_TYPE(Base);
CEREAL_REGISTER_TYPE(Derived1);

class NetTypeA {
public:
	void synchronize(std::shared_ptr<NetTypeA> a_other) {
		std::cout << "A: " << name << " syncing with: " << a_other->name << "\n";
		name = a_other->name;
	}

	void destroy(std::shared_ptr<NetTypeA> a_other){
		std::cout << "A: DESTROY " << name << "\n";
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const) {
		archive(CEREAL_NVP(name));
	}

	std::string name;
};

class NetTypeB {
public:
	void synchronize(std::shared_ptr<NetTypeB> a_other) {
		std::cout << "B: " << id << " syncing with: " << a_other->id << "\n";
		id = a_other->id;
	}

	void destroy(std::shared_ptr<NetTypeB> a_other) {
		std::cout << "B: DESTROY " << id << "\n";
		id = a_other->id;
	}

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const) {
		archive(CEREAL_NVP(id));
	}

	int id;
};

void PathfindingTest();


template<size_t SizeX, size_t SizeY, size_t Common>
MV::Matrix<SizeX, Common> M_1(const MV::Matrix<SizeX, SizeY>& a_lhs, const MV::Matrix<SizeY, Common>& a_rhs) {
	MV::Matrix<SizeX, Common> result;
	for (size_t x = 0; x != SizeX; ++x) {
		for (size_t c = 0; c != Common; c++) {
			for (size_t y = 0; y != SizeY; y++) {
				result(x, c) += a_lhs(x, y) * a_rhs(y, c);
			}
		}
	}
	return result;
}

template<size_t sizeAX, size_t sizeAY, size_t sizeBY>
MV::Matrix<sizeAX, sizeBY> M_2(const MV::Matrix<sizeAX, sizeAY>& A, const MV::Matrix<sizeAY, sizeBY>& B) {
	MV::Matrix<sizeAX, sizeBY> result;
	for (int i = 0; i < sizeAX; i++) {
		for (int j = 0; j < sizeBY; j++) {
			for (int k = 0; k < sizeAY; k++) {
				result(i, j) += A(i, k) * B(k, j);
			}
		}
	}
	return result;
}


int main(int argc, char *argv[]) {
	MV::info("Hello world!");
	MV::debug(":D :D :D");
	MV::warning(":C :C :C");
	MV::error("Whoopse!");

	MV::Matrix<3, 1> m1;
	MV::Matrix<1, 3> m2;
	for (int i = 0; i < 3; ++i) {
		m1(i, 0) = (float)i + 3;
		m2(0, i) = (float)i;
	}

	auto ra = M_1(m2, m1);
	auto rb = M_2(m2, m1);
	//auto rc = M_3(m2, m1);
	MV::info("RESULT 1 a: ", ra);
	MV::info("RESULT 1 b: ", ra);
	//MV::info("RESULT 1 b: ", rc);

	auto r2a = M_1(m1, m2);
	MV::info("RESULT 2 a: ", r2a);
	auto r2b = M_2(m1, m2);
	MV::info("RESULT 2 b: ", r2b);
	//auto r2c = M_3(m1, m2);
	//MV::info("RESULT 2 c: ", r2c);

	std::string name;
	std::string pass;
	for (int i = 0; i < argc-1; ++i) {
		if (strcmp(argv[i], "-n") == 0) {
			name = argv[i + 1];
			std::cout << "Got name: " << name << std::endl;
		} else if (strcmp(argv[i], "-p") == 0) {
			pass = argv[i + 1];
			std::cout << "Got pass: " << pass << std::endl;
		}
	}
	auto derived = std::make_shared<Derived1>();
	derived->baseMember = 5;
	derived->derived1Member = 10;

	auto saved = MV::toJson(derived);
	auto loaded = MV::fromJson<std::shared_ptr<Derived1>>(saved);

	CreatureNetworkState stateSizeTest;
	stateSizeTest.animationName = "idle";
	stateSizeTest.creatureTypeId = "Life_T1";
	stateSizeTest.position = MV::Point<>(0, 0, 0);
	MV::info("Creature NetworkA DELTA SIZE: ", MV::toBinaryString(stateSizeTest).size());

	stateSizeTest.animationTime = 10.0;
	stateSizeTest.position = MV::Point<>(0, 0, 0);
	MV::info("Creature NetworkA DELTA SIZE POS: ", MV::toBinaryString(stateSizeTest).size());

	stateSizeTest.animationTime = 10.0;
	MV::info("Creature NetworkA DELTA SIZE POS: ", MV::toBinaryString(stateSizeTest).size());

	stateSizeTest.animationLoops = true;
	MV::info("Creature NetworkA DELTA SIZE NONE: ", MV::toBinaryString(stateSizeTest).size());

	std::cout << "done saveload test.";

	//PathfindingTest();
	//return 0;

// 	std::string content = "Hello World";
// 	auto scriptString = "puts('['); puts(arg_0); puts(']'); puts('['); puts(arg_1); puts(']'); puts('['); puts(arg_2); puts(\"]\n\");";
// 
// 	MV::Signal<void(const std::string&, const std::string&, const std::string&)> callbackTest;
// 	callbackTest.scriptEngine(&chaiScript);
// 	callbackTest.connect("test", scriptString);
// 	{
// 		auto connection2 = callbackTest.connect("puts(\"![\"); puts(arg_0); puts(\"]!\n\");");
// 		callbackTest(content, ":D :D :D", "TEST CHAR");
// 	}
// 	callbackTest("!!!", "VVV", "~~~");
// 
// 	auto jsonCallback = MV::toJson(callbackTest);
// 
// 	auto callbackLoadTest = MV::fromJson<MV::Signal<void(const std::string&, const std::string&, const std::string&)>>(jsonCallback, [&](cereal::JSONInputArchive& archive) {
// 		archive.add(cereal::make_nvp("script", &chaiScript));
// 	});
// 
// 	callbackLoadTest("LoadTest2", "DidThisWork?", "Maybe");
// 
// 	std::cout << std::endl;

	// 	pqxx::connection c("host=mutedvision.cqki4syebn0a.us-west-2.rds.amazonaws.com port=5432 dbname=bindstone user=m2tm password=Tinker123");
	// 	pqxx::work txn(c);
	// 
	// 
	// 	txn.exec(
	// 		"CREATE EXTENSION IF NOT EXISTS citext WITH SCHEMA public;"
	// 		"CREATE TABLE Instances ("
	// 		"	Id SERIAL primary key,"
	// 		"	Available boolean			default false,"
	// 		"	Host text						default '',"
	// 		"	Port integer				default 0,"
	// 		"	PlayerLeft	int				default 0,"
	// 		"	PlayerRight	int				default 0,"
	// 		"	LastUpdate timestamp without time zone default (now() at time zone 'utc'),"
	// 		"	Result JSON"
	// 		");");
	// 	txn.commit();

	// 	pqxx::result r = txn.exec(
	// 		"SELECT state "
	// 		"FROM players "
	// 		"WHERE email = " + txn.quote("maxmike@gmail.com"));
	// 
	// 	if (r.size() != 1)
	// 	{
	// 		std::cerr
	// 			<< "Expected 1 player with email " << txn.quote("maxmike@gmail.com") << ", "
	// 			<< "but found " << r.size() << std::endl;
	// 		return 1;
	// 	}
	// 
	// 	std::string status = r[0][0].c_str();
	// 	std::cout << "Updating employee #" << status << std::endl;


	//auto emailer = MV::Email::make("email-smtp.us-west-2.amazonaws.com", "587", { "AKIAIVINRAMKWEVUT6UQ", "AiUjj1lS/k3g9r0REJ1eCoy/xeYZgLXmB8Nrep36pUVw" });
	//emailer->send({ "jai", "jackaldurante@gmail.com", "Derv", "maxmike@gmail.com" }, "Testing new Interface", "Does this work too?");
// 	{
// 		std::ofstream codes("codes.txt");
// 		std::set<std::string> generated;
// 		while (generated.size() < 1800) {
// 			generated.insert("VF18_" + MV::randomString("23456789ABCDEFGHJKLMPQRSTUVWXYZ", 4) + "-" + MV::randomString("23456789ABCDEFGHJKLMPQRSTUVWXYZ", 4) + "-" + MV::randomString("23456789ABCDEFGHJKLMPQRSTUVWXYZ", 4));
// 		}
// 		for(auto&& codeString : generated)
// 		{
// 			codes << codeString << "\n";
// 		}
// 	}
	
	MV::NetworkObjectPool<NetTypeA, NetTypeB> pool, pool2;

	pool.onSpawn<NetTypeA>([](std::shared_ptr<MV::NetworkObject<NetTypeA>> a_newItem) {
		std::cout << "1A:" << a_newItem->self()->name << "\n";
	});
	pool2.onSpawn<NetTypeA>([&](std::shared_ptr<MV::NetworkObject<NetTypeA>> a_newItem) {
		std::cout << "2A:" << a_newItem->self()->name << "\n";
	});

	pool.onSpawn<NetTypeB>([](std::shared_ptr<MV::NetworkObject<NetTypeB>> a_newItem) {
		std::cout << "1B:" << a_newItem->self()->id << "\n";
	});
	pool2.onSpawn<NetTypeB>([&](std::shared_ptr<MV::NetworkObject<NetTypeB>> a_newItem) {
		std::cout << "2B:" << a_newItem->self()->id << "\n";
	});

	auto newItem = std::make_shared<NetTypeA>();
	newItem->name = "Happy!";

	auto newItem2 = std::make_shared<NetTypeB>();
	newItem2->id = 5;

	auto newObject = pool.spawn(newItem);
	auto newObject2 = pool.spawn(newItem2);
	auto testShared = newObject->shared_from_this();
	

	pool2.synchronize(MV::fromJson<decltype(pool.updated())>(MV::toJson(pool.updated())));

	newObject->modify()->name = "Unhappy!";

	pool2.synchronize(pool.updated());

	newObject->destroy();

	pool2.synchronize(pool.updated());

	GameEditor menu(name, pass);

	menu.start();
	
	return 0;
}

void PathfindingTest() {
	auto world = MV::Map::make({ 20, 20 }, false);

	for (int i = 0; i < 20; ++i) {
		world->get({ 8, i }).staticBlock();
	}

	world->get({ 8, 6 }).staticUnblock();
	world->get({ 8, 7 }).staticUnblock();


	std::vector<std::shared_ptr<MV::NavigationAgent>> agents{
		MV::NavigationAgent::make(world, MV::Point<int>(2, 2), 2),
		MV::NavigationAgent::make(world, MV::Point<int>(0, 2), 2),
		MV::NavigationAgent::make(world, MV::Point<int>(2, 0), 2),
		MV::NavigationAgent::make(world, MV::Point<int>(0, 0), 2) };

	for (int i = 0; i < agents.size(); ++i) {
		agents[i]->debugId(i);
	}
	MV::Stopwatch timer;

	for (int i = 0; i < agents.size(); ++i) {
		agents[i]->onStart.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
			std::cout << (i + 1) << ": START" << std::endl;
		});
		agents[i]->onStop.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
			std::cout << (i + 1) << ": STOP" << std::endl;
		});
		agents[i]->onBlocked.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
			std::cout << (i + 1) << ": BLOCKED" << std::endl;
		});
		agents[i]->onArrive.connect("start", [=](std::shared_ptr<MV::NavigationAgent> agent) {
			std::cout << (i + 1) << ": ARRIVED" << std::endl;
		});
		agents[i]->goal(MV::Point<int>(10, 17), 0);
	}


	int i = 0;

	/*
	for (int i = 0; i < 23; ++i) {
		for (auto&& agent : agents) {
			agent->update(1.0f);
			for (int y = 0; y < world->size().height; ++y) {
				for (int x = 0; x < world->size().width; ++x) {
					bool wasAgent = false;
					for (int i = 0; i < agents.size(); ++i) {
						if (agents[i]->overlaps({ x, y })) {
							wasAgent = true;
							break;
						}
					}
					if (!wasAgent) {
						if (world->blocked({ x, y })) {
						} else {
							world->get({ x, y }).clearance();
						}
					}
				}
			}
		}
	}
	*/

	while (std::find_if(agents.begin(), agents.end(), [](auto&& agent) {
		return agent->pathfinding();
	}) != agents.end()) {
		char a;
		while (true) {
			for (auto&& agent : agents) {
				agent->update(1.0f);
			}
			std::vector<MV::PathNode> pathNodes;
			for (int y = 0; y < world->size().height; ++y) {
				for (int x = 0; x < world->size().width; ++x) {
					bool wasAgent = false;
					for (int k = 0; k < agents.size(); ++k) {
						if (agents[k]->overlaps({ x, y })) {
							std::cout << "[" << (char)(k + 65) << "]";
							wasAgent = true;
							break;
						}
					}
					if (!wasAgent) {
						if (world->blocked({ x, y })) {
							std::cout << " " << "X" << " ";
						}
						else {
							std::cout << " " << world->get({ x, y }).clearance() << " ";
						}
					}
				}
				std::cout << std::endl;
			}
			std::cout << "\n\n\n" << std::endl;
			++i;
			std::cin >> a;
		}
	}
	std::cout << "YO" << std::endl;
}

#else
/*
 *  rectangles.c
 *  written by Holmes Futrell
 *  use however you want
 */

//Grabbed from here: https://gist.github.com/Khaledgarbaya/86ac0b3cf9e5fc89cdcb
#include "SDL.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#include "MV/Script/script.h"
#include "MV/Utility/log.h"

#include "MV/Network/dynamicVariable.h"


using namespace std;

/*
 Produces a random int x, min <= x <= max
 following a uniform distribution
 */
int randomInt(int min, int max) {
    return min + rand() % (max - min + 1);
}

/*
 Produces a random float x, min <= x <= max
 following a uniform distribution
 */
float randomFloat(float min, float max) {
    return rand() / (float) RAND_MAX *(max - min) + min;
}

void fatalError(const char *string) {
    printf("%s: %s\n", string, SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, string, SDL_GetError(), NULL);
    exit(1);
}

static Uint64 prevTime = 0;

double updateDeltaTime() {
    Uint64 curTime;
    double deltaTime;
    
    if (prevTime == 0) {
        prevTime = SDL_GetPerformanceCounter();
    }
    
    curTime = SDL_GetPerformanceCounter();
    deltaTime = (double) (curTime - prevTime) / (double) SDL_GetPerformanceFrequency();
    prevTime = curTime;
    
    return deltaTime;
}

void render(SDL_Renderer *renderer) {
    Uint8 r, g, b;
    int renderW;
    int renderH;
    
    SDL_RenderGetLogicalSize(renderer, &renderW, &renderH);
    
    /*  Come up with a random rectangle */
    SDL_Rect rect;
    rect.w = randomInt(64, 128);
    rect.h = randomInt(64, 128);
    rect.x = randomInt(0, renderW);
    rect.y = randomInt(0, renderH);
    
    /* Come up with a random color */
    r = randomInt(50, 255);
    g = randomInt(50, 255);
    b = randomInt(50, 255);
    
    /*  Fill the rectangle in the color */
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderFillRect(renderer, &rect);
    
    /* update screen */
    SDL_RenderPresent(renderer);
}

struct PointTest {
	int x;
	void display() {
		MV::info("Bindstone: PointTest Display");
	}
};

void customPrint(const std::string& string) {
	MV::info("Bindstone: " + string);
}

void hookDynamicVariable2(chaiscript::ChaiScript& a_script) {
	a_script.add(chaiscript::user_type<MV::DynamicVariable>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable()>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable(bool)>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable(int64_t)>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable(int)>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable(size_t)>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable(double)>(), "DynamicVariable");
	a_script.add(chaiscript::constructor<MV::DynamicVariable(const std::string&)>(), "DynamicVariable");

	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, bool a_value) -> decltype(auto) {
		return a_self = a_value;
		}), "=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, int64_t a_value) -> decltype(auto) {
		return a_self = a_value;
		}), "=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, size_t a_value) -> decltype(auto) {
		return a_self = a_value;
		}), "=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, int a_value) -> decltype(auto) {
		return a_self = a_value;
		}), "=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, double a_value) -> decltype(auto) {
		return a_self = a_value;
		}), "=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, std::string a_value) -> decltype(auto) {
		return a_self = a_value;
		}), "=");

	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, bool a_value) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, int64_t a_value) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, size_t a_value) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, int a_value) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, double a_value) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, std::string a_value) -> decltype(auto) {
		return a_self == a_value;
		}), "==");

	a_script.add(chaiscript::fun([&](bool a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](int64_t a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](size_t a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](int a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](double a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self == a_value;
		}), "==");
	a_script.add(chaiscript::fun([&](std::string a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self == a_value;
		}), "==");

	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, bool a_value) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, int64_t a_value) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, size_t a_value) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, int a_value) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, double a_value) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self, std::string a_value) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");

	a_script.add(chaiscript::fun([&](bool a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](int64_t a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](size_t a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](int a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](double a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");
	a_script.add(chaiscript::fun([&](std::string a_value, MV::DynamicVariable& a_self) -> decltype(auto) {
		return a_self != a_value;
		}), "!=");

	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self) {
		return a_self.getBool();
		}), "bool");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self) {
		return a_self.getInt();
		}), "int");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self) {
		return a_self.getDouble();
		}), "double");
	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self) {
		return a_self.getString();
		}), "string");

	a_script.add(chaiscript::fun([&](MV::DynamicVariable& a_self) {
		return a_self.clear();
		}), "clear");

	a_script.add(chaiscript::bootstrap::standard_library::map_type<std::map<std::string, MV::DynamicVariable>>("DynamicVariableMap"));
	a_script.add(chaiscript::bootstrap::standard_library::vector_type<std::vector<MV::DynamicVariable>>("DynamicVariableVector"));
}

int main(int argc, char *argv[]) {
    
    SDL_Window *window;
    SDL_Renderer *renderer;
    int done;
    SDL_Event event;
    int windowW;
    int windowH;
    
    /* initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fatalError("Could not initialize SDL");
    }
    
    /* seed random number generator */
    srand(time(NULL));
    
    /* create window and renderer */
    window = SDL_CreateWindow(NULL, 0, 0, 480, 320, SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == 0) {
        fatalError("Could not initialize Window");
    }
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        fatalError("Could not create renderer");
    }
    
    SDL_GetWindowSize(window, &windowW, &windowH);
    SDL_RenderSetLogicalSize(renderer, windowW, windowH);
    
    /* Fill screen with black */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
	chaiscript::ChaiScript script;
	script.add(chaiscript::fun([](const std::string& a_output) {
		MV::info("CHAISCRIPT: ", a_output);
	}), "log_chaiscript_output");
	script.eval("global print = fun(x){ log_chaiscript_output(to_string(x)); };");
	script.add(chaiscript::user_type<PointTest>(), "PointTest");
	script.add(chaiscript::constructor<PointTest()>(), "PointTest");
	script.add(chaiscript::fun(&PointTest::display), "display");
	script.add(chaiscript::fun(&PointTest::x), "x");
	script.add(chaiscript::fun(&customPrint), "log_chaiscript_output");
	script.add(chaiscript::bootstrap::standard_library::map_type<std::map<std::string, PointTest>>("PointTestMap"));
	
	script.eval(R"(
		print("Trying PointTest");
		var testStuff = PointTest();
		print("Trying PrintVar");
		print(testStuff.x);
		testStuff.x = 5;
		print(testStuff.x);
		testStuff.display();
	)");

	script.eval(R"(
		print("TRYING DYNAMIC VARIABLE");
		var v = DynamicVariable(10);
		print(v.int);
		var v2 = DynamicVariable("Test");
		print(v2.string);
		print("TEST SUCCESS");

		var testStuff2 = DynamicVariableMap();
		print("f1");
		testStuff2["v1"] = false;
		print("f2");
		print(testStuff2["v1"].bool);
		testStuff2["v2"] = 0;
		print("f3");
		print(testStuff2["v2"].int);
		)");
    /* Enter render loop, waiting for user to quit */
    done = 0;
    while (!done) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                done = 1;
            }
        }
        render(renderer);
        SDL_Delay(1);
    }
    
    /* shutdown SDL */
    SDL_Quit();
    
    return 0;
}
#endif