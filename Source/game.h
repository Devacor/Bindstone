#include <SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
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
	std::shared_ptr<MV::DrawNode> initializeCatapultScene();
	void loadCatapultTextures();
	void initializeWindow();
	void loadPatternTextures();

	MV::Draw2D renderer;

	MV::TextureManager textures;
	MV::FrameSwapperRegister animationLibrary;

	MV::TextLibrary textLibrary;
	MV::DrawNode mainScene;
	MV::DrawRectangle testShape;

	MV::AxisAngles angleIncrement;

	bool done;
	struct MouseState {
		void update(){
			int x, y;
			uint32_t state = SDL_GetMouseState(&x, &y);
			position.x = x;
			position.y = y;
			left = (state & SDL_BUTTON(1)) != false;
			right = (state & SDL_BUTTON(2)) != false;
		}
		MV::Point position;
		bool left;
		bool right;
	};
	MouseState mouse;
};

void quit(void);