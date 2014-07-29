#ifndef __MV_EDITOR_PANEL_H__
#define __MV_EDITOR_PANEL_H__

#include <memory>
#include <map>
#include "editorDefines.h"
#include "Render/package.h"

class EditorControls;
class EditableElement;

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
		if(a_textbox == activeTextbox){
			activeTextbox = nullptr;
		} else{
			activeTextbox = a_textbox;
			activeTextbox->enableCursor();
		}
	}
protected:
	EditorControls &panel;
	std::shared_ptr<MV::Scene::Text> activeTextbox;
};

class SelectedEditorPanel : public EditorPanel {
public:
	SelectedEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableElement> a_controls);
	~SelectedEditorPanel(){
		int i = 0;
	}

	virtual void handleInput(SDL_Event &a_event);

private:
	std::shared_ptr<EditableElement> controls;
	std::shared_ptr<MV::Scene::Text> posY;
	std::shared_ptr<MV::Scene::Text> posX;
	std::shared_ptr<MV::Scene::Text> width;
	std::shared_ptr<MV::Scene::Text> height;
};

class DeselectedEditorPanel : public EditorPanel {
public:
	DeselectedEditorPanel(EditorControls &a_panel);
private:
	void completeSelection(const MV::BoxAABB &a_selected);
};

#endif
