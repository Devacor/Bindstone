#ifndef __MV_EDITOR_TEXTURE_PICKER_H__
#define __MV_EDITOR_TEXTURE_PICKER_H__

#include <memory>
#include "editorDefines.h"
#include "editorFactories.h"

class TexturePicker {
public:
	TexturePicker(std::shared_ptr<MV::Scene::Node> a_root, MV::Services& a_sharedResources, std::function<void(std::shared_ptr<MV::TextureHandle>, bool)> a_setter) :
		root(a_root),
		sharedResources(a_sharedResources),
		packs(sharedResources.get<MV::SharedTextures>()->packIds()),
		files(sharedResources.get<MV::SharedTextures>()->fileIds()),
		setter(a_setter) {

		files.erase(std::remove_if(files.begin(), files.end(), [&](std::pair<std::string, bool> &a_item) {
			if (a_item.first.find("/Map") != std::string::npos) {
				mapFiles.push_back(a_item);
				return true;
			}
			return false;
		}), files.end());

		initializeRootPicker();
	}

	~TexturePicker() {
		box->parent()->removeFromParent();
	}

private:
	void initializeRootPicker() {
		auto gridNode = MV::Scene::Node::make(root->renderer());
		grid = gridNode->attach<MV::Scene::Grid>()->cellSize(MV::size(100.0f, 27.0f))->padding({ 2.0f, 2.0f })->columns(6)->margin({ 4.0f, 4.0f })->color({ BOX_BACKGROUND });
		makeButton(grid->owner(), *sharedResources.get<MV::TextLibrary>(), *sharedResources.get<MV::TapDevice>(), "Close", { 100.0f, 27.0f }, UTF_CHAR_STR("Close"))->
			onAccept.connect("Close", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
				setter(nullptr, false);
			});
		makeButton(grid->owner(), *sharedResources.get<MV::TextLibrary>(), *sharedResources.get<MV::TapDevice>(), "Null", { 100.0f, 27.0f }, UTF_CHAR_STR("Clear"))->
			onAccept.connect("Null", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
				setter(nullptr, true);
			});

		auto imageButton = makeButton(grid->owner(), *sharedResources.get<MV::TextLibrary>(), *sharedResources.get<MV::TapDevice>(), "Images", { 100.0f, 27.0f }, "Images");
		imageButton->onAccept.connect("Accept", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			initializeFilePicker(files);
		});

		auto imageButton2 = makeButton(grid->owner(), *sharedResources.get<MV::TextLibrary>(), *sharedResources.get<MV::TapDevice>(), "Map", { 100.0f, 27.0f }, "Map");
		imageButton2->onAccept.connect("Accept", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			initializeFilePicker(mapFiles);
		});

		for (auto&& packId : packs) {
			auto button = makeButton(grid->owner(), *sharedResources.get<MV::TextLibrary>(), *sharedResources.get<MV::TapDevice>(), packId, { 100.0f, 27.0f }, packId);
			button->onAccept.connect("Accept", [&, packId](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
				initializeImagePicker(packId);
			});
		}

		auto pos = box ? box->parent()->position() : MV::Point<>(200.0f, 0.0f);
		box = makeDraggableBox("TexturePicker", root, grid->bounds().size(), *sharedResources.get<MV::TapDevice>());
		box->parent()->position(pos);
		box->add(gridNode);
	}

	void initializeFilePicker(std::vector<std::pair<std::string, bool>>& a_fileList) {
		auto cellSize = MV::size(64.0f, 64.0f);
		auto gridNode = MV::Scene::Node::make(root->renderer());
		grid = gridNode->attach<MV::Scene::Grid>()->cellSize(cellSize)->padding({ 2.0f, 2.0f })->columns(6)->color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f });
		makeButton(grid->owner(), *sharedResources.get<MV::TextLibrary>(), *sharedResources.get<MV::TapDevice>(), "Back", cellSize, UTF_CHAR_STR("Back"))->
			onAccept.connect("Back", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			initializeRootPicker();
		});

		for (auto&& textureId : a_fileList) {
			auto handle = sharedResources.get<MV::SharedTextures>()->file(textureId.first, textureId.second)->makeHandle();

			auto button = makeButton(grid->owner(), *sharedResources.get<MV::TextLibrary>(), *sharedResources.get<MV::TapDevice>(), textureId.first, cellSize, "");
			button->onAccept.connect("Accept", [&, handle](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
				setter(handle, false);
			});
			button->owner()->make("icon")->attach<MV::Scene::Sprite>()->bounds({ MV::fitAspect(MV::cast<MV::PointPrecision>(handle->bounds().size()), cellSize) })->texture(handle);
		}

		auto pos = box ? box->parent()->position() : MV::Point<>(200.0f, 0.0f);
		box = makeDraggableBox("TexturePicker", root, grid->bounds().size(), *sharedResources.get<MV::TapDevice>());
		box->parent()->position(pos);
		box->add(grid->owner());
	}

	void initializeImagePicker(const std::string &a_packId) {
		auto cellSize = MV::size(64.0f, 64.0f);
		auto gridNode = MV::Scene::Node::make(root->renderer());
		grid = gridNode->attach<MV::Scene::Grid>()->cellSize(cellSize)->padding({ 2.0f, 2.0f })->columns(6)->color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f });
		makeButton(grid->owner(), *sharedResources.get<MV::TextLibrary>(), *sharedResources.get<MV::TapDevice>(), "Back", cellSize, UTF_CHAR_STR("Back"))->
			onAccept.connect("Back", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			initializeRootPicker();
		});

		auto pack = sharedResources.get<MV::SharedTextures>()->pack(a_packId);
		for (auto&& textureId : pack->handleIds()) {
			auto handle = pack->handle(textureId);

			auto button = makeButton(grid->owner(), *sharedResources.get<MV::TextLibrary>(), *sharedResources.get<MV::TapDevice>(), textureId, cellSize, "");
			button->onAccept.connect("Accept", [&, handle](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
				setter(handle, false);
			});
			button->owner()->make("icon")->attach<MV::Scene::Sprite>()->bounds({ MV::fitAspect(MV::round<MV::PointPrecision>(handle->bounds().size()), cellSize) })->texture(handle);
		}

		auto pos = box ? box->parent()->position() : MV::Point<>(200.0f, 0.0f);
		box = makeDraggableBox("TexturePicker", root, grid->bounds().size(), *sharedResources.get<MV::TapDevice>());
		box->parent()->position(pos);
		box->add(grid->owner());
	}

	MV::Services& sharedResources;
	std::shared_ptr<MV::Scene::Node> root;
	std::shared_ptr<MV::Scene::Node> box;
	std::shared_ptr<MV::Scene::Grid> grid;

	std::vector<std::pair<std::string, bool>> files;
	std::vector<std::pair<std::string, bool>> mapFiles;
	std::vector<std::string> packs;

	std::string selectedPack;
	std::shared_ptr<MV::TexturePack> activePack;

	std::function<void(std::shared_ptr<MV::TextureHandle>, bool)> setter;
};

#endif
