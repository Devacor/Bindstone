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
	deselectButton->position({8.0, lastY})->onAccept.connect("click", [&](std::shared_ptr<MV::Scene::Clickable>){
		panel.loadPanel<DeselectedEditorPanel>();
	});
	
	lastY += spacing;

	posX = makeInputField(this, *panel.mouse(), node, *panel.textLibrary(), "posX", MV::size(50.0f, 27.0f))->position({8.0, lastY});
	posY = makeInputField(this, *panel.mouse(), node, *panel.textLibrary(), "posY", MV::size(50.0f, 27.0f))->position({68.0, lastY});

	lastY += spacing;
	width = makeInputField(this, *panel.mouse(), node, *panel.textLibrary(), "width", MV::size(50.0f, 27.0f))->position({8.0, lastY});
	height = makeInputField(this, *panel.mouse(), node, *panel.textLibrary(), "height", MV::size(50.0f, 27.0f))->position({68.0, lastY});

	if(controls){
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
	lastY += spacing;
	auto deselectLocalAABB = deselectButton->localAABB();

	auto background = node->make<MV::Scene::Rectangle>("Background", MV::BoxAABB(MV::point(0.0f, 20.0f), MV::point(deselectLocalAABB.maxPoint.x + 8.0f, lastY)));
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
