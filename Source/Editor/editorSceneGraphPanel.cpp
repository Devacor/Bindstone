#include "editorSceneGraphPanel.h"
#include "editor.h"

void SceneGraphPanel::clickedChild(std::shared_ptr<MV::Scene::Node> a_child) {
	sharedResources.editor->panel().loadPanel<SelectedNodeEditorPanel>(std::make_shared<EditableNode>(a_child, root, sharedResources.mouse));
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
	}
	if (refreshNeeded) {
		refreshNeeded = false;
		refresh();
	}
}

void SceneGraphPanel::refresh(std::shared_ptr<MV::Scene::Node> a_newScene /*= nullptr*/) {
	MV::Point<> position{ 200.0f, 0.0f };
	MV::Point<> boxInnerPosition{ 0.0f, 0.0f };
	std::shared_ptr<MV::Scene::Node> box;
	box = root->get("SceneNodePicker", false);

	if (a_newScene) {
		scene = a_newScene;
	}

	auto gridNode = MV::Scene::Node::make(root->renderer(), "SceneNodeGrid");
	auto newGrid = gridNode->attach<MV::Scene::Grid>();
	newGrid->columns(1)->color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f });
	grid = newGrid.get();
	makeChildButton(scene, 0, gridNode);

	if (!box) {
		auto size = grid->bounds().size();
		size.height = 620.0f;
		box = makeDraggableBox("SceneNodePicker", root, size, *sharedResources.mouse);
		box->parent()->position(position);
		box->position(boxInnerPosition);
		box->make("CONTAINER");
	}
	else {
		position = box->parent()->position();
		boxInnerPosition = box->position();
	}

	box->get("CONTAINER")->remove("SceneNodeGrid", false);
	box->get("CONTAINER")->add(gridNode);
	box->position(boxInnerPosition);
	box->parent()->position(position);
}

void SceneGraphPanel::makeChildButton(std::shared_ptr<MV::Scene::Node> a_node, size_t a_depth, std::shared_ptr<MV::Scene::Node> a_grid) {
	if (!a_node->serializable()) {
		return;
	}
	MV::Size<> buttonSize(200.0f, 18.0f);
	auto buttonName = std::string(a_depth * 3, ' ') + a_node->id();

	auto button = makeSceneButton(a_grid, *sharedResources.textLibrary, *sharedResources.mouse, a_node->id(), buttonSize, buttonName);
	auto expandButton = makeButton(button->owner(), *sharedResources.textLibrary, *sharedResources.mouse, "Expand", MV::Size<>(18.0f, 18.0f), (a_node->empty() ? "" : (expanded[a_node.get()] ? "+" : "-")));

	expandButton->owner()->position({ 182.0f, 0.0f });
	auto dragBetween = a_grid->make()->attach<MV::Scene::Clickable>(*sharedResources.mouse)->bounds(MV::size(buttonSize.width, 5.0f));
	dragBetween->onDrop.connect("dropped", [&, a_node](std::shared_ptr<MV::Scene::Clickable> a_clickable, const MV::Point<float> &) {
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
			refreshNeeded = true;
			removeSelection = true;
		}
	});

	if(a_node->parent()){
		button->onPress.connect("Press", [&, a_node](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			activeSelection = a_node;
		});

		button->onAccept.connect("Back", [&, a_node](std::shared_ptr<MV::Scene::Clickable> a_clickable){
			clickedChild(a_node);
		});

		button->onRelease.connect("Remove", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable, const MV::Point<MV::PointPrecision> &){
			removeSelection = true;
		});

		button->onCancel.connect("Cancel", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
			removeSelection = true;
		});

		a_node->onRemove.connect("RemoveFromList", [&](std::shared_ptr<MV::Scene::Node> a_callingNode) mutable{
			if (!activeSelection) {
				removeSelection = true;
			}
		});
	}

	button->onDrop.connect("Back", [&, a_node](std::shared_ptr<MV::Scene::Clickable> a_clickable, const MV::Point<float> &){
		if(activeSelection && activeSelection != a_node){
			try {
				auto originalWorldPosition = activeSelection->worldPosition();
				a_node->add(activeSelection);
				activeSelection->worldPosition(originalWorldPosition);
				removeSelection = true;
				refreshNeeded = true;
			} catch (MV::RangeException &a_e) {
				std::cerr << a_e.what() << std::endl;
			}
		}
	});

	auto gridNode = a_grid->make();
	auto newGrid = gridNode->attach<MV::Scene::Grid>();
	newGrid->columns(1)->color({ BOX_BACKGROUND })->layoutPolicy(MV::Scene::Grid::AutoLayoutPolicy::Comprehensive);
	std::weak_ptr<MV::Scene::Node> weakGrid = gridNode;
	MV::Scene::Node* nodePointer = a_node.get();
    expandButton->onAccept.connect("Expand", [=](std::shared_ptr<MV::Scene::Clickable> a_self) {
		if (auto lockedGrid = weakGrid.lock()) {
			if (lockedGrid->visible()) {
				expanded[nodePointer] = false;
				lockedGrid->hide();
				a_self->owner()->component<MV::Scene::Button>()->text(a_node->empty() ? "" : "+");
			} else {
				expanded[nodePointer] = true;
				lockedGrid->show();
				a_self->owner()->component<MV::Scene::Button>()->text(a_node->empty() ? "" : "-");
			}
			//layoutParents(weakGrid.lock());
		}
	});

	if (!expanded[a_node.get()]) {
		gridNode->hide();
	}

	loadButtons(gridNode, a_node, a_depth + 1);
}

void SceneGraphPanel::layoutParents(std::shared_ptr<MV::Scene::Node> a_parent) {
	//a_parent->parent()->childBounds();
	auto parentGrids = a_parent->componentsInParents<MV::Scene::Grid>(true, true);
	std::reverse(parentGrids.begin(), parentGrids.end());
	MV::visit(parentGrids, [&](const MV::Scene::SafeComponent<MV::Scene::Grid> &a_grid) {
		a_grid->layoutCells();
	});
}
