#include "editorSceneGraphPanel.h"
#include "editor.h"

void SceneGraphPanel::clickedChild(std::shared_ptr<MV::Scene::Node> a_child) {
	if(std::dynamic_pointer_cast<MV::Scene::Rectangle>(a_child)){
		sharedResources.editor->panel().loadPanel<SelectedRectangleEditorPanel>(std::make_shared<EditableRectangle>(std::static_pointer_cast<MV::Scene::Rectangle>(a_child), root, sharedResources.mouse));
	} else if(std::dynamic_pointer_cast<MV::Scene::Emitter>(a_child)){
		auto emitter = std::static_pointer_cast<MV::Scene::Emitter>(a_child);
		auto editableEmitter = std::make_shared<EditableEmitter>(emitter, root, sharedResources.mouse);
		editableEmitter->size(emitter->basicAABB().size());
		sharedResources.editor->panel().loadPanel<SelectedEmitterEditorPanel>(editableEmitter);
	}
}

void SceneGraphPanel::loadButtons(std::shared_ptr<MV::Scene::Grid> a_grid, std::shared_ptr<MV::Scene::Node> a_node, size_t a_depth /*= 0*/) {
	auto children = a_node->children();
	for(auto&& child : children){
		makeChildButton(child, a_depth, a_grid);
	}
}

void SceneGraphPanel::update(){
	if(removeSelection){
		removeSelection = false;
		activeSelection = nullptr;
		refresh();
	}
}

void SceneGraphPanel::refresh(std::shared_ptr<MV::Scene::Node> a_newScene /*= nullptr*/) {
	if(a_newScene){
		scene = a_newScene;
	}
	grid = MV::Scene::Grid::make(root->getRenderer())->rows(1)->color({BOX_BACKGROUND})->margin({5.0f, 5.0f});

	makeChildButton(scene, 0, grid);

	MV::Point<> position;
	if(box){
		position = box->parent()->position();
	}
	box = makeDraggableBox("SceneNodePicker", root, grid->basicAABB().size(), *sharedResources.mouse);
	box->parent()->position(position);
	box->add("SceneNodeGrid", grid);
}

void SceneGraphPanel::makeChildButton(std::shared_ptr<MV::Scene::Node> a_node, size_t a_depth, std::shared_ptr<MV::Scene::Grid> a_grid) {
	MV::Size<> buttonSize(200.0f, 18.0f);
	auto buttonName = MV::stringToWide(std::string(a_depth * 3, ' ') + a_node->id());

	auto button = makeSceneButton(a_grid, *sharedResources.textLibrary, *sharedResources.mouse, a_node->id(), buttonSize, buttonName);
	if(a_node->parent()){
		button->onPress.connect("Press", [&, a_node](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			activeSelection = a_node;
		});

		button->onAccept.connect("Back", [&, a_node](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			clickedChild(a_node);
		});

		button->onRelease.connect("Remove", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			removeSelection = true;
		});
	}

	button->onDrop.connect("Back", [&, a_node](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		if(activeSelection && activeSelection != a_node){
			a_node->add(activeSelection);
		}
	});

	auto dragBetween = a_grid->make<MV::Scene::Clickable>(sharedResources.mouse, MV::size(buttonSize.width, 4.0f));
	dragBetween->onDrop.connect("dropped", [&, a_node](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		if(activeSelection){
			activeSelection->depth(a_node->depth() + .01f);
			activeSelection->normalizeChildDepth();
		}
	});

	loadButtons(a_grid, a_node, a_depth + 1);
}
