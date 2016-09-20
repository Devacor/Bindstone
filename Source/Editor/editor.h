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
		return controlPanel;
	}

	Managers& getManager() {
		return managers;
	}

	MV::MouseState& getMouse() {
		return mouse;
	}

private:
	Editor(const Editor &) = delete;
	Editor& operator=(const Editor &) = delete;

	void initializeWindow();
	void initializeControls();
	void handleScroll(int a_amount);

	Managers& managers;

	MV::MouseState mouse;
	chaiscript::ChaiScript chaiScript;

	std::shared_ptr<MV::Scene::Text> fps;
	std::shared_ptr<MV::Scene::Node> scene;
	std::shared_ptr<MV::Scene::Node> controls;
	std::shared_ptr<MV::Scene::Node> testNode;

	double lastUpdateDelta = 0.0;

	bool done = false;

	EditorControls controlPanel;
	SceneGraphPanel selectorPanel;
};

#endif