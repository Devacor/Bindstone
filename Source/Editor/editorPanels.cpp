#include "editorPanels.h"
#include "editorControls.h"
#include "editorFactories.h"

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

SelectedRectangleEditorPanel::SelectedRectangleEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableRectangle> a_controls):
	EditorPanel(a_panel),
	controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make<MV::Scene::Grid>("Background")->rowWidth(126.0f)->
		color({BOX_BACKGROUND})->margin({{5.0f, 4.0f}, {0.0f, 8.0f}})->
		padding({3.0f, 4.0f})->position({0.0f, 20.0f});
	auto deselectButton = makeButton(grid, *panel.textLibrary(), *panel.mouse(), "Deselect", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.loadPanel<DeselectedEditorPanel>();
	});
	float textboxWidth = 52.0f;
	posX = makeInputField(this, *panel.mouse(), grid, *panel.textLibrary(), "posX", MV::size(textboxWidth, 27.0f));
	posY = makeInputField(this, *panel.mouse(), grid, *panel.textLibrary(), "posY", MV::size(textboxWidth, 27.0f));

	width = makeInputField(this, *panel.mouse(), grid, *panel.textLibrary(), "width", MV::size(textboxWidth, 27.0f));
	height = makeInputField(this, *panel.mouse(), grid, *panel.textLibrary(), "height", MV::size(textboxWidth, 27.0f));

	auto sliderThing = grid->make<MV::Scene::Slider>(panel.mouse(), MV::Size<>(100.0f, 10.0f), false);
	sliderThing->area()->color({.25f, .25f, .25f, 1.0f});
	if(controls){
		sliderThing->onPercentChange.connect("updateX", [&](std::shared_ptr<MV::Scene::Slider> a_slider){
			controls->position({a_slider->percent() * a_slider->getRenderer()->world().width(), controls->position().y});
		});

		auto xClick = posX->get<MV::Scene::Clickable>("Clickable");
		xClick->onAccept.connect("updateX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->position({posX->number(), posY->number()});
		});
		posX->onEnter.connect("updateX", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->position({posX->number(), posY->number()});
		});
		auto yClick = posY->get<MV::Scene::Clickable>("Clickable");
		yClick->onAccept.connect("updateY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->position({posX->number(), posY->number()});
		});
		posY->onEnter.connect("updateY", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->position({posX->number(), posY->number()});
		});

		auto widthClick = width->get<MV::Scene::Clickable>("Clickable");
		widthClick->onAccept.connect("updateWidth", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->size({width->number(), height->number()});
		});
		width->onEnter.connect("updateWidth", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->size({width->number(), height->number()});
		});
		auto heightClick = width->get<MV::Scene::Clickable>("Clickable");
		heightClick->onAccept.connect("updateHeight", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->size({width->number(), height->number()});
		});
		height->onEnter.connect("updateHeight", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->size({width->number(), height->number()});
		});

		controls->onChange = [&](EditableRectangle *a_element){
			posX->number(static_cast<int>(controls->position().x + .5f));
			posY->number(static_cast<int>(controls->position().y + .5f));

			width->number(static_cast<int>(controls->size().width + .5f));
			height->number(static_cast<int>(controls->size().height + .5f));
		};
	}
	auto deselectLocalAABB = deselectButton->localAABB();

	panel.updateBoxHeader(grid->basicAABB().width());

	SDL_StartTextInput();
}

void SelectedRectangleEditorPanel::handleInput(SDL_Event &a_event) {
	if(activeTextbox){
		activeTextbox->text(a_event);
	}
}

SelectedEmitterEditorPanel::SelectedEmitterEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableEmitter> a_controls):
EditorPanel(a_panel),
controls(a_controls) {

	auto node = panel.content();
	auto grid = node->make<MV::Scene::Grid>("Background")->rowWidth(242.0f)->
		color({InterfaceColors::BOX_BACKGROUND})->margin({{5.0f, 4.0f}, {0.0f, 8.0f}})->
		padding({3.0f, 4.0f})->position({0.0f, 20.0f});
	auto deselectButton = makeButton(grid, *panel.textLibrary(), *panel.mouse(), "Deselect", MV::size(226.0f, 27.0f), UTF_CHAR_STR("Deselect"));
	deselectButton->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.loadPanel<DeselectedEditorPanel>();
	});
	float textboxWidth = 52.0f;
	posX = makeInputField(this, *panel.mouse(), grid, *panel.textLibrary(), "posX", MV::size(textboxWidth, 27.0f));
	posY = makeInputField(this, *panel.mouse(), grid, *panel.textLibrary(), "posY", MV::size(textboxWidth, 27.0f));

	width = makeInputField(this, *panel.mouse(), grid, *panel.textLibrary(), "width", MV::size(textboxWidth, 27.0f));
	height = makeInputField(this, *panel.mouse(), grid, *panel.textLibrary(), "height", MV::size(textboxWidth, 27.0f));

	const MV::Size<> labelSize{226.0f, 20.0f};

	makeLabel(this, grid, *panel.textLibrary(), "spawnRate", labelSize, UTF_CHAR_STR("Spawn Rate"));
	auto maximumSpawnRate = makeSlider(*node->getRenderer(), *panel.mouse(), [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximumSpawnRate = MV::mixIn(0.0f, 1.25f, a_slider->percent(), 3);
	}, .5f);
	makeSlider(*panel.mouse(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimumSpawnRate = MV::mixIn(0.0f, 1.25f, a_slider->percent(), 3);
		maximumSpawnRate->percent(a_slider->percent());
	}, .5f);
	grid->add(maximumSpawnRate);

	makeLabel(this, grid, *panel.textLibrary(), "lifeSpan", labelSize, UTF_CHAR_STR("Lifespan"));
	auto maximumLifespan = makeSlider(*node->getRenderer(), *panel.mouse(), [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.maxLifespan = MV::mixIn(0.01f, 60.0f, a_slider->percent(), 2);
	}, 0.15f);
	makeSlider(*panel.mouse(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.maxLifespan = MV::mixIn(0.01f, 60.0f, a_slider->percent(), 2);
		maximumLifespan->percent(a_slider->percent());
	}, 0.15f);
	grid->add(maximumLifespan);


	auto maximumEndSpeed = makeSlider(*node->getRenderer(), *panel.mouse(), [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endSpeed = MV::mixInOut(-1000.0f, 1000.0f, a_slider->percent(), 2);
	}, 0.5f);
	auto minimumEndSpeed = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endSpeed = MV::mixInOut(-1000.0f, 1000.0f, a_slider->percent(), 2);
		maximumEndSpeed->percent(a_slider->percent());
	}, 0.5f);

	makeLabel(this, grid, *panel.textLibrary(), "initialSpeed", labelSize, UTF_CHAR_STR("Start Speed"));
	auto startSpeed = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginSpeed = MV::mixInOut(-1000.0f, 1000.0f, a_slider->percent(), 2);
		maximumEndSpeed->percent(a_slider->percent());
	}, 0.5f);
	makeSlider(*panel.mouse(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginSpeed = MV::mixInOut(-1000.0f, 1000.0f, a_slider->percent(), 2);
		startSpeed->percent(a_slider->percent());
		minimumEndSpeed->percent(a_slider->percent());
	}, 0.5f);
	grid->add(startSpeed);

	makeLabel(this, grid, *panel.textLibrary(), "speedChange", labelSize, UTF_CHAR_STR("End Speed"));
	grid->add(minimumEndSpeed);
	grid->add(maximumEndSpeed);

	makeLabel(this, grid, *panel.textLibrary(), "initialDirection", labelSize, UTF_CHAR_STR("Start Direction"));
	auto maximumStartDirection = makeSlider(*node->getRenderer(), *panel.mouse(), [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximumDirection = {0.0f, 0.0f, a_slider->percent() * 720.0f};
	}, 0.0f);
	makeSlider(*panel.mouse(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimumDirection = {0.0f, 0.0f, a_slider->percent() * 720.0f};
		maximumStartDirection->percent(a_slider->percent());
	}, 0.0f);
	grid->add(maximumStartDirection);

	makeLabel(this, grid, *panel.textLibrary(), "directionChange", labelSize, UTF_CHAR_STR("Direction Change"));
	auto maximumDirectionChange = makeSlider(*node->getRenderer(), *panel.mouse(), [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.directionalChange = {0.0f, 0.0f, MV::mix(-720.0f, 720.0f, a_slider->percent())};
	}, 0.5f);
	makeSlider(*panel.mouse(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.directionalChange = {0.0f, 0.0f, MV::mix(-720.0f, 720.0f, a_slider->percent())};
		maximumDirectionChange->percent(a_slider->percent());
	}, 0.5f);
	grid->add(maximumDirectionChange);

	auto maximumEndSize = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endScale = MV::mix(-60.0f, 60.0f, a_slider->percent());
	}, 0.5f);
	auto minimumEndSize = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endScale = MV::mix(-60.0f, 60.0f, a_slider->percent());
		maximumEndSize->percent(a_slider->percent());
	}, 0.5f);

	makeLabel(this, grid, *panel.textLibrary(), "startSize", labelSize, UTF_CHAR_STR("Start Size"));
	auto startSize = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginScale = MV::mix(-60.0f, 60.0f, a_slider->percent());
		maximumEndSize->percent(a_slider->percent());
	}, 0.15f);
	makeSlider(*panel.mouse(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginScale = MV::mix(-60.0f, 60.0f, a_slider->percent());
		startSize->percent(a_slider->percent());
		minimumEndSize->percent(a_slider->percent());
	}, 0.15f);
	grid->add(startSize);

	makeLabel(this, grid, *panel.textLibrary(), "endSize", labelSize, UTF_CHAR_STR("End Size"));
	grid->add(minimumEndSize);
	grid->add(maximumEndSize);

	makeLabel(this, grid, *panel.textLibrary(), "initialRotation", labelSize, UTF_CHAR_STR("Initialize Rotation"));
	auto maximumRotation = makeSlider(*node->getRenderer(), *panel.mouse(), [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximumRotation = {0.0f, 0.0f, a_slider->percent() * 360.0f};
	}, 0.0f);
	makeSlider(*panel.mouse(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimumRotation = {0.0f, 0.0f, a_slider->percent() * 360.0f};
		maximumRotation->percent(a_slider->percent());
	}, 0.0f);
	grid->add(maximumRotation);

	makeLabel(this, grid, *panel.textLibrary(), "rotationChange", labelSize, UTF_CHAR_STR("Rotation Change"));
	auto maximumRotationChange = makeSlider(*node->getRenderer(), *panel.mouse(), [&](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.rotationalChange = {0.0f, 0.0f, MV::mix(-720.0f, 720.0f, a_slider->percent())};
	}, 0.0f);
	makeSlider(*panel.mouse(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.rotationalChange = {0.0f, 0.0f, MV::mix(-720.0f, 720.0f, a_slider->percent())};
		maximumRotationChange->percent(a_slider->percent());
	}, 0.0f);
	grid->add(maximumRotationChange);

	auto maximumREndMax = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endColor.R = a_slider->percent();
	});
	auto maximumGEndMax = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endColor.G = a_slider->percent();
	});
	auto maximumBEndMax = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endColor.B = a_slider->percent();
	});
	auto maximumAEndMax = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.endColor.A = a_slider->percent();
	});

	auto maximumREndMin = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endColor.R = a_slider->percent();
		maximumREndMax->percent(a_slider->percent());
	});
	auto maximumGEndMin = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endColor.G = a_slider->percent();
		maximumGEndMax->percent(a_slider->percent());
	});
	auto maximumBEndMin = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endColor.B = a_slider->percent();
		maximumBEndMax->percent(a_slider->percent());
	});
	auto maximumAEndMin = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.endColor.A = a_slider->percent();
		maximumAEndMax->percent(a_slider->percent());
	});

	auto maximumRInitial = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginColor.R = a_slider->percent();
		maximumREndMax->percent(a_slider->percent());
	});
	auto maximumGInitial = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginColor.G = a_slider->percent();
		maximumGEndMax->percent(a_slider->percent());
	});
	auto maximumBInitial = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginColor.B = a_slider->percent();
		maximumBEndMax->percent(a_slider->percent());
	});
	auto maximumAInitial = makeSlider(*node->getRenderer(), *panel.mouse(), [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().maximum.beginColor.A = a_slider->percent();
		maximumAEndMax->percent(a_slider->percent());
	});

	makeLabel(this, grid, *panel.textLibrary(), "initialColorMin", labelSize, UTF_CHAR_STR("Initial Color Min"));
	makeSlider(*panel.mouse(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginColor.R = a_slider->percent();
		maximumRInitial->percent(a_slider->percent());
		maximumREndMin->percent(a_slider->percent());
	});
	makeSlider(*panel.mouse(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginColor.G = a_slider->percent();
		maximumGInitial->percent(a_slider->percent());
		maximumGEndMin->percent(a_slider->percent());
	});
	makeSlider(*panel.mouse(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginColor.B = a_slider->percent();
		maximumBInitial->percent(a_slider->percent());
		maximumBEndMin->percent(a_slider->percent());
	});
	makeSlider(*panel.mouse(), grid, [=](std::shared_ptr<MV::Scene::Slider> a_slider){
		controls->elementToEdit->properties().minimum.beginColor.A = a_slider->percent();
		maximumAInitial->percent(a_slider->percent());
		maximumAEndMin->percent(a_slider->percent());
	});

	makeLabel(this, grid, *panel.textLibrary(), "initialColorMax", labelSize, UTF_CHAR_STR("Initial Color Max"));
	grid->add(maximumRInitial);
	grid->add(maximumGInitial);
	grid->add(maximumBInitial);
	grid->add(maximumAInitial);

	makeLabel(this, grid, *panel.textLibrary(), "endColorMin", labelSize, UTF_CHAR_STR("End Color Min"));
	grid->add(maximumREndMin);
	grid->add(maximumGEndMin);
	grid->add(maximumBEndMin);
	grid->add(maximumAEndMin);

	makeLabel(this, grid, *panel.textLibrary(), "endColorMax", labelSize, UTF_CHAR_STR("End Color Max"));
	grid->add(maximumREndMax);
	grid->add(maximumGEndMax);
	grid->add(maximumBEndMax);
	grid->add(maximumAEndMax);



	auto xClick = posX->get<MV::Scene::Clickable>("Clickable");
	xClick->onAccept.connect("updateX", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		controls->position({posX->number(), posY->number()});
	});
	posX->onEnter.connect("updateX", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
		controls->position({posX->number(), posY->number()});
	});
	auto yClick = posY->get<MV::Scene::Clickable>("Clickable");
	yClick->onAccept.connect("updateY", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		controls->position({posX->number(), posY->number()});
	});
	posY->onEnter.connect("updateY", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
		controls->position({posX->number(), posY->number()});
	});

	auto widthClick = width->get<MV::Scene::Clickable>("Clickable");
	widthClick->onAccept.connect("updateWidth", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		controls->size({width->number(), height->number()});
	});
	width->onEnter.connect("updateWidth", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
		controls->size({width->number(), height->number()});
	});
	auto heightClick = width->get<MV::Scene::Clickable>("Clickable");
	heightClick->onAccept.connect("updateHeight", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		controls->size({width->number(), height->number()});
	});
	height->onEnter.connect("updateHeight", [&](std::shared_ptr<MV::Scene::Text> a_clickable){
		controls->size({width->number(), height->number()});
	});

	controls->onChange = [&](EditableEmitter *a_element){
		posX->number(std::floor(controls->position().x + .5f));
		posY->number(std::floor(controls->position().y + .5f));

		width->number(std::floor(controls->size().width + .5f));
		height->number(std::floor(controls->size().height + .5f));
	};

	auto deselectLocalAABB = deselectButton->localAABB();

	panel.updateBoxHeader(grid->basicAABB().width());

	SDL_StartTextInput();
}

void SelectedEmitterEditorPanel::handleInput(SDL_Event &a_event) {
	if(activeTextbox){
		activeTextbox->text(a_event);
	}
}

DeselectedEditorPanel::DeselectedEditorPanel(EditorControls &a_panel):
	EditorPanel(a_panel) {
	auto node = panel.content();
	auto grid = node->make<MV::Scene::Grid>("grid")->rowWidth(126.0f)->
		color({InterfaceColors::BOX_BACKGROUND})->margin({{5.0f, 4.0f}, {0.0f, 8.0f}})->
		padding({3.0f, 4.0f})->position({0.0f, 20.0f});
	auto createButton = makeButton(grid, *panel.textLibrary(), *panel.mouse(), "Create", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Create"));
	auto selectButton = makeButton(grid, *panel.textLibrary(), *panel.mouse(), "Select", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Select"));

	panel.updateBoxHeader(grid->basicAABB().width());

	createButton->onAccept.connect("create", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.selection().enable([&](const MV::BoxAABB &a_selected){
			completeSelection(a_selected);
		});
	});

	selectButton->onAccept.connect("select", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.deleteScene();
	});
}

void DeselectedEditorPanel::completeSelection(const MV::BoxAABB &a_selected) {
	static long i = 0;
	panel.selection().disable();
	//auto newShape = panel.root()->make<MV::Scene::Rectangle>("Constructed_" + boost::lexical_cast<std::string>(i++), a_selected.size())->position(a_selected.minPoint);
	//newShape->color({CREATED_DEFAULT});

	//panel.loadPanel<SelectedRectangleEditorPanel>(std::make_shared<EditableRectangle>(newShape, panel.editor(), panel.mouse()));
	auto newEmitter = panel.root()->make<MV::Scene::Emitter>("Constructed_" + std::to_string(i++), panel.pool())->position(a_selected.minPoint);
	auto editableEmitter = std::make_shared<EditableEmitter>(newEmitter, panel.editor(), panel.mouse());
	editableEmitter->size(a_selected.size());

	panel.loadPanel<SelectedEmitterEditorPanel>(editableEmitter);
}
