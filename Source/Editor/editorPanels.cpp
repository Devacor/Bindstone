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
	int i = 0;
}

SelectedEditorPanel::SelectedEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableElement> a_controls):
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

		controls->onChange = [&](EditableElement *a_element){
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

void SelectedEditorPanel::handleInput(SDL_Event &a_event) {
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
	auto newShape = panel.root()->make<MV::Scene::Rectangle>("Constructed_" + boost::lexical_cast<std::string>(i++), a_selected.size())->position(a_selected.minPoint);
	newShape->color({CREATED_DEFAULT});

	panel.loadPanel<SelectedEditorPanel>(std::make_shared<EditableElement>(newShape, panel.editor(), panel.mouse()));
}
