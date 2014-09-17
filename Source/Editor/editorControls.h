#ifndef __MV_EDITOR_CONTROLS_H__
#define __MV_EDITOR_CONTROLS_H__

#include <memory>
#include "editorSelection.h"
#include "editorDefines.h"
#include "editorPanels.h"
#include "editorFactories.h"

namespace MV{
	namespace Scene {
		class Node;
	}
}

class TexturePicker {
public:
	TexturePicker(std::shared_ptr<MV::Scene::Node> a_root, SharedResources a_sharedResources, std::function<void (std::shared_ptr<MV::TextureHandle>)> a_setter):
		root(a_root),
		sharedResources(a_sharedResources),
		packs(sharedResources.textures->packIds()),
		setter(a_setter){

		initializeRootPicker();
	}

	~TexturePicker(){
		box->parent()->removeFromParent();
	}

private:
	void initializeRootPicker(){
		grid = MV::Scene::Grid::make(root->getRenderer(), MV::size(100.0f, 27.0f))->padding({4.0f, 4.0f})->rows(6)->color({BOX_BACKGROUND});
		auto backButton = makeButton(grid, *sharedResources.textLibrary, *sharedResources.mouse, "Back", {100.0f, 27.0f}, UTF_CHAR_STR("Back"));
		backButton->onAccept.connect("Back", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			setter(nullptr);
		});
		box = makeDraggableBox("TexturePicker", root, grid->basicAABB().size(), *sharedResources.mouse);
		box->add("TexturePickerGrid", grid);
		for(auto&& packId : packs){
			auto button = makeButton(grid, *sharedResources.textLibrary, *sharedResources.mouse, packId, {100.0f, 27.0f}, MV::stringToWide(packId));
			button->onAccept.connect("Accept", [&,packId](std::shared_ptr<MV::Scene::Clickable> a_clickable){
				initializeImagePicker(packId);
			});
		}
	}

	void initializeImagePicker(const std::string &a_packId){
		auto cellSize = MV::size(64.0f, 64.0f);
		grid = MV::Scene::Grid::make(root->getRenderer(), cellSize)->padding({4.0f, 4.0f})->rows(6)->color({BOX_BACKGROUND});
		auto backButton = makeButton(grid, *sharedResources.textLibrary, *sharedResources.mouse, "Back", {100.0f, 27.0f}, UTF_CHAR_STR("Back"));
		backButton->onAccept.connect("Back", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			initializeRootPicker();
		});
		box = makeDraggableBox("TexturePicker", root, grid->basicAABB().size(), *sharedResources.mouse);
		box->add("TexturePickerGrid", grid);
		auto pack = sharedResources.textures->pack(a_packId);
		for(auto&& textureId : pack->handleIds()){
			auto handle = pack->handle(textureId);

			auto button = makeButton(grid, *sharedResources.textLibrary, *sharedResources.mouse, textureId, cellSize, MV::stringToWide(textureId));
			button->onAccept.connect("Accept", [&, handle](std::shared_ptr<MV::Scene::Clickable> a_clickable){
				setter(handle);
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

	std::function<void(std::shared_ptr<MV::TextureHandle>)> setter;
};

class EditorControls {
public:
	EditorControls(std::shared_ptr<MV::Scene::Node> a_editor, std::shared_ptr<MV::Scene::Node> a_root, SharedResources a_sharedResources);

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
	SharedResources sharedResources;

	std::shared_ptr<MV::Scene::Node> editorScene;
	std::shared_ptr<MV::Scene::Node> rootScene;
	std::shared_ptr<MV::Scene::Node> draggableBox;
	std::shared_ptr<MV::Scene::Clickable> boxHeader;
	MV::Scene::Clickable::Drag boxHeaderDrag;

	std::unique_ptr<EditorPanel> currentPanel;
	Selection currentSelection;
};

#endif
