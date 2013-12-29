#include <SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include "Network/package.h"
#include <string>
#include <ctime>

class Game {
public:
	Game();

	//return true if we're still good to go
	bool passTime(double dt);
	void handleInput();
	void render();
private:
	std::shared_ptr<MV::Scene::Node> initializeCatapultScene();
	void initializeWindow();
	std::shared_ptr<MV::Scene::Node> initializeTextScene();

	MV::Draw2D renderer;

	MV::SharedTextures textures;
	MV::FrameSwapperRegister animationLibrary;

	MV::TextLibrary textLibrary;
	MV::TextBox testBox;
	std::shared_ptr<MV::Scene::Node> mainScene;
	std::shared_ptr<MV::Scene::Rectangle> testShape;

	MV::AxisAngles angleIncrement;

	bool done;
	struct MouseState {
		void update(){
			uint32_t state = SDL_GetMouseState(&position.x, &position.y);
			left = (state & SDL_BUTTON(1)) != false;
			right = (state & SDL_BUTTON(2)) != false;
		}
		MV::Point<int> position;
		bool left;
		bool right;
	};
	MouseState mouse;
};

void quit(void);