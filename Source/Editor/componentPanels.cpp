#include "componentPanels.h"
#include "editorControls.h"
#include "editorFactories.h"
#include "texturePicker.h"
#include "editor.h"

EditorPanel::EditorPanel(EditorControls &a_panel):
	panel(a_panel) {
	panel.deleteScene();
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

SelectedGridEditorPanel::SelectedGridEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableGrid> a_controls) :
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
		padding({ 2.0f, 2.0f })->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);
	auto deselectButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Deselect", buttonSize, UTF_CHAR_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.loadPanel<DeselectedEditorPanel>();
	});

	auto deleteButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Delete", buttonSize, UTF_CHAR_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->owner()->removeFromParent();
		panel.loadPanel<DeselectedEditorPanel>();
	});

	makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "Name", buttonSize)->
		text(MV::toWide(controls->elementToEdit->owner()->id()))->
		onEnter.connect("rename", [&](std::shared_ptr<MV::Scene::Text> a_text) {
		controls->elementToEdit->owner()->id(MV::toString(a_text->text()));
		panel.resources().editor->sceneUpdated();
	});

	float textboxWidth = 52.0f;
	posX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "posX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->position().x))));
	posY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "posY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->position().y))));

	width = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "width", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->gridWidth()))));
	columns = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "columns", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(a_controls->elementToEdit->columns())));

	paddingX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "paddingX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->padding().first.x))));
	paddingY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "paddingY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->padding().first.y))));

	marginsX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "marginsX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->margin().first.x))));
	marginsY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "marginsY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->elementToEdit->margin().first.y))));

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

		controls->onChange = [&](EditableGrid *a_element) {
			posX->number(static_cast<int>(controls->position().x + .5f));
			posY->number(static_cast<int>(controls->position().y + .5f));
		};
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

SelectedRectangleEditorPanel::SelectedRectangleEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableRectangle> a_controls):
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({BOX_BACKGROUND})->margin({4.0f, 4.0f})->
		padding({2.0f, 2.0f})->owner();
	auto buttonSize = MV::size(110.0f, 27.0f);
	auto deselectButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Deselect", buttonSize, UTF_CHAR_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.loadPanel<DeselectedEditorPanel>();
	});

	auto deleteButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Delete", buttonSize, UTF_CHAR_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->owner()->removeFromParent();
		panel.loadPanel<DeselectedEditorPanel>();
	});

	OpenTexturePicker();

	makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "Name", buttonSize)->
		text(MV::toWide(controls->elementToEdit->owner()->id()))->
		onEnter.connect("rename", [&](std::shared_ptr<MV::Scene::Text> a_text){
			controls->elementToEdit->owner()->id(MV::toString(a_text->text()));
			panel.resources().editor->sceneUpdated();
		});

	float textboxWidth = 52.0f;
	posX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "posX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->position().x))));
	posY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "posY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->position().y))));

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
				controls->aspect(MV::cast<MV::PointPrecision>(size));
			}
		});

	makeButton(grid, *a_panel.resources().textLibrary, *a_panel.resources().mouse, "SetSize", buttonSize, UTF_CHAR_STR("Snap Size"))->
		onAccept.connect("openTexture", [&](std::shared_ptr<MV::Scene::Clickable>){
			if (controls->texture()) {
				auto size = controls->texture()->bounds().size();
				width->number(size.width);
				height->number(size.width);
				controls->size(MV::cast<MV::PointPrecision>(size));
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
		auto xClick = posX->owner()->component<MV::Scene::Clickable>();
		xClick->onAccept.connect("updateX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->position({posX->number(), posY->number()});
		});
		posX->onEnter.connect("updateX", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->position({posX->number(), posY->number()});
		});
		auto yClick = posY->owner()->component<MV::Scene::Clickable>();
		yClick->onAccept.connect("updateY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->position({posX->number(), posY->number()});
		});
		posY->onEnter.connect("updateY", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->position({posX->number(), posY->number()});
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

SelectedEmitterEditorPanel::SelectedEmitterEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableEmitter> a_controls):
EditorPanel(a_panel),
controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make("Background")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(232.0f)->
		color({InterfaceColors::BOX_BACKGROUND})->margin({4.0f, 4.0f})->
		padding({2.0f, 2.0f})->owner();
	auto buttonSize = MV::size(226.0f, 27.0f);
	auto deselectButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Deselect", buttonSize, UTF_CHAR_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.loadPanel<DeselectedEditorPanel>();
	});

	auto deleteButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Delete", buttonSize, UTF_CHAR_STR("Delete"));
	deleteButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>) {
		controls->elementToEdit->owner()->removeFromParent();
		panel.loadPanel<DeselectedEditorPanel>();
	});

	makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "Name", buttonSize)->
		text(MV::toWide(controls->elementToEdit->owner()->id()))->
		onEnter.connect("rename", [&](const std::shared_ptr<MV::Scene::Text> &a_text){
			controls->elementToEdit->owner()->id(MV::toString(a_text->text()));
			panel.resources().editor->sceneUpdated();
		});

	float textboxWidth = 52.0f;

	posX = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "posX", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->position().x))));
	posY = makeInputField(this, *panel.resources().mouse, grid, *panel.resources().textLibrary, "posY", MV::size(textboxWidth, 27.0f), MV::toWide(std::to_string(std::lround(a_controls->position().y))));

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



	auto xClick = posX->owner()->component<MV::Scene::Clickable>();
	xClick->onAccept.connect("updateX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		controls->position({posX->number(), posY->number()});
	});
	posX->onEnter.connect("updateX", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
		controls->position({posX->number(), posY->number()});
	});
	auto yClick = posY->owner()->component<MV::Scene::Clickable>();
	yClick->onAccept.connect("updateY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		controls->position({posX->number(), posY->number()});
	});
	posY->onEnter.connect("updateY", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
		controls->position({posX->number(), posY->number()});
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
		posX->number(static_cast<int>(std::lround(controls->position().x)));
		posY->number(static_cast<int>(std::lround(controls->position().y)));

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
		panel.loadPanel<ChooseElementCreationType>();
	});

	saveButton->onAccept.connect("save", [&](std::shared_ptr<MV::Scene::Clickable>){
		std::ofstream stream(fileName->text());
		cereal::JSONOutputArchive archive(stream);
		archive(cereal::make_nvp("scene", panel.root()));
	});

	loadButton->onAccept.connect("load", [&](std::shared_ptr<MV::Scene::Clickable>){
		std::ifstream stream(fileName->text());

		cereal::JSONInputArchive archive(stream);

		archive.add(
			cereal::make_nvp("mouse", panel.resources().mouse),
			cereal::make_nvp("renderer", &panel.root()->renderer()),
			cereal::make_nvp("textLibrary", panel.resources().textLibrary),
			cereal::make_nvp("pool", panel.resources().pool),
			cereal::make_nvp("texture", panel.resources().textures)
		);

		std::shared_ptr<MV::Scene::Node> newRoot;
		archive(cereal::make_nvp("scene", newRoot));

		panel.root(newRoot);
	});
}

ChooseElementCreationType::ChooseElementCreationType(EditorControls &a_panel):
	EditorPanel(a_panel) {

	auto node = panel.content();
	auto grid = node->make("grid")->position({ 0.0f, 20.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
		color({InterfaceColors::BOX_BACKGROUND})->margin({4.0f, 4.0f})->
		padding({2.0f, 2.0f})->owner();
	auto createRectangleButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Rectangle", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Rectangle"));
	auto createEmitterButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Emitter", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Emitter"));
	auto createSpineButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Spine", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Spine"));
	auto createGridButton = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Grid", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Grid"));
	auto cancel = makeButton(grid, *panel.resources().textLibrary, *panel.resources().mouse, "Cancel", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Cancel"));

	panel.updateBoxHeader(grid->bounds().width());

	createRectangleButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.selection().enable([&](const MV::BoxAABB<int> &a_selected){
			createRectangle(a_selected);
		});
	});

	createSpineButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.selection().enable([&](const MV::BoxAABB<int> &a_selected){
			createSpine(a_selected);
		});
	});

	createEmitterButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.selection().enable([&](const MV::BoxAABB<int> &a_selected){
			createEmitter(a_selected);
		});
	});

	createGridButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>) {
		panel.selection().enable([&](const MV::BoxAABB<int> &a_selected) {
			createGrid(a_selected);
		});
	});

	cancel->onAccept.connect("select", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.loadPanel<DeselectedEditorPanel>();
	});
}

void ChooseElementCreationType::createRectangle(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();
	auto transformedSelection = panel.root()->localFromScreen(a_selected);

	auto newShape = panel.root()->make(MV::guid("rectangle"))->position(transformedSelection.minPoint)->attach<MV::Scene::Sprite>();
	newShape->size(transformedSelection.size())->color({ CREATED_DEFAULT })->shader(MV::DEFAULT_ID);

	panel.resources().editor->sceneUpdated();

	panel.loadPanel<SelectedRectangleEditorPanel>(std::make_shared<EditableRectangle>(newShape, panel.editor(), panel.resources().mouse));
}

void ChooseElementCreationType::createEmitter(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();
	auto transformedSelection = panel.root()->localFromScreen(a_selected);

	auto newEmitter = panel.root()->make(MV::guid("emitter"))->position(transformedSelection.minPoint)->attach<MV::Scene::Emitter>(*panel.resources().pool);
	auto editableEmitter = std::make_shared<EditableEmitter>(newEmitter, panel.editor(), panel.resources().mouse);
	editableEmitter->size(transformedSelection.size());

	panel.resources().editor->sceneUpdated();

	panel.loadPanel<SelectedEmitterEditorPanel>(editableEmitter);
}

void ChooseElementCreationType::createGrid(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();
	auto transformedSelection = panel.root()->localFromScreen(a_selected);

	auto newShape = panel.root()->make(MV::guid("grid"))->position(transformedSelection.minPoint)->attach<MV::Scene::Grid>();

	panel.resources().editor->sceneUpdated();

	panel.loadPanel<SelectedGridEditorPanel>(std::make_shared<EditableGrid>(newShape, panel.editor(), panel.resources().mouse));
}

void ChooseElementCreationType::createSpine(const MV::BoxAABB<int> &a_selected) {
	panel.selection().disable();
	auto transformedSelection = panel.root()->localFromScreen(a_selected);

	auto newEmitter = panel.root()->make(MV::guid("spine"))->position(transformedSelection.minPoint)->attach<MV::Scene::Emitter>(*panel.resources().pool);
	auto editableEmitter = std::make_shared<EditableEmitter>(newEmitter, panel.editor(), panel.resources().mouse);
	editableEmitter->size(transformedSelection.size());

	panel.resources().editor->sceneUpdated();

	panel.loadPanel<SelectedEmitterEditorPanel>(editableEmitter);
}