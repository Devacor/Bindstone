#include <SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include "Network/package.h"
#include "Interface/package.h"
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

	std::shared_ptr<MV::Scene::Clickable> armScene;

	bool done;
	MV::MouseState mouse;

	MV::Scene::ClickableSignals armInputHandles;
};

void quit(void);
