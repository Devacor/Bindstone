#include "editComponents.h"
#include "MV/Utility/generalUtility.h"

long Selection::gid = 0;

void Selection::callback(std::function<void(const MV::BoxAABB<int> &)> a_callback){
	selectedCallback = a_callback;
}

void Selection::enable(std::function<void(const MV::BoxAABB<int> &)> a_callback){
	selectedCallback = a_callback;
	enable();
}

void Selection::enable(){
	onMouseDownHandle = mouse.onLeftMouseDown.connect([&](MV::TapDevice &mouse){
		mouse.queueExclusiveAction(MV::ExclusiveTapAction(true, {10000}, [&](){
			inSelection = true;
			selection.initialize(mouse.position());
			visibleSelection = scene->make("Selection_" + std::to_string(id))->position(scene->localFromScreen(selection.minPoint))->attach<MV::Scene::Sprite>()->color(MV::Color(1.0f, 1.0f, 0.0f, .25f))->safe();
			auto originalPosition = visibleSelection->owner()->localFromScreen(mouse.position());
			onMouseMoveHandle = mouse.onMove.connect([&, originalPosition](MV::TapDevice &mouse){
				visibleSelection->bounds({originalPosition, visibleSelection->owner()->localFromScreen(mouse.position())});
			});
		}, [](){}, "SelectBox"));
	});

	onMouseUpHandle = mouse.onLeftMouseUp.connect([&](MV::TapDevice &mouse){
		if(!inSelection){
			return;
		}
		SCOPE_EXIT{
			exitSelection();
		};

		selection.expandWith(mouse.position());
		if(selectedCallback){
			selectedCallback(selection);
		}
	});
}

void Selection::exitSelection(){
	inSelection = false;
	onMouseMoveHandle.reset();
	if(visibleSelection){
		visibleSelection->owner()->removeFromParent();
		visibleSelection.reset();
	}
}

void Selection::disable(){
	onMouseDownHandle.reset();
	onMouseUpHandle.reset();
	exitSelection();
}

Selection::Selection(std::shared_ptr<MV::Scene::Node> a_scene, MV::TapDevice &a_mouse):
	mouse(a_mouse),
	scene(a_scene),
	id(gid++) {
}

EditableNode::EditableNode(std::shared_ptr<MV::Scene::Node> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse) :
	elementToEdit(a_elementToEdit),
	controlContainer(a_rootContainer->make("EditableNode")->depth(-90.0f)),
	mouse(a_mouse) {

	resetHandles();
}

EditableNode::~EditableNode() {
	controlContainer->removeFromParent();
}

void EditableNode::removeHandles() {
	if (positionHandle) {
		positionHandle->owner()->removeFromParent();
	}
	positionHandle.reset();
}

void EditableNode::resetHandles() {
	removeHandles();

	EditableNode* self = this;
	positionHandle = controlContainer->make("EditControls")->serializable(false)->attach<MV::Scene::Clickable>(*mouse);
	positionHandle->bounds({ MV::size(7.0f, 7.0f), true })->show()->color({ POSITION_HANDLE })->globalPriority(positionHandle->globalPriority() + 100);
	positionHandle->owner()->worldPosition(elementToEdit->worldPosition())->attach<MV::Scene::Sprite>()->bounds({MV::size(1.0f, 1.0f ), true})->color({ POSITION_HANDLE_CENTER });
	positionHandle->onDrag.connect("position", [&, self](const std::shared_ptr<MV::Scene::Clickable> &handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		handle->owner()->translate(elementToEdit->renderer().worldFromScreen(deltaPosition));
		elementToEdit->worldPosition(handle->owner()->worldPosition());
		onChange(self);
	});

	positionHandle->owner()->worldRotationRad(elementToEdit->worldRotationRad());
}

void EditableNode::rotation(const MV::AxisAngles &a_rotate) {
	elementToEdit->rotation(a_rotate);
	resetHandles();
}

MV::AxisAngles EditableNode::rotation() const {
	return elementToEdit->rotation();
}

void EditableNode::position(const MV::Point<> &a_newPosition) {
	elementToEdit->position(a_newPosition);
	resetHandles();
}

MV::Point<> EditableNode::position() const {
	return elementToEdit->position();
}


ResizeHandles::ResizeHandles(MV::Scene::SafeComponent<MV::Scene::Component> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse) :
	elementToEditBase(a_elementToEdit),
	controlContainer(a_rootContainer->make("Editable")->depth(-100.0f)),
	mouse(a_mouse) {

	resetHandles();

	nodeMoved = elementToEditBase->owner()->onTransformChange.connect([&](const std::shared_ptr<MV::Scene::Node> &a_this) {
		resetHandles();
	});
}

void ResizeHandles::resetHandles() {
	removeHandles();
	auto rectBox = MV::round<MV::PointPrecision>(elementToEditBase->worldBounds());

	MV::Size<> currentDimensions = rectBox.size();
	if (aspectSize.width != 0.0f && aspectSize.height != 0.0f) {
		float aspectRatio = aspectSize.width / aspectSize.height;

		if (currentDimensions.width < currentDimensions.height) {
			currentDimensions.height = currentDimensions.width * aspectSize.height / aspectSize.width;
		}
		else if (currentDimensions.width > currentDimensions.height) {
			currentDimensions.width = currentDimensions.height * aspectSize.width / aspectSize.height;
		}
		rectBox.maxPoint = rectBox.minPoint + toPoint(currentDimensions);
	}

	auto handleSize = MV::size(8.0f, 8.0f);
	ResizeHandles* self = this;
	positionHandle = controlContainer->make(MV::guid("position"))->worldPosition(rectBox.topLeftPoint())->attach<MV::Scene::Clickable>(*mouse)->bounds({MV::Point<>(), rectBox.size()});
	positionHandle->onDrag.connect("position", [&, self](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		auto castPosition = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(castPosition);
		elementToEditBase->bounds({ elementToEditBase->owner()->localFromWorld(handle->owner()->worldPosition()), elementToEditBase->bounds().size() });
		topLeftSizeHandle->owner()->translate(castPosition);
		topRightSizeHandle->owner()->translate(castPosition);
		bottomLeftSizeHandle->owner()->translate(castPosition);
		bottomRightSizeHandle->owner()->translate(castPosition);
		onChange(self);
	});

	topLeftSizeHandle = controlContainer->make(MV::guid("topLeft"))->worldPosition(rectBox.topLeftPoint() - MV::toPoint(handleSize))->attach<MV::Scene::Clickable>(*mouse);
	topLeftSizeHandle->bounds(handleSize)->color({ SIZE_HANDLES })->show();
	topLeftSizeHandle->onDrag.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		auto worldDelta = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(worldDelta);
		topRightSizeHandle->owner()->translate(MV::point(0.0f, worldDelta.y));
		bottomLeftSizeHandle->owner()->translate(MV::point(worldDelta.x, 0.0f));

		repositionHandles();
	});

	topLeftSizeHandle->onRelease.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &) {
		resetHandles();
	});

	topRightSizeHandle = controlContainer->make(MV::guid("topRight"))->worldPosition(rectBox.topRightPoint() - MV::point(0.0f, handleSize.height))->attach<MV::Scene::Clickable>(*mouse);
	topRightSizeHandle->bounds(handleSize)->color({ SIZE_HANDLES })->show();
	topRightSizeHandle->onDrag.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		auto worldDelta = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(worldDelta);
		topLeftSizeHandle->owner()->translate(MV::point(0.0f, worldDelta.y));
		bottomRightSizeHandle->owner()->translate(MV::point(worldDelta.x, 0.0f));

		repositionHandles();
	});
	topRightSizeHandle->onRelease.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &) {
		resetHandles();
	});

	bottomLeftSizeHandle = controlContainer->make(MV::guid("bottomLeft"))->worldPosition(rectBox.bottomLeftPoint() - MV::point(handleSize.width, 0.0f))->attach<MV::Scene::Clickable>(*mouse);
	bottomLeftSizeHandle->bounds(handleSize)->color({ SIZE_HANDLES })->show();
	bottomLeftSizeHandle->onDrag.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		auto worldDelta = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(worldDelta);
		topLeftSizeHandle->owner()->translate(MV::point(worldDelta.x, 0.0f));
		bottomRightSizeHandle->owner()->translate(MV::point(0.0f, worldDelta.y));

		repositionHandles();
	});
	bottomLeftSizeHandle->onRelease.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &) {
		resetHandles();
	});

	bottomRightSizeHandle = controlContainer->make(MV::guid("bottomRight"))->worldPosition(rectBox.bottomRightPoint())->attach<MV::Scene::Clickable>(*mouse);
	bottomRightSizeHandle->bounds(handleSize)->color({ SIZE_HANDLES })->show();
	bottomRightSizeHandle->onDrag.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		auto worldDelta = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(worldDelta);
		topRightSizeHandle->owner()->translate(MV::point(worldDelta.x, 0.0f));
		bottomLeftSizeHandle->owner()->translate(MV::point(0.0f, worldDelta.y));

		repositionHandles();
	});
	bottomRightSizeHandle->onRelease.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &) {
		resetHandles();
	});

	repositionHandles(false, false, false);
}

void ResizeHandles::removeHandles() {
	if (positionHandle && positionHandle->ownerIsAlive()) {
		controlContainer->remove(positionHandle->owner());
		controlContainer->remove(topLeftSizeHandle->owner());
		controlContainer->remove(topRightSizeHandle->owner());
		controlContainer->remove(bottomLeftSizeHandle->owner());
		controlContainer->remove(bottomRightSizeHandle->owner());
	}
	positionHandle.reset();
	topLeftSizeHandle.reset();
	topRightSizeHandle.reset();
	bottomLeftSizeHandle.reset();
	bottomRightSizeHandle.reset();
}

void ResizeHandles::size(MV::Size<> a_newSize) {
	elementToEditBase->bounds({ elementToEditBase->bounds().minPoint, a_newSize });
	resetHandles();
}

MV::Size<> ResizeHandles::size() {
	return elementToEditBase->bounds().size();
}

void ResizeHandles::position(MV::Point<> a_newPosition) {
	elementToEditBase->bounds({ a_newPosition, elementToEditBase->bounds().size() });
	resetHandles();
}

MV::Point<> ResizeHandles::position() const {
	return elementToEditBase->bounds().minPoint;
}

void ResizeHandles::repositionHandles(bool a_fireOnChange, bool a_repositionElement, bool a_resizeElement) {
	auto box = MV::BoxAABB<>(topLeftSizeHandle->worldBounds().bottomRightPoint(), bottomRightSizeHandle->worldBounds().topLeftPoint());
	auto originalPosition = elementToEditBase->bounds().minPoint;
	auto corners = elementToEditBase->owner()->localFromWorld(box);
	MV::Size<> currentDimensions = corners.size();

	if (aspectSize.width != 0.0f && aspectSize.height != 0.0f) {
		float aspectRatio = aspectSize.width / aspectSize.height;

		if (currentDimensions.width < currentDimensions.height) {
			currentDimensions.height = currentDimensions.width * aspectSize.height / aspectSize.width;
		}
		else if (currentDimensions.width > currentDimensions.height) {
			currentDimensions.width = currentDimensions.height * aspectSize.width / aspectSize.height;
		}
	}

	if (a_resizeElement) {
		elementToEditBase->bounds({ corners.minPoint, corners.size() });
	} else if (a_repositionElement) {
		elementToEditBase->bounds({ corners.minPoint, elementToEditBase->bounds().size() });
	}

	auto rectBox = MV::round<MV::PointPrecision>(MV::round<MV::PointPrecision>(elementToEditBase->worldBounds()));
	positionHandle->bounds(rectBox.size());
	positionHandle->owner()->worldPosition(rectBox.minPoint);

	if (a_fireOnChange && onChange) {
		onChange(this);
	}
}

EditablePoints::EditablePoints(MV::Scene::SafeComponent<MV::Scene::Drawable> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse) :
	elementToEdit(a_elementToEdit),
	controlContainer(a_rootContainer->make("Editable")->depth(-100.0f)),
	mouse(a_mouse) {

	resetHandles();

	nodeMoved = elementToEdit->owner()->onTransformChange.connect([&](const std::shared_ptr<MV::Scene::Node> &a_this) {
		resetHandles();
	});
}

void EditablePoints::removeHandles() {
	for (auto&& pointHandle : pointHandles) {
		pointHandle->owner()->removeFromParent();
	}
	pointHandles.clear();
}

void EditablePoints::resetHandles() {
	removeHandles();
	for (size_t i = 0; i < elementToEdit->pointSize();++i) {
		auto point = elementToEdit->point(i);
		auto screenPosition = elementToEdit->owner()->screenFromLocal(point.point());
		auto position = MV::round<MV::PointPrecision>(screenPosition);
		if (std::find_if(pointHandles.begin(), pointHandles.end(), [&](auto& ph) { return ph->owner()->position() == position; }) == pointHandles.end()) {
			auto pointHandle = controlContainer->make(std::to_string(i))->position(position)->attach<MV::Scene::Clickable>(*mouse);
			pointHandle->bounds({ MV::size(6.0f, 6.0f), true })->color({ 1.0f, 0.0f, 1.0f, .25f })->show();
			hookupSignals(pointHandle, static_cast<int>(i));
			pointHandles.push_back(pointHandle);
		}
	}
	//positionHandle = controlContainer->make(MV::guid("position"))->position(rectBox.minPoint)->attach<MV::Scene::Sprite>();
	//positionHandle->bounds(currentDimensions)->color({ 0x22FFFFFF });
}

void EditablePoints::hookupSignals(MV::Scene::SafeComponent<MV::Scene::Clickable> pointHandle, int i) {
	pointHandle->onPress.connect("!", [=](std::shared_ptr<MV::Scene::Clickable>) {
		std::cout << "Clicked " << elementToEdit->point(i).point() << std::endl;
		if (onSelected) { onSelected(elementToEdit->point(i)); }
	});
	pointHandle->onDrag.connect("!", [=](std::shared_ptr<MV::Scene::Clickable> a_this, const MV::Point<int> &, const MV::Point<int> &deltaPosition) {
		auto worldDelta = a_this->owner()->renderer().worldFromScreen(deltaPosition);
		a_this->owner()->translate(worldDelta);
		onDragged(static_cast<size_t>(i), elementToEdit->owner()->localFromWorld(a_this->owner()->worldPosition()));
	});
}


EditableGrid::EditableGrid(MV::Scene::SafeComponent<MV::Scene::Grid> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse):
	elementToEdit(a_elementToEdit),
	controlContainer(a_rootContainer->make("Editable")->depth(-100.0f)),
	mouse(a_mouse) {

	resetHandles();
	
	nodeMoved = elementToEdit->owner()->onTransformChange.connect([&](const std::shared_ptr<MV::Scene::Node> &a_this) {
		resetHandles();
	});
}

void EditableGrid::removeHandles() {
	if (positionHandle) {
		controlContainer->remove(positionHandle->owner());
	}
	positionHandle.reset();
}

void EditableGrid::resetHandles() {
	removeHandles();
	auto rectBox = MV::round<MV::PointPrecision>(elementToEdit->screenBounds());

	MV::Size<> currentDimensions = rectBox.size();

	EditableGrid* self = this;
	positionHandle = controlContainer->make(MV::guid("position"))->position(rectBox.minPoint)->attach<MV::Scene::Sprite>();
	positionHandle->bounds(currentDimensions)->color({0x22FFFFFF});
}


EditableSpine::EditableSpine(MV::Scene::SafeComponent<MV::Scene::Spine> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse) :
	elementToEdit(a_elementToEdit),
	controlContainer(a_rootContainer->make("Editable")->depth(-100.0f)),
	mouse(a_mouse) {

	resetHandles();

	nodeMoved = elementToEdit->owner()->onTransformChange.connect([&](const std::shared_ptr<MV::Scene::Node> &a_this) {
		resetHandles();
	});
}

void EditableSpine::removeHandles() {
	if (positionHandle) {
		controlContainer->remove(positionHandle->owner());
	}
	positionHandle.reset();
}

void EditableSpine::resetHandles() {
	removeHandles();
	auto rectBox = MV::round<MV::PointPrecision>(elementToEdit->screenBounds());

	auto currentDimensions = MV::Size<>(std::max(rectBox.size().width, 5.0f), std::max(rectBox.size().height, 5.0f));

	EditableSpine* self = this;
	positionHandle = controlContainer->make(MV::guid("position"))->position(rectBox.minPoint)->attach<MV::Scene::Sprite>();
	positionHandle->bounds(currentDimensions)->color({ 0x11FFFFFF });
}

EditableParallax::EditableParallax(MV::Scene::SafeComponent<MV::Scene::Parallax> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice* a_mouse) :
	elementToEdit(a_elementToEdit),
	controlContainer(a_rootContainer->make("Editable")->depth(-100.0f)),
	mouse(a_mouse) {

	resetHandles();

	nodeMoved = elementToEdit->owner()->onTransformChange.connect([&](const std::shared_ptr<MV::Scene::Node>& a_this) {
		resetHandles();
	});
}

void EditableParallax::removeHandles() {
	if (positionHandle) {
		controlContainer->remove(positionHandle->owner());
	}
	positionHandle.reset();
}

void EditableParallax::resetHandles() {
	removeHandles();
	auto rectBox = MV::round<MV::PointPrecision>(elementToEdit->screenBounds());

	auto currentDimensions = MV::Size<>(std::max(rectBox.size().width, 5.0f), std::max(rectBox.size().height, 5.0f));

	EditableParallax* self = this;
	positionHandle = controlContainer->make(MV::guid("position"))->position(rectBox.minPoint)->attach<MV::Scene::Sprite>();
	positionHandle->bounds(currentDimensions)->color({ 0x11FFFFFF });
}

EditableButton::EditableButton(MV::Scene::SafeComponent<MV::Scene::Button> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse) :
	ResizeHandles(a_elementToEdit.cast<MV::Scene::Component>(), a_rootContainer, a_mouse),
	elementToEdit(a_elementToEdit) {
	elementToEdit->color({ 1.0f, 1.0f, 1.0f, .1f })->show();
}

EditableButton::~EditableButton(){
	elementToEdit->color({ 1.0f, 1.0f, 1.0f, 1.0f })->hide();
}

EditableClickable::EditableClickable(MV::Scene::SafeComponent<MV::Scene::Clickable> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse) :
	ResizeHandles(a_elementToEdit.cast<MV::Scene::Component>(), a_rootContainer, a_mouse),
	elementToEdit(a_elementToEdit) {
	elementToEdit->color({ 1.0f, 1.0f, 1.0f, .1f })->show();
}

EditableClickable::~EditableClickable() {
	elementToEdit->color({ 1.0f, 1.0f, 1.0f, 1.0f })->hide();
}

EditableRectangle::EditableRectangle(MV::Scene::SafeComponent<MV::Scene::Sprite> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse):
	ResizeHandles(a_elementToEdit.cast<MV::Scene::Component>(), a_rootContainer, a_mouse),
	elementToEdit(a_elementToEdit) {
}

void EditableRectangle::aspect(MV::Size<> a_newAspect) {
	aspectSize = a_newAspect;
	resetHandles();
}

EditableText::EditableText(MV::Scene::SafeComponent<MV::Scene::Text> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse) :
	ResizeHandles(a_elementToEdit.cast<MV::Scene::Component>(), a_rootContainer, a_mouse),
	elementToEdit(a_elementToEdit) {
	resetHandles();
	nodeMoved = elementToEdit->owner()->onTransformChange.connect([&](const std::shared_ptr<MV::Scene::Node> &a_this) {
		resetHandles();
	});
}

EditableEmitter::EditableEmitter(MV::Scene::SafeComponent<MV::Scene::Emitter> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse):
	ResizeHandles(a_elementToEdit.cast<MV::Scene::Component>(), a_rootContainer, a_mouse),
	elementToEdit(a_elementToEdit) {
}

void EditableEmitter::repositionHandles(bool a_fireOnChange, bool a_repositionElement, bool a_resizeElement) {
	auto box = MV::BoxAABB<>(topLeftSizeHandle->worldBounds().bottomRightPoint(), bottomRightSizeHandle->worldBounds().topLeftPoint());

	auto originalPosition = elementToEdit->bounds().minPoint;
	auto corners = elementToEdit->owner()->localFromWorld(box);
	MV::Size<> currentDimensions = corners.size();

	if (a_resizeElement) {
		elementToEdit->properties().minimumPosition = corners.minPoint;
		elementToEdit->properties().maximumPosition = corners.maxPoint;
	}
	auto newSize = elementToEdit->properties().maximumPosition - elementToEdit->properties().minimumPosition;
	if (a_repositionElement) {
		elementToEdit->properties().minimumPosition = corners.minPoint;
		elementToEdit->properties().maximumPosition = corners.minPoint + newSize;
	}
	else {
		elementToEdit->properties().minimumPosition = originalPosition;
		elementToEdit->properties().maximumPosition = originalPosition + newSize;
	}

	auto rectBox = MV::cast<MV::PointPrecision>(elementToEdit->screenBounds());
	positionHandle->bounds({ MV::point(0.0f, 0.0f), rectBox.size() });
	positionHandle->owner()->position(rectBox.minPoint);

	if (a_fireOnChange) {
		onChange(this);
	}
}

void EditableEmitter::position(MV::Point<> a_newPosition) {
	elementToEdit->properties().minimumPosition = a_newPosition;
	resetHandles();
}

MV::Point<> EditableEmitter::position() const {
	return elementToEdit->properties().minimumPosition;
}

void EditableEmitter::size(MV::Size<> a_newSize){
	elementToEdit->properties().maximumPosition = MV::toPoint(a_newSize) + elementToEdit->properties().minimumPosition;
	resetHandles();
}

MV::Size<> EditableEmitter::size(){
	return MV::BoxAABB<>(elementToEdit->properties().minimumPosition, elementToEdit->properties().maximumPosition).size();
}

//EDITABLE PATH MAP

void EditablePathMap::resetHandles() {
	removeHandles();
	auto rectBox = MV::round<MV::PointPrecision>(elementToEdit->screenBounds());

	auto handleSize = MV::size(8.0f, 8.0f);
	EditablePathMap* self = this;
	positionHandle = controlContainer->make(MV::guid("position"))->attach<MV::Scene::Clickable>(*mouse)->bounds(rectBox);
	positionHandle->onDrag.connect("position", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		auto gridPosition = MV::cast<int>(elementToEdit->gridFromLocal(elementToEdit->owner()->localFromScreen(handle->mouse().position())));
		if (lastGridPosition != gridPosition) {
			lastGridPosition = gridPosition;
			if (elementToEdit->inBounds(gridPosition)) {
				auto& gridNode = elementToEdit->nodeFromGrid(gridPosition);
				if (gridNode.staticallyBlocked()) {
					gridNode.staticUnblock();
				}
				else {
					gridNode.staticBlock();
				}
			}
		}
	});

	topLeftSizeHandle = controlContainer->make(MV::guid("topLeft"))->position(rectBox.topLeftPoint() - MV::toPoint(handleSize))->attach<MV::Scene::Clickable>(*mouse);
	topLeftSizeHandle->bounds(handleSize)->color({ SIZE_HANDLES })->show();
	topLeftSizeHandle->onDrag.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		auto worldDelta = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(worldDelta);
		topRightSizeHandle->owner()->translate(MV::point(0.0f, worldDelta.y));
		bottomLeftSizeHandle->owner()->translate(MV::point(worldDelta.x, 0.0f));

		repositionHandles();
	});

	topLeftSizeHandle->onRelease.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &) {
		resetHandles();
	});

	topRightSizeHandle = controlContainer->make(MV::guid("topRight"))->position(rectBox.topRightPoint() - MV::point(0.0f, handleSize.height))->attach<MV::Scene::Clickable>(*mouse);
	topRightSizeHandle->bounds(handleSize)->color({ SIZE_HANDLES })->show();
	topRightSizeHandle->onDrag.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		auto worldDelta = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(worldDelta);
		topLeftSizeHandle->owner()->translate(MV::point(0.0f, worldDelta.y));
		bottomRightSizeHandle->owner()->translate(MV::point(worldDelta.x, 0.0f));

		repositionHandles();
	});
	topRightSizeHandle->onRelease.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &) {
		resetHandles();
	});

	bottomLeftSizeHandle = controlContainer->make(MV::guid("bottomLeft"))->position(rectBox.bottomLeftPoint() - MV::point(handleSize.width, 0.0f))->attach<MV::Scene::Clickable>(*mouse);
	bottomLeftSizeHandle->bounds(handleSize)->color({ SIZE_HANDLES })->show();
	bottomLeftSizeHandle->onDrag.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		auto worldDelta = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(worldDelta);
		topLeftSizeHandle->owner()->translate(MV::point(worldDelta.x, 0.0f));
		bottomRightSizeHandle->owner()->translate(MV::point(0.0f, worldDelta.y));

		repositionHandles();
	});
	bottomLeftSizeHandle->onRelease.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &) {
		resetHandles();
	});

	bottomRightSizeHandle = controlContainer->make(MV::guid("bottomRight"))->position(rectBox.bottomRightPoint())->attach<MV::Scene::Clickable>(*mouse);
	bottomRightSizeHandle->bounds(handleSize)->color({ SIZE_HANDLES })->show();
	bottomRightSizeHandle->onDrag.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		auto worldDelta = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(worldDelta);
		topRightSizeHandle->owner()->translate(MV::point(worldDelta.x, 0.0f));
		bottomLeftSizeHandle->owner()->translate(MV::point(0.0f, worldDelta.y));

		repositionHandles();
	});
	bottomRightSizeHandle->onRelease.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &) {
		resetHandles();
	});

	repositionHandles(false, false, false);;
}

EditablePathMap::EditablePathMap(MV::Scene::SafeComponent<MV::Scene::PathMap> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse) :
	ResizeHandles(a_elementToEdit.cast<MV::Scene::Component>(), a_rootContainer, a_mouse),
	elementToEdit(a_elementToEdit) {
	elementToEdit->show();
}

void EditablePathMap::position(MV::Point<> a_newPosition) {
	elementToEdit->bounds({ a_newPosition, elementToEdit->bounds().size() });
	resetHandles();
}

MV::Point<> EditablePathMap::position() const {
	return elementToEdit->bounds().minPoint;
}

void EditablePathMap::size(MV::Size<> a_newSize) {
	elementToEdit->bounds({ elementToEdit->bounds().minPoint, a_newSize });
	resetHandles();
}

MV::Size<> EditablePathMap::size() {
	return elementToEdit->bounds().size();
}