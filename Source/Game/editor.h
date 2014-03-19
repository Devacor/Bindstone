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
	EditorControlPanel(std::shared_ptr<MV::Scene::Node> a_scene, MV::TextLibrary *a_textLibrary, MV::MouseState *a_mouse):
		scene(a_scene),
		textLibrary(a_textLibrary),
		mouse(a_mouse){

		draggableBox = scene->make<MV::Scene::Node>("ContextMenu");
	}

	void createDeselectedScene(){
		deleteScene();

		auto createButton = makeButton(draggableBox, *textLibrary, *mouse, MV::size(110.0, 27.0), UTF_CHAR_STR("Create"));
		createButton->position({8.0, 28.0});
		auto selectButton = makeButton(draggableBox, *textLibrary, *mouse, MV::size(110.0, 27.0), UTF_CHAR_STR("Select"));
		selectButton->position(createButton->localAABB().bottomLeftPoint() + MV::point(0.0, 5.0));

		auto background = draggableBox->make<MV::Scene::Rectangle>("Background", MV::point(0.0, 20.0), selectButton->localAABB().bottomRightPoint() + MV::point(8.0, 8.0));
		background->color({0x646b7c});
		background->setSortDepth(-1.0);

		makeBoxHeader(background->basicAABB().width());

		clickSignals["create"] = createButton->onAccept.connect([&](std::shared_ptr<MV::Scene::Clickable>){
			deleteScene();
		});
	}
private:
	void deleteScene(){
		clickSignals.clear();
		draggableBox->clear();
		if(boxHeader){
			draggableBox->add("ContextMenuHandle", boxHeader);
		}
	}
	void makeBoxHeader(double a_width){
		if(!boxHeader){
			boxHeader = draggableBox->make<MV::Scene::Clickable>("ContextMenuHandle", mouse, MV::size(a_width, 20.0));
			boxHeader->color({0x1b1f29});

			boxHeaderDrag = boxHeader->onDrag.connect([](std::shared_ptr<MV::Scene::Clickable> boxHeader, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
				boxHeader->parent()->translate(MV::castPoint<double>(deltaPosition));
			});
		}else{
			boxHeader->setSize({a_width, 20.0});
		}
	}

	std::shared_ptr<MV::Scene::Node> scene;
	std::shared_ptr<MV::Scene::Node> draggableBox;
	std::shared_ptr<MV::Scene::Clickable> boxHeader;
	MV::Scene::ClickableSignals::Drag boxHeaderDrag;

	std::map<std::string, MV::Scene::ClickableSignals::Click> clickSignals;

	MV::TextLibrary *textLibrary;
	MV::MouseState *mouse;
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

	std::shared_ptr<MV::Scene::Node> scene;
	std::shared_ptr<MV::Scene::Node> controls;

	MV::MouseState mouse;

	MV::Stopwatch watch;
	double lastSecond = 0;
	bool done = false;

	Selection selection;
	EditorControlPanel controlPanel;

};

void sdl_quit(void);