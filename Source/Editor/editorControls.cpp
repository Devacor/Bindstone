#include "editorControls.h"
#include "Render/package.h"

void EditorControls::updateBoxHeader(MV::PointPrecision a_width) {
	if(!boxHeader){
		boxHeader = draggableBox->make<MV::Scene::Clickable>("ContextMenuHandle", mouseHandle, MV::size(a_width, 20.0f));
		boxHeader->color({BOX_HEADER});

		boxHeaderDrag = boxHeader->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> boxHeader, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
			if(currentPanel){
				currentPanel->cancelInput();
			}
			draggableBox->translate(MV::castPoint<MV::PointPrecision>(deltaPosition));
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

EditorControls::EditorControls(std::shared_ptr<MV::Scene::Node> a_editor, std::shared_ptr<MV::Scene::Node> a_root, MV::TextLibrary *a_textLibrary, MV::MouseState *a_mouse):
	editorScene(a_editor),
	rootScene(a_root),
	textLibraryHandle(a_textLibrary),
	mouseHandle(a_mouse),
	currentSelection(a_root, *a_mouse) {
	
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
