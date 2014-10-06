#ifndef __MV_EDITOR_H__
#define __MV_EDITOR_H__

#include <SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Render/Scene/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include "Network/package.h"
#include "Interface/package.h"

#include "editorSceneGraphPanel.h"
#include "editorControls.h"

#include <string>
#include <ctime>

class EditorControls;
class Editor {
public:
	Editor();

	//return true if we're still good to go
	bool update(double dt);
	void handleInput();
	void render();
	void sceneUpdated();

	EditorControls& panel(){
		return controlPanel;
	}

private:
	Editor(const Editor &) = delete;
	Editor& operator=(const Editor &) = delete;

	void initializeWindow();
	void initializeControls();

	MV::ThreadPool pool;
	MV::Draw2D renderer;

	MV::SharedTextures textures;
	MV::FrameSwapperRegister animationLibrary;

	MV::TextLibrary textLibrary;

	std::shared_ptr<MV::Scene::Text> fps;
	std::shared_ptr<MV::Scene::Node> scene;
	std::shared_ptr<MV::Scene::Node> controls;

	MV::MouseState mouse;

	MV::Stopwatch watch;
	double lastSecond = 0;
	bool done = false;

	float accumulatedTime = 0.0f;
	float accumulatedFrames = 0.0f;

	EditorControls controlPanel;
	SceneGraphPanel selectorPanel;
};

void sdl_quit(void);

#endif