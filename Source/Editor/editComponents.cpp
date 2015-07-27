#include "editComponents.h"
#include "componentPanels.h"
#include "Utility/generalUtility.h"

long Selection::gid = 0;

void Selection::callback(std::function<void(const MV::BoxAABB<int> &)> a_callback){
	selectedCallback = a_callback;
}

void Selection::enable(std::function<void(const MV::BoxAABB<int> &)> a_callback){
	selectedCallback = a_callback;
	enable();
}

void Selection::enable(){
	onMouseDownHandle = mouse.onLeftMouseDown.connect([&](MV::MouseState &mouse){
		mouse.queueExclusiveAction(MV::ExclusiveMouseAction(true, {10000}, [&](){
			inSelection = true;
			selection.initialize(mouse.position());
			visibleSelection = scene->make("Selection_" + std::to_string(id))->position(scene->localFromScreen(selection.minPoint))->attach<MV::Scene::Sprite>()->color(MV::Color(1.0f, 1.0f, 0.0f, .25f))->safe();
			auto originalPosition = visibleSelection->owner()->localFromScreen(mouse.position());
			onMouseMoveHandle = mouse.onMove.connect([&, originalPosition](MV::MouseState &mouse){
				visibleSelection->bounds({originalPosition, visibleSelection->owner()->localFromScreen(mouse.position())});
			});
		}, [](){}, "SelectBox"));
	});

	onMouseUpHandle = mouse.onLeftMouseUp.connect([&](MV::MouseState &mouse){
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

Selection::Selection(std::shared_ptr<MV::Scene::Node> a_scene, MV::MouseState &a_mouse):
	mouse(a_mouse),
	scene(a_scene),
	id(gid++) {
}

EditableNode::EditableNode(std::shared_ptr<MV::Scene::Node> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::MouseState *a_mouse) :
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
	positionHandle->size({ 7.0f, 7.0f }, true)->show()->color({ POSITION_HANDLE })->globalPriority(positionHandle->globalPriority() + 100);
	positionHandle->owner()->nodePosition(elementToEdit)->attach<MV::Scene::Sprite>()->size({ 1.0f, 1.0f }, true)->color({ POSITION_HANDLE_CENTER });
	positionHandle->onDrag.connect("position", [&, self](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		handle->owner()->translate(elementToEdit->renderer().worldFromScreen(deltaPosition));
		elementToEdit->nodePosition(handle->owner());
		onChange(self);
	});

	rotationHandle = positionHandle->owner()->make("RotationControl")->serializable(false)->attach<MV::Scene::Clickable>(*mouse);
	rotationHandle->size({ 5.0f, 5.0f }, {-5.0f,-5.0f})->show()->color({ ROTATION_HANDLE })->globalPriority(rotationHandle->globalPriority() + 90);
	rotationHandle->onDrag.connect("rotation", [&, self](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		MV::PointPrecision angle = static_cast<float>(MV::angle(positionHandle->owner()->screenPosition(), handle->mouse().position()));
		positionHandle->owner()->worldRotation({ 0.0f, 0.0f, angle - 45.0f });
		elementToEdit->worldRotation(positionHandle->owner()->worldRotation());
		onChange(self);
	});

	positionHandle->owner()->worldRotation(elementToEdit->worldRotation());
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


EditableGrid::EditableGrid(MV::Scene::SafeComponent<MV::Scene::Grid> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::MouseState *a_mouse):
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
	positionHandle->size(currentDimensions)->color({0x22FFFFFF});
}

void EditableRectangle::resetHandles() {
	removeHandles();
	auto rectBox = MV::round<MV::PointPrecision>(elementToEdit->screenBounds());

	MV::Size<> currentDimensions = rectBox.size();
	if(aspectSize.width != 0.0f && aspectSize.height != 0.0f){
		float aspectRatio = aspectSize.width / aspectSize.height;

		if(currentDimensions.width < currentDimensions.height){
			currentDimensions.height = currentDimensions.width * aspectSize.height / aspectSize.width;
		} else if(currentDimensions.width > currentDimensions.height){
			currentDimensions.width = currentDimensions.height * aspectSize.width / aspectSize.height;
		}
	}
	rectBox.maxPoint = rectBox.minPoint + toPoint(currentDimensions);

	auto handleSize = MV::point(8.0f, 8.0f);
	EditableRectangle* self = this;
	positionHandle = controlContainer->make(MV::guid("position"))->attach<MV::Scene::Clickable>(*mouse);
	positionHandle->onDrag.connect("position", [&, self](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		auto castPosition = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(castPosition);
		elementToEdit->bounds({ elementToEdit->owner()->localFromWorld(handle->owner()->worldPosition()), elementToEdit->bounds().size() });
		topLeftSizeHandle->owner()->translate(castPosition);
		topRightSizeHandle->owner()->translate(castPosition);
		bottomLeftSizeHandle->owner()->translate(castPosition);
		bottomRightSizeHandle->owner()->translate(castPosition);
		onChange(self);
	});

	topLeftSizeHandle = controlContainer->make(MV::guid("topLeft"))->position(rectBox.topLeftPoint() - handleSize)->attach<MV::Scene::Clickable>(*mouse);
	topLeftSizeHandle->size(toSize(handleSize))->color({SIZE_HANDLES})->show();
	topLeftSizeHandle->onDrag.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		auto worldDelta = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(worldDelta);
		topRightSizeHandle->owner()->translate(MV::point(0.0f, worldDelta.y));
		bottomLeftSizeHandle->owner()->translate(MV::point(worldDelta.x, 0.0f));

		repositionHandles();
	});

	topLeftSizeHandle->onRelease.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &){
		resetHandles();
	});

	topRightSizeHandle = controlContainer->make(MV::guid("topRight"))->position(rectBox.topRightPoint() - MV::point(0.0f, handleSize.y))->attach<MV::Scene::Clickable>(*mouse);
	topRightSizeHandle->size(toSize(handleSize))->color({SIZE_HANDLES})->show();
	topRightSizeHandle->onDrag.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		auto worldDelta = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(worldDelta);
		topLeftSizeHandle->owner()->translate(MV::point(0.0f, worldDelta.y));
		bottomRightSizeHandle->owner()->translate(MV::point(worldDelta.x, 0.0f));

		repositionHandles();
	});
	topRightSizeHandle->onRelease.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &){
		resetHandles();
	});

	bottomLeftSizeHandle = controlContainer->make(MV::guid("bottomLeft"))->position(rectBox.bottomLeftPoint() - MV::point(handleSize.x, 0.0f))->attach<MV::Scene::Clickable>(*mouse);
	bottomLeftSizeHandle->size(toSize(handleSize))->color({SIZE_HANDLES})->show();
	bottomLeftSizeHandle->onDrag.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		auto worldDelta = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(worldDelta);
		topLeftSizeHandle->owner()->translate(MV::point(worldDelta.x, 0.0f));
		bottomRightSizeHandle->owner()->translate(MV::point(0.0f, worldDelta.y));

		repositionHandles();
	});
	bottomLeftSizeHandle->onRelease.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &){
		resetHandles();
	});

	bottomRightSizeHandle = controlContainer->make(MV::guid("bottomRight"))->position(rectBox.bottomRightPoint())->attach<MV::Scene::Clickable>(*mouse);
	bottomRightSizeHandle->size(toSize(handleSize))->color({SIZE_HANDLES})->show();
	bottomRightSizeHandle->onDrag.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		auto worldDelta = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(worldDelta);
		topRightSizeHandle->owner()->translate(MV::point(worldDelta.x, 0.0f));
		bottomLeftSizeHandle->owner()->translate(MV::point(0.0f, worldDelta.y));

		repositionHandles();
	});
	bottomRightSizeHandle->onRelease.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &){
		resetHandles();
	});

	repositionHandles(false, false, false);
}

EditableRectangle::EditableRectangle(MV::Scene::SafeComponent<MV::Scene::Sprite> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::MouseState *a_mouse):
	elementToEdit(a_elementToEdit),
	controlContainer(a_rootContainer->make("Editable")->depth(-100.0f)),
	mouse(a_mouse) {
	resetHandles();
	nodeMoved = elementToEdit->owner()->onTransformChange.connect([&](const std::shared_ptr<MV::Scene::Node> &a_this){
		resetHandles();
	});
}

void EditableRectangle::removeHandles() {
	if (positionHandle) {
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

void EditableRectangle::repositionHandles(bool a_fireOnChange, bool a_repositionElement, bool a_resizeElement) {
	auto box = MV::BoxAABB<>(topLeftSizeHandle->worldBounds().bottomRightPoint(), bottomRightSizeHandle->worldBounds().topLeftPoint());

	auto originalPosition = elementToEdit->bounds().minPoint;
	auto corners = elementToEdit->owner()->localFromWorld(box);
	MV::Size<> currentDimensions = corners.size();

	if(aspectSize.width != 0.0f && aspectSize.height != 0.0f){
		float aspectRatio = aspectSize.width / aspectSize.height;

		if(currentDimensions.width < currentDimensions.height){
			currentDimensions.height = currentDimensions.width * aspectSize.height / aspectSize.width;
		} else if(currentDimensions.width > currentDimensions.height){
			currentDimensions.width = currentDimensions.height * aspectSize.width / aspectSize.height;
		}
	}

	if (a_resizeElement) {
		elementToEdit->size(currentDimensions);
	}
	if (a_repositionElement) {
		elementToEdit->bounds({ corners.minPoint, corners.size() });
	} else {
		elementToEdit->bounds({ originalPosition, corners.size() });
	}

	auto rectBox = MV::round<MV::PointPrecision>(MV::round<MV::PointPrecision>(elementToEdit->screenBounds()));
	positionHandle->bounds({ MV::point(0.0f, 0.0f), rectBox.size() });
	positionHandle->owner()->position(rectBox.minPoint);

	if(a_fireOnChange){
		onChange(this);
	}
}

void EditableRectangle::position(MV::Point<> a_newPosition) {
	elementToEdit->bounds({ a_newPosition, elementToEdit->bounds().size() });
	resetHandles();
}

MV::Point<> EditableRectangle::position() const {
	return elementToEdit->bounds().minPoint;
}

void EditableRectangle::size(MV::Size<> a_newSize){
	elementToEdit->bounds({elementToEdit->bounds().minPoint, a_newSize});
	resetHandles();
}

MV::Size<> EditableRectangle::size(){
	return elementToEdit->bounds().size();
}

void EditableRectangle::texture(const std::shared_ptr<MV::TextureHandle> a_handle) {
	elementToEdit->texture(a_handle);
}

std::shared_ptr<MV::TextureHandle> EditableRectangle::texture() const {
	return elementToEdit->texture();
}

void EditableRectangle::aspect(MV::Size<> a_newAspect) {
	aspectSize = a_newAspect;
	resetHandles();
}



//EDITABLE EMITTER

void EditableEmitter::resetHandles() {
	removeHandles();
	auto rectBox = MV::round<MV::PointPrecision>(elementToEdit->screenBounds());

	auto handleSize = MV::point(8.0f, 8.0f);
	EditableEmitter* self = this;
	positionHandle = controlContainer->make(MV::guid("position"))->attach<MV::Scene::Clickable>(*mouse)->bounds(rectBox);
	positionHandle->onDrag.connect("position", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		auto castPosition = handle->owner()->renderer().worldFromScreen(deltaPosition);
		handle->owner()->translate(castPosition);
		elementToEdit->owner()->nodePosition(handle->owner());
		topLeftSizeHandle->owner()->translate(castPosition);
		topRightSizeHandle->owner()->translate(castPosition);
		bottomLeftSizeHandle->owner()->translate(castPosition);
		bottomRightSizeHandle->owner()->translate(castPosition);
		onChange(self);
	});

	topLeftSizeHandle = controlContainer->make(MV::guid("topLeft"))->position(rectBox.topLeftPoint() - handleSize)->attach<MV::Scene::Clickable>(*mouse);
	topLeftSizeHandle->size(toSize(handleSize))->color({ SIZE_HANDLES })->show();
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

	topRightSizeHandle = controlContainer->make(MV::guid("topRight"))->position(rectBox.topRightPoint() - MV::point(0.0f, handleSize.y))->attach<MV::Scene::Clickable>(*mouse);
	topRightSizeHandle->size(toSize(handleSize))->color({ SIZE_HANDLES })->show();
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

	bottomLeftSizeHandle = controlContainer->make(MV::guid("bottomLeft"))->position(rectBox.bottomLeftPoint() - MV::point(handleSize.x, 0.0f))->attach<MV::Scene::Clickable>(*mouse);
	bottomLeftSizeHandle->size(toSize(handleSize))->color({ SIZE_HANDLES })->show();
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
	bottomRightSizeHandle->size(toSize(handleSize))->color({ SIZE_HANDLES })->show();
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

EditableEmitter::EditableEmitter(MV::Scene::SafeComponent<MV::Scene::Emitter> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::MouseState *a_mouse):
	elementToEdit(a_elementToEdit),
	controlContainer(a_rootContainer->make("Editable")->depth(-100.0f)),
	mouse(a_mouse) {
	resetHandles();

	nodeMoved = elementToEdit->owner()->onTransformChange.connect([&](const std::shared_ptr<MV::Scene::Node> &a_this) {
		resetHandles();
	});
}

void EditableEmitter::removeHandles() {
	if (positionHandle) {
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

void EditableEmitter::texture(const std::shared_ptr<MV::TextureHandle> a_handle) {
	elementToEdit->texture(a_handle);
}
