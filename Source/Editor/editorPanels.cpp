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

SelectedEditorPanel::SelectedEditorPanel(EditorControls &a_panel, std::shared_ptr<EditableElement> a_controls):
	EditorPanel(a_panel),
	controls(a_controls) {

	MV::PointPrecision spacing = 33.0;
	MV::PointPrecision lastY = 28.0;

	auto node = panel.content();
	auto deselectButton = makeButton(node, *panel.textLibrary(), *panel.mouse(), "Deselect", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Deselect"));
	deselectButton->position({8.0, lastY});
	lastY += spacing;

	posX = makeInputField(this, *panel.mouse(), node, *panel.textLibrary(), "posX", MV::size(50.0f, 27.0f))->position({8.0, lastY});
	posY = makeInputField(this, *panel.mouse(), node, *panel.textLibrary(), "posY", MV::size(50.0f, 27.0f))->position({68.0, lastY});

	lastY += spacing;
	width = makeInputField(this, *panel.mouse(), node, *panel.textLibrary(), "width", MV::size(50.0f, 27.0f))->position({8.0, lastY});
	height = makeInputField(this, *panel.mouse(), node, *panel.textLibrary(), "height", MV::size(50.0f, 27.0f))->position({68.0, lastY});

	if(controls){
		auto xClick = posX->get<MV::Scene::Clickable>("Clickable");
		xClick->clickSignals["updateX"] = xClick->onAccept.connect([=](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->position({posX->number(), posY->number()});
		});
		posX->enterSignals["updateX"] = posX->onEnter.connect([=](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->position({posX->number(), posY->number()});
		});
		auto yClick = posY->get<MV::Scene::Clickable>("Clickable");
		yClick->clickSignals["updateY"] = xClick->onAccept.connect([=](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->position({posX->number(), posY->number()});
		});
		posY->enterSignals["updateY"] = posY->onEnter.connect([=](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->position({posX->number(), posY->number()});
		});

		auto widthClick = width->get<MV::Scene::Clickable>("Clickable");
		widthClick->clickSignals["updateWidth"] = widthClick->onAccept.connect([=](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->size({width->number(), height->number()});
		});
		width->enterSignals["updateWidth"] = width->onEnter.connect([=](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->size({width->number(), height->number()});
		});
		auto heightClick = width->get<MV::Scene::Clickable>("Clickable");
		heightClick->clickSignals["updateHeight"] = heightClick->onAccept.connect([=](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			controls->size({width->number(), height->number()});
		});
		height->enterSignals["updateHeight"] = height->onEnter.connect([=](std::shared_ptr<MV::Scene::Text> a_clickable){
			controls->size({width->number(), height->number()});
		});

		controls->onChange = [=](EditableElement *a_element){
			//a_element->position({posX->number(), posY->number()});
			posX->number(static_cast<int>(a_element->position().x));
			posY->number(static_cast<int>(a_element->position().y));

			width->number(static_cast<int>(a_element->size().width));
			height->number(static_cast<int>(a_element->size().height));
		};
	}
	lastY += spacing;
	auto deselectLocalAABB = deselectButton->localAABB();

	auto background = node->make<MV::Scene::Rectangle>("Background", MV::BoxAABB(MV::point(0.0f, 20.0f), deselectButton->localAABB().bottomRightPoint() + MV::point(8.0f, 8.0f)));
	background->color({BOX_BACKGROUND});
	background->depth(-1.0f);

	panel.updateBoxHeader(background->basicAABB().width());

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
	auto createButton = makeButton(node, *panel.textLibrary(), *panel.mouse(), "Create", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Create"));
	createButton->position({8.0f, 28.0f});
	auto selectButton = makeButton(node, *panel.textLibrary(), *panel.mouse(), "Select", MV::size(110.0f, 27.0f), UTF_CHAR_STR("Select"));
	selectButton->position(createButton->localAABB().bottomLeftPoint() + MV::point(0.0f, 5.0f));

	auto background = node->make<MV::Scene::Rectangle>("Background", MV::BoxAABB(MV::point(0.0f, 20.0f), selectButton->localAABB().bottomRightPoint() + MV::point(8.0f, 8.0f)));
	background->color({BOX_BACKGROUND});
	background->depth(-1.0f);

	panel.updateBoxHeader(background->basicAABB().width());

	clickSignals["create"] = createButton->onAccept.connect([&](std::shared_ptr<MV::Scene::Clickable>){
		panel.selection().enable([&](const MV::BoxAABB &a_selected){
			completeSelection(a_selected);
		});
	});

	clickSignals["select"] = selectButton->onAccept.connect([&](std::shared_ptr<MV::Scene::Clickable>){
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
