#include "editorSelection.h"
#include "editorPanels.h"

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
			visibleSelection = scene->make("Selection_" + std::to_string(id))->position(scene->localFromScreen(selection.minPoint))->attach<MV::Scene::Sprite>()->color(MV::Color(1.0, 1.0, 0.0, .25))->safe();
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
			exitSelection(); //callback might throw, let's be safe.
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

EditableGrid::EditableGrid(MV::Scene::SafeComponent<MV::Scene::Grid> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::MouseState *a_mouse):
	elementToEdit(a_elementToEdit),
	controlContainer(a_rootContainer->make("Editable")->depth(-100.0f)),
	mouse(a_mouse) {

	resetHandles();
}

void EditableGrid::removeHandles() {
	if (positionHandle) {
		controlContainer->remove(positionHandle->owner());
	}
	positionHandle.reset();
}

void EditableGrid::resetHandles() {
	removeHandles();
	auto rectBox = MV::cast<MV::PointPrecision>(elementToEdit->owner()->screenBounds(false));

	MV::Size<> currentDimensions = rectBox.size();
	if (currentDimensions.area() < 25.0f) {
		currentDimensions.width = 5.0f;
		currentDimensions.height = 5.0f;
	}

	rectBox.maxPoint = rectBox.minPoint + toPoint(currentDimensions);

	EditableGrid* self = this;
	positionHandle = controlContainer->make(MV::guid("position"))->position(rectBox.minPoint)->attach<MV::Scene::Clickable>(*mouse);
	positionHandle->size(currentDimensions)->show();
	positionHandle->onDrag.connect("position", [&, self](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		auto castPosition = MV::cast<MV::PointPrecision>(deltaPosition);
		handle->owner()->translate(castPosition);
		elementToEdit->owner()->translate(castPosition);
		onChange(self);
	});
}

void EditableGrid::position(MV::Point<> a_newPosition) {
	elementToEdit->owner()->position(a_newPosition);
	resetHandles();
}

MV::Point<> EditableGrid::position() const {
	return elementToEdit->owner()->position();
}

void EditableRectangle::resetHandles() {
	removeHandles();
	auto rectBox = MV::cast<MV::PointPrecision>(elementToEdit->owner()->screenBounds(false));

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
		auto castPosition = MV::cast<MV::PointPrecision>(deltaPosition);
		handle->owner()->translate(castPosition);
		elementToEdit->owner()->position(elementToEdit->owner()->localFromScreen(elementToEdit->owner()->screenPosition() + deltaPosition));
		topLeftSizeHandle->owner()->translate(castPosition);
		topRightSizeHandle->owner()->translate(castPosition);
		bottomLeftSizeHandle->owner()->translate(castPosition);
		bottomRightSizeHandle->owner()->translate(castPosition);
		onChange(self);
	});

	topLeftSizeHandle = controlContainer->make(MV::guid("topLeft"))->attach<MV::Scene::Clickable>(*mouse)->bounds(MV::BoxAABB<>(rectBox.topLeftPoint(), rectBox.topLeftPoint() - (handleSize * MV::point(1.0f, 1.0f))));
	topLeftSizeHandle->color({SIZE_HANDLES})->show();
	topLeftSizeHandle->onDrag.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->owner()->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topRightSizeHandle->owner()->position(MV::point(topRightSizeHandle->owner()->position().x, topLeftSizeHandle->owner()->position().y));
		bottomLeftSizeHandle->owner()->position(MV::point(topLeftSizeHandle->owner()->position().x, bottomLeftSizeHandle->owner()->position().y));

		repositionHandles();
	});
	topLeftSizeHandle->onRelease.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &){
		resetHandles();
	});

	topRightSizeHandle = controlContainer->make(MV::guid("topRight"))->attach<MV::Scene::Clickable>(*mouse)->bounds(MV::BoxAABB<>(rectBox.topRightPoint(), rectBox.topRightPoint() + (handleSize * MV::point(1.0f, -1.0f))));
	topRightSizeHandle->color({SIZE_HANDLES})->show();
	topRightSizeHandle->onDrag.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->owner()->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topLeftSizeHandle->owner()->position(MV::point(topLeftSizeHandle->owner()->position().x, topRightSizeHandle->owner()->position().y));
		bottomRightSizeHandle->owner()->position(MV::point(topRightSizeHandle->owner()->position().x, bottomRightSizeHandle->owner()->position().y));

		repositionHandles();
	});
	topRightSizeHandle->onRelease.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &){
		resetHandles();
	});

	bottomLeftSizeHandle = controlContainer->make(MV::guid("bottomLeft"))->attach<MV::Scene::Clickable>(*mouse)->bounds(MV::BoxAABB<>(rectBox.bottomLeftPoint(), rectBox.bottomLeftPoint() - (handleSize * MV::point(1.0f, -1.0f))));
	bottomLeftSizeHandle->color({SIZE_HANDLES})->show();
	bottomLeftSizeHandle->onDrag.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->owner()->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topLeftSizeHandle->owner()->position(MV::point(bottomLeftSizeHandle->owner()->position().x, topLeftSizeHandle->owner()->position().y));
		bottomRightSizeHandle->owner()->position(MV::point(bottomRightSizeHandle->owner()->position().x, bottomLeftSizeHandle->owner()->position().y));

		repositionHandles();
	});
	bottomLeftSizeHandle->onRelease.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &){
		resetHandles();
	});

	bottomRightSizeHandle = controlContainer->make(MV::guid("bottomRight"))->attach<MV::Scene::Clickable>(*mouse)->bounds(MV::BoxAABB<>(rectBox.bottomRightPoint(), rectBox.bottomRightPoint() + (handleSize * MV::point(1.0f, 1.0f))));
	bottomRightSizeHandle->color({SIZE_HANDLES})->show();
	bottomRightSizeHandle->onDrag.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->owner()->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topRightSizeHandle->owner()->position(MV::point(bottomRightSizeHandle->owner()->position().x, topRightSizeHandle->owner()->position().y));
		bottomLeftSizeHandle->owner()->position(MV::point(bottomLeftSizeHandle->owner()->position().x, bottomRightSizeHandle->owner()->position().y));

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
	auto box = MV::BoxAABB<int>(topLeftSizeHandle->screenBounds().bottomRightPoint(), bottomRightSizeHandle->screenBounds().topLeftPoint());

	auto originalPosition = elementToEdit->owner()->position();
	elementToEdit->owner()->position({});
	auto corners = MV::round(elementToEdit->owner()->localFromScreen(box));
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
		elementToEdit->owner()->position(corners.minPoint);
	} else {
		elementToEdit->owner()->position(originalPosition);
	}

	auto rectBox = MV::round(MV::cast<MV::PointPrecision>(elementToEdit->screenBounds()));
	positionHandle->bounds(rectBox);
	positionHandle->owner()->position({});

	if(a_fireOnChange){
		onChange(this);
	}
}

void EditableRectangle::position(MV::Point<> a_newPosition) {
	elementToEdit->owner()->position(a_newPosition);
	resetHandles();
}

MV::Point<> EditableRectangle::position() const {
	return elementToEdit->owner()->position();
}

void EditableRectangle::size(MV::Size<> a_newSize){
	elementToEdit->size(a_newSize);
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
	auto rectBox = MV::cast<MV::PointPrecision>(elementToEdit->screenBounds());

	auto handleSize = MV::point(8.0f, 8.0f);
	EditableEmitter* self = this;
	positionHandle = controlContainer->make(MV::guid("position"))->attach<MV::Scene::Clickable>(*mouse)->bounds(rectBox);
	positionHandle->onDrag.connect("position", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		auto castPosition = MV::cast<MV::PointPrecision>(deltaPosition);
		handle->owner()->translate(castPosition);
		elementToEdit->owner()->position(elementToEdit->owner()->localFromScreen(elementToEdit->owner()->screenPosition() + deltaPosition));
		topLeftSizeHandle->owner()->translate(castPosition);
		topRightSizeHandle->owner()->translate(castPosition);
		bottomLeftSizeHandle->owner()->translate(castPosition);
		bottomRightSizeHandle->owner()->translate(castPosition);
		onChange(self);
	});

	topLeftSizeHandle = controlContainer->make(MV::guid("topLeft"))->attach<MV::Scene::Clickable>(*mouse)->bounds(MV::BoxAABB<>(rectBox.topLeftPoint(), rectBox.topLeftPoint() - (handleSize * MV::point(1.0f, 1.0f))));
	topLeftSizeHandle->color({SIZE_HANDLES})->show();
	topLeftSizeHandle->onDrag.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->owner()->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topRightSizeHandle->owner()->position(MV::point(topRightSizeHandle->owner()->position().x, topLeftSizeHandle->owner()->position().y));
		bottomLeftSizeHandle->owner()->position(MV::point(topLeftSizeHandle->owner()->position().x, bottomLeftSizeHandle->owner()->position().y));

		repositionHandles();
	});
	topLeftSizeHandle->onRelease.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &){
		resetHandles();
	});

	topRightSizeHandle = controlContainer->make(MV::guid("topRight"))->attach<MV::Scene::Clickable>(*mouse)->bounds(MV::BoxAABB<>(rectBox.topRightPoint(), rectBox.topRightPoint() + (handleSize * MV::point(1.0f, -1.0f))));
	topRightSizeHandle->color({SIZE_HANDLES})->show();
	topRightSizeHandle->onDrag.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->owner()->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topLeftSizeHandle->owner()->position(MV::point(topLeftSizeHandle->owner()->position().x, topRightSizeHandle->owner()->position().y));
		bottomRightSizeHandle->owner()->position(MV::point(topRightSizeHandle->owner()->position().x, bottomRightSizeHandle->owner()->position().y));

		repositionHandles();
	});
	topRightSizeHandle->onRelease.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &){
		resetHandles();
	});

	bottomLeftSizeHandle = controlContainer->make(MV::guid("bottomLeft"))->attach<MV::Scene::Clickable>(*mouse)->bounds(MV::BoxAABB<>(rectBox.bottomLeftPoint(), rectBox.bottomLeftPoint() - (handleSize * MV::point(1.0f, -1.0f))));
	bottomLeftSizeHandle->color({SIZE_HANDLES})->show();
	bottomLeftSizeHandle->onDrag.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->owner()->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topLeftSizeHandle->owner()->position(MV::point(bottomLeftSizeHandle->owner()->position().x, topLeftSizeHandle->owner()->position().y));
		bottomRightSizeHandle->owner()->position(MV::point(bottomRightSizeHandle->owner()->position().x, bottomLeftSizeHandle->owner()->position().y));

		repositionHandles();
	});
	bottomLeftSizeHandle->onRelease.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &){
		resetHandles();
	});

	bottomRightSizeHandle = controlContainer->make(MV::guid("bottomRight"))->attach<MV::Scene::Clickable>(*mouse)->bounds(MV::BoxAABB<>(rectBox.bottomRightPoint(), rectBox.bottomRightPoint() + (handleSize * MV::point(1.0f, 1.0f))));
	bottomRightSizeHandle->color({SIZE_HANDLES})->show();
	bottomRightSizeHandle->onDrag.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->owner()->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topRightSizeHandle->owner()->position(MV::point(bottomRightSizeHandle->owner()->position().x, topRightSizeHandle->owner()->position().y));
		bottomLeftSizeHandle->owner()->position(MV::point(bottomLeftSizeHandle->owner()->position().x, bottomRightSizeHandle->owner()->position().y));

		repositionHandles();
	});
	bottomRightSizeHandle->onRelease.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<MV::PointPrecision> &){
		resetHandles();
	});

	repositionHandles(false, false, false);;
}

EditableEmitter::EditableEmitter(MV::Scene::SafeComponent<MV::Scene::Emitter> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::MouseState *a_mouse):
	elementToEdit(a_elementToEdit),
	controlContainer(a_rootContainer->make("Editable")->depth(-100.0f)),
	mouse(a_mouse) {
	resetHandles();
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
	auto box = MV::boxaabb(topLeftSizeHandle->screenBounds().bottomRightPoint(), bottomRightSizeHandle->screenBounds().topLeftPoint());

	auto originalPosition = elementToEdit->owner()->position();
	elementToEdit->owner()->position({});
	auto corners = elementToEdit->owner()->localFromScreen(box);

	if (a_resizeElement) {
		elementToEdit->properties().minimumPosition = { 0.0f, 0.0f };
		elementToEdit->properties().maximumPosition = MV::toPoint(corners.size());
	}
	if (a_repositionElement) {
		elementToEdit->owner()->position(corners.minPoint);
	} else {
		elementToEdit->owner()->position(originalPosition);
	}

	auto rectBox = MV::cast<MV::PointPrecision>(elementToEdit->screenBounds());
	positionHandle->bounds(rectBox);
	positionHandle->owner()->position({});

	if(a_fireOnChange){
		onChange(this);
	}
}

void EditableEmitter::position(MV::Point<> a_newPosition) {
	elementToEdit->owner()->position(a_newPosition);
	resetHandles();
}

MV::Point<> EditableEmitter::position() const {
	return elementToEdit->owner()->position();
}

void EditableEmitter::size(MV::Size<> a_newSize){
	elementToEdit->properties().minimumPosition = {0.0f, 0.0f};
	elementToEdit->properties().maximumPosition = MV::toPoint(a_newSize);
	resetHandles();
}

MV::Size<> EditableEmitter::size(){
	return MV::BoxAABB<>(elementToEdit->properties().minimumPosition, elementToEdit->properties().maximumPosition).size();
}

void EditableEmitter::texture(const std::shared_ptr<MV::TextureHandle> a_handle) {
	elementToEdit->texture(a_handle);
}
