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
class EditableText;
class EditablePathMap;
class EditableSpine;
class TexturePicker;
class AnchorEditor;
class EditableButton;
class EditableClickable;
class EditablePoints;

class EditorPanel {
protected:
	MV::Signal<void(const std::string &)> onNameChangeSignal;

public:
	MV::SignalRegister<void(const std::string &)> onNameChange;
	EditorPanel(EditorControls &a_panel);
	virtual ~EditorPanel();
	virtual void cancelInput();

	virtual void handleInput(SDL_Event &a_event);

	virtual void onSceneDrag(const MV::Point<int> &/*a_delta*/){
	}
	virtual void onSceneZoom() {
	}
	bool toggleText(std::weak_ptr<MV::Scene::Text> a_textbox) {
		if(!activeTextbox.expired() && activeTextbox.lock() == a_textbox.lock()){
			deactivateText();
			return false;
		}else{
			activateText(a_textbox);
			return true;
		}
	}
	void activateText(std::weak_ptr<MV::Scene::Text> a_textbox){
		if (auto lockedAT = activeTextbox.lock()) {
			lockedAT->disableCursor();
		}
		activeTextbox.reset();
		if(auto lockedNewAT = a_textbox.lock()){
			activeTextbox = lockedNewAT;
			lockedNewAT->enableCursor();
		}
	}
	void deactivateText() {
		if (auto lockedAT = activeTextbox.lock()) {
			lockedAT->disableCursor();
		}
		activeTextbox.reset();
	}
	SharedResources resources();

	virtual void clearTexturePicker() {
		picker = nullptr;
	}
	virtual void clearAnchorEditor() {
		anchorEditor = nullptr;
	}
protected:
	virtual void openTexturePicker(size_t /*a_textureId*/ = 0) {}
	virtual void openAnchorEditor(std::shared_ptr<MV::Scene::Component> a_componentToAnchor);
	virtual std::shared_ptr<MV::Scene::Component> getEditingComponent() { return nullptr; }

	EditorControls &panel;
	std::shared_ptr<TexturePicker> picker;
	std::shared_ptr<AnchorEditor> anchorEditor;
	std::weak_ptr<MV::Scene::Text> activeTextbox;
};

class SelectedNodeEditorPanel : public EditorPanel {
public:
	SelectedNodeEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableNode> a_controls);

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;

	MV::Scene::SafeComponent<MV::Scene::Button> CreateSpriteComponentButton(const MV::Scene::SafeComponent<MV::Scene::Sprite> & a_sprite);
	MV::Scene::SafeComponent<MV::Scene::Button> CreateGridComponentButton(const MV::Scene::SafeComponent<MV::Scene::Grid> & a_grid);
	MV::Scene::SafeComponent<MV::Scene::Button> CreateSpineComponentButton(const MV::Scene::SafeComponent<MV::Scene::Spine> & a_grid);
	MV::Scene::SafeComponent<MV::Scene::Button> CreateEmitterComponentButton(const MV::Scene::SafeComponent<MV::Scene::Emitter> & a_emitter);
	MV::Scene::SafeComponent<MV::Scene::Button> CreatePathMapComponentButton(const MV::Scene::SafeComponent<MV::Scene::PathMap> & a_pathMap);
	MV::Scene::SafeComponent<MV::Scene::Button> CreateTextComponentButton(const MV::Scene::SafeComponent<MV::Scene::Text> & a_text);
	MV::Scene::SafeComponent<MV::Scene::Button> CreateButtonComponentButton(const MV::Scene::SafeComponent<MV::Scene::Button> & a_button);
	MV::Scene::SafeComponent<MV::Scene::Button> CreateClickableComponentButton(const MV::Scene::SafeComponent<MV::Scene::Clickable> & a_clickable);
	MV::Scene::SafeComponent<MV::Scene::Button> CreateDrawableComponentButton(const MV::Scene::SafeComponent<MV::Scene::Drawable> & a_drawable);
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

	virtual void handleInput(SDL_Event &a_event)  override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta)  override;
	virtual void onSceneZoom() override;
protected:
	std::shared_ptr<MV::Scene::Component> getEditingComponent() override;

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

class SelectedDrawableEditorPanel : public EditorPanel {
public:
	SelectedDrawableEditorPanel(EditorControls &a_panel, std::shared_ptr<EditablePoints> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton);

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;
protected:
	std::shared_ptr<MV::Scene::Component> getEditingComponent() override;

private:
	virtual void openTexturePicker(size_t a_textureId = 0) override;
	MV::Scale aspectRatio;

	std::shared_ptr<EditablePoints> controls;

	std::shared_ptr<MV::Scene::Button> colorButton;

	size_t selectedPointIndex = 0;

	std::shared_ptr<MV::Scene::Text> shaderId;

	bool createMode = false;
};

class SelectedRectangleEditorPanel : public EditorPanel {
public:
	SelectedRectangleEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableRectangle> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton);

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;
protected:
	std::shared_ptr<MV::Scene::Component> getEditingComponent() override;
private:
	virtual void openTexturePicker(size_t a_textureId = 0) override;
	MV::Scale aspectRatio;

	std::shared_ptr<EditableRectangle> controls;
	std::shared_ptr<MV::Scene::Text> subdivided;
	std::shared_ptr<MV::Scene::Text> offsetY;
	std::shared_ptr<MV::Scene::Text> offsetX;
	std::shared_ptr<MV::Scene::Text> width;
	std::shared_ptr<MV::Scene::Text> height;
	std::shared_ptr<MV::Scene::Text> aspectX;
	std::shared_ptr<MV::Scene::Text> aspectY;
};

class SelectedTextEditorPanel : public EditorPanel {
public:
	SelectedTextEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableText> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton);

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;
protected:
	std::shared_ptr<MV::Scene::Component> getEditingComponent() override;
private:
	MV::Scale aspectRatio;

	std::shared_ptr<EditableText> controls;
	std::shared_ptr<MV::Scene::Text> offsetY;
	std::shared_ptr<MV::Scene::Text> offsetX;
	std::shared_ptr<MV::Scene::Text> width;
	std::shared_ptr<MV::Scene::Text> height;
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
protected:
	std::shared_ptr<MV::Scene::Component> getEditingComponent() override;
private:
	virtual void openTexturePicker(size_t a_textureId = 0) override;

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

protected:
	std::shared_ptr<MV::Scene::Component> getEditingComponent() override;

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

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;
protected:
	std::shared_ptr<MV::Scene::Component> getEditingComponent() override;

private:
	std::shared_ptr<EditablePathMap> controls;
	std::shared_ptr<MV::Scene::Text> posY;
	std::shared_ptr<MV::Scene::Text> posX;
	std::shared_ptr<MV::Scene::Text> cellsX;
	std::shared_ptr<MV::Scene::Text> cellsY;
	std::shared_ptr<MV::Scene::Text> width;
	std::shared_ptr<MV::Scene::Text> height;
};

class SelectedButtonEditorPanel : public EditorPanel {
public:
	SelectedButtonEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableButton> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton);

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;

protected:
	std::shared_ptr<MV::Scene::Component> getEditingComponent() override;

private:
	std::shared_ptr<EditableButton> controls;
	std::shared_ptr<MV::Scene::Text> detectType;
	std::shared_ptr<MV::Scene::Text> active;
	std::shared_ptr<MV::Scene::Text> idle;
	std::shared_ptr<MV::Scene::Text> disabled;

	std::shared_ptr<MV::Scene::Text> offsetY;
	std::shared_ptr<MV::Scene::Text> offsetX;
	std::shared_ptr<MV::Scene::Text> width;
	std::shared_ptr<MV::Scene::Text> height;
};

class SelectedClickableEditorPanel : public EditorPanel {
public:
	SelectedClickableEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableClickable> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton);

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;

protected:
	std::shared_ptr<MV::Scene::Component> getEditingComponent() override;

private:
	std::shared_ptr<EditableClickable> controls;
	std::shared_ptr<MV::Scene::Text> detectType;
	std::shared_ptr<MV::Scene::Text> active;
	std::shared_ptr<MV::Scene::Text> idle;
	std::shared_ptr<MV::Scene::Text> disabled;

	std::shared_ptr<MV::Scene::Text> offsetY;
	std::shared_ptr<MV::Scene::Text> offsetX;
	std::shared_ptr<MV::Scene::Text> width;
	std::shared_ptr<MV::Scene::Text> height;
};

class DeselectedEditorPanel : public EditorPanel {
public:
	DeselectedEditorPanel(EditorControls &a_panel);

	virtual void handleInput(SDL_Event &a_event) override;
private:
	static std::string previousFileName;
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
	void createText(const MV::BoxAABB<int> &a_selected);
	void createButton(const MV::BoxAABB<int> &a_selected);
	void createClickable(const MV::BoxAABB<int> &a_selected);
	void createDrawable();

	SelectedNodeEditorPanel* editorPanel;
};

#endif
