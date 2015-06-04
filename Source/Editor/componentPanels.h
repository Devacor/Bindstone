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
class TexturePicker;

class EditorPanel {
public:
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

	virtual void handleInput(SDL_Event &a_event) override;

	virtual void onSceneDrag(const MV::Point<int> &a_delta) override;
	virtual void onSceneZoom() override;
private:

	std::shared_ptr<EditableNode> controls;
	std::shared_ptr<MV::Scene::Text> posY;
	std::shared_ptr<MV::Scene::Text> posX;
};

class SelectedGridEditorPanel : public EditorPanel {
public:
	SelectedGridEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableGrid> a_controls);

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
	SelectedRectangleEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableRectangle> a_controls);

	~SelectedRectangleEditorPanel(){
		clearTexturePicker();
	}

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
	SelectedEmitterEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableEmitter> a_controls);
	~SelectedEmitterEditorPanel(){
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
	std::shared_ptr<MV::Scene::Text> posY;
	std::shared_ptr<MV::Scene::Text> posX;
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
	ChooseElementCreationType(EditorControls &a_panel);
private:
	void createRectangle(const MV::BoxAABB<int> &a_selected);
	void createEmitter(const MV::BoxAABB<int> &a_selected);
	void createSpine(const MV::BoxAABB<int> &a_selected);
	void createGrid(const MV::BoxAABB<int> &a_selected);
};


#endif
