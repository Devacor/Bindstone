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
	std::map<std::string, MV::Scene::Clickable::Click> clickSignals;
	std::shared_ptr<MV::Scene::Text> activeTextbox;
};

class SelectedEditorPanel : public EditorPanel {
public:
	SelectedEditorPanel(EditorControls &a_panel, std::unique_ptr<EditableElement> a_controls);

	virtual void handleInput(SDL_Event &a_event);

private:
	std::unique_ptr<EditableElement> controls;
	std::shared_ptr<MV::Scene::Text> ourBox;
};

class DeselectedEditorPanel : public EditorPanel {
public:
	DeselectedEditorPanel(EditorControls &a_panel);
private:
	void completeSelection(const MV::BoxAABB &a_selected);
};

#endif
