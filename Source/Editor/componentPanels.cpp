#include "componentPanels.h"
#include "editorControls.h"
#include "editorFactories.h"
#include "texturePicker.h"
#include "anchorEditor.h"
#include "editor.h"

EditorPanel::EditorPanel(EditorControls &a_panel):
	panel(a_panel),
	onNameChange(onNameChangeSignal){
	panel.deletePanelContents();
}

void EditorPanel::cancelInput() {
	panel.selection().exitSelection();
}

std::shared_ptr<MV::Scene::Component> CopiedComponent;

void EditorPanel::handleInput(SDL_Event &a_event) {
	if(auto lockedAT = activeTextbox.lock()){
		lockedAT->text(a_event);
	} else {
		if (a_event.type == SDL_KEYDOWN && a_event.key.keysym.sym == SDLK_c) {
			const Uint8* keystate = SDL_GetKeyboardState(NULL);
			if (keystate[SDL_SCANCODE_LSHIFT] && keystate[SDL_SCANCODE_LCTRL]) {
				CopiedComponent = getEditingComponent();
			}
		}
	}
}

MV::Services& EditorPanel::services() {
	return panel.services();
}

EditorPanel::~EditorPanel() {
//	deactivateText();
	clearTexturePicker();
	clearAnchorEditor();
}

void EditorPanel::openAnchorEditor(std::shared_ptr<MV::Scene::Component> a_componentToAnchor) {
	clearAnchorEditor();
	anchorEditor = std::make_shared<AnchorEditor>(panel.editor(), std::static_pointer_cast<MV::Scene::Drawable>(a_componentToAnchor), *this);
}

SelectedNodeEditorPanel::SelectedNodeEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableNode> a_controls) :
	EditorPanel(a_panel),
	componentPanel(std::make_unique<EditorControls>(panel.editor(), panel.root(), panel.services())),
	controls(a_controls){

	auto node = panel.content();
	grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);

	auto nameField = makeInputField(this, panel.services(), grid, "Name", buttonSize)->
		text(controls->elementToEdit->id());

	nameField->onEnter.connect("rename", [&](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->id(a_text->text());
		onNameChangeSignal(controls->elementToEdit->id());
		panel.services().get<Editor>()->sceneUpdated();
	});
	nameField->owner()->component<MV::Scene::Clickable>()->onAccept.connect("rename", [&](std::shared_ptr<MV::Scene::Clickable> a_textClickable){
		controls->elementToEdit->id(a_textClickable->owner()->component<MV::Scene::Text>()->text());
		onNameChangeSignal(controls->elementToEdit->id());
		panel.services().get<Editor>()->sceneUpdated();
	});

	auto deselectButton = makeButton(grid, panel.services(), "De-Select", buttonSize, U8_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.loadPanel<DeselectedEditorPanel>();
	});

	auto saveButton = makeButton(grid, panel.services(), "Save", buttonSize, U8_STR("Save"));
	saveButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->save("Prefabs/" + controls->elementToEdit->id() + ".prefab");
	});

	auto loadButton = makeButton(grid, panel.services(), "Load", buttonSize, U8_STR("Load"));
	loadButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		if (MV::fileExistsAbsolute("Prefabs/" + controls->elementToEdit->id() + ".prefab")) {
			auto newNode = controls->elementToEdit->parent()->make("Prefabs/" + controls->elementToEdit->id() + ".prefab", panel.services());

			auto editableNode = std::make_shared<EditableNode>(newNode, panel.editor(), panel.services().get<MV::TapDevice>());

			panel.loadPanel<SelectedNodeEditorPanel>(editableNode);
			panel.services().get<Editor>()->sceneUpdated();
		}
	});

	auto copyButton = makeButton(grid, panel.services(), "Copy", buttonSize, U8_STR("Copy"));
	copyButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->clone();
		panel.services().get<Editor>()->sceneUpdated();
		panel.loadPanel<DeselectedEditorPanel>();
	});

	auto childCreateButton = makeButton(grid, panel.services(), "Create", buttonSize, U8_STR("Create"));
	childCreateButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		auto newNode = controls->elementToEdit->make();

		auto editableNode = std::make_shared<EditableNode>(newNode, panel.editor(), panel.services().get<MV::TapDevice>());

		panel.services().get<Editor>()->sceneUpdated();
		panel.loadPanel<SelectedNodeEditorPanel>(editableNode);
	});

	auto deleteButton = makeButton(grid, panel.services(), "Delete", buttonSize, U8_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->removeFromParent();
		panel.services().get<Editor>()->sceneUpdated();
		panel.loadPanel<DeselectedEditorPanel>();
	});

	const MV::Size<> labelSize{ buttonSize.width, 20.0f };

	makeLabel(grid, panel.services(), "Position", labelSize, U8_STR("Position"));
	float textboxWidth = 52.0f;
	posX = makeInputField(this, panel.services(), grid, "posX", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->position().x)));
	posY = makeInputField(this, panel.services(), grid, "posY", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->position().y)));

	makeLabel(grid, panel.services(), "Rotate", MV::size(textboxWidth, 27.0f), U8_STR("Rotate"));
	rotate = makeInputField(this, panel.services(), grid, "rotateZ", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->elementToEdit->rotation().z)));
	rotateX = makeInputField(this, panel.services(), grid, "rotateX", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->elementToEdit->rotation().x)));
	rotateY = makeInputField(this, panel.services(), grid, "rotateY", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->elementToEdit->rotation().y)));

	makeLabel(grid, panel.services(), "Scale", labelSize, U8_STR("Scale"));
	scaleX = makeInputField(this, panel.services(), grid, "scaleX", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->elementToEdit->scale().x)));
	scaleY = makeInputField(this, panel.services(), grid, "scaleY", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->elementToEdit->scale().y)));

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

		auto rotateClick = [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->elementToEdit->rotation({ MV::wrap(0.0f, 360.0f, rotateX->number()),  MV::wrap(0.0f, 360.0f, rotateY->number()),  MV::wrap(0.0f, 360.0f, rotate->number()) });
		};
		rotate->owner()->component<MV::Scene::Clickable>()->onAccept.connect("updateRotate", rotateClick);
		rotateX->owner()->component<MV::Scene::Clickable>()->onAccept.connect("updateRotate", rotateClick);
		rotateY->owner()->component<MV::Scene::Clickable>()->onAccept.connect("updateRotate", rotateClick);

		auto rotateEnter = [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->rotation({ MV::wrap(0.0f, 360.0f, rotateX->number()),  MV::wrap(0.0f, 360.0f, rotateY->number()),  MV::wrap(0.0f, 360.0f, rotate->number()) });
		};
		rotate->onEnter.connect("updateRotate", rotateEnter);
		rotateX->onEnter.connect("updateRotateX", rotateEnter);
		rotateY->onEnter.connect("updateRotateY", rotateEnter);

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
			posX->set(static_cast<int>(std::lround(controls->position().x)));
			posY->set(static_cast<int>(std::lround(controls->position().y)));

			rotate->set(static_cast<int>(std::lround(controls->rotation().z)));
		};
	}
	
	auto attachComponentButton = makeButton(grid, panel.services(), "Attach", buttonSize, U8_STR("Attach"));
	attachComponentButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) mutable {
		componentPanel->loadPanel<ChooseElementCreationType>(controls->elementToEdit, this);
	});
	
	updateComponentEditButtons(false);

	detachSignal = controls->elementToEdit->onDetach.connect([&](const std::shared_ptr<MV::Scene::Component> &a_component) {
		updateComponentEditButtons(false);
	});

	panel.updateBoxHeader(grid->bounds().width());
}

//BUTTON MASTER LIST [HERE]
void SelectedNodeEditorPanel::updateComponentEditButtons(bool a_attached) {
	for (auto&& node : componentEditButtons) {
		node->removeFromParent();
	}
	componentEditButtons.clear();
	buttonSize = MV::size(110.0f, 27.0f);
	auto componentList = controls->elementToEdit->components<MV::Scene::Sprite, MV::Scene::Text, MV::Scene::Grid, MV::Scene::Emitter, MV::Scene::Spine, MV::Scene::PathMap, MV::Scene::Button, MV::Scene::Clickable, MV::Scene::Drawable>(true);
	/*
	MV::visit(componentList,
	[&](const MV::Scene::SafeComponent<MV::Scene::Sprite> &a_sprite) {
		CreateSpriteComponentButton(a_sprite);
	},
	[&](const MV::Scene::SafeComponent<MV::Scene::Text> &a_text) {
		CreateTextComponentButton(a_text);
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
	},
	[&](const MV::Scene::SafeComponent<MV::Scene::Button> &a_button) {
		CreateButtonComponentButton(a_button);
	},
	[&](const MV::Scene::SafeComponent<MV::Scene::Clickable> &a_clickable) {
		CreateClickableComponentButton(a_clickable);
	},
	[&](const MV::Scene::SafeComponent<MV::Scene::Drawable> &a_drawable) {
		CreateDrawableComponentButton(a_drawable);
	});
	*/
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreateSpriteComponentButton(const MV::Scene::SafeComponent<MV::Scene::Sprite> & a_sprite) {
	auto button = makeButton(grid, panel.services(), MV::guid("EditSprite"), buttonSize, std::string("S: ") + a_sprite->id());
	button->onAccept.connect(MV::guid("click"), [&, a_sprite, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedRectangleEditorPanel>(std::make_shared<EditableRectangle>(a_sprite, panel.editor()->make("EditableSprite"), panel.services().get<MV::TapDevice>()), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), std::string("S: ") + a_newName);
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreateGridComponentButton(const MV::Scene::SafeComponent<MV::Scene::Grid> & a_grid) {
	auto button = makeButton(grid, panel.services(), MV::guid("EditGrid"), buttonSize, std::string("G: ") + a_grid->id());
	button->onAccept.connect(MV::guid("click"), [&, a_grid, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedGridEditorPanel>(std::make_shared<EditableGrid>(a_grid, panel.editor()->make("EditableGrid"), panel.services().get<MV::TapDevice>()), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), std::string("G: ") + a_newName);
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreateDrawableComponentButton(const MV::Scene::SafeComponent<MV::Scene::Drawable> & a_drawable) {
	auto button = makeButton(grid, panel.services(), MV::guid("EditGrid"), buttonSize, std::string("D: ") + a_drawable->id());
	button->onAccept.connect(MV::guid("click"), [&, a_drawable, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedDrawableEditorPanel>(std::make_shared<EditablePoints>(a_drawable, panel.editor()->make("EditableDrawable"), panel.services().get<MV::TapDevice>()), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), std::string("D: ") + a_newName);
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreatePathMapComponentButton(const MV::Scene::SafeComponent<MV::Scene::PathMap> & a_pathMap) {
	auto button = makeButton(grid, panel.services(), MV::guid("EditPathMap"), buttonSize, std::string("P: ") + a_pathMap->id());
	button->onAccept.connect(MV::guid("click"), [&, a_pathMap, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedPathMapEditorPanel>(std::make_shared<EditablePathMap>(a_pathMap, panel.editor()->make("EditablePathMap"), panel.services().get<MV::TapDevice>()), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), std::string("P: ") + a_newName);
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreateTextComponentButton(const MV::Scene::SafeComponent<MV::Scene::Text> & a_text) {
	auto button = makeButton(grid, panel.services(), MV::guid("EditText"), buttonSize, std::string("T: ") + a_text->id());
	button->onAccept.connect(MV::guid("click"), [&, a_text, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedTextEditorPanel>(std::make_shared<EditableText>(a_text, panel.editor()->make("EditableText"), panel.services().get<MV::TapDevice>()), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), std::string("T: ") + a_newName);
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}


MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreateEmitterComponentButton(const MV::Scene::SafeComponent<MV::Scene::Emitter> & a_emitter) {
	auto button = makeButton(grid, panel.services(), MV::guid("EditEmitter"), buttonSize, std::string("E: ") + a_emitter->id());
	button->onAccept.connect(MV::guid("click"), [&, a_emitter, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedEmitterEditorPanel>(std::make_shared<EditableEmitter>(a_emitter, panel.editor()->make("EditableEmitter"), panel.services().get<MV::TapDevice>()), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), std::string("E: ") + a_newName);
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreateSpineComponentButton(const MV::Scene::SafeComponent<MV::Scene::Spine> & a_spine) {
	auto button = makeButton(grid, panel.services(), MV::guid("EditSpine"), buttonSize, std::string("SP: ") + a_spine->id());
	button->onAccept.connect(MV::guid("click"), [&, a_spine, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedSpineEditorPanel>(std::make_shared<EditableSpine>(a_spine, panel.editor()->make("EditableSpine"), panel.services().get<MV::TapDevice>()), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), std::string("SP: ") + a_newName);
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreateButtonComponentButton(const MV::Scene::SafeComponent<MV::Scene::Button> & a_spine) {
	auto button = makeButton(grid, panel.services(), MV::guid("EditButton"), buttonSize, std::string("B: ") + a_spine->id());
	button->onAccept.connect(MV::guid("click"), [&, a_spine, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedButtonEditorPanel>(std::make_shared<EditableButton>(a_spine, panel.editor()->make("EditableButton"), panel.services().get<MV::TapDevice>()), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), std::string("B: ") + a_newName);
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

MV::Scene::SafeComponent<MV::Scene::Button> SelectedNodeEditorPanel::CreateClickableComponentButton(const MV::Scene::SafeComponent<MV::Scene::Clickable> & a_spine) {
	auto button = makeButton(grid, panel.services(), MV::guid("EditClickable"), buttonSize, std::string("C: ") + a_spine->id());
	button->onAccept.connect(MV::guid("click"), [&, a_spine, button](std::shared_ptr<MV::Scene::Clickable>) {
		componentPanel->loadPanel<SelectedClickableEditorPanel>(std::make_shared<EditableClickable>(a_spine, panel.editor()->make("EditableClickable"), panel.services().get<MV::TapDevice>()), button);
		componentPanel->activePanel()->onNameChange.connect(MV::guid("NameChange"), [button](const std::string &a_newName) {
			renameButton(button->safe(), std::string("C: ") + a_newName);
		});
	});
	componentEditButtons.push_back(button->owner());
	return button->safe();
}

void SelectedNodeEditorPanel::handleInput(SDL_Event &a_event) {
	EditorPanel::handleInput(a_event);
	componentPanel->handleInput(a_event);

	if (CopiedComponent && a_event.type == SDL_KEYDOWN && a_event.key.keysym.sym == SDLK_v) {
		const Uint8* keystate = SDL_GetKeyboardState(NULL);
		if (keystate[SDL_SCANCODE_LSHIFT] && keystate[SDL_SCANCODE_LCTRL]) {
			CopiedComponent->clone(controls->elementToEdit);
		}
	}

	if (a_event.type == SDL_DROPFILE) {
		std::cout << a_event.drop.file << std::endl;
		if (MV::fileExistsAbsolute(a_event.drop.file)) {
			auto newNode = controls->elementToEdit->make(a_event.drop.file, panel.services());

			//auto editableNode = std::make_shared<EditableNode>(newNode, panel.editor(), panel.services().get<MV::TapDevice>());

			//panel.loadPanel<SelectedNodeEditorPanel>(editableNode);
			panel.services().get<Editor>()->sceneUpdated();
		}
	}
}

void SelectedNodeEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->resetHandles();
	componentPanel->onSceneDrag(a_delta);
}

void SelectedNodeEditorPanel::onSceneZoom() {
	controls->resetHandles();
	componentPanel->onSceneZoom();
}

SelectedDrawableEditorPanel::SelectedDrawableEditorPanel(EditorControls &a_panel, std::shared_ptr<EditablePoints> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton) :
	EditorPanel(a_panel),
	controls(a_controls) {

	controls->onDragged = [&](size_t a_pointIndex, MV::Point<> a_final){
		auto pointList = controls->elementToEdit->getPoints();
		auto pointSelected = pointList[a_pointIndex];
		pointList[a_pointIndex] = a_final;
		controls->elementToEdit->setPoint(a_pointIndex, pointList[a_pointIndex]);

		const Uint8* keystate = SDL_GetKeyboardState(NULL);
		if (!keystate[SDL_SCANCODE_LSHIFT]) {
			for (size_t i = 0; i < pointList.size(); ++i) {
				if (i != a_pointIndex && MV::distance(pointSelected.point(), pointList[i].point()) < 0.75f) {
					pointList[i] = a_final;
					controls->elementToEdit->setPoint(i, pointList[i]);
				}
			}
		}
		return true;
	};

	controls->onSelected = [&](MV::Point<> a_point) {
		auto pointList = controls->elementToEdit->getPoints();
		for (int i = 0; i < pointList.size(); ++i) {
			if (MV::distance(a_point, pointList[i].point()) < 1.0f) {
				selectedPointIndex = i;
				applyColorToColorButton(colorButton, pointList[i].color());
				return;
			}
		}
	};

	float gridWidth = 220.0f;
	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(gridWidth + 6.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();
	auto buttonSize = MV::size(gridWidth, 27.0f);
	auto deselectButton = makeButton(grid, *panel.services().get<MV::TextLibrary>(), *panel.services().get<MV::TapDevice>(), "Deselect", buttonSize, U8_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, *panel.services().get<MV::TextLibrary>(), *panel.services().get<MV::TapDevice>(), "Delete", buttonSize, U8_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	openTexturePicker();

	std::weak_ptr<EditablePoints> weakControls = controls;
	colorButton = makeColorButton(grid, panel.content(), *panel.services().get<MV::TextLibrary>(), *panel.services().get<MV::TapDevice>(), buttonSize, controls->elementToEdit->color(), [=](const MV::Color &a_color) {
		auto element = weakControls.lock()->elementToEdit;
		if (element->pointSize() == 0) {
			return;
		}
		auto specifiedPoint = element->point(selectedPointIndex).point();
		auto pointList = element->getPoints();
		for (int i = 0; i < pointList.size(); ++i) {
			if (MV::distance(specifiedPoint, pointList[i].point()) < 1.0f) {
				element->setPoint(i, a_color);
			}
		}
	});

	makeInputField(this, *panel.services().get<MV::TapDevice>(), grid, *panel.services().get<MV::TextLibrary>(), "Name", buttonSize)->
		text(controls->elementToEdit->id())->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->id(a_text->text());
		onNameChangeSignal(controls->elementToEdit->id());
		if (a_associatedButton) {
			renameButton(a_associatedButton->safe(), std::string("D: ") + a_text->text());
		}
	});

	float textboxWidth = 52.0f;

	makeButton(grid, *a_panel.services().get<MV::TextLibrary>(), *a_panel.services().get<MV::TapDevice>(), "Texture", buttonSize, U8_STR("Texture"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>) {
		openTexturePicker();
	});

	makeButton(grid, *a_panel.services().get<MV::TextLibrary>(), *a_panel.services().get<MV::TapDevice>(), "Texture1", buttonSize, U8_STR("Texture1"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>) {
		openTexturePicker(1);
	});

	makeInputField(this, *panel.services().get<MV::TapDevice>(), grid, *panel.services().get<MV::TextLibrary>(), "Shader", buttonSize)->
		text(controls->elementToEdit->shader())->
		onEnter.connect("shader", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
			controls->elementToEdit->shader(a_text->text());
		});

	makeInputField(this, *panel.services().get<MV::TapDevice>(), grid, *panel.services().get<MV::TextLibrary>(), "BlendMode", buttonSize)->
		text(std::to_string(static_cast<int>(controls->elementToEdit->blend())))->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
			try {
				controls->elementToEdit->blend(static_cast<MV::Scene::Drawable::BlendModePreset>(std::stoi(a_text->text())));
			} catch (std::invalid_argument&) {} catch (std::out_of_range&) {}
		});

	auto deselectLocalAABB = deselectButton->bounds();

	panel.updateBoxHeader(grid->bounds().width());
}

void SelectedDrawableEditorPanel::openTexturePicker(size_t a_textureId) {
	clearTexturePicker();
	picker = std::make_shared<TexturePicker>(panel.editor(), panel.services(), [&, a_textureId](std::shared_ptr<MV::TextureHandle> a_handle, bool a_allowClear) {
		if (a_handle || a_allowClear) {
			controls->elementToEdit->texture(a_handle, a_textureId);
		}
		clearTexturePicker();
	});
}

void SelectedDrawableEditorPanel::handleInput(SDL_Event &a_event) {
	if (auto lockedAT = activeTextbox.lock()) {
		lockedAT->text(a_event);
	} else if (a_event.type == SDL_KEYDOWN) {
		EditorPanel::handleInput(a_event);
		auto offsetPosition = controls->elementToEdit->owner()->localFromScreen(panel.services().get<MV::TapDevice>()->position());
		auto originalMouseLocalPosition = offsetPosition;
		auto ourPoints = controls->elementToEdit->getPoints();
		auto shortestDistance = std::numeric_limits<MV::PointPrecision>().max();
		float minimumDistance = 10.0f;
		for (auto&& point : ourPoints) {
			auto currentDistance = MV::distance(point.point(), originalMouseLocalPosition);
			if (currentDistance < shortestDistance && currentDistance < minimumDistance) {
				shortestDistance = currentDistance;
				offsetPosition = point.point();
			}
		}

		MV::Scale scale(.5f, .5f);

		if (a_event.key.keysym.sym == SDLK_p) {
			if (selectedPointIndex != -1) {
				std::set<size_t> attachedIndices;
				attachedIndices.insert(selectedPointIndex);

				selectedPointIndex = -1;
				auto pointList = controls->elementToEdit->getPoints();
				auto indexList = controls->elementToEdit->pointIndices();

				for (size_t i = 0; i <= (indexList.size() - 1) / 3; ++i) {
					bool firstFound = attachedIndices.find(indexList[i * 3]) != attachedIndices.end();
					bool secondFound = attachedIndices.find(indexList[i * 3 + 1]) != attachedIndices.end();
					bool thirdFound = attachedIndices.find(indexList[i * 3 + 2]) != attachedIndices.end();
					
					bool allFound = firstFound && secondFound && thirdFound;

					if (!allFound && (firstFound || secondFound || thirdFound)) {
						attachedIndices.insert(indexList[i * 3]);
						attachedIndices.insert(indexList[i * 3 + 1]);
						attachedIndices.insert(indexList[i * 3 + 2]);
						i = -1; // reset search!
					}
				}
				indexList.erase(std::remove_if(indexList.begin(), indexList.end(), [&](size_t index) {
					return attachedIndices.find(index) != attachedIndices.end();
				}), indexList.end());
				size_t currentPointIndex = 0;
				for (auto it = pointList.begin();it != pointList.end();) {
					if (attachedIndices.find(currentPointIndex++) != attachedIndices.end()) {
						it = pointList.erase(it);
					} else {
						++it;
					}
				}

				for (auto&& index : indexList) {
					int originalIndex = index;
					for (auto deletingIndex : attachedIndices) {
						if (originalIndex >= deletingIndex) {
							--index;
						}
					}
				}
				controls->elementToEdit->setPoints(pointList, indexList);
				controls->resetHandles();
			}
		} else if (a_event.key.keysym.sym == SDLK_q) {
			controls->elementToEdit->appendPoints({
				MV::DrawPoint{ MV::point(0.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.0f) },
				MV::DrawPoint{ MV::point(0.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 1.0f) },
				MV::DrawPoint{ MV::point(100.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 1.0f) },
				MV::DrawPoint{ MV::point(100.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.0f) },

				MV::DrawPoint{ MV::point(0.0f, 25.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.25f) },
				MV::DrawPoint{ MV::point(0.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.5f) },
				MV::DrawPoint{ MV::point(0.0f, 75.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.75f) },

				MV::DrawPoint{ MV::point(12.5f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.125f, 1.0f) },
				MV::DrawPoint{ MV::point(25.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.25f, 1.0f) },
				MV::DrawPoint{ MV::point(37.5f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.375f, 1.0f) },
				MV::DrawPoint{ MV::point(50.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 1.0f) },
				MV::DrawPoint{ MV::point(62.5f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.625f, 1.0f) },
				MV::DrawPoint{ MV::point(75.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.75f, 1.0f) },
				MV::DrawPoint{ MV::point(87.5f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.875f, 1.0f) },

				MV::DrawPoint{ MV::point(100.0f, 75.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.75f) },
				MV::DrawPoint{ MV::point(100.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.5f) },
				MV::DrawPoint{ MV::point(100.0f, 25.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.25f) },

				MV::DrawPoint{ MV::point(87.5f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.875f, 0.0f) },
				MV::DrawPoint{ MV::point(75.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.75f, 0.0f) },
				MV::DrawPoint{ MV::point(62.5f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.625f, 0.0f) },
				MV::DrawPoint{ MV::point(50.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 0.0f) },
				MV::DrawPoint{ MV::point(37.5f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.375f, 0.0f) },
				MV::DrawPoint{ MV::point(25.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.25f, 0.0f) },
				MV::DrawPoint{ MV::point(12.5f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.125f, 0.0f) },

				MV::DrawPoint{ MV::point(25.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.25f, 0.5f) },
				MV::DrawPoint{ MV::point(50.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 0.5f) },
				MV::DrawPoint{ MV::point(75.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.75f, 0.5f) },
			},
				{
					21, 22, 24,
					22, 23, 24,
					23, 0, 24,
					0, 4, 24,
					4, 5, 24,
					5, 6, 24,
					6, 1, 24,
					1, 7, 24,
					7, 8, 24,
					8, 9, 24,

					24, 9, 25,
					9, 10, 25,
					10, 11, 25,
					25, 11, 26,

					11, 12, 26,
					12, 13, 26,
					13, 2, 26,
					2, 14, 26,
					14, 15, 26,
					15, 16, 26,
					16, 3, 26,
					3, 17, 26,
					17, 18, 26,
					18, 19, 26,

					19, 25, 26,
					19, 20, 25,
					20, 21, 25,
					21, 24, 25
				});
			controls->resetHandles();
		} else if (a_event.key.keysym.sym == SDLK_a) {
			controls->elementToEdit->appendPoints({
				MV::DrawPoint{ MV::point(0.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.0f) },
				MV::DrawPoint{ MV::point(0.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 1.0f) },
				MV::DrawPoint{ MV::point(100.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 1.0f) },
				MV::DrawPoint{ MV::point(100.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.0f) },

				MV::DrawPoint{ MV::point(0.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.5f) },
				MV::DrawPoint{ MV::point(50.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 1.0f) },
				MV::DrawPoint{ MV::point(100.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.5f) },
				MV::DrawPoint{ MV::point(50.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 0.0f) },

				MV::DrawPoint{ MV::point(50.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 0.5f) } },
				{ 
					0, 4, 8, 8, 7, 0,
					7, 8, 6, 6, 3, 7,
					4, 1, 5, 5, 8, 4,
					8, 5, 2, 2, 6, 8
				});

			controls->resetHandles();
		} else if (a_event.key.keysym.sym == SDLK_z) {
			controls->elementToEdit->appendPoints({
				MV::DrawPoint{ MV::point(0.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.0f) },
				MV::DrawPoint{ MV::point(0.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 1.0f) },
				MV::DrawPoint{ MV::point(100.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 1.0f) },
				MV::DrawPoint{ MV::point(100.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.0f) },

				MV::DrawPoint{ MV::point(0.0f, 25.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.25f) },
				MV::DrawPoint{ MV::point(0.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.5f) },
				MV::DrawPoint{ MV::point(0.0f, 75.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.75f) },

				MV::DrawPoint{ MV::point(50.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 1.0f) },

				MV::DrawPoint{ MV::point(100.0f, 75.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.75f) },
				MV::DrawPoint{ MV::point(100.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.5f) },
				MV::DrawPoint{ MV::point(100.0f, 25.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.25f) },

				MV::DrawPoint{ MV::point(50.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 0.0f) },

				MV::DrawPoint{ MV::point(50.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 0.5f) } },
				{
					0, 4, 12, 12, 11, 0,
					4, 5, 12, 5, 6, 12,
					6, 1, 7, 7, 12, 6,
					11, 12, 10, 10, 3, 11,
					12, 9, 10, 12, 8, 9,
					12, 7, 2, 2, 8, 12
				});

			controls->resetHandles();
		} else if (a_event.key.keysym.sym == SDLK_s) {
			controls->elementToEdit->appendPoints({
				MV::DrawPoint{ MV::point(0.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.0f) },
				MV::DrawPoint{ MV::point(-75.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 1.0f) },
				MV::DrawPoint{ MV::point(175.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 1.0f) },
				MV::DrawPoint{ MV::point(100.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.0f) },

				MV::DrawPoint{ MV::point(0.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.5f) },   //4
				MV::DrawPoint{ MV::point(25.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 1.0f) }, //5
				MV::DrawPoint{ MV::point(75.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 1.0f) }, //6
				MV::DrawPoint{ MV::point(100.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.5f) }, //7
				MV::DrawPoint{ MV::point(50.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 0.0f) },   //8

				MV::DrawPoint{ MV::point(50.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.5f) },  //9
				MV::DrawPoint{ MV::point(50.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.5f) },  //10

				MV::DrawPoint{ MV::point(50.0f, 25.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 0.25f) }, //11

				MV::DrawPoint{ MV::point(25.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 0.5f) },  //12
				MV::DrawPoint{ MV::point(-25.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 1.0f) },//13

				MV::DrawPoint{ MV::point(75.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 0.5f) },  //14
				MV::DrawPoint{ MV::point(125.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 1.0f) },//15
			},
				{
					0, 4, 11, 11, 8, 0,
					8, 11, 7, 7, 3, 8,
					11, 4, 12, 12, 9, 11,
					11, 10, 14, 14, 7, 11,
					4, 1, 13, 13, 12, 4,
					12, 13, 5, 5, 9, 12,
					10, 6, 15, 15, 14, 10,
					14, 15, 2, 2, 7, 14
				});

			controls->resetHandles();
		} else if (a_event.key.keysym.sym == SDLK_x) {
			controls->elementToEdit->appendPoints({
				MV::DrawPoint{ MV::point(0.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.0f) },
				MV::DrawPoint{ MV::point(-75.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 1.0f) },
				MV::DrawPoint{ MV::point(175.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 1.0f) },
				MV::DrawPoint{ MV::point(100.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.0f) },

				MV::DrawPoint{ MV::point(0.0f, 25.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.25f) },   //4
				MV::DrawPoint{ MV::point(0.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.5f) },    //5
				MV::DrawPoint{ MV::point(-37.5f, 75.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.75f) }, //6

				MV::DrawPoint{ MV::point(-25.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 1.0f) }, //7
				MV::DrawPoint{ MV::point(25.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 1.0f) },  //8

				MV::DrawPoint{ MV::point(37.5f, 75.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.75f) },  //9

				MV::DrawPoint{ MV::point(50.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.5f) },   //10
				MV::DrawPoint{ MV::point(50.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.5f) },   //11

				MV::DrawPoint{ MV::point(62.5f, 75.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 0.75f) },   //12

				MV::DrawPoint{ MV::point(75.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.0f, 1.0f) },  //13
				MV::DrawPoint{ MV::point(125.0f, 100.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 1.0f) }, //14

				MV::DrawPoint{ MV::point(137.5f, 75.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.75f) }, //15
				MV::DrawPoint{ MV::point(100.0f, 50.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.5f) },  //16
				MV::DrawPoint{ MV::point(100.0f, 25.0f) * scale + offsetPosition, MV::TexturePoint(1.0f, 0.25f) }, //17

				MV::DrawPoint{ MV::point(50.0f, 0.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 0.0f) },    //18
				MV::DrawPoint{ MV::point(50.0f, 25.0f) * scale + offsetPosition, MV::TexturePoint(0.5f, 0.25f) },  //19
			},
			{
				0, 4, 19, 19, 18, 0,
				18, 19, 17, 17, 3, 18,
				4, 5, 10, 10, 19, 4,
				19, 11, 16, 16, 17, 19,
				5, 6, 9, 9, 10, 5,
				6, 1, 7, 6, 7, 9, 7, 8, 9,
				11, 12, 15, 15, 16, 11,
				12, 13, 14, 12, 14, 15, 14, 2, 15
			});

			controls->resetHandles();
		}
	}
}

void SelectedDrawableEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->resetHandles();
}

void SelectedDrawableEditorPanel::onSceneZoom() {
	controls->resetHandles();
}

std::shared_ptr<MV::Scene::Component> SelectedDrawableEditorPanel::getEditingComponent() {
	return std::static_pointer_cast<MV::Scene::Component>(controls->elementToEdit.self());
}

SelectedGridEditorPanel::SelectedGridEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableGrid> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton) :
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();

	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);
	auto deselectButton = makeButton(grid, panel.services(), "Deselect", buttonSize, U8_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, panel.services(), "Delete", buttonSize, U8_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	makeInputField(this, panel.services(), grid, "Name", buttonSize)->
		text(controls->elementToEdit->id())->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->id(a_text->text());
		renameButton(a_associatedButton->safe(), std::string("G: ") + a_text->text());
		onNameChangeSignal(a_text->text());
	});

	float textboxWidth = 52.0f;
	makeLabel(grid, panel.services(), "Width/Columns", buttonSize, U8_STR("Width|Column"));
	width = makeInputField(this, panel.services(), grid, "width", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->elementToEdit->gridWidth())));
	columns = makeInputField(this, panel.services(), grid, "columns", MV::size(textboxWidth, 27.0f), std::to_string(a_controls->elementToEdit->columns()));

	makeLabel(grid, panel.services(), "Padding", buttonSize, U8_STR("Padding X|Y"));
	paddingX = makeInputField(this, panel.services(), grid, "paddingX", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->elementToEdit->padding().first.x)));
	paddingY = makeInputField(this, panel.services(), grid, "paddingY", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->elementToEdit->padding().first.y)));

	makeLabel(grid, panel.services(), "Margins", buttonSize, U8_STR("Margins X|Y"));
	marginsX = makeInputField(this, panel.services(), grid, "marginsX", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->elementToEdit->margin().first.x)));
	marginsY = makeInputField(this, panel.services(), grid, "marginsY", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->elementToEdit->margin().first.y)));

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

	auto anchorsButton = makeButton(grid, panel.services(), "Anchors", buttonSize, U8_STR("Anchors"));
	anchorsButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) mutable {
		openAnchorEditor(controls->elementToEdit.self());
	});

	panel.updateBoxHeader(grid->bounds().width());
}

void SelectedGridEditorPanel::handleInput(SDL_Event &a_event) {
	EditorPanel::handleInput(a_event);
}

void SelectedGridEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->resetHandles();
}

void SelectedGridEditorPanel::onSceneZoom() {
	controls->resetHandles();
}

std::shared_ptr<MV::Scene::Component> SelectedGridEditorPanel::getEditingComponent() {
	return std::static_pointer_cast<MV::Scene::Component>(controls->elementToEdit.self());
}

SelectedSpineEditorPanel::SelectedSpineEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableSpine> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton) :
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();

	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);
	auto deselectButton = makeButton(grid, panel.services(), "Deselect", buttonSize, U8_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, panel.services(), "Delete", buttonSize, U8_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	makeInputField(this, panel.services(), grid, "Name", buttonSize)->
		text(controls->elementToEdit->id())->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->id(a_text->text());
		renameButton(a_associatedButton->safe(), std::string("G: ") + a_text->text());
		onNameChangeSignal(a_text->text());
	});

	float textboxWidth = 52.0f;
	makeLabel(grid, panel.services(), "Json", buttonSize, U8_STR("Json"));
	assetJson = makeInputField(this, panel.services(), grid, "json", buttonSize, a_controls->elementToEdit->bundle().skeletonFile);
	makeLabel(grid, panel.services(), "Atlas", buttonSize, U8_STR("Atlas"));
	assetAtlas = makeInputField(this, panel.services(), grid, "atlas", buttonSize, a_controls->elementToEdit->bundle().atlasFile);
	
	makeLabel(grid, panel.services(), "Scale", buttonSize, U8_STR("Scale"));
	scale = makeInputField(this, panel.services(), grid, "scale", buttonSize, std::to_string(a_controls->elementToEdit->bundle().loadScale));

	makeLabel(grid, panel.services(), "Animation", buttonSize, U8_STR("Animation"));
	animationPreview = makeInputField(this, panel.services(), grid, "preview", buttonSize, U8_STR(""));

	auto addAttachment = makeButton(grid, panel.services(), "Add", buttonSize, U8_STR("Add Binding"));
	std::weak_ptr<MV::Scene::Node> weakGrid = grid;
	addAttachment->onAccept.connect("click", [&, weakGrid](std::shared_ptr<MV::Scene::Clickable>) { handleMakeButton(weakGrid.lock()); });

	auto clearAttachments = makeButton(grid, panel.services(), "Clear", buttonSize, U8_STR("Clear Bindings"));
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
			if (MV::fileExistsAbsolute(assetJson->text()) && MV::fileExistsAbsolute(assetAtlas->text())) {
				try { controls->elementToEdit->load({ assetJson->text(), assetAtlas->text(), scale->number() }); }
				catch (...) {}
			}
		};
		auto bundleChangeClick = [&](std::shared_ptr<MV::Scene::Clickable>) {
			if (MV::fileExistsAbsolute(assetJson->text()) && MV::fileExistsAbsolute(assetAtlas->text())) {
				try { controls->elementToEdit->load({ assetJson->text(), assetAtlas->text(), scale->number() }); }
				catch (...) {}
			}
		};

		animationPreview->onEnter.connect("animate", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->animate(animationPreview->text());
		});
		animationPreview->owner()->component<MV::Scene::Clickable>()->onAccept.connect("animate", [&](std::shared_ptr<MV::Scene::Clickable>) {
			controls->elementToEdit->animate(animationPreview->text());
		});

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
}

void SelectedSpineEditorPanel::handleMakeButton(std::shared_ptr<MV::Scene::Node> a_grid, const std::string &a_socket, const std::string &a_node) {
	if (controls) {
		float textboxWidth = 52.0f;
		auto socketText = makeInputField(this, panel.services(), a_grid, MV::guid("json"), MV::size(textboxWidth, 27.0f), a_socket);
		auto nodeText = makeInputField(this, panel.services(), a_grid, MV::guid("atlas"), MV::size(textboxWidth, 27.0f), a_node);
		linkedSockets.push_back(socketText);
		linkedNodes.push_back(nodeText);

		auto nodeConnectionChange = [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->elementToEdit->unbindAll();
			for (size_t i = 0; i < linkedSockets.size(); ++i) {
				controls->elementToEdit->bindNode(linkedSockets[i]->text(), linkedNodes[i]->text());
			}
		};
		auto nodeConnectionChangeClick = [&](std::shared_ptr<MV::Scene::Clickable>) {
			controls->elementToEdit->unbindAll();
			for (size_t i = 0; i < linkedSockets.size(); ++i) {
				controls->elementToEdit->bindNode(linkedSockets[i]->text(), linkedNodes[i]->text());
			}
		};

		socketText->onEnter.connect("changeNode", nodeConnectionChange);
		socketText->owner()->component<MV::Scene::Clickable>()->onAccept.connect("changeNode", nodeConnectionChangeClick);
		nodeText->onEnter.connect("changeNode", nodeConnectionChange);
		nodeText->owner()->component<MV::Scene::Clickable>()->onAccept.connect("changeNode", nodeConnectionChangeClick);
	}
}

void SelectedSpineEditorPanel::handleInput(SDL_Event &a_event) {
	EditorPanel::handleInput(a_event);
}

void SelectedSpineEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->resetHandles();
}

void SelectedSpineEditorPanel::onSceneZoom() {
	controls->resetHandles();
}

std::shared_ptr<MV::Scene::Component> SelectedSpineEditorPanel::getEditingComponent() {
	return std::static_pointer_cast<MV::Scene::Component>(controls->elementToEdit.self());
}

SelectedRectangleEditorPanel::SelectedRectangleEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableRectangle> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton):
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({BOX_BACKGROUND})->margin({4.0f, 4.0f})->
		padding({2.0f, 2.0f})->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);
	auto deselectButton = makeButton(grid, panel.services(), "Deselect", buttonSize, U8_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, panel.services(), "Delete", buttonSize, U8_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	openTexturePicker();

	std::weak_ptr<EditableRectangle> weakControls = controls;
	makeColorButton(grid, panel.content(), *panel.services().get<MV::TextLibrary>(), *panel.services().get<MV::TapDevice>(), buttonSize, controls->elementToEdit->color(), [=](const MV::Color &a_color) {
		weakControls.lock()->elementToEdit->color(a_color);
	});

	makeInputField(this, panel.services(), grid, "Name", buttonSize)->
		text(controls->elementToEdit->id())->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text){
			controls->elementToEdit->id(a_text->text());
			onNameChangeSignal(controls->elementToEdit->id());
			if (a_associatedButton) {
				renameButton(a_associatedButton->safe(), std::string("S: ") + a_text->text());
			}
		});

	float textboxWidth = 52.0f;
	offsetX = makeInputField(this, panel.services(), grid, "offsetX", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->position().x)));
	offsetY = makeInputField(this, panel.services(), grid, "offsetY", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->position().y)));

	width = makeInputField(this, panel.services(), grid, "width", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->size().width)));
	height = makeInputField(this, panel.services(), grid, "height", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->size().height)));

	makeButton(grid, panel.services(), "Texture", buttonSize, U8_STR("Texture"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>){
			openTexturePicker();
		});

	aspectX = makeInputField(this, panel.services(), grid, "AspectX", MV::size(textboxWidth, 27.0f));
	aspectY = makeInputField(this, panel.services(), grid, "AspectY", MV::size(textboxWidth, 27.0f));

	makeButton(grid, panel.services(), "SetAspect", buttonSize, U8_STR("Snap Aspect"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>){
			if (controls->elementToEdit->texture()) {
				auto size = controls->elementToEdit->texture()->bounds().size();
				aspectX->set(size.width);
				aspectY->set(size.height);
				controls->aspect(MV::round<MV::PointPrecision>(size));
			}
		});

	makeButton(grid, panel.services(), "SetSize", buttonSize, U8_STR("Snap Size"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>){
			if (controls->elementToEdit->texture()) {
				auto size = controls->elementToEdit->texture()->bounds().size();
				width->set(size.width);
				height->set(size.width);
				controls->size(MV::round<MV::PointPrecision>(size));
			}
		});

	makeButton(grid, panel.services(), "FlipX", buttonSize, U8_STR("Flip X"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>) {
			if (controls->elementToEdit->texture()) {
				controls->elementToEdit->texture()->flipX(!controls->elementToEdit->texture()->flipX());
			}
		});

	makeButton(grid, panel.services(), "FlipY", buttonSize, U8_STR("Flip Y"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>) {
			if (controls->elementToEdit->texture()) {
				controls->elementToEdit->texture()->flipY(!controls->elementToEdit->texture()->flipY());
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

		controls->onChange = [&](ResizeHandles *a_element){
			offsetX->set(static_cast<int>(std::lround(controls->position().x)));
			offsetY->set(static_cast<int>(std::lround(controls->position().y)));

			width->set(static_cast<int>(std::lround(controls->size().width)));
			height->set(static_cast<int>(std::lround(controls->size().height)));
		};
	}
	auto deselectLocalAABB = deselectButton->bounds();

	auto anchorsButton = makeButton(grid, panel.services(), "Anchors", buttonSize, U8_STR("Anchors"));
	anchorsButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) mutable {
		openAnchorEditor(controls->elementToEditBase.self());
	});

	makeInputField(this, panel.services(), grid, "Shader", buttonSize)->
		text(controls->elementToEdit->shader())->
		onEnter.connect("shader", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->shader(a_text->text());
	});

	makeInputField(this, panel.services(), grid, "BlendMode", buttonSize)->
		text(std::to_string(static_cast<int>(controls->elementToEdit->blend())))->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
		try {
			controls->elementToEdit->blend(static_cast<MV::Scene::Drawable::BlendModePreset>(std::stoi(a_text->text())));
		} catch (std::invalid_argument&) {} catch (std::out_of_range&) {}
	});

	makeLabel(grid, panel.services(), "subDivideLabel", MV::size(textboxWidth, 23.0f), U8_STR("Subdivisions"));
	subdivided = makeInputField(this, panel.services(), grid, "Subdivided", MV::size(textboxWidth, 27.0f))->
		text(std::to_string(controls->elementToEdit->subdivisions()));
	auto subClick = subdivided->owner()->component<MV::Scene::Clickable>();
	subClick->onAccept.connect("updateSub", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
		controls->elementToEdit->subdivide(std::stoi(subdivided->text()));
	});
	subdivided->onEnter.connect("updateSub", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
		controls->elementToEdit->subdivide(std::stoi(subdivided->text()));
	});

	panel.updateBoxHeader(grid->bounds().width());
}

void SelectedRectangleEditorPanel::openTexturePicker(size_t a_textureId) {
	clearTexturePicker();
	picker = std::make_shared<TexturePicker>(panel.editor(), panel.services(), [&, a_textureId](std::shared_ptr<MV::TextureHandle> a_handle, bool a_allowClear){
		if(a_handle || a_allowClear){
			controls->elementToEdit->texture(a_handle, a_textureId);
		}
		clearTexturePicker();
	});
}

void SelectedRectangleEditorPanel::handleInput(SDL_Event &a_event) {
	EditorPanel::handleInput(a_event);
}

void SelectedRectangleEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->repositionHandles(true, true, false);
}

void SelectedRectangleEditorPanel::onSceneZoom() {
	controls->resetHandles();
}

std::shared_ptr<MV::Scene::Component> SelectedRectangleEditorPanel::getEditingComponent() {
	return std::static_pointer_cast<MV::Scene::Component>(controls->elementToEdit.self());
}

SelectedEmitterEditorPanel::SelectedEmitterEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableEmitter> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton):
	EditorPanel(a_panel),
	controls(a_controls) {

	std::weak_ptr<EditableEmitter> weakControls = controls;
	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(232.0f)->
		color({InterfaceColors::BOX_BACKGROUND})->margin({4.0f, 4.0f})->
		padding({2.0f, 2.0f})->owner();
	auto buttonSize = MV::size(226.0f, 27.0f);
	auto deselectButton = makeButton(grid, panel.services(), "Deselect", buttonSize, U8_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, panel.services(), "Delete", buttonSize, U8_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	makeInputField(this, panel.services(), grid, "Name", buttonSize)->
		text(controls->elementToEdit->id())->
		onEnter.connect("rename", [&, a_associatedButton](const std::shared_ptr<MV::Scene::Text> &a_text){
			controls->elementToEdit->id(a_text->text());
			onNameChangeSignal(controls->elementToEdit->id());
			renameButton(a_associatedButton->safe(), std::string("E: ") + a_text->text());
		});

	float textboxWidth = 52.0f;

	offsetX = makeInputField(this, panel.services(), grid, "offsetX", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->position().x)));
	offsetY = makeInputField(this, panel.services(), grid, "offsetY", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->position().y)));

	width = makeInputField(this, panel.services(), grid, "width", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->size().width)));
	height = makeInputField(this, panel.services(), grid, "height", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(a_controls->size().height)));

	makeButton(grid, panel.services(), "Texture", buttonSize, U8_STR("Texture"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>){
			openTexturePicker();
		});

	const MV::Size<> labelSize{226.0f, 20.0f};

	makeLabel(grid, panel.services(), "spawnRate", labelSize, U8_STR("Spawn Rate"));
	auto maximumSpawnRate = makeSlider(node->renderer(), *panel.services().get<MV::TapDevice>(), [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximumSpawnRate = MV::mixIn(0.0f, 1.25f, a_slider->percent(), 2);
	}, MV::unmixIn(0.0f, 1.25f, controls->elementToEdit->properties().maximumSpawnRate, 2));
	makeSlider(panel.services(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimumSpawnRate = MV::mixIn(0.0f, 1.25f, a_slider->percent(), 2);
		maximumSpawnRate->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmixIn(0.0f, 1.25f, controls->elementToEdit->properties().minimumSpawnRate, 2));
	grid->add(maximumSpawnRate);

	makeLabel(grid, panel.services(), "lifeSpan", labelSize, U8_STR("Lifespan"));
	auto maximumLifespan = makeSlider(node->renderer(), *panel.services().get<MV::TapDevice>(), [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.maxLifespan = MV::mixIn(0.01f, 60.0f, a_slider->percent(), 2);
	}, MV::unmixIn(0.01f, 60.0f, controls->elementToEdit->properties().maximum.maxLifespan, 2));
	makeSlider(panel.services(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.maxLifespan = MV::mixIn(0.01f, 60.0f, a_slider->percent(), 2);
		maximumLifespan->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmixIn(0.01f, 60.0f, controls->elementToEdit->properties().minimum.maxLifespan, 2));
	grid->add(maximumLifespan);

	float maxSpeedFloat = 200.0f;
	auto maximumEndSpeed = makeSlider(panel.services(), [&, maxSpeedFloat](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endSpeed = MV::mixInOut(-maxSpeedFloat, maxSpeedFloat, a_slider->percent(), 2);
	}, MV::unmixInOut(-maxSpeedFloat, maxSpeedFloat, controls->elementToEdit->properties().maximum.endSpeed, 2));
	auto minimumEndSpeed = makeSlider(panel.services(), [&, maxSpeedFloat,maximumEndSpeed](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endSpeed = MV::mixInOut(-maxSpeedFloat, maxSpeedFloat, a_slider->percent(), 2);
		maximumEndSpeed->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmixInOut(-maxSpeedFloat, maxSpeedFloat, controls->elementToEdit->properties().minimum.endSpeed, 2));

	makeLabel(grid, panel.services(), "initialSpeed", labelSize, U8_STR("Start Speed"));
	auto startSpeed = makeSlider(panel.services(), [&, maximumEndSpeed,maxSpeedFloat](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginSpeed = MV::mixInOut(-maxSpeedFloat, maxSpeedFloat, a_slider->percent(), 2);
		maximumEndSpeed->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmixInOut(-maxSpeedFloat, maxSpeedFloat, controls->elementToEdit->properties().maximum.beginSpeed, 2));
	makeSlider(panel.services(), grid, [&, maxSpeedFloat, minimumEndSpeed, startSpeed](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginSpeed = MV::mixInOut(-maxSpeedFloat, maxSpeedFloat, a_slider->percent(), 2);
		startSpeed->component<MV::Scene::Slider>()->percent(a_slider->percent());
		minimumEndSpeed->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmixInOut(-maxSpeedFloat, maxSpeedFloat, controls->elementToEdit->properties().minimum.beginSpeed, 2));
	grid->add(startSpeed);

	makeLabel(grid, panel.services(), "speedChange", labelSize, U8_STR("End Speed"));
	grid->add(minimumEndSpeed);
	grid->add(maximumEndSpeed);

	makeLabel(grid, panel.services(), "initialDirection", labelSize, U8_STR("Start Direction"));

	auto maximumStartTilt = makeSlider(panel.services(), [&](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().maximumDirectionDeg({ a_slider->percent() * 360.0f, controls->elementToEdit->properties().maximumDirectionDeg().y, controls->elementToEdit->properties().maximumDirectionDeg().z });
	}, controls->elementToEdit->properties().maximumDirectionDeg().x / 360.0f);
	makeSlider(panel.services(), grid, [&, maximumStartTilt](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().minimumDirectionDeg({ a_slider->percent() * 360.0f, controls->elementToEdit->properties().minimumDirectionDeg().y, controls->elementToEdit->properties().minimumDirectionDeg().z });
		maximumStartTilt->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimumDirectionDeg().x / 360.0f);
	grid->add(maximumStartTilt);

	auto maximumStartPan = makeSlider(panel.services(), [&](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().maximumDirectionDeg({ controls->elementToEdit->properties().maximumDirectionDeg().x, a_slider->percent() * 360.0f, controls->elementToEdit->properties().maximumDirectionDeg().z });
	}, controls->elementToEdit->properties().maximumDirectionDeg().y / 360.0f);
	makeSlider(panel.services(), grid, [&, maximumStartPan](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().minimumDirectionDeg({ controls->elementToEdit->properties().minimumDirectionDeg().x, a_slider->percent() * 360.0f, controls->elementToEdit->properties().minimumDirectionDeg().z });
		maximumStartPan->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimumDirectionDeg().y / 360.0f);
	grid->add(maximumStartPan);

	auto maximumStartDirection = makeSlider(panel.services(), [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximumDirectionDeg({ controls->elementToEdit->properties().maximumDirectionDeg().x, controls->elementToEdit->properties().maximumDirectionDeg().y, a_slider->percent() * 360.0f});
	}, controls->elementToEdit->properties().maximumDirectionDeg().z / 360.0f);
	makeSlider(panel.services(), grid, [&, maximumStartDirection](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimumDirectionDeg({ controls->elementToEdit->properties().minimumDirectionDeg().x, controls->elementToEdit->properties().minimumDirectionDeg().y, a_slider->percent() * 360.0f});
		maximumStartDirection->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimumDirectionDeg().z / 360.0f);
	grid->add(maximumStartDirection);

	makeLabel(grid, panel.services(), "directionChange", labelSize, U8_STR("Direction Change"));

	auto maximumDirectionChangeTilt = makeSlider(node->renderer(), *panel.services().get<MV::TapDevice>(), [&](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().maximum.directionalChangeDeg({ MV::mix(-720.0f, 720.0f, a_slider->percent()), controls->elementToEdit->properties().maximum.directionalChangeDeg().y, controls->elementToEdit->properties().maximum.directionalChangeDeg().z });
	}, MV::unmix(-720.0f, 720.0f, controls->elementToEdit->properties().maximum.directionalChangeDeg().x));
	makeSlider(panel.services(), grid, [&, maximumDirectionChangeTilt](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().minimum.directionalChangeDeg({ MV::mix(-720.0f, 720.0f, a_slider->percent()), controls->elementToEdit->properties().minimum.directionalChangeDeg().y, controls->elementToEdit->properties().minimum.directionalChangeDeg().z });
		maximumDirectionChangeTilt->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-720.0f, 720.0f, controls->elementToEdit->properties().minimum.directionalChangeDeg().x));
	grid->add(maximumDirectionChangeTilt);

	auto maximumDirectionChangePan = makeSlider(panel.services(), [&](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().maximum.directionalChangeDeg({ controls->elementToEdit->properties().maximum.directionalChangeDeg().x, MV::mix(-720.0f, 720.0f, a_slider->percent()), controls->elementToEdit->properties().maximum.directionalChangeDeg().z });
	}, MV::unmix(-720.0f, 720.0f, controls->elementToEdit->properties().maximum.directionalChangeDeg().y));
	makeSlider(panel.services(), grid, [&, maximumDirectionChangePan](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().minimum.directionalChangeDeg({ controls->elementToEdit->properties().minimum.directionalChangeDeg().x, MV::mix(-720.0f, 720.0f, a_slider->percent()), controls->elementToEdit->properties().minimum.directionalChangeDeg().z });
		maximumDirectionChangePan->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-720.0f, 720.0f, controls->elementToEdit->properties().minimum.directionalChangeDeg().y));
	grid->add(maximumDirectionChangePan);

	auto maximumDirectionChange = makeSlider(panel.services(), [&](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().maximum.directionalChangeDeg({ controls->elementToEdit->properties().maximum.directionalChangeDeg().x, controls->elementToEdit->properties().maximum.directionalChangeDeg().y, MV::mix(-720.0f, 720.0f, a_slider->percent()) });
	}, MV::unmix(-720.0f, 720.0f, controls->elementToEdit->properties().maximum.directionalChangeDeg().z));
	makeSlider(panel.services(), grid, [&, maximumDirectionChange](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().minimum.directionalChangeDeg({ controls->elementToEdit->properties().minimum.directionalChangeDeg().x, controls->elementToEdit->properties().minimum.directionalChangeDeg().y, MV::mix(-720.0f, 720.0f, a_slider->percent()) });
		maximumDirectionChange->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-720.0f, 720.0f, controls->elementToEdit->properties().minimum.directionalChangeDeg().z));
	grid->add(maximumDirectionChange);

	makeLabel(grid, panel.services(), "rateOfChange", labelSize, U8_STR("Rate Of Change"));
	auto maximumRateOfTilt = makeSlider(node->renderer(), *panel.services().get<MV::TapDevice>(), [&](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().maximum.rateOfChangeDeg({ MV::mix(-1480.0f, 1480.0f, a_slider->percent()), controls->elementToEdit->properties().maximum.rateOfChangeDeg().y, controls->elementToEdit->properties().maximum.rateOfChangeDeg().z });
	}, MV::unmix(-1480.0f, 1480.0f, controls->elementToEdit->properties().maximum.rateOfChangeDeg().x));
	makeSlider(panel.services(), grid, [&, maximumRateOfTilt](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().minimum.rateOfChangeDeg({ MV::mix(-1480.0f, 1480.0f, a_slider->percent()), controls->elementToEdit->properties().minimum.rateOfChangeDeg().y, controls->elementToEdit->properties().minimum.rateOfChangeDeg().z });
		maximumRateOfTilt->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-1480.0f, 1480.0f, controls->elementToEdit->properties().minimum.rateOfChangeDeg().x));
	grid->add(maximumRateOfTilt);

	auto maximumRateOfPan = makeSlider(panel.services(), [&](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().maximum.rateOfChangeDeg({ controls->elementToEdit->properties().maximum.rateOfChangeDeg().x, MV::mix(-1480.0f, 1480.0f, a_slider->percent()), controls->elementToEdit->properties().maximum.rateOfChangeDeg().z });
	}, MV::unmix(-1480.0f, 1480.0f, controls->elementToEdit->properties().maximum.rateOfChangeDeg().y));
	makeSlider(panel.services(), grid, [&, maximumRateOfPan](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().minimum.rateOfChangeDeg({ controls->elementToEdit->properties().maximum.rateOfChangeDeg().x, MV::mix(-1480.0f, 1480.0f, a_slider->percent()),controls->elementToEdit->properties().minimum.rateOfChangeDeg().z });
		maximumRateOfPan->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-1480.0f, 1480.0f, controls->elementToEdit->properties().minimum.rateOfChangeDeg().y));
	grid->add(maximumRateOfPan);

	auto maximumRateOfChange = makeSlider(panel.services(), [&](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().maximum.rateOfChangeDeg({ controls->elementToEdit->properties().maximum.rateOfChangeDeg().x, controls->elementToEdit->properties().maximum.rateOfChangeDeg().y, MV::mix(-1480.0f, 1480.0f, a_slider->percent()) });
	}, MV::unmix(-1480.0f, 1480.0f, controls->elementToEdit->properties().maximum.rateOfChangeDeg().z));
	makeSlider(panel.services(), grid, [&, maximumRateOfChange](std::shared_ptr<MV::Scene::Slider> a_slider) {
		controls->elementToEdit->properties().minimum.rateOfChangeDeg({ controls->elementToEdit->properties().minimum.rateOfChangeDeg().x, controls->elementToEdit->properties().minimum.rateOfChangeDeg().y, MV::mix(-1480.0f, 1480.0f, a_slider->percent()) });
		maximumRateOfChange->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-1480.0f, 1480.0f, controls->elementToEdit->properties().minimum.rateOfChangeDeg().z));
	grid->add(maximumRateOfChange);

	auto maximumEndSize = makeSlider(panel.services(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endScale = MV::mix(-60.0f, 60.0f, a_slider->percent());
	}, MV::unmix(-60.0f, 60.0f, controls->elementToEdit->properties().maximum.endScale.x));
	auto minimumEndSize = makeSlider(panel.services(), [&, maximumEndSize](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endScale = MV::mix(-60.0f, 60.0f, a_slider->percent());
		maximumEndSize->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-60.0f, 60.0f, controls->elementToEdit->properties().minimum.endScale.x));

	makeLabel(grid, panel.services(), "startSize", labelSize, U8_STR("Start Size"));
	auto startSize = makeSlider(node->renderer(), *panel.services().get<MV::TapDevice>(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginScale = MV::mix(-60.0f, 60.0f, a_slider->percent());
		maximumEndSize->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-60.0f, 60.0f, controls->elementToEdit->properties().maximum.beginScale.x));
	makeSlider(panel.services(), grid, [&, minimumEndSize, startSize](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginScale = MV::mix(-60.0f, 60.0f, a_slider->percent());
		startSize->component<MV::Scene::Slider>()->percent(a_slider->percent());
		minimumEndSize->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-60.0f, 60.0f, controls->elementToEdit->properties().minimum.beginScale.x));
	grid->add(startSize);
	
	makeLabel(grid, panel.services(), "endSize", labelSize, U8_STR("End Size"));
	grid->add(minimumEndSize);
	grid->add(maximumEndSize);

	makeLabel(grid, panel.services(), "initialRotation", labelSize, U8_STR("Initialize Rotation"));
	auto maximumRotation = makeSlider(panel.services(), [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximumRotation = {0.0f, 0.0f, a_slider->percent() * 360.0f};
	}, controls->elementToEdit->properties().maximumRotation.z / 360.0f);
	makeSlider(*panel.services().get<MV::TapDevice>(), grid, [&, maximumRotation](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimumRotation = {0.0f, 0.0f, a_slider->percent() * 360.0f};
		maximumRotation->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, controls->elementToEdit->properties().minimumRotation.z / 360.0f);
	grid->add(maximumRotation);

	makeLabel(grid, panel.services(), "rotationChange", labelSize, U8_STR("Rotation Change"));
	auto maximumRotationChange = makeSlider(node->renderer(), *panel.services().get<MV::TapDevice>(), [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.rotationalChange = {0.0f, 0.0f, MV::mix(-720.0f, 720.0f, a_slider->percent())};
	}, MV::unmix(-720.0f, 720.0f, controls->elementToEdit->properties().maximum.rotationalChange.z));
	makeSlider(panel.services(), grid, [&, maximumRotationChange](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.rotationalChange = {0.0f, 0.0f, MV::mix(-720.0f, 720.0f, a_slider->percent())};
		maximumRotationChange->component<MV::Scene::Slider>()->percent(a_slider->percent());
	}, MV::unmix(-720.0f, 720.0f, controls->elementToEdit->properties().minimum.rotationalChange.z));
	grid->add(maximumRotationChange);

	auto maxEndColor = makeColorButton(panel.services(), panel.content(), buttonSize, controls->elementToEdit->properties().maximum.endColor, [&](const MV::Color &a_color) {
		controls->elementToEdit->properties().maximum.endColor = a_color;
	}, U8_STR("End Max"));

	auto maxStartColor = makeColorButton(panel.services(), panel.content(), buttonSize, controls->elementToEdit->properties().maximum.beginColor, [&, maxEndColor](const MV::Color &a_color) {
		controls->elementToEdit->properties().maximum.beginColor = a_color;
		controls->elementToEdit->properties().maximum.endColor = a_color;
		applyColorToColorButton(maxEndColor.self(), a_color);
	}, U8_STR("Start Max"));

	auto minEndColor = makeColorButton(panel.services(), panel.content(), buttonSize, controls->elementToEdit->properties().minimum.endColor, [&, maxEndColor](const MV::Color &a_color) {
		controls->elementToEdit->properties().minimum.endColor = a_color;
		controls->elementToEdit->properties().maximum.endColor = a_color;
		applyColorToColorButton(maxEndColor.self(), a_color);
	}, U8_STR("End Min"));

	auto minStartColor = makeColorButton(panel.services(), panel.content(), buttonSize, controls->elementToEdit->properties().minimum.beginColor, [&, maxEndColor, minEndColor, maxStartColor](const MV::Color &a_color) {
		controls->elementToEdit->properties().minimum.beginColor = a_color;
		controls->elementToEdit->properties().maximum.beginColor = a_color;
		controls->elementToEdit->properties().maximum.endColor = a_color;
		controls->elementToEdit->properties().minimum.endColor = a_color;
		applyColorToColorButton(maxEndColor.self(), a_color);
		applyColorToColorButton(minEndColor.self(), a_color);
		applyColorToColorButton(maxStartColor.self(), a_color);
	}, U8_STR("Start Min"));

	makeInputField(this, panel.services(), grid, "Shader", buttonSize)->
		text(std::to_string(static_cast<int>(controls->elementToEdit->blend())))->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
		try {
			controls->elementToEdit->blend(static_cast<MV::Scene::Drawable::BlendModePreset>(std::stoi(a_text->text())));
		} catch (std::invalid_argument&) {} catch (std::out_of_range&) {}
		});

	grid->add(minStartColor->owner());
	grid->add(maxStartColor->owner());

	grid->add(minEndColor->owner());
	grid->add(maxEndColor->owner());

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

	controls->onChange = [&](ResizeHandles *a_element) {
		offsetX->set(static_cast<int>(std::lround(controls->position().x)));
		offsetY->set(static_cast<int>(std::lround(controls->position().y)));

		width->set(static_cast<int>(std::lround(controls->size().width)));
		height->set(static_cast<int>(std::lround(controls->size().height)));
	};

	auto deselectLocalAABB = deselectButton->bounds();

	panel.updateBoxHeader(grid->bounds().width());
}

void SelectedEmitterEditorPanel::openTexturePicker(size_t a_textureId) {
	picker = std::make_shared<TexturePicker>(panel.editor(), panel.services(), [&, a_textureId](std::shared_ptr<MV::TextureHandle> a_handle, bool a_allowNull){
		if(a_handle || a_allowNull){
			controls->elementToEdit->texture(a_handle, a_textureId);
		}
		clearTexturePicker();
	});
}

void SelectedEmitterEditorPanel::handleInput(SDL_Event &a_event) {
	EditorPanel::handleInput(a_event);
}

void SelectedEmitterEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->repositionHandles(true, true, false);
}

void SelectedEmitterEditorPanel::onSceneZoom() {
	controls->resetHandles();
}


std::shared_ptr<MV::Scene::Component> SelectedEmitterEditorPanel::getEditingComponent() {
	return std::static_pointer_cast<MV::Scene::Component>(controls->elementToEdit.self());
}

SelectedPathMapEditorPanel::SelectedPathMapEditorPanel(EditorControls &a_panel, std::shared_ptr<EditablePathMap> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton) :
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);
	auto deselectButton = makeButton(grid, panel.services(), "Deselect", buttonSize, U8_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, panel.services(), "Delete", buttonSize, U8_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	makeInputField(this, panel.services(), grid, "Name", buttonSize)->
		text(controls->elementToEdit->id())->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->id(a_text->text());
		renameButton(a_associatedButton->safe(), std::string("G: ") + a_text->text());
		onNameChangeSignal(a_text->text());
	});

	auto controlBounds = a_controls->elementToEdit->bounds();

	float textboxWidth = 52.0f;
	width = makeInputField(this, panel.services(), grid, "width", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.width())));
	height = makeInputField(this, panel.services(), grid, "height", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.height())));

	posX = makeInputField(this, panel.services(), grid, "posX", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.minPoint.x)));
	posY = makeInputField(this, panel.services(), grid, "posY", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.minPoint.y)));

	cellsX = makeInputField(this, panel.services(), grid, "cellsX", MV::size(textboxWidth, 27.0f), std::to_string(a_controls->elementToEdit->gridSize().width));
	cellsY = makeInputField(this, panel.services(), grid, "cellsY", MV::size(textboxWidth, 27.0f), std::to_string(a_controls->elementToEdit->gridSize().height));

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

		controls->onChange = [&](ResizeHandles *a_element) {
			posX->set(static_cast<int>(std::lround(controls->position().x)));
			posY->set(static_cast<int>(std::lround(controls->position().y)));

			width->set(static_cast<int>(std::lround(controls->size().width)));
			height->set(static_cast<int>(std::lround(controls->size().height)));
		};
	}
	auto deselectLocalAABB = deselectButton->bounds();

	panel.updateBoxHeader(grid->bounds().width());
}

void SelectedPathMapEditorPanel::handleInput(SDL_Event &a_event) {
	EditorPanel::handleInput(a_event);
}

void SelectedPathMapEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->resetHandles();
}

void SelectedPathMapEditorPanel::onSceneZoom() {
	controls->resetHandles();
}

std::shared_ptr<MV::Scene::Component> SelectedPathMapEditorPanel::getEditingComponent() {
	return std::static_pointer_cast<MV::Scene::Component>(controls->elementToEdit.self());
}

DeselectedEditorPanel::DeselectedEditorPanel(EditorControls &a_panel):
	EditorPanel(a_panel) {
	auto node = panel.content();
	auto grid = node->make("grid");
	auto gridComponent = grid->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({InterfaceColors::BOX_BACKGROUND})->margin({4.0f, 4.0f})->
		padding({2.0f, 2.0f});
	auto createButton = makeButton(grid, panel.services(), "Create", MV::size(110.0f, 27.0f), U8_STR("Create"));
	fileName = makeInputField(this, panel.services(), grid, "Filename", MV::size(110.0f, 27.0f), previousFileName);
	fileName->onChange.connect("!!!", [&](std::shared_ptr<MV::Scene::Text>) {
		previousFileName = fileName->text();
	});
	auto saveButton = makeButton(grid, panel.services(), "Save", MV::size(110.0f, 27.0f), U8_STR("Save"));
	auto loadButton = makeButton(grid, panel.services(), "Load", MV::size(110.0f, 27.0f), U8_STR("Load"));
	panel.updateBoxHeader(grid->bounds().width());
	//panel.updateBoxHeader(grid->component<MV::Scene::Grid>()->bounds().width());

	createButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>){
		//panel.loadPanel<ChooseElementCreationType>();
		auto& renderer = panel.root()->renderer();
		auto node = panel.root()->make(MV::guid("node"))->screenPosition(MV::toPoint(renderer.window().size() / 2));

		auto editableNode = std::make_shared<EditableNode>(node, panel.editor(), panel.services().get<MV::TapDevice>());
		
		panel.services().get<Editor>()->sceneUpdated();
		panel.loadPanel<SelectedNodeEditorPanel>(editableNode);
	});

	saveButton->onAccept.connect("save", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.root()->save(fileName->text());
	});

	loadButton->onAccept.connect("load", [&](std::shared_ptr<MV::Scene::Clickable>){
		auto newRoot = MV::Scene::Node::load(fileName->text(), panel.services(), false);
		panel.root(newRoot);
		newRoot->postLoadStep();
		std::cout << "\n____\n";
		std::cout << "\nRecalculateLocalBounds: " << MV::Scene::Node::recalculateLocalBoundsCalls;
		std::cout << "\nRecalculateChildBounds: " << MV::Scene::Node::recalculateChildBoundsCalls;
		std::cout << "\nRecalculateMatrixBounds: " << MV::Scene::Node::recalculateMatrixCalls;
		std::cout << "\n____\n";
	});
}

void DeselectedEditorPanel::handleInput(SDL_Event &a_event) {
	if (auto lockedAT = activeTextbox.lock()) {
		lockedAT->text(a_event);
	}
	else if (a_event.type == SDL_KEYDOWN) {
		if (a_event.key.keysym.sym == SDLK_s) {
			panel.content()->renderer().reloadShaders();
		}
	}
}

std::string DeselectedEditorPanel::previousFileName = "Scenes/map.scene";

ChooseElementCreationType::ChooseElementCreationType(EditorControls &a_panel, const std::shared_ptr<MV::Scene::Node> &a_nodeToAttachTo, SelectedNodeEditorPanel *a_editorPanel):
	EditorPanel(a_panel),
	editorPanel(a_editorPanel),
	nodeToAttachTo(a_nodeToAttachTo){

	auto node = panel.content();
	auto grid = node->make("grid")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({InterfaceColors::BOX_BACKGROUND})->margin({4.0f, 4.0f})->
		padding({2.0f, 2.0f})->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);
	auto createRectangleButton = makeButton(grid, panel.services(), buttonSize, "Rectangle");
	auto createTextButton = makeButton(grid, panel.services(), buttonSize, "Text");
	auto createEmitterButton = makeButton(grid, panel.services(), buttonSize, "Emitter");
	auto createSpineButton = makeButton(grid, panel.services(), buttonSize, "Spine");
	auto createGridButton = makeButton(grid, panel.services(), buttonSize, "Grid");
	auto createPathMapButton = makeButton(grid, panel.services(), buttonSize, "PathMap");
	auto createButtonButton = makeButton(grid, panel.services(), buttonSize, "Button");
	auto createClickableButton = makeButton(grid, panel.services(), buttonSize, "Clickable");
	auto createDrawableButton = makeButton(grid, panel.services(), buttonSize, "Drawable");

	auto cancel = makeButton(grid, panel.services(), buttonSize, "Cancel");

	panel.updateBoxHeader(grid->bounds().width());

	createRectangleButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.selection().enable([&](const MV::BoxAABB<int> &a_selected){
			createRectangle(a_selected);
		});
	});

	createTextButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.selection().enable([&](const MV::BoxAABB<int> &a_selected) {
			createText(a_selected);
		});
	});

	createSpineButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>){
		createSpine();
	});

	createDrawableButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>) {
		createDrawable();
	});

	createButtonButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.selection().enable([&](const MV::BoxAABB<int> &a_selected) {
			createButton(a_selected);
		});
	});

	createClickableButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.selection().enable([&](const MV::BoxAABB<int> &a_selected) {
			createClickable(a_selected);
		});
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

void ChooseElementCreationType::createText(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();
	auto newShape = nodeToAttachTo->attach<MV::Scene::Text>(*panel.services().get<MV::TextLibrary>());
	newShape->bounds(nodeToAttachTo->localFromScreen(a_selected))->shader(MV::DEFAULT_ID)->id(MV::guid("text"));

	panel.loadPanel<SelectedTextEditorPanel>(std::make_shared<EditableText>(newShape, panel.editor(), panel.services().get<MV::TapDevice>()), editorPanel->CreateTextComponentButton(newShape).self());
}

void ChooseElementCreationType::createRectangle(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();
	auto newShape = nodeToAttachTo->attach<MV::Scene::Sprite>();
	newShape->bounds(nodeToAttachTo->localFromScreen(a_selected))->color({ CREATED_DEFAULT })->shader(MV::DEFAULT_ID)->id(MV::guid("sprite"));

	panel.loadPanel<SelectedRectangleEditorPanel>(std::make_shared<EditableRectangle>(newShape, panel.editor(), panel.services().get<MV::TapDevice>()), editorPanel->CreateSpriteComponentButton(newShape).self());
}

void ChooseElementCreationType::createPathMap(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();
	auto localBounds = nodeToAttachTo->localFromScreen(a_selected);
	auto newShape = nodeToAttachTo->attach<MV::Scene::PathMap>(MV::Size<>(10.0f, 10.0f), MV::Size<int>(std::max(static_cast<int>(localBounds.size().width / 10.0f), 1), std::max(static_cast<int>(localBounds.size().height / 10.0f), 1)));
	newShape->show();
	newShape->bounds(localBounds);
	panel.loadPanel<SelectedPathMapEditorPanel>(std::make_shared<EditablePathMap>(newShape, panel.editor(), panel.services().get<MV::TapDevice>()), editorPanel->CreatePathMapComponentButton(newShape).self());
}

void ChooseElementCreationType::createEmitter(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();

	auto newEmitter = nodeToAttachTo->attach<MV::Scene::Emitter>(*panel.services().get<MV::ThreadPool>());
	newEmitter->id(MV::guid("emitter"))->shader(MV::DEFAULT_ID);
	auto transformedSelection = nodeToAttachTo->localFromScreen(a_selected);
	newEmitter->properties().minimumPosition = transformedSelection.minPoint;
	newEmitter->properties().maximumPosition = transformedSelection.maxPoint;

	auto editableEmitter = std::make_shared<EditableEmitter>(newEmitter, panel.editor(), panel.services().get<MV::TapDevice>());
	panel.loadPanel<SelectedEmitterEditorPanel>(editableEmitter, editorPanel->CreateEmitterComponentButton(newEmitter).self());
}

void ChooseElementCreationType::createGrid() {
	panel.selection().disable();
	auto newGrid = nodeToAttachTo->attach<MV::Scene::Grid>();

	panel.loadPanel<SelectedGridEditorPanel>(std::make_shared<EditableGrid>(newGrid, panel.editor(), panel.services().get<MV::TapDevice>()), editorPanel->CreateGridComponentButton(newGrid).self());
}

void ChooseElementCreationType::createDrawable() {
	panel.selection().disable();
	auto newDrawable = nodeToAttachTo->attach<MV::Scene::Drawable>();
	newDrawable->setPoints({}, {});

	panel.loadPanel<SelectedDrawableEditorPanel>(std::make_shared<EditablePoints>(newDrawable, panel.editor(), panel.services().get<MV::TapDevice>()), editorPanel->CreateDrawableComponentButton(newDrawable).self());
}

void ChooseElementCreationType::createButton(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();
	auto newButton = nodeToAttachTo->attach<MV::Scene::Button>(*(editorPanel->services().get<MV::TapDevice>()));

	std::vector<std::shared_ptr<MV::Scene::Node>> nodes { nodeToAttachTo->make("Active"), nodeToAttachTo->make("Idle"), nodeToAttachTo->make("Disabled") };

	auto attachSprite = [&](const std::shared_ptr<MV::Scene::Node> &a_target, const MV::Color &a_color) {
		a_target->attach<MV::Scene::Sprite>()->color(a_color)->anchors().anchor({ MV::point(0.0f, 0.0f), MV::size(1.0f, 1.0f) }).parent(newButton.self());
	};

	auto attachText = [&](const std::shared_ptr<MV::Scene::Node> &a_target) {
		a_target->attach<MV::Scene::Text>(*panel.services().get<MV::TextLibrary>())->text("!")->useBoundsForLineHeight(true)->justification(MV::TextJustification::CENTER)->anchors().anchor({ MV::point(0.0f, 0.0f), MV::size(1.0f, 1.0f) }).parent(newButton.self());
	};

	std::vector<MV::Color> colors{ {0.25f, 0.25f, 0.25f, 1.0f},{ 1.0f, 1.0f, 1.0f, 1.0f }, {0.5f, 0.5f, 0.5f, 0.75f} };
	for (int i = 0; i < nodes.size();++i) {
		attachSprite(nodes[i], colors[i]);
		attachText(nodes[i]);
	}

	newButton->activeNode(nodes[0]);
	newButton->idleNode(nodes[1]);
	newButton->disabledNode(nodes[2]);

	auto localBounds = nodeToAttachTo->localFromScreen(a_selected);
	newButton->bounds(localBounds);

	panel.services().get<Editor>()->sceneUpdated();

	panel.loadPanel<SelectedButtonEditorPanel>(std::make_shared<EditableButton>(newButton, panel.editor(), panel.services().get<MV::TapDevice>()), editorPanel->CreateButtonComponentButton(newButton).self());
}

void ChooseElementCreationType::createClickable(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();
	auto newClickable = nodeToAttachTo->attach<MV::Scene::Clickable>(*(editorPanel->services().get<MV::TapDevice>()));
	newClickable->id(MV::guid("click"));
	auto localBounds = nodeToAttachTo->localFromScreen(a_selected);
	newClickable->bounds(localBounds);
	panel.services().get<Editor>()->sceneUpdated();

	panel.loadPanel<SelectedClickableEditorPanel>(std::make_shared<EditableClickable>(newClickable, panel.editor(), panel.services().get<MV::TapDevice>()), editorPanel->CreateClickableComponentButton(newClickable).self());
}

void ChooseElementCreationType::createSpine() {
	panel.selection().disable();

	auto newSpine = nodeToAttachTo->attach<MV::Scene::Spine>()->shader(MV::DEFAULT_ID)->safe();
	newSpine->id(MV::guid("spine"));

	panel.loadPanel<SelectedSpineEditorPanel>(std::make_shared<EditableSpine>(newSpine, panel.editor(), panel.services().get<MV::TapDevice>()), editorPanel->CreateSpineComponentButton(newSpine).self());
}

SelectedTextEditorPanel::SelectedTextEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableText> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton):
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();

	auto buttonSize = MV::size(110.0f, 27.0f);
	auto deselectButton = makeButton(grid, panel.services(), buttonSize, "Deselect");
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, panel.services(), buttonSize, "Delete");
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	auto editButton = makeButton(grid, panel.services(), buttonSize, "Edit");
	std::weak_ptr<MV::Scene::Button> weakEditButton(editButton);
	editButton->onAccept.connect("click", [&, weakEditButton](std::shared_ptr<MV::Scene::Clickable>) {
		renameButton(weakEditButton.lock()->safe(), toggleText(controls->elementToEdit.get()) ? "Edit" : "Stop");
	});

	std::vector<MV::TextWrapMethod> wrapMethods{ MV::TextWrapMethod::SOFT, MV::TextWrapMethod::HARD, MV::TextWrapMethod::SCALE, MV::TextWrapMethod::NONE };
	std::vector<std::string> wrapMethodStrings {"Soft", "Hard", "Scale", "None"};
	auto wrapButton = makeButton(grid, panel.services(), "WrapMode", buttonSize, wrapMethodStrings[MV::indexOf(wrapMethods, controls->elementToEdit->wrapping())]);
	std::weak_ptr<MV::Scene::Button> weakWrapButton(wrapButton);
	wrapButton->onAccept.connect("click", [&, weakWrapButton, wrapMethods, wrapMethodStrings](std::shared_ptr<MV::Scene::Clickable>) mutable {
		auto index = MV::wrap(0, static_cast<int>(wrapMethods.size()), MV::indexOf(wrapMethods, controls->elementToEdit->wrapping()) + 1);
		controls->elementToEdit->wrapping(wrapMethods[index]);
		renameButton(weakWrapButton.lock()->safe(), wrapMethodStrings[index]);
	});

	std::vector<MV::TextJustification> justificationList{ MV::TextJustification::LEFT, MV::TextJustification::CENTER, MV::TextJustification::RIGHT };
	std::vector<std::string> justificationStrings{ "Left", "Center", "Right" };
	auto justificationButton = makeButton(grid, panel.services(), "JustificationMode", buttonSize, justificationStrings[MV::indexOf(justificationList, controls->elementToEdit->justification())]);
	std::weak_ptr<MV::Scene::Button> weakJustifcationButton(justificationButton);
	justificationButton->onAccept.connect("click", [&, weakJustifcationButton, justificationList, justificationStrings](std::shared_ptr<MV::Scene::Clickable>) mutable {
		auto index = MV::wrap(0, static_cast<int>(justificationList.size()), MV::indexOf(justificationList, controls->elementToEdit->justification()) + 1);
		controls->elementToEdit->justification(justificationList[index]);
		renameButton(weakJustifcationButton.lock()->safe(), justificationStrings[index]);
	});

	makeInputField(this, panel.services(), grid, "Name", buttonSize)->
		text(controls->elementToEdit->id())->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
			controls->elementToEdit->id(a_text->text());
			renameButton(a_associatedButton->safe(), std::string("T: ") + a_text->text());
			onNameChangeSignal(a_text->text());
		});

	auto controlBounds = a_controls->elementToEdit->bounds();

	float textboxWidth = 52.0f;
	width = makeInputField(this, panel.services(), grid, "width", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.width())));
	height = makeInputField(this, panel.services(), grid, "height", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.height())));

	offsetX = makeInputField(this, panel.services(), grid, "posX", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.minPoint.x)));
	offsetY = makeInputField(this, panel.services(), grid, "posY", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.minPoint.y)));

	makeLabel(grid, panel.services(), "HeightBoundsLabel", buttonSize, U8_STR("Height Bounds"));
	makeToggle(grid, panel.services(), "HeightBounds", a_controls->elementToEdit->useBoundsForLineHeight(), [&] {
		controls->elementToEdit->useBoundsForLineHeight(true);
	}, [&] {
		controls->elementToEdit->useBoundsForLineHeight(false);
	}, buttonSize);

	makeLabel(grid, panel.services(), "PasswordToggleLabel", buttonSize, U8_STR("Password"));
	makeToggle(grid, panel.services(), "PasswordToggle", a_controls->elementToEdit->passwordField(), [&]{
		controls->elementToEdit->passwordField(true);
	}, [&]{
		controls->elementToEdit->passwordField(false);
	}, buttonSize);

	if (controls) {
		auto xClick = offsetX->owner()->component<MV::Scene::Clickable>();
		xClick->onAccept.connect("updateX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->position({ offsetX->number(), offsetY->number() });
		});
		offsetX->onEnter.connect("updateX", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->position({ offsetX->number(), offsetY->number() });
		});
		auto yClick = offsetY->owner()->component<MV::Scene::Clickable>();
		yClick->onAccept.connect("updateY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->position({ offsetX->number(), offsetY->number() });
		});
		offsetY->onEnter.connect("updateY", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->position({ offsetX->number(), offsetY->number() });
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

		controls->onChange = [&](ResizeHandles *a_element) {
			offsetX->set(static_cast<int>(std::lround(controls->position().x)));
			offsetY->set(static_cast<int>(std::lround(controls->position().y)));

			width->set(static_cast<int>(std::lround(controls->size().width)));
			height->set(static_cast<int>(std::lround(controls->size().height)));
		};
	}

	auto anchorsButton = makeButton(grid, panel.services(), buttonSize, "Anchors");
	anchorsButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) mutable {
		openAnchorEditor(controls->elementToEditBase.self());
	});

	panel.updateBoxHeader(grid->bounds().width());
}


void SelectedTextEditorPanel::handleInput(SDL_Event &a_event) {
	EditorPanel::handleInput(a_event);
}

void SelectedTextEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->repositionHandles(true, true, false);
}

void SelectedTextEditorPanel::onSceneZoom() {
	controls->resetHandles();
}


std::shared_ptr<MV::Scene::Component> SelectedTextEditorPanel::getEditingComponent() {
	return std::static_pointer_cast<MV::Scene::Component>(controls->elementToEdit.self());
}

SelectedButtonEditorPanel::SelectedButtonEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableButton> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton) :
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();

	auto buttonSize = MV::size(110.0f, 27.0f);
	makeInputField(this, panel.services(), grid, "Name", buttonSize)->
		text(controls->elementToEdit->id())->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->id(a_text->text());
		renameButton(a_associatedButton->safe(), std::string("B: ") + a_text->text());
		onNameChangeSignal(a_text->text());
	});

	auto deselectButton = makeButton(grid, panel.services(), buttonSize, "Deselect");
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, panel.services(), buttonSize, U8_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	makeLabel(grid, panel.services(), "ActiveLabel", buttonSize, "Active");
	makeInputField(this, panel.services(), grid, "Active", buttonSize)->
		text(controls->elementToEdit->activeNode() ? controls->elementToEdit->activeNode()->id() : "")->
		onEnter.connect("!", [&](std::shared_ptr<MV::Scene::Text> a_text) {
			controls->elementToEdit->activeNode(controls->elementToEdit->owner()->get(a_text->text(), false));
		});

	makeLabel(grid, panel.services(), "IdleLabel", buttonSize, "Idle");
	makeInputField(this, panel.services(), grid, "Idle", buttonSize)->
		text(controls->elementToEdit->idleNode() ? controls->elementToEdit->idleNode()->id() : "")->
		onEnter.connect("!", [&](std::shared_ptr<MV::Scene::Text> a_text) {
			controls->elementToEdit->idleNode(controls->elementToEdit->owner()->get(a_text->text(), false));
		});

	makeLabel(grid, panel.services(), "DisabledLabel", buttonSize, "Disabled");
	makeInputField(this, panel.services(), grid, "Disabled", buttonSize)->
		text(controls->elementToEdit->disabledNode() ? controls->elementToEdit->disabledNode()->id() : "")->
		onEnter.connect("!", [&](std::shared_ptr<MV::Scene::Text> a_text) {
			controls->elementToEdit->disabledNode(controls->elementToEdit->owner()->get(a_text->text(), false));
		});
	
	makeLabel(grid, panel.services(), "TextLabel", buttonSize, "Text");
	makeInputField(this, panel.services(), grid, "Text", buttonSize)->
		text(controls->elementToEdit->text())->
		onEnter.connect("!", [&](std::shared_ptr<MV::Scene::Text> a_text) {
			controls->elementToEdit->text(a_text->text());
		});

	makeLabel(grid, panel.services(), "ScriptLabel", buttonSize, "Script File");
	makeInputField(this, panel.services(), grid, "Script", buttonSize)->
		text("")->
		onEnter.connect("!", [&](std::shared_ptr<MV::Scene::Text> a_text) {
			if (a_text->text().empty()) {
				controls->elementToEdit->onAccept.disconnect("script");
			} else {
				auto content = MV::fileContents("Scripts/" + a_text->text());
				if (content.empty()) {
					content = a_text->text();
				}
				controls->elementToEdit->onAccept.connect("script", content);
				std::cout << "Hooked up script to button [" << content << "]" << std::endl;
			}
		});

	std::vector<MV::Scene::Button::BoundsType> boundsTypes{ MV::Scene::Button::BoundsType::LOCAL, MV::Scene::Button::BoundsType::NODE, MV::Scene::Button::BoundsType::NODE_CHILDREN, MV::Scene::Button::BoundsType::CHILDREN, MV::Scene::Button::BoundsType::NONE };
	std::vector<std::string> boundsTypeStrings{ "Local", "Node", "Children", "Node Children", "None" };
	auto boundsButton = makeButton(grid, panel.services(), "WrapMode", buttonSize, boundsTypeStrings[MV::indexOf(boundsTypes, controls->elementToEdit->clickDetectionType())]);
	std::weak_ptr<MV::Scene::Button> weakBoundsButton(boundsButton);
	boundsButton->onAccept.connect("click", [&, weakBoundsButton, boundsTypes, boundsTypeStrings](std::shared_ptr<MV::Scene::Clickable>) mutable {
		auto index = MV::wrap(0, static_cast<int>(boundsTypes.size()), MV::indexOf(boundsTypes, controls->elementToEdit->clickDetectionType()) + 1);
		controls->elementToEdit->clickDetectionType(boundsTypes[index]);
		renameButton(weakBoundsButton.lock()->safe(), boundsTypeStrings[index]);
	});

	auto controlBounds = a_controls->elementToEdit->bounds();

	float textboxWidth = 52.0f;
	width = makeInputField(this, panel.services(), grid, "width", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.width())));
	height = makeInputField(this, panel.services(), grid, "height", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.height())));

	offsetX = makeInputField(this, panel.services(), grid, "posX", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.minPoint.x)));
	offsetY = makeInputField(this, panel.services(), grid, "posY", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.minPoint.y)));

	if (controls) {
		auto xClick = offsetX->owner()->component<MV::Scene::Clickable>();
		xClick->onAccept.connect("updateX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->position({ offsetX->number(), offsetY->number() });
		});
		offsetX->onEnter.connect("updateX", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->position({ offsetX->number(), offsetY->number() });
		});
		auto yClick = offsetY->owner()->component<MV::Scene::Clickable>();
		yClick->onAccept.connect("updateY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->position({ offsetX->number(), offsetY->number() });
		});
		offsetY->onEnter.connect("updateY", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->position({ offsetX->number(), offsetY->number() });
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

		controls->onChange = [&](ResizeHandles *a_element) {
			offsetX->set(static_cast<int>(std::lround(controls->position().x)));
			offsetY->set(static_cast<int>(std::lround(controls->position().y)));

			width->set(static_cast<int>(std::lround(controls->size().width)));
			height->set(static_cast<int>(std::lround(controls->size().height)));
		};
	}

	auto anchorsButton = makeButton(grid, panel.services(), buttonSize, "Anchors");
	anchorsButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) mutable {
		openAnchorEditor(controls->elementToEditBase.self());
	});

	panel.updateBoxHeader(grid->bounds().width());
}


void SelectedButtonEditorPanel::handleInput(SDL_Event &a_event) {
	EditorPanel::handleInput(a_event);
}

void SelectedButtonEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->repositionHandles(true, true, false);
}

void SelectedButtonEditorPanel::onSceneZoom() {
	controls->resetHandles();
}




std::shared_ptr<MV::Scene::Component> SelectedButtonEditorPanel::getEditingComponent() {
	return std::static_pointer_cast<MV::Scene::Component>(controls->elementToEdit.self());
}

SelectedClickableEditorPanel::SelectedClickableEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableClickable> a_controls, std::shared_ptr<MV::Scene::Button> a_associatedButton) :
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();

	auto buttonSize = MV::size(110.0f, 27.0f);
	makeInputField(this, panel.services(), grid, "Name", buttonSize)->
		text(controls->elementToEdit->id())->
		onEnter.connect("rename", [&, a_associatedButton](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->id(a_text->text());
		renameButton(a_associatedButton->safe(), std::string("C: ") + a_text->text());
		onNameChangeSignal(a_text->text());
	});

	auto deselectButton = makeButton(grid, panel.services(), buttonSize, "Deselect");
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.deleteFullScene();
	});

	auto deleteButton = makeButton(grid, panel.services(), buttonSize, "Delete");
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->detach();
		panel.deleteFullScene();
	});

	makeLabel(grid, panel.services(), "ScriptLabel", buttonSize, "Script File");
	makeInputField(this, panel.services(), grid, "Script", buttonSize)->
		text("")->
		onEnter.connect("!", [&](std::shared_ptr<MV::Scene::Text> a_text) {
		if (a_text->text().empty()) {
			controls->elementToEdit->onAccept.disconnect("script");
		}
		else {
			auto content = MV::fileContents("Scripts/" + a_text->text());
			if (content.empty()) {
				content = a_text->text();
			}
			controls->elementToEdit->onAccept.connect("script", content);
			std::cout << "Hooked up script to clickable [" << content << "]" << std::endl;
		}
	});

	std::vector<MV::Scene::Clickable::BoundsType> boundsTypes{ MV::Scene::Clickable::BoundsType::LOCAL, MV::Scene::Clickable::BoundsType::NODE, MV::Scene::Clickable::BoundsType::NODE_CHILDREN, MV::Scene::Clickable::BoundsType::CHILDREN, MV::Scene::Clickable::BoundsType::NONE };
	std::vector<std::string> boundsTypeStrings{ "Local", "Node", "Children", "Node Children", "None" };
	auto boundsButton = makeButton(grid, panel.services(), "WrapMode", buttonSize, boundsTypeStrings[MV::indexOf(boundsTypes, controls->elementToEdit->clickDetectionType())]);
	std::weak_ptr<MV::Scene::Button> weakBoundsButton(boundsButton);
	boundsButton->onAccept.connect("click", [&, weakBoundsButton, boundsTypes, boundsTypeStrings](std::shared_ptr<MV::Scene::Clickable>) mutable {
		auto index = MV::wrap(0, static_cast<int>(boundsTypes.size()), MV::indexOf(boundsTypes, controls->elementToEdit->clickDetectionType()) + 1);
		controls->elementToEdit->clickDetectionType(boundsTypes[index]);
		renameButton(weakBoundsButton.lock()->safe(), boundsTypeStrings[index]);
	});

	auto controlBounds = a_controls->elementToEdit->bounds();

	float textboxWidth = 52.0f;
	width = makeInputField(this, panel.services(), grid, "width", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.width())));
	height = makeInputField(this, panel.services(), grid, "height", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.height())));

	offsetX = makeInputField(this, panel.services(), grid, "posX", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.minPoint.x)));
	offsetY = makeInputField(this, panel.services(), grid, "posY", MV::size(textboxWidth, 27.0f), std::to_string(std::lround(controlBounds.minPoint.y)));

	if (controls) {
		auto xClick = offsetX->owner()->component<MV::Scene::Clickable>();
		xClick->onAccept.connect("updateX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->position({ offsetX->number(), offsetY->number() });
		});
		offsetX->onEnter.connect("updateX", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->position({ offsetX->number(), offsetY->number() });
		});
		auto yClick = offsetY->owner()->component<MV::Scene::Clickable>();
		yClick->onAccept.connect("updateY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			controls->position({ offsetX->number(), offsetY->number() });
		});
		offsetY->onEnter.connect("updateY", [&](std::shared_ptr<MV::Scene::Text> a_clickable) {
			controls->position({ offsetX->number(), offsetY->number() });
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

		controls->onChange = [&](ResizeHandles *a_element) {
			offsetX->set(static_cast<int>(std::lround(controls->position().x)));
			offsetY->set(static_cast<int>(std::lround(controls->position().y)));

			width->set(static_cast<int>(std::lround(controls->size().width)));
			height->set(static_cast<int>(std::lround(controls->size().height)));
		};
	}

	auto anchorsButton = makeButton(grid, panel.services(), buttonSize, "Anchors");
	anchorsButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) mutable {
		openAnchorEditor(controls->elementToEditBase.self());
	});

	panel.updateBoxHeader(grid->bounds().width());
}


void SelectedClickableEditorPanel::handleInput(SDL_Event &a_event) {
	EditorPanel::handleInput(a_event);
}

void SelectedClickableEditorPanel::onSceneDrag(const MV::Point<int> &a_delta) {
	controls->repositionHandles(true, true, false);
}

void SelectedClickableEditorPanel::onSceneZoom() {
	controls->resetHandles();
}

std::shared_ptr<MV::Scene::Component> SelectedClickableEditorPanel::getEditingComponent() {
	return std::static_pointer_cast<MV::Scene::Component>(controls->elementToEdit.self());
}
