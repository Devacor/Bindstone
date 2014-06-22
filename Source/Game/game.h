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
	bool update(double dt);
	void handleInput();
	void render();
private:
	Game(const Game &) = delete;
	Game& operator=(const Game &) = delete;

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

	MV::Stopwatch watch;
	double lastSecond = 0;

	void hookUpInput();

	void saveTest(){
		std::cout << "Save Begin" << std::endl;
		std::stringstream stream;
		{
			std::shared_ptr<MV::Scene::Node> saveScene;
			cereal::JSONOutputArchive archive(stream);
			saveScene = mainScene->get("clipped");
			archive(cereal::make_nvp("test", saveScene));
			saveScene->parent()->remove(saveScene);
			saveScene.reset();
			if(lastSecond == 0){
				lastSecond = 1;
				std::ofstream toFile("sceneSave.txt");
				toFile << stream.str();
			}
		}
		std::cout << "YAYA" << std::endl;

		{
			cereal::JSONInputArchive archive(stream);
			archive.add(cereal::make_nvp("mouse", &mouse), cereal::make_nvp("renderer", &renderer));
			std::shared_ptr<MV::Scene::Node> loadScene;
			auto nvp = cereal::make_nvp("test", loadScene);
			archive(nvp);
			loadScene->shared_from_this();
			if(!loadScene){
				std::cout << "What the fuck" << std::endl;
			}
			mainScene->add("clipped", loadScene);
			loadScene->shared_from_this();
			armScene = loadScene->get<MV::Scene::Clickable>("clickArm");
		}

		auto test = mainScene->get("clipped");

		if(std::floor(watch.check()) > lastSecond){
			lastSecond = std::floor(watch.check());
			std::cout << lastSecond << std::endl;
		}
		std::cout << "Save End" << std::endl;
		hookUpInput();
	}
};

void quit(void);
