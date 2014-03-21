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
	Selection(std::shared_ptr<MV::Scene::Node> a_scene, MV::MouseState &a_mouse):
		mouse(a_mouse),
		scene(a_scene),
		id(gid++){
	}

	void callback(std::function<void(const MV::BoxAABB &)> a_callback);
	void enable(std::function<void (const MV::BoxAABB &)> a_callback);
	void enable();
	void disable();
	void exitSelection();

private:
	std::shared_ptr<MV::Scene::Node> scene;

	std::shared_ptr<MV::Scene::Rectangle> visibleSelection;
	MV::MouseState &mouse;
	MV::BoxAABB selection;
	std::function<void(const MV::BoxAABB &)> selectedCallback;

	MV::MouseState::SignalType onMouseDownHandle;
	MV::MouseState::SignalType onMouseUpHandle;

	MV::MouseState::SignalType onMouseMoveHandle;
	
	bool inSelection = false;

	static long gid;
	long id;
};

std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier = "default");

class EditorPanel;

class EditorControls {
public:
	EditorControls(std::shared_ptr<MV::Scene::Node> a_editor, std::shared_ptr<MV::Scene::Node> a_root, MV::TextLibrary *a_textLibrary, MV::MouseState *a_mouse):
		editorScene(a_editor),
		rootScene(a_root),
		textLibraryHandle(a_textLibrary),
		mouseHandle(a_mouse){

		draggableBox = editorScene->make<MV::Scene::Node>("ContextMenu");
	}

	MV::TextLibrary* textLibrary() const{
		return textLibraryHandle;
	}

	MV::MouseState* mouse() const{
		return mouseHandle;
	}

	std::shared_ptr<MV::Scene::Node> root(){
		return rootScene;
	}

	template <typename PanelType>
	void loadPanel(){
		currentPanel = std::make_unique<PanelType>(*this);
	}

	void deleteScene(){
		currentPanel.reset();
		draggableBox->clear();
		if(boxHeader){
			draggableBox->add("ContextMenuHandle", boxHeader);
		}
	}

	void updateBoxHeader(double a_width);

	std::shared_ptr<MV::Scene::Node> content(){
		auto found = draggableBox->get("content", false);
		if(found){
			return found;
		}else{
			return draggableBox->make<MV::Scene::Node>("content");
		}
	}

private:
	std::shared_ptr<MV::Scene::Node> editorScene;
	std::shared_ptr<MV::Scene::Node> rootScene;
	std::shared_ptr<MV::Scene::Node> draggableBox;
	std::shared_ptr<MV::Scene::Clickable> boxHeader;
	MV::Scene::ClickableSignals::Drag boxHeaderDrag;

	MV::TextLibrary *textLibraryHandle;
	MV::MouseState *mouseHandle;

	std::unique_ptr<EditorPanel> currentPanel;
};

class EditorPanel {
public:
	EditorPanel(EditorControls &a_panel):
		panel(a_panel),
		selection(a_panel.root(), *a_panel.mouse()){
	}

	virtual void cancelInput(){
		selection.exitSelection();
	}

protected:
	EditorControls &panel;
	Selection selection;
	std::map<std::string, MV::Scene::ClickableSignals::Click> clickSignals;
};

class DeselectedEditorPanel : public EditorPanel {
public:
	DeselectedEditorPanel(EditorControls &a_panel):
		EditorPanel(a_panel){

		panel.deleteScene();
		auto node = panel.content();
		auto createButton = makeButton(node, *panel.textLibrary(), *panel.mouse(), MV::size(110.0, 27.0), UTF_CHAR_STR("Create"));
		createButton->position({8.0, 28.0});
		auto selectButton = makeButton(node, *panel.textLibrary(), *panel.mouse(), MV::size(110.0, 27.0), UTF_CHAR_STR("Select"));
		selectButton->position(createButton->localAABB().bottomLeftPoint() + MV::point(0.0, 5.0));

		auto background = node->make<MV::Scene::Rectangle>("Background", MV::point(0.0, 20.0), selectButton->localAABB().bottomRightPoint() + MV::point(8.0, 8.0));
		background->color({0x646b7c});
		background->setSortDepth(-1.0);

		panel.updateBoxHeader(background->basicAABB().width());

		clickSignals["create"] = createButton->onAccept.connect([&](std::shared_ptr<MV::Scene::Clickable>){
			selection.enable([&](const MV::BoxAABB &a_selected){
				completeSelection(a_selected);
			});
		});

		clickSignals["select"] = selectButton->onAccept.connect([&](std::shared_ptr<MV::Scene::Clickable>){
			panel.deleteScene();
		});
	}
private:
	void completeSelection(const MV::BoxAABB &a_selected){
		static long i = 0;
		auto newShape = panel.root()->make<MV::Scene::Rectangle>("Constructed_"+ boost::lexical_cast<std::string>(i++), a_selected);
		newShape->color({0x3355cc});
		selection.disable();
	}
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

	EditorControls controlPanel;

};

void sdl_quit(void);