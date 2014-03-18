#include <SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Render/Scene/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include "Network/package.h"
#include "Interface/package.h"
#include <string>
#include <ctime>
#include <boost/lexical_cast.hpp>

class Selection {
public:
	Selection(std::shared_ptr<MV::Scene::Node> a_controls, MV::MouseState &a_mouse):
		mouse(a_mouse),
		controls(a_controls),
		id(gid++){
	}

	void callback(std::function<void(const MV::BoxAABB &)> a_callback);
	void enable(std::function<void (const MV::BoxAABB &)> a_callback);
	void enable();
	void disable();

private:
	std::shared_ptr<MV::Scene::Node> controls;

	std::shared_ptr<MV::Scene::Rectangle> visibleSelection;
	MV::MouseState &mouse;
	MV::BoxAABB selection;
	std::function<void(const MV::BoxAABB &)> selectedCallback;

	MV::MouseState::SignalType onMouseDownHandle;
	MV::MouseState::SignalType onMouseUpHandle;

	MV::MouseState::SignalType onMouseMoveHandle;
	
	static long gid;
	long id;
};

std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier = "default");

class EditorControlPanel {
public:
	EditorControlPanel(std::shared_ptr<MV::Scene::Node> a_scene):
		scene(a_scene){
	}

	void createNoSelectionScene(){
		//scene->make<Rectangle>("Background");
	}
private:
	std::shared_ptr<MV::Scene::Node> scene;
};

class Editor {
public:
	Editor();

	//return true if we're still good to go
	bool update(double dt);
	void handleInput();
	void render();
private:
	Editor(const Editor &) = delete;
	Editor& operator=(const Editor &) = delete;

	void initializeWindow();
	void initializeControls();

	MV::Draw2D renderer;

	MV::SharedTextures textures;
	MV::FrameSwapperRegister animationLibrary;

	MV::TextLibrary textLibrary;
	MV::TextBox textBox;

	std::shared_ptr<MV::Scene::Node> scene;
	std::shared_ptr<MV::Scene::Node> controls;

	MV::MouseState mouse;

	MV::Scene::ClickableSignals inputHandles;

	MV::Stopwatch watch;
	double lastSecond = 0;
	bool done = false;

	Selection selection;
};

void sdl_quit(void);