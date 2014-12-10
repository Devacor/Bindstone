#ifndef __MV_EDITOR_CONTROLS_H__
#define __MV_EDITOR_CONTROLS_H__

#include <memory>
#include "editorSelection.h"
#include "editorDefines.h"
#include "editorPanels.h"
#include "editorFactories.h"

class TexturePicker {
public:
	TexturePicker(std::shared_ptr<MV::Scene::Node> a_root, SharedResources a_sharedResources, std::function<void(std::shared_ptr<MV::TextureHandle>, bool)> a_setter):
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
		auto gridNode = MV::Scene::Node::make(root->renderer());
		grid = gridNode->attach<MV::Scene::Grid>()->cellSize(MV::size(100.0f, 27.0f))->padding({3.0f, 4.0f})->columns(6)->margin({5.0f, 4.0f})->color({ BOX_BACKGROUND });
		makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, "Back", {100.0f, 27.0f}, UTF_CHAR_STR("Back"))->
			onAccept.connect("Back", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
				setter(nullptr, false);
			});
		makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, "Null", {100.0f, 27.0f}, UTF_CHAR_STR("Remove Texture"))->
			onAccept.connect("Null", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
				setter(nullptr, true);
			});
		
		auto pos = box ? box->parent()->position() : MV::Point<>(200.0f, 0.0f);
		box = makeDraggableBox("TexturePicker", root, grid->bounds().size(), *sharedResources.mouse);
		box->parent()->position(pos);
		box->add(gridNode);
		for(auto&& packId : packs){
			auto button = makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, packId, {100.0f, 27.0f}, MV::stringToWide(packId));
			button->onAccept.connect("Accept", [&,packId](std::shared_ptr<MV::Scene::Clickable> a_clickable){
				initializeImagePicker(packId);
			});
		}
	}

	void initializeImagePicker(const std::string &a_packId){
		auto cellSize = MV::size(64.0f, 64.0f);
		auto gridNode = MV::Scene::Node::make(root->renderer());
		grid = gridNode->attach<MV::Scene::Grid>()->cellSize(cellSize)->padding({3.0f, 4.0f})->columns(6)->color({BOX_BACKGROUND})->margin({5.0f, 4.0f});
		makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, "Back", cellSize, UTF_CHAR_STR("Back"))->
			onAccept.connect("Back", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
				initializeRootPicker();
			});
		
		auto pos = box ? box->parent()->position() : MV::Point<>(200.0f, 0.0f);
		box = makeDraggableBox("TexturePicker", root, grid->bounds().size(), *sharedResources.mouse);
		box->parent()->position(pos);
		box->add(grid->owner());
		auto pack = sharedResources.textures->pack(a_packId);
		for(auto&& textureId : pack->handleIds()){
			auto handle = pack->handle(textureId);

			auto button = makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, textureId, cellSize, MV::stringToWide(textureId));
			button->onAccept.connect("Accept", [&, handle](std::shared_ptr<MV::Scene::Clickable> a_clickable){
				setter(handle, false);
			});
			button->owner()->attach<MV::Scene::Sprite>()->bounds({ MV::fitAspect(MV::cast<MV::PointPrecision>(handle->bounds().size()), cellSize) })->texture(handle);
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

	void onSceneDrag(const MV::Point<int> &a_delta){
		if(currentPanel){
			currentPanel->onSceneDrag(a_delta);
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

	std::unique_ptr<EditorPanel> currentPanel;
	Selection currentSelection;
	int mouseBlocked = 0;
};

class SceneNavigator {
public:
	SceneNavigator(std::shared_ptr<MV::Scene::Node> a_root, SharedResources a_sharedResources);
private:
};

#endif
