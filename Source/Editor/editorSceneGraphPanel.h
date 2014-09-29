#ifndef __MV_EDITOR_SCENE_GRAPH_PANEL_H__
#define __MV_EDITOR_SCENE_GRAPH_PANEL_H__

#include <memory>
#include "editorSelection.h"
#include "editorDefines.h"
#include "editorPanels.h"
#include "editorFactories.h"

class SceneGraphPanel {
public:
	SceneGraphPanel(std::shared_ptr<MV::Scene::Node> a_root, SharedResources a_sharedResources, std::function<void(std::shared_ptr<MV::TextureHandle>, bool)> a_setter):
		root(a_root),
		sharedResources(a_sharedResources),
		packs(sharedResources.textures->packIds()),
		setter(a_setter){

		initializeRootPicker();
	}

	~SceneGraphPanel(){
		box->parent()->removeFromParent();
	}

private:
	void initializeRootPicker(){
		grid = MV::Scene::Grid::make(root->getRenderer(), MV::size(100.0f, 27.0f))->padding({4.0f, 4.0f})->rows(6)->color({BOX_BACKGROUND});
		makeButton(grid, *sharedResources.textLibrary, *sharedResources.mouse, "Back", {100.0f, 27.0f}, UTF_CHAR_STR("Back"))->
			onAccept.connect("Back", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			setter(nullptr, false);
		});
		makeButton(grid, *sharedResources.textLibrary, *sharedResources.mouse, "Null", {100.0f, 27.0f}, UTF_CHAR_STR("Remove Texture"))->
			onAccept.connect("Null", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			setter(nullptr, true);
		});
		box = makeDraggableBox("TexturePicker", root, grid->basicAABB().size(), *sharedResources.mouse);
		box->add("TexturePickerGrid", grid);
		for(auto&& packId : packs){
			auto button = makeButton(grid, *sharedResources.textLibrary, *sharedResources.mouse, packId, {100.0f, 27.0f}, MV::stringToWide(packId));
			button->onAccept.connect("Accept", [&, packId](std::shared_ptr<MV::Scene::Clickable> a_clickable){
				initializeImagePicker(packId);
			});
		}
	}

	void initializeImagePicker(const std::string &a_packId){
		auto cellSize = MV::size(64.0f, 64.0f);
		grid = MV::Scene::Grid::make(root->getRenderer(), cellSize)->padding({4.0f, 4.0f})->rows(6)->color({BOX_BACKGROUND});
		makeButton(grid, *sharedResources.textLibrary, *sharedResources.mouse, "Back", cellSize, UTF_CHAR_STR("Back"))->
			onAccept.connect("Back", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			initializeRootPicker();
		});
		box = makeDraggableBox("TexturePicker", root, grid->basicAABB().size(), *sharedResources.mouse);
		box->add("TexturePickerGrid", grid);
		auto pack = sharedResources.textures->pack(a_packId);
		for(auto&& textureId : pack->handleIds()){
			auto handle = pack->handle(textureId);

			auto button = makeButton(grid, *sharedResources.textLibrary, *sharedResources.mouse, textureId, cellSize, MV::stringToWide(textureId));
			button->onAccept.connect("Accept", [&, handle](std::shared_ptr<MV::Scene::Clickable> a_clickable){
				setter(handle, false);
			});
			button->make<MV::Scene::Rectangle>(MV::fitAspect(MV::cast<MV::PointPrecision>(handle->bounds().size()), cellSize))->texture(handle);
		}
	}

	SharedResources sharedResources;
	std::shared_ptr<MV::Scene::Node> root;
	std::shared_ptr<MV::Scene::Node> box;
	std::shared_ptr<MV::Scene::Grid> grid;
	std::vector<std::string> packs;

	std::string selectedPack;
	std::shared_ptr<MV::TexturePack> activePack;

	std::function<void(std::shared_ptr<MV::TextureHandle>, bool)> setter;
};


#endif