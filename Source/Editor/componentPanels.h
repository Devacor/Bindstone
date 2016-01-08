#ifndef __MV_EDITOR_PANEL_H__
#define __MV_EDITOR_PANEL_H__

#include <memory>
#include <map>
#include "editorDefines.h"
#include "Render/package.h"

class EditorControls;
class EditableNode;
class EditableRectangle;
class EditableEmitter;
class EditableGrid;
class EditablePathMap;
class EditableSpine;
class TexturePicker;

class EditorPanel {
protected:
	MV::Signal<void(const std::string &)> onNameChangeSignal;

public:
	MV::SignalRegister<void(const std::string &)> onNameChange;
	EditorPanel(EditorControls &a_panel);
	virtual ~EditorPanel();
	virtual void cancelInput();

	virtual void handleInput(SDL_Event &a_event);

	virtual void onSceneDrag(const MV::Point<int> &a_delta){
	}
	virtual void onSceneZoom() {
	}
	void activate(std::shared_ptr<MV::Scene::Text> a_textbox){
		if(activeTextbox){
			activeTextbox->disableCursor();
		}
		if(!a_textbox || a_textbox == activeTextbox){
			activeTextbox = nullptr;
		} else if(a_textbox){
			activeTextbox = a_textbox;
			activeTextbox->enableCursor();
		}
	}
	
protected:
	EditorControls &panel;
	std::shared_ptr<TexturePicker> picker;
	std::shared_ptr<MV::Scene::Text> activeTextbox;
};

class SelectedNodeEditorPanel : public EditorPanel {
public:
	SelectedNodeEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableNode> a_controls);
	~SelectedNodeEditorPanel();

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;

	MV::Scene::SafeComponent<MV::Scene::Button> CreateSpriteComponentButton(const MV::Scene::SafeComponent<MV::Scene::Sprite> & a_sprite);
	MV::Scene::SafeComponent<MV::Scene::Button> CreateGridComponentButton(const MV::Scene::SafeComponent<MV::Scene::Grid> & a_grid);
	MV::Scene::SafeComponent<MV::Scene::Button> CreateSpineComponentButton(const MV::Scene::SafeComponent<MV::Scene::Spine> & a_grid);
	MV::Scene::SafeComponent<MV::Scene::Button> CreateEmitterComponentButton(const MV::Scene::SafeComponent<MV::Scene::Emitter> & a_emitter);
	MV::Scene::SafeComponent<MV::Scene::Button> CreatePathMapComponentButton(const MV::Scene::SafeComponent<MV::Scene::PathMap> & a_pathMap);
private:
	void updateComponentEditButtons(bool a_attached);

	MV::Size<> buttonSize;

	std::unique_ptr<EditorControls> componentPanel;

	std::shared_ptr<EditableNode> controls;
	
	std::shared_ptr<MV::Scene::Text> posY;
	std::shared_ptr<MV::Scene::Text> posX;

	std::shared_ptr<MV::Scene::Text> scaleX;
	std::shared_ptr<MV::Scene::Text> scaleY;

	std::shared_ptr<MV::Scene::Text> rotate;
	std::shared_ptr<MV::Scene::Text> rotateX;
	std::shared_ptr<MV::Scene::Text> rotateY;

	std::shared_ptr<MV::Scene::Node> grid;

	MV::Scene::Node::ComponentSharedSignalType attachSignal;
	MV::Scene::Node::ComponentSharedSignalType detachSignal;

	std::vector<std::shared_ptr<MV::Scene::Node>> componentEditButtons;
};

class SelectedGridEditorPanel : public EditorPanel {
public:
	SelectedGridEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableGrid> a_controls, std::shared_ptr<MV::Scene::Button> a_text);

	~SelectedGridEditorPanel() {
	}

	virtual void handleInput(SDL_Event &a_event)  override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta)  override;
	virtual void onSceneZoom() override;
private:
	std::shared_ptr<EditableGrid> controls;
	std::shared_ptr<MV::Scene::Text> posY;
	std::shared_ptr<MV::Scene::Text> posX;
	std::shared_ptr<MV::Scene::Text> columns;
	std::shared_ptr<MV::Scene::Text> width;
	std::shared_ptr<MV::Scene::Text> paddingX;
	std::shared_ptr<MV::Scene::Text> paddingY;
	std::shared_ptr<MV::Scene::Text> marginsX;
	std::shared_ptr<MV::Scene::Text> marginsY;
};

class SelectedRectangleEditorPanel : public EditorPanel {
public:
	SelectedRectangleEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableRectangle> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton);

	~SelectedRectangleEditorPanel();

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;
private:
	void OpenTexturePicker();
	void clearTexturePicker(){
		picker = nullptr;
	}

	MV::Scale aspectRatio;

	std::shared_ptr<EditableRectangle> controls;
	std::shared_ptr<MV::Scene::Text> offsetY;
	std::shared_ptr<MV::Scene::Text> offsetX;
	std::shared_ptr<MV::Scene::Text> width;
	std::shared_ptr<MV::Scene::Text> height;
	std::shared_ptr<MV::Scene::Text> aspectX;
	std::shared_ptr<MV::Scene::Text> aspectY;
};

class SelectedEmitterEditorPanel : public EditorPanel {
public:
	SelectedEmitterEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableEmitter> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton);
	~SelectedEmitterEditorPanel(){
		std::cout << "deadPanel" << std::endl;
	}

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;
private:
	void OpenTexturePicker();
	void clearTexturePicker(){
		picker = nullptr;
	}

	std::shared_ptr<EditableEmitter> controls;
	std::shared_ptr<MV::Scene::Text> offsetX;
	std::shared_ptr<MV::Scene::Text> offsetY;
	std::shared_ptr<MV::Scene::Text> width;
	std::shared_ptr<MV::Scene::Text> height;
};

class SelectedSpineEditorPanel : public EditorPanel {
public:
	SelectedSpineEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableSpine> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton);
	~SelectedSpineEditorPanel() {
	}

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;
private:
	void handleMakeButton(std::shared_ptr<MV::Scene::Node> a_grid, const std::string &a_socket = "", const std::string &a_node = "");
	void clearTexturePicker() {
		picker = nullptr;
	}

	std::shared_ptr<EditableSpine> controls;
	std::shared_ptr<MV::Scene::Text> assetJson;
	std::shared_ptr<MV::Scene::Text> assetAtlas;
	std::shared_ptr<MV::Scene::Text> animationPreview;
	std::shared_ptr<MV::Scene::Text> scale;

	std::vector<std::shared_ptr<MV::Scene::Text>> linkedSockets;
	std::vector<std::shared_ptr<MV::Scene::Text>> linkedNodes;
};

class SelectedPathMapEditorPanel : public EditorPanel {
public:
	SelectedPathMapEditorPanel(EditorControls &a_panel, std::shared_ptr<EditablePathMap> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton);
	~SelectedPathMapEditorPanel() {
	}

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;
private:

	std::shared_ptr<EditablePathMap> controls;
	std::shared_ptr<MV::Scene::Text> posY;
	std::shared_ptr<MV::Scene::Text> posX;
	std::shared_ptr<MV::Scene::Text> cellsX;
	std::shared_ptr<MV::Scene::Text> cellsY;
	std::shared_ptr<MV::Scene::Text> width;
	std::shared_ptr<MV::Scene::Text> height;
};

class DeselectedEditorPanel : public EditorPanel {
public:
	DeselectedEditorPanel(EditorControls &a_panel);

private:
	std::shared_ptr<MV::Scene::Text> fileName;
};

class ChooseElementCreationType : public EditorPanel {
public:
	ChooseElementCreationType(EditorControls &a_panel, const std::shared_ptr<MV::Scene::Node> &a_node, SelectedNodeEditorPanel *a_editorPanel);
private:
	std::shared_ptr<MV::Scene::Node> nodeToAttachTo;
	std::shared_ptr<MV::Scene::Button> associatedButton;
	void createRectangle(const MV::BoxAABB<int> &a_selected);
	void createEmitter(const MV::BoxAABB<int> &a_selected);
	void createSpine();
	void createPathMap(const MV::BoxAABB<int> &a_selected);
	void createGrid();

	SelectedNodeEditorPanel* editorPanel;
};


#endif
