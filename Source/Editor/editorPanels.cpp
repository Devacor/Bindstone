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

	makeLabel(this, grid, *panel.textLibrary(), "minimumSpawnRate", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Spawn Rate"));
	auto minimumRate = grid->make<MV::Scene::Slider>(panel.mouse(), MV::Size<>(110.0f, 10.0f), false);
	minimumRate->area()->color({.25f, .25f, .25f, 1.0f});
	auto maximumRate = grid->make<MV::Scene::Slider>(panel.mouse(), MV::Size<>(110.0f, 10.0f), false);
	maximumRate->area()->color({.25f, .25f, .25f, 1.0f});

	makeLabel(this, grid, *panel.textLibrary(), "startSize", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Start Size"));
	auto startSizeA = grid->make<MV::Scene::Slider>(panel.mouse(), MV::Size<>(110.0f, 10.0f), false);
	startSizeA->area()->color({.25f, .25f, .25f, 1.0f});
	auto startSizeB = grid->make<MV::Scene::Slider>(panel.mouse(), MV::Size<>(110.0f, 10.0f), false);
	startSizeB->area()->color({.25f, .25f, .25f, 1.0f});

	makeLabel(this, grid, *panel.textLibrary(), "endSize", MV::size(110.0f, 27.0f), UTF_CHAR_STR("End Size"));
	auto endSizeA = grid->make<MV::Scene::Slider>(panel.mouse(), MV::Size<>(110.0f, 10.0f), false);
	endSizeA->area()->color({.25f, .25f, .25f, 1.0f});
	auto endSizeB = grid->make<MV::Scene::Slider>(panel.mouse(), MV::Size<>(110.0f, 10.0f), false);
	endSizeB->area()->color({.25f, .25f, .25f, 1.0f});

	if(controls){
		minimumRate->onPercentChange.connect("updateStartSize", [=](std::shared_ptr<MV::Scene::Slider> a_slider){
			controls->elementToEdit->properties().minimumSpawnRate = a_slider->percent() * .1f;
			maximumRate->percent(a_slider->percent());
		});
		maximumRate->onPercentChange.connect("updateStartSize", [&](std::shared_ptr<MV::Scene::Slider> a_slider){
			controls->elementToEdit->properties().maximumSpawnRate = a_slider->percent() * .1f;
		});

		startSizeA->onPercentChange.connect("updateStartSize", [=](std::shared_ptr<MV::Scene::Slider> a_slider){
			controls->elementToEdit->properties().minimum.beginScale = a_slider->percent() * 110.0f;
			startSizeB->percent(a_slider->percent());
		});
		startSizeB->onPercentChange.connect("updateStartSize", [&](std::shared_ptr<MV::Scene::Slider> a_slider){
			controls->elementToEdit->properties().maximum.beginScale = a_slider->percent() * 110.0f;
		});

		endSizeA->onPercentChange.connect("updateStartSize", [=](std::shared_ptr<MV::Scene::Slider> a_slider){
			controls->elementToEdit->properties().minimum.endScale = a_slider->percent() * 110.0f;
			endSizeB->percent(a_slider->percent());
		});
		endSizeB->onPercentChange.connect("updateStartSize", [&](std::shared_ptr<MV::Scene::Slider> a_slider){
			controls->elementToEdit->properties().maximum.endScale = a_slider->percent() * 110.0f;
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

		controls->onChange = [&](EditableEmitter *a_element){
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

void SelectedEmitterEditorPanel::handleInput(SDL_Event &a_event) {
	if(activeTextbox){
		activeTextbox->text(a_event);
	}
}

DeselectedEditorPanel::DeselectedEditorPanel(EditorControls &a_panel):
	EditorPanel(a_panel) {
	auto node = panel.content();
	auto grid = node->make<MV::Scene::Grid>("grid")->rowWidth(126.0f)->
		color({BOX_BACKGROUND})->margin({{5.0f, 4.0f}, {0.0f, 8.0f}})->
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
	auto newEmitter = panel.root()->make<MV::Scene::Emitter>("Constructed_" + std::to_string(i++))->position(a_selected.minPoint);
	auto editableEmitter = std::make_shared<EditableEmitter>(newEmitter, panel.editor(), panel.mouse());
	editableEmitter->size(a_selected.size());

	panel.loadPanel<SelectedEmitterEditorPanel>(editableEmitter);
}
