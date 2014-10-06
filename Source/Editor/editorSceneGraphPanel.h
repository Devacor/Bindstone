#ifndef __MV_EDITOR_SCENE_GRAPH_PANEL_H__
#define __MV_EDITOR_SCENE_GRAPH_PANEL_H__

#include <memory>
#include "editorSelection.h"
#include "editorDefines.h"
#include "editorPanels.h"
#include "editorFactories.h"

class SceneGraphPanel {
public:
	SceneGraphPanel(std::shared_ptr<MV::Scene::Node> a_scene, std::shared_ptr<MV::Scene::Node> a_root, SharedResources a_sharedResources):
		root(a_root),
		scene(a_scene),
		sharedResources(a_sharedResources),
		packs(sharedResources.textures->packIds()){
	}

	~SceneGraphPanel(){
		box->parent()->removeFromParent();
	}

	void refresh(std::shared_ptr<MV::Scene::Node> a_newScene = nullptr);

	void update();

private:
	void loadButtons(std::shared_ptr<MV::Scene::Grid> a_grid, std::shared_ptr<MV::Scene::Node> a_node, size_t a_depth = 0);

	void makeChildButton(std::shared_ptr<MV::Scene::Node> a_node, size_t a_depth, std::shared_ptr<MV::Scene::Grid> a_grid);

	void clickedChild(std::shared_ptr<MV::Scene::Node> a_child);

	SharedResources sharedResources;
	std::shared_ptr<MV::Scene::Node> scene;
	std::shared_ptr<MV::Scene::Node> root;
	std::shared_ptr<MV::Scene::Node> box;
	std::shared_ptr<MV::Scene::Grid> grid;
	std::vector<std::string> packs;

	std::shared_ptr<MV::Scene::Node> activeSelection;

	bool removeSelection = false;

	std::string selectedPack;
	std::shared_ptr<MV::TexturePack> activePack;
};


#endif