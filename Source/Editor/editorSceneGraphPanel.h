#ifndef __MV_EDITOR_SCENE_GRAPH_PANEL_H__
#define __MV_EDITOR_SCENE_GRAPH_PANEL_H__

#include <memory>
#include <map>
#include "editorDefines.h"

class SceneGraphPanel {
public:
	SceneGraphPanel(std::shared_ptr<MV::Scene::Node> a_scene, std::shared_ptr<MV::Scene::Node> a_root, MV::Services& a_sharedResources):
		root(a_root),
		scene(a_scene),
		sharedResources(a_sharedResources){
	}

	~SceneGraphPanel(){
		if (box && box->parent()) {
			box->parent()->removeFromParent();
		}
	}

	void refresh(std::shared_ptr<MV::Scene::Node> a_newScene = nullptr);

	void update();

private:
	void loadButtons(std::shared_ptr<MV::Scene::Node> a_grid, std::shared_ptr<MV::Scene::Node> a_node, size_t a_depth = 0);

	void makeChildButton(std::shared_ptr<MV::Scene::Node> a_node, size_t a_depth, std::shared_ptr<MV::Scene::Node> a_grid);

	void clickedChild(std::shared_ptr<MV::Scene::Node> a_child);

	void layoutParents(std::shared_ptr<MV::Scene::Node> a_parent);

	MV::Services& sharedResources;
	std::shared_ptr<MV::Scene::Node> scene;
	std::shared_ptr<MV::Scene::Node> root;
	std::shared_ptr<MV::Scene::Node> box;
	std::shared_ptr<MV::Scene::Grid> grid;
	
	std::shared_ptr<MV::Scene::Node> activeSelection;

	bool removeSelection = false;
	bool refreshNeeded = true;

	std::string selectedPack;
	std::shared_ptr<MV::TexturePack> activePack;

	std::map<MV::Scene::Node*, bool> expanded;
};


#endif