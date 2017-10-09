#include "editorControls.h"
#include "Render/package.h"

void EditorControls::updateBoxHeader(MV::PointPrecision a_width) {
	if(!boxHeader){
		boxHeader = draggableBox->make("ContextMenuHandle")->attach<MV::Scene::Clickable>(*sharedResources.mouse)->bounds(MV::size(a_width, 20.0f))->
			color({BOX_HEADER})->show();

		boxHeader->onDrag.connect("header", [&](std::shared_ptr<MV::Scene::Clickable> boxHeader, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
			if(currentPanel){
				currentPanel->cancelInput();
			}
			draggableBox->translate(draggableBox->renderer().worldFromScreen(deltaPosition));
		});
	} else {
		boxHeader->bounds(MV::Size<>(a_width, 20.0f));
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
	
	draggableBox = editorScene->make(MV::guid("ContextMenu"));
}

EditorControls::~EditorControls() {
	currentPanel.reset();
	if (draggableBox) {
		draggableBox->removeFromParent();
		draggableBox->clear();
	}
}

std::shared_ptr<MV::Scene::Node> EditorControls::content() {
	auto found = draggableBox->get("content", false);
	if(found){
		return found;
	} else{
		return draggableBox->make("content");
	}
}

void EditorControls::deleteFullScene() {
	currentPanel.reset();
	boxHeader.reset();
	draggableBox->clear();
}

void EditorControls::deletePanelContents() {
	currentPanel.reset();
	auto boxHeaderOwner = (boxHeader) ? boxHeader->owner() : nullptr;
	draggableBox->clear();
	if (boxHeader) {
		draggableBox->add(boxHeader->owner());
	}
}
