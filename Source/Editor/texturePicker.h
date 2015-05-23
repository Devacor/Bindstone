#ifndef __MV_EDITOR_TEXTURE_PICKER_H__
#define __MV_EDITOR_TEXTURE_PICKER_H__

#include <memory>
#include "editorDefines.h"
#include "editorFactories.h"

class TexturePicker {
public:
	TexturePicker(std::shared_ptr<MV::Scene::Node> a_root, SharedResources a_sharedResources, std::function<void(std::shared_ptr<MV::TextureHandle>, bool)> a_setter) :
		root(a_root),
		sharedResources(a_sharedResources),
		packs(sharedResources.textures->packIds()),
		files(sharedResources.textures->fileIds()),
		setter(a_setter) {

		initializeRootPicker();
	}

	~TexturePicker() {
		box->parent()->removeFromParent();
	}

private:
	void initializeRootPicker() {
		auto gridNode = MV::Scene::Node::make(root->renderer());
		grid = gridNode->attach<MV::Scene::Grid>()->cellSize(MV::size(100.0f, 27.0f))->padding({ 2.0f, 2.0f })->columns(6)->margin({ 4.0f, 4.0f })->color({ BOX_BACKGROUND });
		makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, "Back", { 100.0f, 27.0f }, UTF_CHAR_STR("Back"))->
			onAccept.connect("Back", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			setter(nullptr, false);
		});
		makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, "Null", { 100.0f, 27.0f }, UTF_CHAR_STR("Clear"))->
			onAccept.connect("Null", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			setter(nullptr, true);
		});

		auto pos = box ? box->parent()->position() : MV::Point<>(200.0f, 0.0f);
		box = makeDraggableBox("TexturePicker", root, grid->bounds().size(), *sharedResources.mouse);
		box->parent()->position(pos);
		box->add(gridNode);

		auto imageButton = makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, "Files", { 100.0f, 27.0f }, MV::toWide("Files"));
		imageButton->onAccept.connect("Accept", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			initializeFilePicker();
		});
		for (auto&& packId : packs) {
			auto button = makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, packId, { 100.0f, 27.0f }, MV::toWide(packId));
			button->onAccept.connect("Accept", [&, packId](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
				initializeImagePicker(packId);
			});
		}
	}

	void initializeFilePicker() {
		auto cellSize = MV::size(64.0f, 64.0f);
		auto gridNode = MV::Scene::Node::make(root->renderer());
		grid = gridNode->attach<MV::Scene::Grid>()->cellSize(cellSize)->padding({ 2.0f, 2.0f })->columns(6)->color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f });
		makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, "Back", cellSize, UTF_CHAR_STR("Back"))->
			onAccept.connect("Back", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			initializeRootPicker();
		});

		auto pos = box ? box->parent()->position() : MV::Point<>(200.0f, 0.0f);
		box = makeDraggableBox("TexturePicker", root, grid->bounds().size(), *sharedResources.mouse);
		box->parent()->position(pos);
		box->add(grid->owner());

		for (auto&& textureId : files) {
			auto handle = sharedResources.textures->file(textureId.first, textureId.second)->makeHandle();

			auto button = makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, textureId.first, cellSize, MV::toWide(""));
			button->onAccept.connect("Accept", [&, handle](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
				setter(handle, false);
			});
			button->owner()->make("icon")->attach<MV::Scene::Sprite>()->bounds({ MV::fitAspect(MV::cast<MV::PointPrecision>(handle->bounds().size()), cellSize) })->texture(handle);
		}
	}

	void initializeImagePicker(const std::string &a_packId) {
		auto cellSize = MV::size(64.0f, 64.0f);
		auto gridNode = MV::Scene::Node::make(root->renderer());
		grid = gridNode->attach<MV::Scene::Grid>()->cellSize(cellSize)->padding({ 2.0f, 2.0f })->columns(6)->color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f });
		makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, "Back", cellSize, UTF_CHAR_STR("Back"))->
			onAccept.connect("Back", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			initializeRootPicker();
		});

		auto pos = box ? box->parent()->position() : MV::Point<>(200.0f, 0.0f);
		box = makeDraggableBox("TexturePicker", root, grid->bounds().size(), *sharedResources.mouse);
		box->parent()->position(pos);
		box->add(grid->owner());
		auto pack = sharedResources.textures->pack(a_packId);
		for (auto&& textureId : pack->handleIds()) {
			auto handle = pack->handle(textureId);

			auto button = makeButton(grid->owner(), *sharedResources.textLibrary, *sharedResources.mouse, textureId, cellSize, MV::toWide(""));
			button->onAccept.connect("Accept", [&, handle](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
				setter(handle, false);
			});
			button->owner()->make("icon")->attach<MV::Scene::Sprite>()->bounds({ MV::fitAspect(MV::cast<MV::PointPrecision>(handle->bounds().size()), cellSize) })->texture(handle);
		}
	}

	SharedResources sharedResources;
	std::shared_ptr<MV::Scene::Node> root;
	std::shared_ptr<MV::Scene::Node> box;
	std::shared_ptr<MV::Scene::Grid> grid;

	std::vector<std::pair<std::string, bool>> files;
	std::vector<std::string> packs;

	std::string selectedPack;
	std::shared_ptr<MV::TexturePack> activePack;

	std::function<void(std::shared_ptr<MV::TextureHandle>, bool)> setter;
};

#endif
