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
		activeTextbox->setText(a_event);
	}
}

SelectedEditorPanel::SelectedEditorPanel(EditorControls &a_panel, std::unique_ptr<EditableElement> a_controls):
	EditorPanel(a_panel),
	controls(std::move(a_controls)) {

	MV::PointPrecision spacing = 28.0;
	MV::PointPrecision lastY = 28.0;

	auto node = panel.content();
	auto createButton = makeButton(node, *panel.textLibrary(), *panel.mouse(), MV::size(110.0f, 27.0f), UTF_CHAR_STR("Deselect"));
	createButton->position({8.0, lastY});
	lastY += spacing;

	ourBox = makeInputField(*panel.textLibrary(), MV::size(50.0f, 27.0f));
		
		/*std::shared_ptr<MV::TextBox>(new MV::TextBox(a_panel.textLibrary(), "default", UTF_CHAR_STR("0"), MV::size(50.0, 27.0)));
	ourBox->justification(MV::CENTER);*/
	ourBox->scene()->position({8.0, lastY});
	node->parent()->add("posX", ourBox->scene());
	
	lastY += spacing;

	auto background = node->make<MV::Scene::Rectangle>("Background", MV::point(0.0f, 20.0f), createButton->localAABB().bottomRightPoint() + MV::point(8.0f, 8.0f));
	background->color({BOX_BACKGROUND});
	background->setSortDepth(-1.0f);

	panel.updateBoxHeader(background->basicAABB().width());

	SDL_StartTextInput();

	activeTextbox = ourBox;
}

void SelectedEditorPanel::handleInput(SDL_Event &a_event) {
	if(activeTextbox){
		activeTextbox->setText(a_event);
	}
}

DeselectedEditorPanel::DeselectedEditorPanel(EditorControls &a_panel):
	EditorPanel(a_panel) {
	auto node = panel.content();
	node->position({-20.0f, -20.0f});
	auto createButton = makeButton(node, *panel.textLibrary(), *panel.mouse(), MV::size(110.0f, 77.0f), UTF_CHAR_STR("Create[[f|small]][[c|0:0:0]]123 [[f|big]] 456 7890 [[f|default]]asdfasdf 12314afasdfa"));
	createButton->position({8.0f, 28.0f});
	auto selectButton = makeButton(node, *panel.textLibrary(), *panel.mouse(), MV::size(110.0f, 27.0f), UTF_CHAR_STR("Select"));
	selectButton->position(createButton->localAABB().bottomLeftPoint() + MV::point(0.0f, 5.0f));

	auto background = node->make<MV::Scene::Rectangle>("Background", MV::point(0.0f, 20.0f), selectButton->localAABB().bottomRightPoint() + MV::point(8.0f, 8.0f));
	background->color({BOX_BACKGROUND});
	background->setSortDepth(-1.0f);

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
	auto newShape = panel.root()->make<MV::Scene::Rectangle>("Constructed_" + boost::lexical_cast<std::string>(i++), a_selected);
	newShape->color({CREATED_DEFAULT});

	panel.loadPanel<SelectedEditorPanel>(std::make_unique<EditableElement>(newShape, panel.editor(), panel.mouse()));
}
