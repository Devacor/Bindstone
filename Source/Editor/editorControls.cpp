#include "editorControls.h"
#include "Render/package.h"

void EditorControls::updateBoxHeader(MV::PointPrecision a_width) {
	if(!boxHeader){
		boxHeader = draggableBox->make<MV::Scene::Clickable>("ContextMenuHandle", sharedResources.mouse, MV::size(a_width, 20.0f))->
			color({BOX_HEADER});

		boxHeaderDrag = boxHeader->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> boxHeader, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
			if(currentPanel){
				currentPanel->cancelInput();
			}
			draggableBox->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		});
	} else{
		boxHeader->size({a_width, 20.0f});
	}
}

void EditorControls::handleInput(SDL_Event &a_event) {
	if(currentPanel){
		currentPanel->handleInput(a_event);
	}
}

EditorControls::EditorControls(std::shared_ptr<MV::Scene::Node> a_editor, std::shared_ptr<MV::Scene::Node> a_root, SharedResources a_resources):
	sharedResources(a_resources),
	editorScene(a_editor),
	rootScene(a_root),
	currentSelection(a_editor, *sharedResources.mouse){
	
	draggableBox = editorScene->make<MV::Scene::Node>("ContextMenu");
}

std::shared_ptr<MV::Scene::Node> EditorControls::content() {
	auto found = draggableBox->get("content", false);
	if(found){
		return found;
	} else{
		return draggableBox->make<MV::Scene::Node>("content");
	}
}

void EditorControls::deleteScene() {
	currentPanel.reset();
	draggableBox->clear();
	if(boxHeader){
		draggableBox->add("ContextMenuHandle", boxHeader);
	}
}
