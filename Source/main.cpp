#if 1

#include "Game/gameEditor.h"
#include "Utility/threadPool.hpp"

#include "ArtificialIntelligence/pathfinding.h"
#include "Utility/cerealUtility.h"
//#include "vld.h"


#include "Utility/scopeGuard.hpp"
#include "chaiscript/chaiscript.hpp"

#include <fstream>
#include <windows.h>

int main(int, char *[]) {
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
	
	GameEditor menu;

	menu.start();
	
	return 0;
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