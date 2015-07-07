#ifndef __MV_EDITOR_CONTROLS_H__
#define __MV_EDITOR_CONTROLS_H__

#include <memory>
#include "editComponents.h"
#include "editorDefines.h"
#include "componentPanels.h"
#include "editorFactories.h"

class EditorControls {
public:
	EditorControls(std::shared_ptr<MV::Scene::Node> a_editor, std::shared_ptr<MV::Scene::Node> a_root, SharedResources a_sharedResources);
	~EditorControls();
	SharedResources resources() const{
		return sharedResources;
	}

	std::shared_ptr<MV::Scene::Node> root(){
		return rootScene;
	}

	EditorControls* root(std::shared_ptr<MV::Scene::Node> a_newRoot){
		rootScene = a_newRoot;
		return this;
	}

	void onSceneDrag(const MV::Point<int> &a_delta){
		if(currentPanel){
			currentPanel->onSceneDrag(a_delta);
		}
	}

	void onSceneZoom(){
		if (currentPanel) {
			currentPanel->onSceneZoom();
		}
	}

	template <typename PanelType, typename ...Arg>
	void loadPanel(Arg... a_parameters){
		if(currentPanel){ currentPanel->activate(nullptr); }
		currentPanel = std::make_unique<PanelType>(*this, std::forward<Arg>(a_parameters)...);
	}

	template <typename PanelType, typename ...Arg>
	void loadPanel(){
		currentPanel = std::make_unique<PanelType>(*this);
	}

	void handleInput(SDL_Event &a_event);

	void deleteFullScene();
	void deletePanelContents();

	void updateBoxHeader(MV::PointPrecision a_width);

	std::shared_ptr<MV::Scene::Node> content();

	std::shared_ptr<MV::Scene::Node> editor(){
		return editorScene;
	}

	Selection& selection(){
		return currentSelection;
	}

	EditorPanel* activePanel() {
		return currentPanel.get();
	}

private:
	SharedResources sharedResources;

	std::shared_ptr<MV::Scene::Node> editorScene;
	std::shared_ptr<MV::Scene::Node> rootScene;
	std::shared_ptr<MV::Scene::Node> draggableBox;
	std::shared_ptr<MV::Scene::Clickable> boxHeader;

	std::unique_ptr<EditorPanel> currentPanel;
	Selection currentSelection;
	int mouseBlocked = 0;
};

#endif
