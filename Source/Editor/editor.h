#ifndef __MV_EDITOR_H__
#define __MV_EDITOR_H__

#include <SDL.h>

#include "editorSceneGraphPanel.h"
#include "editorControls.h"

#include "Game/managers.h"

#include <string>
#include <ctime>

class EditorControls;
class Editor {
public:
	Editor(Managers& a_managers);

	//return true if we're still good to go
	bool update(double dt);
	void handleInput();
	void render();
	void sceneUpdated();

	EditorControls& panel(){
		return *controlPanel;
	}

	Managers& getManager() {
		return managers;
	}

	MV::TapDevice& getMouse() {
		return mouse;
	}

	void returnFromBackground() {
		managers.services.connect(&mouse);
		managers.services.connect(&chaiScript);
	}

private:

	void updateFps(double a_dt) {
		lastUpdateDelta = a_dt;
		accumulatedTime += static_cast<float>(a_dt);
		++accumulatedFrames;
		if (accumulatedFrames > 60.0f) {
			accumulatedFrames /= 2.0f;
			accumulatedTime /= 2.0f;
		}
		if (static_cast<int>(accumulatedFrames) % 10 == 0) {
			//fps->text(MV::to_string(accumulatedFrames / accumulatedTime, 3));
			const Uint8* keystate = SDL_GetKeyboardState(NULL);
			if (keystate[SDL_SCANCODE_RSHIFT]) {
				std::cout << accumulatedFrames / accumulatedTime << '\t';
			}
		}
	}

	Editor(const Editor &) = delete;
	Editor& operator=(const Editor &) = delete;

	void initializeWindow();
	void initializeControls();
	void handleScroll(float a_amount);

	Managers& managers;

	MV::TapDevice mouse;
	chaiscript::ChaiScript chaiScript;

	std::shared_ptr<MV::Scene::Text> fps;
	std::shared_ptr<MV::Scene::Node> visor; //scene parent
	std::shared_ptr<MV::Scene::Node> scene;
	std::shared_ptr<MV::Scene::Node> controls;
	std::shared_ptr<MV::Scene::Node> testNode;

	double lastUpdateDelta = 0.0;

	float accumulatedTime = 0.0f;
	float accumulatedFrames = 0.0f;

	bool done = false;

	std::unique_ptr<EditorControls> controlPanel;
	std::unique_ptr<SceneGraphPanel> selectorPanel;
};

#endif