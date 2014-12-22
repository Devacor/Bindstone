#include "editorSceneGraphPanel.h"
#include "editor.h"

void SceneGraphPanel::clickedChild(std::shared_ptr<MV::Scene::Node> a_child) {
	if(a_child->component<MV::Scene::Sprite>(true, false)){
		sharedResources.editor->panel().loadPanel<SelectedRectangleEditorPanel>(std::make_shared<EditableRectangle>(a_child->component<MV::Scene::Sprite>(), root, sharedResources.mouse));
	} else if(a_child->component<MV::Scene::Emitter>(true, false)){
		auto emitter = a_child->component<MV::Scene::Emitter>();
		auto editableEmitter = std::make_shared<EditableEmitter>(emitter, root, sharedResources.mouse);
		editableEmitter->size(emitter->bounds().size());
		sharedResources.editor->panel().loadPanel<SelectedEmitterEditorPanel>(editableEmitter);
	} else if (a_child->component<MV::Scene::Grid>(true, false)) {
		sharedResources.editor->panel().loadPanel<SelectedGridEditorPanel>(std::make_shared<EditableGrid>(a_child->component<MV::Scene::Grid>(), root, sharedResources.mouse));
	}
}

void SceneGraphPanel::loadButtons(std::shared_ptr<MV::Scene::Node> a_grid, std::shared_ptr<MV::Scene::Node> a_node, size_t a_depth /*= 0*/) {
	for(auto&& child : *a_node){
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
	auto gridNode = MV::Scene::Node::make(root->renderer(), "SceneNodeGrid");
	grid = gridNode->attach<MV::Scene::Grid>()->columns(1)->color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f });

	makeChildButton(scene, 0, gridNode);

	MV::Point<> position;
	if(box){
		position = box->parent()->position();
	}
	box = makeDraggableBox("SceneNodePicker", root, grid->bounds().size(), *sharedResources.mouse);
	box->parent()->position(position);
	box->add(gridNode);
}

void SceneGraphPanel::makeChildButton(std::shared_ptr<MV::Scene::Node> a_node, size_t a_depth, std::shared_ptr<MV::Scene::Node> a_grid) {
	MV::Size<> buttonSize(200.0f, 18.0f);
	auto buttonName = MV::toWide(std::string(a_depth * 3, ' ') + a_node->id());

	auto button = makeSceneButton(a_grid, *sharedResources.textLibrary, *sharedResources.mouse, a_node->id(), buttonSize, buttonName);

	auto dragBetween = a_grid->make()->attach<MV::Scene::Clickable>(*sharedResources.mouse)->size(MV::size(buttonSize.width, 5.0f));
	dragBetween->onDrop.connect("dropped", [&, a_node](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
		if (activeSelection) {
			float newDepth = activeSelection->depth();
			if (a_node->parent() != activeSelection->parent()) {
				if (a_node->parent() && a_node->parent()->parent() == activeSelection->parent()) {
					newDepth = a_node->parent()->depth() + .01f;
				} else if (activeSelection->parent() == a_node) {
					newDepth = -0.01f;
				}
			}else{
				newDepth = a_node->depth() + .01f;
			}
			activeSelection->depth(newDepth);
			activeSelection->parent()->normalizeDepth();
		}
	});

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

		a_node->onRemove.connect("RemoveFromList", [&](std::shared_ptr<MV::Scene::Node> a_callingNode) mutable{
			if (!activeSelection) {
				removeSelection = true;
			}
		});
	}

	button->onDrop.connect("Back", [&, a_node](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		if(activeSelection && activeSelection != a_node){
			try {
				auto originalWorldPosition = activeSelection->worldPosition();
				a_node->add(activeSelection);
				activeSelection->worldPosition(originalWorldPosition);
			} catch (MV::RangeException &a_e) {
				std::cerr << a_e.what() << std::endl;
			}
		}
	});

	loadButtons(a_grid, a_node, a_depth + 1);
}
