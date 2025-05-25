#define ASIO_STANDALONE
#include <SDL.h>
#include <SDL_image.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <asio.hpp>

#include "MV/Utility/log.h"
#include "MV/Utility/generalUtility.h"

#undef CreateWindow

#ifdef __ANDROID__
#include <android/log.h>
void output(const std::string& a_str) {
	__android_log_print(ANDROID_LOG_ERROR, "TRACKERS", "%s", a_str.c_str());
}
#else
void output(const std::string& a_str) {
	std::cout << a_str << std::endl;
}
#endif

int posX = 100;
int posY = 200;
int sizeX = 300;
int sizeY = 400;

SDL_Window* window;
SDL_Renderer* renderer;

bool InitEverything();
bool InitSDL();
bool CreateWindow();
bool CreateRenderer();
void SetupRenderer();

void Render();
void RunGame();

SDL_Rect playerPos;

int main(int argc, char* args[])
{
	std::filesystem::path full_path(std::filesystem::current_path());
	output(std::string("path: ") + full_path.string());

	//std::filesystem::path full_path2(std::filesystem::current_path());
	//output(std::string("Current path is : ") + full_path2.string());

	if (!InitEverything())
		return -1;


	// Initlaize our playe
	playerPos.x = 20;
	playerPos.y = 20;
	playerPos.w = 20;
	playerPos.h = 20;

	RunGame();
	return 0;
}
void Render()
{

	// Change color to blue
	SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);

	// Render our "player"
	SDL_RenderFillRect(renderer, &playerPos);

	// Change color to green
	SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

	// Render the changes above
	SDL_RenderPresent(renderer);
}
bool InitEverything()
{
	if (!InitSDL())
		return false;

	if (!CreateWindow())
		return false;

	if (!CreateRenderer())
		return false;

	SetupRenderer();

	return true;
}
bool InitSDL()
{
	if (SDL_Init(SDL_INIT_EVERYTHING) == -1)
	{
		std::cout << " Failed to initialize SDL : " << SDL_GetError() << std::endl;
		return false;
	}

	int imgInitDesiredFlags = IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP;
	int imgInitFlags = IMG_Init(imgInitDesiredFlags);
	if ((imgInitFlags & imgInitDesiredFlags) != imgInitDesiredFlags) {
		output("IMG_Init: Failed to init jpg, png support!");
		std::string error = IMG_GetError();
		output(error);
	}

	return true;
}
bool CreateWindow()
{
	window = SDL_CreateWindow("Server", posX, posY, sizeX, sizeY, 0);

	if (window == nullptr)
	{
		std::cout << "Failed to create window : " << SDL_GetError();
		return false;
	}

	return true;
}
bool CreateRenderer()
{
	renderer = SDL_CreateRenderer(window, -1, 0);

	if (renderer == nullptr)
	{
		std::cout << "Failed to create renderer : " << SDL_GetError();
		return false;
	}

	return true;
}
void SetupRenderer()
{
	// Set size of renderer to the same as window
	SDL_RenderSetLogicalSize(renderer, sizeX, sizeY);

	// Set color of renderer to green
	SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
}
void RunGame()
{
	auto contents = MV::fileContents("Interface/interfaceManager.script");
	const char* cstring = contents.c_str();
	output("FILE_BEGIN_2");
	output(contents);
	output("FILE_END_2");
	std::cout << contents << std::endl;
	MV::writeToFile("Interface/interfaceManager.script", "This is just a test.");
	MV::deleteFile("Interface/interfaceManager.script");
	contents = MV::fileContents("Interface/interfaceManager.script");
	cstring = contents.c_str();
	output("FILE_BEGIN_2");
	output(contents);
	output("FILE_END_2");

	SDL_RWops* sdlIO = SDL_RWFromFile("Interface/interfaceManagerNOEXIST.script", "rb+");
	if (sdlIO) {
		output("YESSSSSSSS");
	}

	sdlIO = SDL_RWFromFile("BindstoneTest.txt", "rb+");
	if (sdlIO) {
		output("YESSSSSSSS2");
	}
	
	//std::string contents = MV::fileContents("Interface/interfaceManager.script");
	//output(contents);

	MV::info("Test");
	output(std::filesystem::unique_path().string());
	SDL_Surface* image = IMG_Load("Images/image.webp");
	
	output(IMG_GetError());
 	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, image);

	bool loop = true;

	while (loop)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT)
				loop = false;
			else if (event.type == SDL_KEYDOWN)
			{
				switch (event.key.keysym.sym)
				{
				case SDLK_RIGHT:
					++playerPos.x;
					break;
				case SDLK_LEFT:
					--playerPos.x;
					break;
					// Remeber 0,0 in SDL is left-top. So when the user pressus down, the y need to increase
				case SDLK_DOWN:
					++playerPos.y;
					break;
				case SDLK_UP:
					--playerPos.y;
					break;
				default:
					break;
				}
			}
		}

		// Clear the window and make it all green
		SDL_RenderClear(renderer);
		//SDL_Rect dstrect = { 5, 5, 320, 240 };
		//SDL_RenderCopy(renderer, texture, NULL, &dstrect);
		//SDL_Rect dstrect = { 5, 5, 320, 240 };
		SDL_RenderCopy(renderer, texture, NULL, NULL);

		SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

		// Render the changes above
		SDL_RenderPresent(renderer);

		// Add a 16msec delay to make our game run at ~60 fps
		SDL_Delay(16);
	}
}