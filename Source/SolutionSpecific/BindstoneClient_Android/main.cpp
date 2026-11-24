#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_main.h>
#include <filesystem>
#include <iostream>
#include <string>

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

SDL_FRect playerPos;

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
	if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_GAMEPAD | SDL_INIT_SENSOR))
	{
		std::cout << " Failed to initialize SDL : " << SDL_GetError() << std::endl;
		return false;
	}

	return true;
}
bool CreateWindow()
{
	window = SDL_CreateWindow("Server", sizeX, sizeY, 0);
	if (window != nullptr)
	{
		SDL_SetWindowPosition(window, posX, posY);
	}

	if (window == nullptr)
	{
		std::cout << "Failed to create window : " << SDL_GetError();
		return false;
	}

	return true;
}
bool CreateRenderer()
{
	renderer = SDL_CreateRenderer(window, nullptr);

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
	SDL_SetRenderLogicalPresentation(renderer, sizeX, sizeY, SDL_LOGICAL_PRESENTATION_LETTERBOX);

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

	SDL_IOStream* sdlIO = SDL_IOFromFile("Interface/interfaceManagerNOEXIST.script", "rb+");
	if (sdlIO) {
		output("YESSSSSSSS");
	}

	sdlIO = SDL_IOFromFile("BindstoneTest.txt", "rb+");
	if (sdlIO) {
		output("YESSSSSSSS2");
	}
	
	//std::string contents = MV::fileContents("Interface/interfaceManager.script");
	//output(contents);

	MV::info("Test");
	output(std::filesystem::unique_path().string());
	SDL_Surface* image = IMG_Load("Images/image.webp");
	
	output(SDL_GetError());
 	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, image);

	bool loop = true;

	while (loop)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT)
				loop = false;
			else if (event.type == SDL_EVENT_KEY_DOWN)
			{
				switch (event.key.key)
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
		//SDL_RenderTexture(renderer, texture, NULL, &dstrect);
		//SDL_Rect dstrect = { 5, 5, 320, 240 };
		SDL_RenderTexture(renderer, texture, NULL, NULL);

		SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

		// Render the changes above
		SDL_RenderPresent(renderer);

		// Add a 16msec delay to make our game run at ~60 fps
		SDL_Delay(16);
	}
}