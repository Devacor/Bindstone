#include "componentPanels.h"
#include "editorControls.h"
#include "editorFactories.h"
#include "texturePicker.h"
#include "editor.h"

EditorPanel::EditorPanel(EditorControls &a_panel):
	panel(a_panel),
	onNameChange(onNameChangeSignal){
	panel.deletePanelContents();
}

void EditorPanel::cancelInput() {
	panel.selection().exitSelection();
}

void EditorPanel::handleInput(SDL_Event &a_event) {
	if(activeTextbox){
		activeTextbox->text(a_event);
	}
}

EditorPanel::~EditorPanel() {
}

SelectedNodeEditorPanel::SelectedNodeEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableNode> a_controls) :
	EditorPanel(a_panel),
	componentPanel(std::make_unique<EditorControls>(panel.editor(), panel.root(), panel.resources())),
	controls(a_controls){

	auto node = panel.content();
	grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);

	auto nameField = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "Name", buttonSize)->
		text(MV::toWide(controls->elementToEdit->id()));

	nameField->onEnter.connect("rename", [&](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->id(MV::toString(a_text->text()));
		onNameChangeSignal(controls->elementToEdit->id());
		panel.resources().editor->sceneUpdated();
	});
	nameField->owner()->component<MV::Scene::Clickable>()->onAccept.connect("rename", [&](std::shared_ptr<MV::Scene::Clickable> a_textClickable){
		controls->elementToEdit->id(MV::toString(a_textClickable->owner()->component<MV::Scene::Text>()->text()));
		onNameChangeSignal(controls->elementToEdit->id());
		panel.resources().editor->sceneUpdated();
	});

	auto deselectButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Deselect", buttonSize, UTF_CHAR_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.loadPanel<DeselectedEditorPanel>();
	});

	auto saveButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Save", buttonSize, UTF_CHAR_STR("Save"));
	saveButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->save("Assets/Prefabs/" + controls->elementToEdit->id() + ".prefab");
	});

	auto loadButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Load", buttonSize, UTF_CHAR_STR("Load"));
	loadButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		if (MV::fileExists("Assets/Prefabs/" + controls->elementToEdit->id() + ".prefab")) {
			auto newNode = controls->elementToEdit->parent()->make("Assets/Prefabs/" + controls->elementToEdit->id() + ".prefab", [&](cereal::JSONInputArchive& archive) {
				archive.add(
					cereal::make_nvp("mouse", panel.resources().mouse),
					cereal::make_nvp("renderer", &panel.root()->renderer()),
					cereal::make_nvp("textLibrary", panel.resources().textLibrary),
					cereal::make_nvp("pool", panel.resources().pool),
					cereal::make_nvp("texture", panel.resources().textures)
					);
			});

			auto editableNode = std::make_shared<EditableNode>(newNode, panel.editor(), panel.resources().mouse);

			panel.loadPanel<SelectedNodeEditorPanel>(editableNode);
			panel.resources().editor->sceneUpdated();
		}
	});

	auto copyButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Copy", buttonSize, UTF_CHAR_STR("Copy"));
	copyButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->clone();
		panel.resources().editor->sceneUpdated();
		panel.loadPanel<DeselectedEditorPanel>();
	});

	auto childCreateButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Create", buttonSize, UTF_CHAR_STR("Create"));
	childCreateButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		auto newNode = controls->elementToEdit->make();

		auto editableNode = std::make_shared<EditableNode>(newNode, panel.editor(), panel.resources().mouse);

		panel.loadPanel<SelectedNodeEditorPanel>(editableNode);
		panel.resources().editor->sceneUpdated();
	});

	auto deleteButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Delete", buttonSize, UTF_CHAR_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->removeFromParent();
		panel.resources().editor->sceneUpdated();
		panel.loadPanel<DeselectedEditorPanel>();
	});

	const MV::Size<> labelSize{ buttonSize.width, 20.0f };

	makeLabel(grid, *panel.resources().textLibrary, "Position", labelSize, UTF_CHAR_STR("Position"));
	float textboxWidth = 52.0f;
	posX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "posX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->position().x))));
	posY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "posY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->position().y))));

	makeLabel(grid, *panel.resources().textLibrary, "Rotate", MV::size(textboxWidth, 27.0f), UTF_CHAR_STR("Rotate"));
	rotate = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "rotate", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->rotation().z))));

	makeLabel(grid, *panel.resources().textLibrary, "Scale", labelSize, UTF_CHAR_STR("Scale"));
	scaleX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "scaleX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->scale().x))));
	scaleY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "scaleY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->scale().y))));

	if (controls) {
		auto xClick = posX->owner()->component<MV::Scene::Clickable>();
		xClick->onAccept.connect("updateX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->position({ posX->number(), posY->number() });
		});
		posX->onEnter.connect("updateX", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->position({ posX->number(), posY->number() });
		});
		auto yClick = posY->owner()->component<MV::Scene::Clickable>();
		yClick->onAccept.connect("updateY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->position({ posX->number(), posY->number() });
		});
		posY->onEnter.connect("updateY", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->position({ posX->number(), posY->number() });
		});
		auto rotateClick = posY->owner()->component<MV::Scene::Clickable>();
		rotateClick->onAccept.connect("updateRotate", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->rotation({ 0.0f, 0.0f, MV::wrap(0.0f, 360.0f, rotate->number())});
		});
		rotate->onEnter.connect("updateRotate", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->rotation({ 0.0f, 0.0f, rotate->number() });
		});
		auto xScaleClick = posX->owner()->component<MV::Scene::Clickable>();
		xScaleClick->onAccept.connect("updateScaleX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->elementToEdit->scale({ scaleX->number(), scaleY->number() });
		});
		scaleX->onEnter.connect("updateScaleX", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->scale({ scaleX->number(), scaleY->number() });
		});
		auto yScaleClick = posY->owner()->component<MV::Scene::Clickable>();
		yScaleClick->onAccept.connect("updateScaleY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->elementToEdit->scale({ scaleX->number(), scaleY->number() });
		});
		scaleY->onEnter.connect("updateScaleY", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->scale({ scaleX->number(), scaleY->number() });
		});

		controls->onChange = [&](EditableNode *a_element) {
			posX->number(static_cast<int>(std::lround(controls->position().x)));
			posY->number(static_cast<int>(std::lround(controls->position().y)));

			rotate->number(static_cast<int>(std::lround(controls->rotation().z)));
		};
	}
	
	auto attachComponentButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Attach", buttonSize, UTF_CHAR_STR("Attach"));
	attachComponentButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) mutable {
		componentPanel->loadPanel<ChooseElementCreationType>(controls->elementToEdit, this);
	});
	
	updateComponentEditButtons(false);

	detachSignal = controls->elementToEdit->onDetach.connect([&](const std::shared_ptr<MV::Scene::Component> &a_component) {
		updateComponentEditButtons(false);
	});

	panel.updateBoxHeader(grid->bounds().width());

	SDL_StartTextInput();
}

void SelectedNodeEditorPanel::updateComponentEditButtons(bool a_attached) {
	for (auto&& node : componentEditButtons) {
		node->removeFromParent();
	}
	componentEditButtons.clear();
	buttonSize = MV::size(110.0f, 27.0f);
	auto componentList = controls->elementToEdit->components<MV::Scene::Sprite, MV::Scene::Grid, MV::Scene::Emitter, MV::Scene::Spine, MV::Scene::PathMap>(true);
	int count = 0;
	for (auto&& component : componentList) {
		++count;
		MV::visit(component,
		[&](const MV::Scene::SafeComponent<MV::Scene::Sprite> &a_sprite) {
			CreateSpriteComponentButton(a_sprite);
		},
		[&](const MV::Scene::SafeComponent<MV::Scene::Grid> &a_grid) {
			CreateGridComponentButton(a_grid);
		},
		[&](const MV::Scene::SafeComponent<MV::Scene::Emitter> &a_emitter) {
			CreateEmitterComponentButton(a_emitter);
		},
		[&](const MV::Scene::SafeComponent<MV::Scene::Spine> &a_spine) {
			CreateSpineComponentButton(a_spine);
		},
		[&](const MV::Scene::SafeComponent<MV::Scene::PathMap> &a_pathMap) {
			CreatePathMapComponentButton(a_pathMap);
		});
	}
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreateSpriteComponentButton(const MV::Scene::SafeComponent<MV::Scene::Sprite> & a_sprite) {
	auto button = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, MV::guid("EditSprite"), buttonSize, MV::toWide("S: " + a_sprite->id()));
	button->onAccept.connect(MV::guid("click"), [&, a_sprite, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedRectangleEditorPanel>(std::make_shared<EditableRectangle>(a_sprite, panel.editor()->make("EditableSprite"), panel.resources().mouse), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), MV::toWide("S: " + a_newName));
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreateGridComponentButton(const MV::Scene::SafeComponent<MV::Scene::Grid> & a_grid) {
	auto button = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, MV::guid("EditGrid"), buttonSize, MV::toWide("G: " + a_grid->id()));
	button->onAccept.connect(MV::guid("click"), [&, a_grid, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedGridEditorPanel>(std::make_shared<EditableGrid>(a_grid, panel.editor()->make("EditableGrid"), panel.resources().mouse), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), MV::toWide("G: " + a_newName));
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreatePathMapComponentButton(const MV::Scene::SafeComponent<MV::Scene::PathMap> & a_pathMap) {
	auto button = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, MV::guid("EditPathMap"), buttonSize, MV::toWide("P: " + a_pathMap->id()));
	button->onAccept.connect(MV::guid("click"), [&, a_pathMap, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedPathMapEditorPanel>(std::make_shared<EditablePathMap>(a_pathMap, panel.editor()->make("EditablePathMap"), panel.resources().mouse), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), MV::toWide("P: " + a_newName));
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreateEmitterComponentButton(const MV::Scene::SafeComponent<MV::Scene::Emitter> & a_emitter) {
	auto button = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, MV::guid("EditEmitter"), buttonSize, MV::toWide("E: " + a_emitter->id()));
	button->onAccept.connect(MV::guid("click"), [&, a_emitter, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedEmitterEditorPanel>(std::make_shared<EditableEmitter>(a_emitter, panel.editor()->make("EditableEmitter"), panel.resources().mouse), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), MV::toWide("E: " + a_newName));
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreateSpineComponentButton(const MV::Scene::SafeComponent<MV::Scene::Spine> & a_spine) {
	auto button = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, MV::guid("EditSpine"), buttonSize, MV::toWide("E: " + a_spine->id()));
	button->onAccept.connect(MV::guid("click"), [&, a_spine, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedSpineEditorPanel>(std::make_shared<EditableSpine>(a_spine, panel.editor()->make("EditableSpine"), panel.resources().mouse), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), MV::toWide("E: " + a_newName));
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

void SelectedNodeEditorPanel::handleInput(SDL_Event &a_event) {
	if (activeTextbox) {
		activeTextbox->text(a_event);
	}
	componentPanel->handleInput(a_event);
}

void SelectedNodeEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->resetHandles();
	componentPanel->onSceneDrag(a_delta);
}

void SelectedNodeEditorPanel::onSceneZoom() {
	controls->resetHandles();
	componentPanel->onSceneZoom();
}

SelectedGridEditorPanel::SelectedGridEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableGrid> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton) :
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();

	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);
	auto deselectButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Deselect", buttonSize, UTF_CHAR_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Delete", buttonSize, UTF_CHAR_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "Name", buttonSize)->
		text(MV::toWide(controls->elementToEdit->id()))->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->id(MV::toString(a_text->text()));
		renameButton(a_associatedButton->safe(), MV::toWide("G: ") + a_text->text());
		onNameChangeSignal(MV::toString(a_text->text()));
	});

	float textboxWidth = 52.0f;
	makeLabel(grid, *panel.resources().textLibrary, "Width/Columns", buttonSize, UTF_CHAR_STR("Width|Column"));
	width = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "width", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->gridWidth()))));
	columns = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "columns", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(a_controls->elementToEdit->columns())));

	makeLabel(grid, *panel.resources().textLibrary, "Padding", buttonSize, UTF_CHAR_STR("Padding X|Y"));
	paddingX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "paddingX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->padding().first.x))));
	paddingY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "paddingY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->padding().first.y))));

	makeLabel(grid, *panel.resources().textLibrary, "Margins", buttonSize, UTF_CHAR_STR("Margins X|Y"));
	marginsX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "marginsX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->margin().first.x))));
	marginsY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "marginsY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->margin().first.y))));

	if (controls) {
		auto paddingChange = [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->padding({ paddingX->number(), paddingX->number() });
		};
		auto paddingXClick = paddingX->onEnter.connect("updateX", paddingChange);
		paddingX->onEnter.connect("updateX", paddingChange);
		auto paddingYClick = paddingY->onEnter.connect("updateY", paddingChange);
		paddingY->onEnter.connect("updateY", paddingChange);

		auto marginChange = [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->margin({ paddingX->number(), paddingX->number() });
		};
		auto marginsXClick = paddingX->onEnter.connect("updateX", marginChange);
		marginsX->onEnter.connect("updateX", marginChange);
		auto marginsYClick = paddingY->onEnter.connect("updateY", marginChange);
		marginsY->onEnter.connect("updateY", marginChange);

		auto widthClick = width->owner()->component<MV::Scene::Clickable>();
		widthClick->onAccept.connect("updateWidth", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->elementToEdit->gridWidth(width->number());
		});
		width->onEnter.connect("updateWidth", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->gridWidth(width->number());
		});
		auto columnsClick = width->owner()->component<MV::Scene::Clickable>();
		columnsClick->onAccept.connect("updateHeight", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->elementToEdit->columns(static_cast<size_t>(columns->number()));
		});
		columns->onEnter.connect("updateHeight", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->columns(static_cast<size_t>(columns->number()));
		});
	}
	auto deselectLocalAABB = deselectButton->bounds();

	panel.updateBoxHeader(grid->bounds().width());

	SDL_StartTextInput();
}

void SelectedGridEditorPanel::handleInput(SDL_Event &a_event) {
	if (activeTextbox) {
		activeTextbox->text(a_event);
	}
}

void SelectedGridEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->resetHandles();
}

void SelectedGridEditorPanel::onSceneZoom() {
	controls->resetHandles();
}

SelectedSpineEditorPanel::SelectedSpineEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableSpine> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton) :
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();

	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);
	auto deselectButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Deselect", buttonSize, UTF_CHAR_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Delete", buttonSize, UTF_CHAR_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "Name", buttonSize)->
		text(MV::toWide(controls->elementToEdit->id()))->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->id(MV::toString(a_text->text()));
		renameButton(a_associatedButton->safe(), MV::toWide("G: ") + a_text->text());
		onNameChangeSignal(MV::toString(a_text->text()));
	});

	float textboxWidth = 52.0f;
	makeLabel(grid, *panel.resources().textLibrary, "Json", buttonSize, UTF_CHAR_STR("Json"));
	assetJson = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "json", buttonSize, MV::toWide(a_controls->elementToEdit->bundle().skeletonFile));
	makeLabel(grid, *panel.resources().textLibrary, "Atlas", buttonSize, UTF_CHAR_STR("Atlas"));
	assetAtlas = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "atlas", buttonSize, MV::toWide(a_controls->elementToEdit->bundle().atlasFile));
	
	makeLabel(grid, *panel.resources().textLibrary, "Scale", buttonSize, UTF_CHAR_STR("Scale"));
	scale = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "scale", buttonSize, MV::toWide(std::to_string(a_controls->elementToEdit->bundle().loadScale)));

	auto addAttachment = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Add", buttonSize, UTF_CHAR_STR("Add Binding"));
	std::weak_ptr<MV::Scene::Node> weakGrid = grid;
	addAttachment->onAccept.connect("click", [&, weakGrid](std::shared_ptr<MV::Scene::Clickable>) { handleMakeButton(weakGrid.lock()); });

	auto clearAttachments = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Clear", buttonSize, UTF_CHAR_STR("Clear Bindings"));
	clearAttachments->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->unbindAll();
		for (auto&& socket : linkedSockets) {
			socket->owner()->removeFromParent();
		}
		for (auto&& node : linkedNodes) {
			node->owner()->removeFromParent();
		}
		linkedSockets.clear();
		linkedNodes.clear();
	});

	if (controls) {
		auto bundleChange = [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			if (MV::fileExists(MV::toString(assetJson->text())) && MV::fileExists(MV::toString(assetAtlas->text()))) {
				try { controls->elementToEdit->load({ MV::toString(assetJson->text()), MV::toString(assetAtlas->text()), scale->number() }); }
				catch (...) {}
			}
		};
		auto bundleChangeClick = [&](std::shared_ptr<MV::Scene::Clickable>) {
			if (MV::fileExists(MV::toString(assetJson->text())) && MV::fileExists(MV::toString(assetAtlas->text()))) {
				try { controls->elementToEdit->load({ MV::toString(assetJson->text()), MV::toString(assetAtlas->text()), scale->number() }); }
				catch (...) {}
			}
		};
		assetJson->onEnter.connect("json", bundleChange);
		assetJson->owner()->component<MV::Scene::Clickable>()->onAccept.connect("json", bundleChangeClick);
		assetAtlas->onEnter.connect("atlas", bundleChange);
		assetAtlas->owner()->component<MV::Scene::Clickable>()->onAccept.connect("json", bundleChangeClick);
		scale->onEnter.connect("atlas", bundleChange);
		scale->owner()->component<MV::Scene::Clickable>()->onAccept.connect("json", bundleChangeClick);

	}

	for (auto&& slotKV : controls->elementToEdit->boundSlots()) {
		for (auto&& node : slotKV.second) {
			handleMakeButton(grid, slotKV.first, node);
		}
	}

	auto deselectLocalAABB = deselectButton->bounds();

	panel.updateBoxHeader(grid->bounds().width());

	SDL_StartTextInput();
}

void SelectedSpineEditorPanel::handleMakeButton(std::shared_ptr<MV::Scene::Node> a_grid, const std::string &a_socket, const std::string &a_node) {
	if (controls) {
		float textboxWidth = 52.0f;
		auto socketText = makeInputField(this, *panel.resources().mouse, a_grid, *panel.resources().textLibrary, MV::guid("json"), MV::size(textboxWidth, 27.0f), MV::toWide(a_socket));
		auto nodeText = makeInputField(this, *panel.resources().mouse, a_grid, *panel.resources().textLibrary, MV::guid("atlas"), MV::size(textboxWidth, 27.0f), MV::toWide(a_node));
		linkedSockets.push_back(socketText);
		linkedNodes.push_back(nodeText);

		auto nodeConnectionChange = [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->unbindAll();
			for (size_t i = 0; i < linkedSockets.size(); ++i) {
				controls->elementToEdit->bindNode(MV::toString(linkedSockets[i]->text()), MV::toString(linkedNodes[i]->text()));
			}
		};
		auto nodeConnectionChangeClick = [&](std::shared_ptr<MV::Scene::Clickable>) {
			controls->elementToEdit->unbindAll();
			for (size_t i = 0; i < linkedSockets.size(); ++i) {
				controls->elementToEdit->bindNode(MV::toString(linkedSockets[i]->text()), MV::toString(linkedNodes[i]->text()));
			}
		};

		socketText->onEnter.connect("changeNode", nodeConnectionChange);
		socketText->owner()->component<MV::Scene::Clickable>()->onAccept.connect("changeNode", nodeConnectionChangeClick);
		nodeText->onEnter.connect("changeNode", nodeConnectionChange);
		nodeText->owner()->component<MV::Scene::Clickable>()->onAccept.connect("changeNode", nodeConnectionChangeClick);
	}
}

void SelectedSpineEditorPanel::handleInput(SDL_Event &a_event) {
	if (activeTextbox) {
		activeTextbox->text(a_event);
	}
}

void SelectedSpineEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->resetHandles();
}

void SelectedSpineEditorPanel::onSceneZoom() {
	controls->resetHandles();
}

SelectedRectangleEditorPanel::SelectedRectangleEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableRectangle> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton):
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({BOX_BACKGROUND})->margin({4.0f, 4.0f})->
		padding({2.0f, 2.0f})->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);
	auto deselectButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Deselect", buttonSize, UTF_CHAR_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Delete", buttonSize, UTF_CHAR_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	OpenTexturePicker();

	makeColorButton(grid, panel.content(), *panel.resources().textLibrary, *panel.resources().mouse, buttonSize, controls->elementToEdit->color(), [&](const MV::Color &a_color) {
		controls->elementToEdit->color(a_color);
	});

	makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "Name", buttonSize)->
		text(MV::toWide(controls->elementToEdit->id()))->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text){
			controls->elementToEdit->id(MV::toString(a_text->text()));
			onNameChangeSignal(controls->elementToEdit->id());
			if (a_associatedButton) {
				renameButton(a_associatedButton->safe(), MV::toWide("S: ") + a_text->text());
			}
		});

	float textboxWidth = 52.0f;
	offsetX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "offsetX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->position().x))));
	offsetY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "offsetY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->position().y))));

	width = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "width", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->size().width))));
	height = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "height", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->size().height))));

	makeButton(grid, *a_panel.resources().textLibrary, *a_panel.resources().mouse, "Texture", buttonSize, UTF_CHAR_STR("Texture"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>){
			OpenTexturePicker();
		});

	aspectX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "AspectX", MV::size(textboxWidth, 27.0f));
	aspectY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "AspectY", MV::size(textboxWidth, 27.0f));

	makeButton(grid, *a_panel.resources().textLibrary, *a_panel.resources().mouse, "SetAspect", buttonSize, UTF_CHAR_STR("Snap Aspect"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>){
			if (controls->texture()) {
				auto size = controls->texture()->bounds().size();
				aspectX->number(size.width);
				aspectY->number(size.height);
				controls->aspect(MV::round<MV::PointPrecision>(size));
			}
		});

	makeButton(grid, *a_panel.resources().textLibrary, *a_panel.resources().mouse, "SetSize", buttonSize, UTF_CHAR_STR("Snap Size"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>){
			if (controls->texture()) {
				auto size = controls->texture()->bounds().size();
				width->number(size.width);
				height->number(size.width);
				controls->size(MV::round<MV::PointPrecision>(size));
			}
		});

	makeButton(grid, *a_panel.resources().textLibrary, *a_panel.resources().mouse, "FlipX", buttonSize, UTF_CHAR_STR("Flip X"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>) {
			if (controls->texture()) {
				controls->texture()->flipX(!controls->texture()->flipX());
			}
		});

	makeButton(grid, *a_panel.resources().textLibrary, *a_panel.resources().mouse, "FlipY", buttonSize, UTF_CHAR_STR("Flip Y"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>) {
			if (controls->texture()) {
				controls->texture()->flipY(!controls->texture()->flipY());
			}
		});

	if(controls){
		auto xClick = offsetX->owner()->component<MV::Scene::Clickable>();
		xClick->onAccept.connect("updateX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->position({ offsetX->number(), offsetY->number()});
		});
		offsetX->onEnter.connect("updateX", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->position({ offsetX->number(), offsetY->number()});
		});
		auto yClick = offsetY->owner()->component<MV::Scene::Clickable>();
		yClick->onAccept.connect("updateY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->position({ offsetX->number(), offsetY->number()});
		});
		offsetY->onEnter.connect("updateY", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->position({ offsetX->number(), offsetY->number()});
		});

		aspectX->onEnter.connect("updateAspectX", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->aspect({aspectX->number(), aspectY->number()});
		});
		aspectY->onEnter.connect("updateAspectY", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->aspect({aspectX->number(), aspectY->number()});
		});

		auto widthClick = width->owner()->component<MV::Scene::Clickable>();
		widthClick->onAccept.connect("updateWidth", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->size({width->number(), height->number()});
		});
		width->onEnter.connect("updateWidth", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->size({width->number(), height->number()});
		});
		auto heightClick = width->owner()->component<MV::Scene::Clickable>();
		heightClick->onAccept.connect("updateHeight", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->size({width->number(), height->number()});
		});
		height->onEnter.connect("updateHeight", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->size({width->number(), height->number()});
		});

		controls->onChange = [&](EditableRectangle *a_element){
			offsetX->number(static_cast<int>(std::lround(controls->position().x)));
			offsetY->number(static_cast<int>(std::lround(controls->position().y)));

			width->number(static_cast<int>(std::lround(controls->size().width)));
			height->number(static_cast<int>(std::lround(controls->size().height)));
		};
	}
	auto deselectLocalAABB = deselectButton->bounds();

	panel.updateBoxHeader(grid->bounds().width());

	SDL_StartTextInput();
}

void SelectedRectangleEditorPanel::OpenTexturePicker() {
	clearTexturePicker();
	picker = std::make_shared<TexturePicker>(panel.editor(), panel.resources(), [&](std::shared_ptr<MV::TextureHandle> a_handle, bool a_allowClear){
		if(a_handle || a_allowClear){
			controls->texture(a_handle);
		}
		clearTexturePicker();
	});
}

void SelectedRectangleEditorPanel::handleInput(SDL_Event &a_event) {
	if(activeTextbox){
		activeTextbox->text(a_event);
	}
}

void SelectedRectangleEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->repositionHandles(true, true, false);
}

void SelectedRectangleEditorPanel::onSceneZoom() {
	controls->resetHandles();
}

SelectedEmitterEditorPanel::SelectedEmitterEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableEmitter> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton):
EditorPanel(a_panel),
controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(232.0f)->
		color({InterfaceColors::BOX_BACKGROUND})->margin({4.0f, 4.0f})->
		padding({2.0f, 2.0f})->owner();
	auto buttonSize = MV::size(226.0f, 27.0f);
	auto deselectButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Deselect", buttonSize, UTF_CHAR_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Delete", buttonSize, UTF_CHAR_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "Name", buttonSize)->
		text(MV::toWide(controls->elementToEdit->id()))->
		onEnter.connect("rename", [&, a_associatedButton](const std::shared_ptr<MV::Scene::Text> &a_text){
			controls->elementToEdit->id(MV::toString(a_text->text()));
			onNameChangeSignal(controls->elementToEdit->id());
			renameButton(a_associatedButton->safe(), MV::toWide("E: ") + a_text->text());
		});

	float textboxWidth = 52.0f;

	offsetX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "offsetX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->position().x))));
	offsetY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "offsetY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->position().y))));

	width = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "width", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->size().width))));
	height = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "height", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->size().height))));

	makeButton(grid, *a_panel.resources().textLibrary, *a_panel.resources().mouse, "Texture", buttonSize, UTF_CHAR_STR("Texture"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>){
			OpenTexturePicker();
		});

	const MV::Size<> labelSize{226.0f, 20.0f};

	makeLabel(grid, *panel.resources().textLibrary, "spawnRate", labelSize, UTF_CHAR_STR("Spawn Rate"));
	auto maximumSpawnRate = makeSlider(node->renderer(), *panel.resources().mouse, [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximumSpawnRate = MV::mixIn(0.0f, 1.25f, a_slider->percent(), 3);
	}, MV::unmixIn(0.0f, 1.25f, controls->elementToEdit->properties().maximumSpawnRate, 3));
	makeSlider(*panel.resources().mouse, grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimumSpawnRate = MV::mixIn(0.0f, 1.25f, a_slider->percent(), 3);
		maximumSpawnRate->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmixIn(0.0f, 1.25f, controls->elementToEdit->properties().minimumSpawnRate, 3));
	grid->add(maximumSpawnRate);

	makeLabel(grid, *panel.resources().textLibrary, "lifeSpan", labelSize, UTF_CHAR_STR("Lifespan"));
	auto maximumLifespan = makeSlider(node->renderer(), *panel.resources().mouse, [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.maxLifespan = MV::mixIn(0.01f, 60.0f, a_slider->percent(), 2);
	}, MV::unmixIn(0.01f, 60.0f, controls->elementToEdit->properties().maximum.maxLifespan, 2));
	makeSlider(*panel.resources().mouse, grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.maxLifespan = MV::mixIn(0.01f, 60.0f, a_slider->percent(), 2);
		maximumLifespan->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmixIn(0.01f, 60.0f, controls->elementToEdit->properties().minimum.maxLifespan, 2));
	grid->add(maximumLifespan);


	auto maximumEndSpeed = makeSlider(node->renderer(), *panel.resources().mouse, [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endSpeed = MV::mixInOut(-1000.0f, 1000.0f, a_slider->percent(), 2);
	}, MV::unmixInOut(-1000.0f, 1000.0f, controls->elementToEdit->properties().maximum.endSpeed, 2));
	auto minimumEndSpeed = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endSpeed = MV::mixInOut(-1000.0f, 1000.0f, a_slider->percent(), 2);
		maximumEndSpeed->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmixInOut(-1000.0f, 1000.0f, controls->elementToEdit->properties().minimum.endSpeed, 2));

	makeLabel(grid, *panel.resources().textLibrary, "initialSpeed", labelSize, UTF_CHAR_STR("Start Speed"));
	auto startSpeed = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginSpeed = MV::mixInOut(-1000.0f, 1000.0f, a_slider->percent(), 2);
		maximumEndSpeed->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmixInOut(-1000.0f, 1000.0f, controls->elementToEdit->properties().maximum.beginSpeed, 2));
	makeSlider(*panel.resources().mouse, grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginSpeed = MV::mixInOut(-1000.0f, 1000.0f, a_slider->percent(), 2);
		startSpeed->component<MV::Scene::Slider>()->percent(a_slider->percent());
		minimumEndSpeed->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmixInOut(-1000.0f, 1000.0f, controls->elementToEdit->properties().minimum.beginSpeed, 2));
	grid->add(startSpeed);

	makeLabel(grid, *panel.resources().textLibrary, "speedChange", labelSize, UTF_CHAR_STR("End Speed"));
	grid->add(minimumEndSpeed);
	grid->add(maximumEndSpeed);

	makeLabel(grid, *panel.resources().textLibrary, "initialDirection", labelSize, UTF_CHAR_STR("Start Direction"));
	auto maximumStartDirection = makeSlider(node->renderer(), *panel.resources().mouse, [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximumDirection = {0.0f, 0.0f, a_slider->percent() * 720.0f};
	}, controls->elementToEdit->properties().maximumDirection.z / 720.0f);
	makeSlider(*panel.resources().mouse, grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimumDirection = {0.0f, 0.0f, a_slider->percent() * 720.0f};
		maximumStartDirection->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimumDirection.z / 720.0f);
	grid->add(maximumStartDirection);

	makeLabel(grid, *panel.resources().textLibrary, "directionChange", labelSize, UTF_CHAR_STR("Direction Change"));
	auto maximumDirectionChange = makeSlider(node->renderer(), *panel.resources().mouse, [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.directionalChange = {0.0f, 0.0f, MV::mix(-720.0f, 720.0f, a_slider->percent())};
	}, MV::unmix(-720.0f, 720.0f, controls->elementToEdit->properties().maximum.directionalChange.z));
	makeSlider(*panel.resources().mouse, grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.directionalChange = {0.0f, 0.0f, MV::mix(-720.0f, 720.0f, a_slider->percent())};
		maximumDirectionChange->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-720.0f, 720.0f, controls->elementToEdit->properties().minimum.directionalChange.z));
	grid->add(maximumDirectionChange);

	auto maximumEndSize = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endScale = MV::mix(-60.0f, 60.0f, a_slider->percent());
	}, MV::unmix(-60.0f, 60.0f, controls->elementToEdit->properties().maximum.endScale.x));
	auto minimumEndSize = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endScale = MV::mix(-60.0f, 60.0f, a_slider->percent());
		maximumEndSize->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-60.0f, 60.0f, controls->elementToEdit->properties().minimum.endScale.x));

	makeLabel(grid, *panel.resources().textLibrary, "startSize", labelSize, UTF_CHAR_STR("Start Size"));
	auto startSize = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginScale = MV::mix(-60.0f, 60.0f, a_slider->percent());
		maximumEndSize->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-60.0f, 60.0f, controls->elementToEdit->properties().maximum.beginScale.x));
	makeSlider(*panel.resources().mouse, grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginScale = MV::mix(-60.0f, 60.0f, a_slider->percent());
		startSize->component<MV::Scene::Slider>()->percent(a_slider->percent());
		minimumEndSize->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-60.0f, 60.0f, controls->elementToEdit->properties().minimum.beginScale.x));
	grid->add(startSize);

	makeLabel(grid, *panel.resources().textLibrary, "endSize", labelSize, UTF_CHAR_STR("End Size"));
	grid->add(minimumEndSize);
	grid->add(maximumEndSize);

	makeLabel(grid, *panel.resources().textLibrary, "initialRotation", labelSize, UTF_CHAR_STR("Initialize Rotation"));
	auto maximumRotation = makeSlider(node->renderer(), *panel.resources().mouse, [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximumRotation = {0.0f, 0.0f, a_slider->percent() * 360.0f};
	}, controls->elementToEdit->properties().maximumRotation.z / 360.0f);
	makeSlider(*panel.resources().mouse, grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimumRotation = {0.0f, 0.0f, a_slider->percent() * 360.0f};
		maximumRotation->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimumRotation.z / 360.0f);
	grid->add(maximumRotation);

	makeLabel(grid, *panel.resources().textLibrary, "rotationChange", labelSize, UTF_CHAR_STR("Rotation Change"));
	auto maximumRotationChange = makeSlider(node->renderer(), *panel.resources().mouse, [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.rotationalChange = {0.0f, 0.0f, MV::mix(-720.0f, 720.0f, a_slider->percent())};
	}, MV::unmix(-720.0f, 720.0f, controls->elementToEdit->properties().maximum.rotationalChange.z));
	makeSlider(*panel.resources().mouse, grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.rotationalChange = {0.0f, 0.0f, MV::mix(-720.0f, 720.0f, a_slider->percent())};
		maximumRotationChange->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-720.0f, 720.0f, controls->elementToEdit->properties().minimum.rotationalChange.z));
	grid->add(maximumRotationChange);

	auto minStartColor = makeColorButton(node->renderer(), panel.content(), *panel.resources().textLibrary, *panel.resources().mouse, buttonSize, controls->elementToEdit->color(), [&](const MV::Color &a_color) {
		controls->elementToEdit->properties().minimum.beginColor = a_color;
	}, UTF_CHAR_STR("Min Start"));
	auto minEndColor = makeColorButton(node->renderer(), panel.content(), *panel.resources().textLibrary, *panel.resources().mouse, buttonSize, controls->elementToEdit->color(), [&](const MV::Color &a_color) {
		controls->elementToEdit->color(a_color);
	}, UTF_CHAR_STR("Min End"));

	auto maxStartColor = makeColorButton(node->renderer(), panel.content(), *panel.resources().textLibrary, *panel.resources().mouse, buttonSize, controls->elementToEdit->color(), [&](const MV::Color &a_color) {
		controls->elementToEdit->color(a_color);
	}, UTF_CHAR_STR("Max Start"));
	auto maxEndColor = makeColorButton(node->renderer(), panel.content(), *panel.resources().textLibrary, *panel.resources().mouse, buttonSize, controls->elementToEdit->color(), [&](const MV::Color &a_color) {
		controls->elementToEdit->color(a_color);
	}, UTF_CHAR_STR("Max End"));

	/*
	auto maximumREndMax = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endColor.R = a_slider->percent();
	}, controls->elementToEdit->properties().maximum.endColor.R);
	auto maximumGEndMax = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endColor.G = a_slider->percent();
	}, controls->elementToEdit->properties().maximum.endColor.G);
	auto maximumBEndMax = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endColor.B = a_slider->percent();
	}, controls->elementToEdit->properties().maximum.endColor.B);
	auto maximumAEndMax = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endColor.A = a_slider->percent();
	}, controls->elementToEdit->properties().maximum.endColor.A);

	auto maximumREndMin = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endColor.R = a_slider->percent();
		maximumREndMax->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimum.endColor.R);
	auto maximumGEndMin = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endColor.G = a_slider->percent();
		maximumGEndMax->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimum.endColor.G);
	auto maximumBEndMin = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endColor.B = a_slider->percent();
		maximumBEndMax->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimum.endColor.B);
	auto maximumAEndMin = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endColor.A = a_slider->percent();
		maximumAEndMax->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimum.endColor.A);

	auto maximumRInitial = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginColor.R = a_slider->percent();
		maximumREndMax->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().maximum.beginColor.R);
	auto maximumGInitial = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginColor.G = a_slider->percent();
		maximumGEndMax->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().maximum.beginColor.G);
	auto maximumBInitial = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginColor.B = a_slider->percent();
		maximumBEndMax->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().maximum.beginColor.B);
	auto maximumAInitial = makeSlider(node->renderer(), *panel.resources().mouse, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginColor.A = a_slider->percent();
		maximumAEndMax->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().maximum.beginColor.A);

	makeLabel(grid, *panel.resources().textLibrary, "initialColorMin", labelSize, UTF_CHAR_STR("Initial Color Min"));
	makeSlider(*panel.resources().mouse, grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginColor.R = a_slider->percent();
		maximumRInitial->component<MV::Scene::Slider>()->percent(a_slider->percent());
		maximumREndMin->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimum.beginColor.R);
	makeSlider(*panel.resources().mouse, grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginColor.G = a_slider->percent();
		maximumGInitial->component<MV::Scene::Slider>()->percent(a_slider->percent());
		maximumGEndMin->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimum.beginColor.G);
	makeSlider(*panel.resources().mouse, grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginColor.B = a_slider->percent();
		maximumBInitial->component<MV::Scene::Slider>()->percent(a_slider->percent());
		maximumBEndMin->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimum.beginColor.B);
	makeSlider(*panel.resources().mouse, grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginColor.A = a_slider->percent();
		maximumAInitial->component<MV::Scene::Slider>()->percent(a_slider->percent());
		maximumAEndMin->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimum.beginColor.A);

	makeLabel(grid, *panel.resources().textLibrary, "initialColorMax", labelSize, UTF_CHAR_STR("Initial Color Max"));
	grid->add(maximumRInitial);
	grid->add(maximumGInitial);
	grid->add(maximumBInitial);
	grid->add(maximumAInitial);

	makeLabel(grid, *panel.resources().textLibrary, "endColorMin", labelSize, UTF_CHAR_STR("End Color Min"));
	grid->add(maximumREndMin);
	grid->add(maximumGEndMin);
	grid->add(maximumBEndMin);
	grid->add(maximumAEndMin);

	makeLabel(grid, *panel.resources().textLibrary, "endColorMax", labelSize, UTF_CHAR_STR("End Color Max"));
	grid->add(maximumREndMax);
	grid->add(maximumGEndMax);
	grid->add(maximumBEndMax);
	grid->add(maximumAEndMax);
	*/


	auto xClick = offsetX->owner()->component<MV::Scene::Clickable>();
	xClick->onAccept.connect("updateX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		controls->position({ offsetX->number(), offsetY->number()});
	});
	offsetX->onEnter.connect("updateX", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
		controls->position({ offsetX->number(), offsetY->number()});
	});
	auto yClick = offsetY->owner()->component<MV::Scene::Clickable>();
	yClick->onAccept.connect("updateY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		controls->position({ offsetX->number(), offsetY->number()});
	});
	offsetY->onEnter.connect("updateY", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
		controls->position({ offsetX->number(), offsetY->number()});
	});

	auto widthClick = width->owner()->component<MV::Scene::Clickable>();
	widthClick->onAccept.connect("updateWidth", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		controls->size({width->number(), height->number()});
	});
	width->onEnter.connect("updateWidth", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
		controls->size({width->number(), height->number()});
	});
	auto heightClick = height->owner()->component<MV::Scene::Clickable>();
	heightClick->onAccept.connect("updateHeight", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		controls->size({width->number(), height->number()});
	});
	height->onEnter.connect("updateHeight", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
		controls->size({width->number(), height->number()});
	});

	controls->onChange = [&](EditableEmitter *a_element) {
		offsetX->number(static_cast<int>(std::lround(controls->position().x)));
		offsetY->number(static_cast<int>(std::lround(controls->position().y)));

		width->number(static_cast<int>(std::lround(controls->size().width)));
		height->number(static_cast<int>(std::lround(controls->size().height)));
	};

	auto deselectLocalAABB = deselectButton->bounds();

	panel.updateBoxHeader(grid->bounds().width());

	SDL_StartTextInput();
}

void SelectedEmitterEditorPanel::OpenTexturePicker() {
	picker = std::make_shared<TexturePicker>(panel.editor(), panel.resources(), [&](std::shared_ptr<MV::TextureHandle> a_handle, bool a_allowNull){
		if(a_handle || a_allowNull){
			controls->texture(a_handle);
		}
		clearTexturePicker();
	});
}

void SelectedEmitterEditorPanel::handleInput(SDL_Event &a_event) {
	if(activeTextbox){
		activeTextbox->text(a_event);
	}
}

void SelectedEmitterEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->repositionHandles(true, true, false);
}

void SelectedEmitterEditorPanel::onSceneZoom() {
	controls->resetHandles();
}


SelectedPathMapEditorPanel::SelectedPathMapEditorPanel(EditorControls &a_panel, std::shared_ptr<EditablePathMap> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton) :
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);
	auto deselectButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Deselect", buttonSize, UTF_CHAR_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Delete", buttonSize, UTF_CHAR_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "Name", buttonSize)->
		text(MV::toWide(controls->elementToEdit->id()))->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->id(MV::toString(a_text->text()));
		renameButton(a_associatedButton->safe(), MV::toWide("G: ") + a_text->text());
		onNameChangeSignal(MV::toString(a_text->text()));
	});

	auto controlBounds = a_controls->elementToEdit->bounds();

	float textboxWidth = 52.0f;
	width = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "width", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(controlBounds.width()))));
	height = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "height", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(controlBounds.height()))));

	posX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "posX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(controlBounds.minPoint.x))));
	posY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "posY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(controlBounds.minPoint.y))));

	cellsX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "cellsX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(a_controls->elementToEdit->gridSize().width)));
	cellsY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "cellsY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(a_controls->elementToEdit->gridSize().height)));

	if (controls) {
		auto xClick = posX->owner()->component<MV::Scene::Clickable>();
		xClick->onAccept.connect("updateX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->position({ posX->number(), posY->number() });
		});
		posX->onEnter.connect("updateX", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->position({ posX->number(), posY->number() });
		});
		auto yClick = posY->owner()->component<MV::Scene::Clickable>();
		yClick->onAccept.connect("updateY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->position({ posX->number(), posY->number() });
		});
		posY->onEnter.connect("updateY", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->position({ posX->number(), posY->number() });
		});

		auto widthClick = width->owner()->component<MV::Scene::Clickable>();
		widthClick->onAccept.connect("updateWidth", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->size({ width->number(), height->number() });
		});
		width->onEnter.connect("updateWidth", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->size({ width->number(), height->number() });
		});
		auto heightClick = width->owner()->component<MV::Scene::Clickable>();
		heightClick->onAccept.connect("updateHeight", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->size({ width->number(), height->number() });
		});
		height->onEnter.connect("updateHeight", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->size({ width->number(), height->number() });
		});

		auto cellsClickX = cellsX->owner()->component<MV::Scene::Clickable>();
		cellsClickX->onAccept.connect("updateCellsX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->elementToEdit->resizeGrid({static_cast<int>(cellsX->number()), static_cast<int>(cellsY->number())});
		});
		cellsX->onEnter.connect("updateCellsX", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->resizeGrid({ static_cast<int>(cellsX->number()), static_cast<int>(cellsY->number()) });
		});

		auto cellsClickY = cellsY->owner()->component<MV::Scene::Clickable>();
		cellsClickY->onAccept.connect("updateCellsY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->elementToEdit->resizeGrid({ static_cast<int>(cellsX->number()), static_cast<int>(cellsY->number()) });
		});
		cellsY->onEnter.connect("updateCellsY", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->resizeGrid({ static_cast<int>(cellsX->number()), static_cast<int>(cellsY->number()) });
		});

		controls->onChange = [&](EditablePathMap *a_element) {
			posX->number(static_cast<int>(std::lround(controls->position().x)));
			posY->number(static_cast<int>(std::lround(controls->position().y)));

			width->number(static_cast<int>(std::lround(controls->size().width)));
			height->number(static_cast<int>(std::lround(controls->size().height)));
		};
	}
	auto deselectLocalAABB = deselectButton->bounds();

	panel.updateBoxHeader(grid->bounds().width());

	SDL_StartTextInput();
}

void SelectedPathMapEditorPanel::handleInput(SDL_Event &a_event) {
	if (activeTextbox) {
		activeTextbox->text(a_event);
	}
}

void SelectedPathMapEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->resetHandles();
}

void SelectedPathMapEditorPanel::onSceneZoom() {
	controls->resetHandles();
}

DeselectedEditorPanel::DeselectedEditorPanel(EditorControls &a_panel):
	EditorPanel(a_panel) {
	auto node = panel.content();
	auto grid = node->make("grid")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({InterfaceColors::BOX_BACKGROUND})->margin({4.0f, 4.0f})->
		padding({2.0f, 2.0f})->owner();
	auto createButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Create", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Create"));
	fileName = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "Filename", MV::size(110.0f, 27.0f), UTF_CHAR_STR("map.scene"));
	auto saveButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Save", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Save"));
	auto loadButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Load", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Load"));

	panel.updateBoxHeader(grid->bounds().width());
	//panel.updateBoxHeader(grid->component<MV::Scene::Grid>()->bounds().width());

	createButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>){
		//panel.loadPanel<ChooseElementCreationType>();
		auto& renderer = panel.root()->renderer();
		auto node = panel.root()->make(MV::guid("node"))->screenPosition(MV::toPoint(renderer.window().size() / 2));

		auto editableNode = std::make_shared<EditableNode>(node, panel.editor(), panel.resources().mouse);
		
		panel.resources().editor->sceneUpdated();
		panel.loadPanel<SelectedNodeEditorPanel>(editableNode);
	});

	saveButton->onAccept.connect("save", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.root()->save(MV::toString(fileName->text()));
	});

	loadButton->onAccept.connect("load", [&](std::shared_ptr<MV::Scene::Clickable>){
		auto newRoot = MV::Scene::Node::load(MV::toString(fileName->text()), [&](cereal::JSONInputArchive& archive) {
			archive.add(
				cereal::make_nvp("mouse", panel.resources().mouse),
				cereal::make_nvp("renderer", &panel.root()->renderer()),
				cereal::make_nvp("textLibrary", panel.resources().textLibrary),
				cereal::make_nvp("pool", panel.resources().pool),
				cereal::make_nvp("texture", panel.resources().textures)
				);
		});
		panel.root(newRoot);
	});
}

ChooseElementCreationType::ChooseElementCreationType(EditorControls &a_panel, const std::shared_ptr<MV::Scene::Node> &a_nodeToAttachTo, SelectedNodeEditorPanel *a_editorPanel):
	EditorPanel(a_panel),
	editorPanel(a_editorPanel),
	nodeToAttachTo(a_nodeToAttachTo){

	auto node = panel.content();
	auto grid = node->make("grid")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({InterfaceColors::BOX_BACKGROUND})->margin({4.0f, 4.0f})->
		padding({2.0f, 2.0f})->owner();
	auto createRectangleButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Sprite", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Rectangle"));
	auto createEmitterButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Emitter", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Emitter"));
	auto createSpineButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Spine", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Spine"));
	auto createGridButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Grid", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Grid"));
	auto createPathMapButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "PathMap", MV::size(110.0f, 27.0f), UTF_CHAR_STR("PathMap"));
	auto cancel = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Cancel", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Cancel"));

	panel.updateBoxHeader(grid->bounds().width());

	createRectangleButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.selection().enable([&](const MV::BoxAABB<int> &a_selected){
			createRectangle(a_selected);
		});
	});

	createSpineButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>){
		createSpine();
	});

	createEmitterButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.selection().enable([&](const MV::BoxAABB<int> &a_selected){
			createEmitter(a_selected);
		});
	});

	createGridButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>) {
		createGrid();
	});

	createPathMapButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.selection().enable([&](const MV::BoxAABB<int> &a_selected) {
			createPathMap(a_selected);
		});
	});

	cancel->onAccept.connect("select", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.loadPanel<DeselectedEditorPanel>();
	});
}

void ChooseElementCreationType::createRectangle(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();
	auto tmp = MV::Scene::Node::make(nodeToAttachTo->renderer());
	auto newShape = nodeToAttachTo->attach<MV::Scene::Sprite>();
	newShape->bounds(nodeToAttachTo->localFromScreen(a_selected))->color({ CREATED_DEFAULT })->shader(MV::DEFAULT_ID)->id(MV::guid("sprite"));

	panel.loadPanel<SelectedRectangleEditorPanel>(std::make_shared<EditableRectangle>(newShape, panel.editor(), panel.resources().mouse), editorPanel->CreateSpriteComponentButton(newShape).self());
}

void ChooseElementCreationType::createPathMap(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();
	auto tmp = MV::Scene::Node::make(nodeToAttachTo->renderer());
	auto localBounds = nodeToAttachTo->localFromScreen(a_selected);
	auto newShape = nodeToAttachTo->attach<MV::Scene::PathMap>(MV::Size<>(10.0f, 10.0f), MV::Size<int>(std::max(static_cast<int>(localBounds.size().width / 10.0f), 1), std::max(static_cast<int>(localBounds.size().height / 10.0f), 1)));
	newShape->show();
	newShape->bounds(localBounds);
	panel.loadPanel<SelectedPathMapEditorPanel>(std::make_shared<EditablePathMap>(newShape, panel.editor(), panel.resources().mouse), editorPanel->CreatePathMapComponentButton(newShape).self());
}

void ChooseElementCreationType::createEmitter(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();

	auto newEmitter = nodeToAttachTo->attach<MV::Scene::Emitter>(*panel.resources().pool);
	newEmitter->id(MV::guid("emitter"))->shader(MV::DEFAULT_ID);
	auto transformedSelection = nodeToAttachTo->localFromScreen(a_selected);
	newEmitter->properties().minimumPosition = transformedSelection.minPoint;
	newEmitter->properties().maximumPosition = transformedSelection.maxPoint;

	auto editableEmitter = std::make_shared<EditableEmitter>(newEmitter, panel.editor(), panel.resources().mouse);
	panel.loadPanel<SelectedEmitterEditorPanel>(editableEmitter, editorPanel->CreateEmitterComponentButton(newEmitter).self());
}

void ChooseElementCreationType::createGrid() {
	panel.selection().disable();
	auto newGrid = nodeToAttachTo->attach<MV::Scene::Grid>();

	panel.loadPanel<SelectedGridEditorPanel>(std::make_shared<EditableGrid>(newGrid, panel.editor(), panel.resources().mouse), editorPanel->CreateGridComponentButton(newGrid).self());
}

void ChooseElementCreationType::createSpine() {
	panel.selection().disable();

	auto newSpine = nodeToAttachTo->attach<MV::Scene::Spine>()->shader(MV::DEFAULT_ID)->safe();
	newSpine->id(MV::guid("spine"));

	panel.loadPanel<SelectedSpineEditorPanel>(std::make_shared<EditableSpine>(newSpine, panel.editor(), panel.resources().mouse), editorPanel->CreateSpineComponentButton(newSpine).self());
}