#if 1

#include "Game/gameEditor.h"
#include "Utility/threadPool.hpp"
#include "Utility/services.h"

#include "ArtificialIntelligence/pathfinding.h"
#include "Utility/cerealUtility.h"
//#include "vld.h"


#include "Utility/scopeGuard.hpp"
#include "chaiscript/chaiscript.hpp"
#include "Game/NetworkLayer/synchronizeAction.h"
#include "Network/networkObject.h"

#include <fstream>

class NetTypeA {
public:
	void synchronize(std::shared_ptr<NetTypeA> a_other) {
		std::cout << "A: " << name << " syncing with: " << a_other->name << "\n";
		name = a_other->name;
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

	template <class Archive>
	void serialize(Archive & archive, std::uint32_t const) {
		archive(CEREAL_NVP(id));
	}

	int id;
};

struct A {
	virtual void print() {
		std::cout << "A\n";
	}
};

struct B : public A {
	virtual void print() {
		std::cout << "A::B\n";
	}
};

struct C {
	virtual void print() {
		std::cout << "C\n";
	}
};
struct D : public C {
	virtual void print() {
		std::cout << "C::D\n";
	}

	void printd() {
		std::cout << "C::D\n";
	}
};

void PathfindingTest();

int main(int, char *[]) {

	PathfindingTest();
	return 0;

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

	{
		MV::Services services;
		std::unique_ptr<A> a = std::make_unique<A>();
		std::unique_ptr<A> b = std::make_unique<B>();
		std::unique_ptr<C> c = std::make_unique<C>();
		std::unique_ptr<D> d = std::make_unique<D>();
		services.connect(b.get());
		services.connect<C>(d.get());
		services.get<A>()->print();
		services.get<C, D>()->printd();
	}
	
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

	pool2.synchronize(pool.updated());

	newObject->self()->name = "Unhappy!";
	newObject->markDirty();

	pool2.synchronize(pool.updated());

	GameEditor menu;

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
					for (int i = 0; i < agents.size(); ++i) {
						if (agents[i]->overlaps({ x, y })) {
							std::cout << "[" << (char)(i + 65) << "]";
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