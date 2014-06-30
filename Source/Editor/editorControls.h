#ifndef __MV_EDITOR_CONTROLS_H__
#define __MV_EDITOR_CONTROLS_H__

#include <memory>
#include "editorSelection.h"
#include "editorDefines.h"
#include "editorPanels.h"

namespace MV{
	namespace Scene {
		class Node;
	}
}

class EditorControls {
public:
	EditorControls(std::shared_ptr<MV::Scene::Node> a_editor, std::shared_ptr<MV::Scene::Node> a_root, MV::TextLibrary *a_textLibrary, MV::MouseState *a_mouse);

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

	void deleteScene();

	void updateBoxHeader(MV::PointPrecision a_width);

	std::shared_ptr<MV::Scene::Node> content();

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
	MV::Scene::Clickable::Drag boxHeaderDrag;

	MV::TextLibrary *textLibraryHandle;
	MV::MouseState *mouseHandle;

	std::unique_ptr<EditorPanel> currentPanel;
	Selection currentSelection;
};

#endif
