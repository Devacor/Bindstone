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

enum InterfaceColors {
	BUTTON_TOP_IDLE = 0x707070,
	BUTTON_BOTTOM_IDLE = 0x636363,
	BUTTON_TOP_ACTIVE = 0x3d3d3d,
	BUTTON_BOTTOM_ACTIVE = 0x323232,
	BOX_BACKGROUND = 0x4f4f4f,
	BOX_HEADER = 0x2d2d2d,
	CREATED_DEFAULT = 0xdfdfdf,
	SIZE_HANDLES = 0xffb400
};

class EditableElement {
public:
	EditableElement(std::shared_ptr<MV::Scene::Rectangle> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_controlContainer, MV::MouseState *a_mouse):
		elementToEdit(a_elementToEdit),
		controlContainer(a_controlContainer),
		mouse(a_mouse){

		resetHandles();
	}

	~EditableElement(){
		removeHandles();
	}

	void removeHandles(){
		controlContainer->remove(positionHandle);
		controlContainer->remove(topLeftSizeHandle);
		controlContainer->remove(topRightSizeHandle);
		controlContainer->remove(bottomLeftSizeHandle);
		controlContainer->remove(bottomRightSizeHandle);
	}

	void resetHandles(){
		removeHandles();
		auto rectBox = elementToEdit->screenAABB();

		auto handleSize = MV::point(8.0, 8.0);

		positionHandle = controlContainer->make<MV::Scene::Clickable>(MV::stringGUID("position"), mouse, rectBox);
		dragSignals["position"] = positionHandle->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
			auto castPosition = MV::castPoint<double>(deltaPosition);
			handle->translate(castPosition);
			elementToEdit->translate(castPosition);
			topLeftSizeHandle->translate(castPosition);
			topRightSizeHandle->translate(castPosition);
			bottomLeftSizeHandle->translate(castPosition);
			bottomRightSizeHandle->translate(castPosition);
		});

		topLeftSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::stringGUID("topLeft"), mouse, MV::BoxAABB(rectBox.topLeftPoint(), rectBox.topLeftPoint() - (handleSize * MV::point(1.0, 1.0))));
		topLeftSizeHandle->color({SIZE_HANDLES});
		dragSignals["topLeft"] = topLeftSizeHandle->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
			handle->translate(MV::castPoint<double>(deltaPosition));
			topRightSizeHandle->position(MV::point(topRightSizeHandle->position().x, topLeftSizeHandle->position().y));
			bottomLeftSizeHandle->position(MV::point(topLeftSizeHandle->position().x, bottomLeftSizeHandle->position().y));

			dragUpdateFromHandles();
		});

		topRightSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::stringGUID("topRight"), mouse, MV::BoxAABB(rectBox.topRightPoint(), rectBox.topRightPoint() + (handleSize * MV::point(1.0, -1.0))));
		topRightSizeHandle->color({SIZE_HANDLES});
		dragSignals["topRight"] = topRightSizeHandle->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
			handle->translate(MV::castPoint<double>(deltaPosition));
			topLeftSizeHandle->position(MV::point(topLeftSizeHandle->position().x, topRightSizeHandle->position().y));
			bottomRightSizeHandle->position(MV::point(topRightSizeHandle->position().x, bottomRightSizeHandle->position().y));

			dragUpdateFromHandles();
		});

		bottomLeftSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::stringGUID("bottomLeft"), mouse, MV::BoxAABB(rectBox.bottomLeftPoint(), rectBox.bottomLeftPoint() - (handleSize * MV::point(1.0, -1.0))));
		bottomLeftSizeHandle->color({SIZE_HANDLES});
		dragSignals["bottomLeft"] = bottomLeftSizeHandle->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
			handle->translate(MV::castPoint<double>(deltaPosition));
			topLeftSizeHandle->position(MV::point(bottomLeftSizeHandle->position().x, topLeftSizeHandle->position().y));
			bottomRightSizeHandle->position(MV::point(bottomRightSizeHandle->position().x, bottomLeftSizeHandle->position().y));

			dragUpdateFromHandles();
		});

		bottomRightSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::stringGUID("bottomRight"), mouse, MV::BoxAABB(rectBox.bottomRightPoint(), rectBox.bottomRightPoint() + (handleSize * MV::point(1.0, 1.0))));
		bottomRightSizeHandle->color({SIZE_HANDLES});
		dragSignals["bottomRight"] = bottomRightSizeHandle->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
			handle->translate(MV::castPoint<double>(deltaPosition));
			topRightSizeHandle->position(MV::point(bottomRightSizeHandle->position().x, topRightSizeHandle->position().y));
			bottomLeftSizeHandle->position(MV::point(bottomLeftSizeHandle->position().x, bottomRightSizeHandle->position().y));

			dragUpdateFromHandles();
		});
	}

private:
	void dragUpdateFromHandles(){
		if((topLeftSizeHandle->position().x - .5) > bottomRightSizeHandle->position().x || (topLeftSizeHandle->position().y - .5) > bottomRightSizeHandle->position().y){
			resetHandles();
		}

		auto box = MV::BoxAABB(topLeftSizeHandle->screenAABB().bottomRightPoint(), bottomRightSizeHandle->screenAABB().topLeftPoint());

		elementToEdit->position({});
		positionHandle->position({});

		auto corners = elementToEdit->localFromScreen(box);

		elementToEdit->setSizeAndCornerPoint(corners.minPoint, corners.size());
		positionHandle->setSizeAndCornerPoint(corners.minPoint, corners.size());
	}

	MV::MouseState *mouse;

	std::shared_ptr<MV::Scene::Rectangle> elementToEdit;

	std::shared_ptr<MV::Scene::Clickable> topLeftSizeHandle;
	std::shared_ptr<MV::Scene::Clickable> topRightSizeHandle;
	std::shared_ptr<MV::Scene::Clickable> bottomLeftSizeHandle;
	std::shared_ptr<MV::Scene::Clickable> bottomRightSizeHandle;

	std::shared_ptr<MV::Scene::Clickable> positionHandle;

	std::map<std::string, MV::Scene::ClickableSignals::Drag> dragSignals;

	std::shared_ptr<MV::Scene::Node> controlContainer;
};

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
		mouseHandle(a_mouse),
		currentSelection(a_root, *a_mouse){

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

	template <typename PanelType, typename ...Arg>
	void loadPanel(Arg... a_parameters){
		currentPanel = std::make_unique<PanelType>(*this, std::forward<Arg>(a_parameters)...);
	}

	template <typename PanelType, typename ...Arg>
	void loadPanel(){
		currentPanel = std::make_unique<PanelType>(*this);
	}

	void handleInput(SDL_Event &a_event);

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

	std::shared_ptr<MV::Scene::Node> editor(){
		return editorScene;
	}

	Selection& selection(){
		return currentSelection;
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
	Selection currentSelection;
};

class EditorPanel {
public:
	EditorPanel(EditorControls &a_panel):
		panel(a_panel){

		panel.deleteScene();
	}

	virtual void cancelInput(){
		panel.selection().exitSelection();
	}

	virtual void handleInput(SDL_Event &a_event){
		if(activeTextbox){
			activeTextbox->setText(a_event);
		}
	}

protected:
	EditorControls &panel;
	std::map<std::string, MV::Scene::ClickableSignals::Click> clickSignals;
	std::shared_ptr<MV::TextBox> activeTextbox;
};

class SelectedEditorPanel : public EditorPanel {
public:
	SelectedEditorPanel(EditorControls &a_panel, std::unique_ptr<EditableElement> a_controls):
		EditorPanel(a_panel),
		controls(std::move(a_controls)){

		auto node = panel.content();
		auto createButton = makeButton(node, *panel.textLibrary(), *panel.mouse(), MV::size(110.0, 27.0), UTF_CHAR_STR("Create"));
		createButton->position({8.0, 28.0});

		auto background = node->make<MV::Scene::Rectangle>("Background", MV::point(0.0, 20.0), createButton->localAABB().bottomRightPoint() + MV::point(8.0, 8.0));
		background->color({BOX_BACKGROUND});
		background->setSortDepth(-1.0);

		panel.updateBoxHeader(background->basicAABB().width());
		
		SDL_StartTextInput();
		ourBox = std::shared_ptr<MV::TextBox>(new MV::TextBox(a_panel.textLibrary(), "default", UTF_CHAR_STR("Create"), MV::size(250.0, 150.0)));
		ourBox->scene()->translate({100.0, 100.0});
		node->parent()->add("texttest", ourBox->scene());
		
		activeTextbox = ourBox;
		//MV::TextBox twat(a_panel->textLibrary, "default", MV::size(200.0, 50.0));
	}

	virtual void handleInput(SDL_Event &a_event){
		if(activeTextbox){
			activeTextbox->setText(a_event);
		}
	}

private:
	std::unique_ptr<EditableElement> controls;
	std::shared_ptr<MV::TextBox> ourBox;
};

class DeselectedEditorPanel : public EditorPanel {
public:
	DeselectedEditorPanel(EditorControls &a_panel):
		EditorPanel(a_panel){

		auto node = panel.content();
		auto createButton = makeButton(node, *panel.textLibrary(), *panel.mouse(), MV::size(110.0, 27.0), UTF_CHAR_STR("Create"));
		createButton->position({8.0, 28.0});
		auto selectButton = makeButton(node, *panel.textLibrary(), *panel.mouse(), MV::size(110.0, 27.0), UTF_CHAR_STR("Select"));
		selectButton->position(createButton->localAABB().bottomLeftPoint() + MV::point(0.0, 5.0));

		auto background = node->make<MV::Scene::Rectangle>("Background", MV::point(0.0, 20.0), selectButton->localAABB().bottomRightPoint() + MV::point(8.0, 8.0));
		background->color({BOX_BACKGROUND});
		background->setSortDepth(-1.0);

		panel.updateBoxHeader(background->basicAABB().width());

		clickSignals["create"] = createButton->onAccept.connect([&](std::shared_ptr<MV::Scene::Clickable>){
			panel.selection().enable([&](const MV::BoxAABB &a_selected){
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
		panel.selection().disable();
		auto newShape = panel.root()->make<MV::Scene::Rectangle>("Constructed_"+ boost::lexical_cast<std::string>(i++), a_selected);
		newShape->color({CREATED_DEFAULT});
		
		panel.loadPanel<SelectedEditorPanel>(std::make_unique<EditableElement>(newShape, panel.editor(), panel.mouse()));
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