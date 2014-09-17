#ifndef __MV_EDITOR_PANEL_H__
#define __MV_EDITOR_PANEL_H__

#include <memory>
#include <map>
#include "editorDefines.h"
#include "Render/package.h"

class EditorControls;
class EditableRectangle;
class EditableEmitter;
class TexturePicker;
class EditorPanel {
public:
	EditorPanel(EditorControls &a_panel);
	virtual ~EditorPanel();
	virtual void cancelInput();

	virtual void handleInput(SDL_Event &a_event);

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
	std::shared_ptr<MV::Scene::Text> activeTextbox;
};

class SelectedRectangleEditorPanel : public EditorPanel {
public:
	SelectedRectangleEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableRectangle> a_controls);
	~SelectedRectangleEditorPanel(){
		clearTexturePicker();
	}

	virtual void handleInput(SDL_Event &a_event);

private:
	void clearTexturePicker(){
		picker = nullptr;
	}
	std::shared_ptr<EditableRectangle> controls;
	std::shared_ptr<MV::Scene::Text> posY;
	std::shared_ptr<MV::Scene::Text> posX;
	std::shared_ptr<MV::Scene::Text> width;
	std::shared_ptr<MV::Scene::Text> height;
	std::shared_ptr<TexturePicker> picker;
};

class SelectedEmitterEditorPanel : public EditorPanel {
public:
	SelectedEmitterEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableEmitter> a_controls);
	~SelectedEmitterEditorPanel(){
	}

	virtual void handleInput(SDL_Event &a_event);

private:
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
	void createRectangle(const MV::BoxAABB<> &a_selected);
	void createEmitter(const MV::BoxAABB<> &a_selected);
	void createSpine(const MV::BoxAABB<> &a_selected);
};


#endif
