#include "editorSelection.h"
#include "editorPanels.h"

long Selection::gid = 0;

void Selection::callback(std::function<void(const MV::BoxAABB<> &)> a_callback){
	selectedCallback = a_callback;
}

void Selection::enable(std::function<void(const MV::BoxAABB<> &)> a_callback){
	selectedCallback = a_callback;
	enable();
}

void Selection::enable(){
	onMouseDownHandle = mouse.onLeftMouseDown.connect([&](MV::MouseState &mouse){
		inSelection = true;
		selection.initialize(MV::cast<MV::PointPrecision>(mouse.position()));
		visibleSelection = scene->make<MV::Scene::Rectangle>("Selection_" + boost::lexical_cast<std::string>(id), MV::Size<>())->position(selection.minPoint);
		visibleSelection->color(MV::Color(1.0, 1.0, 0.0, .25));
		auto originalPosition = visibleSelection->localFromScreen(mouse.position());
		onMouseMoveHandle = mouse.onMove.connect([&, originalPosition](MV::MouseState &mouse){
			visibleSelection->bounds({originalPosition, visibleSelection->localFromScreen(mouse.position())});
		});
	});

	onMouseUpHandle = mouse.onLeftMouseUp.connect([&](MV::MouseState &mouse){
		if(!inSelection){
			return;
		}
		SCOPE_EXIT{
			exitSelection(); //callback might throw, let's be safe.
		};

		selection.expandWith(MV::cast<MV::PointPrecision>(mouse.position()));
		if(selectedCallback){
			selectedCallback(selection);
		}
	});
}

void Selection::exitSelection(){
	inSelection = false;
	onMouseMoveHandle.reset();
	if(visibleSelection){
		visibleSelection->removeFromParent();
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

void EditableRectangle::resetHandles() {
	removeHandles();
	auto rectBox = MV::cast<MV::PointPrecision>(elementToEdit->screenAABB());

	auto handleSize = MV::point(8.0f, 8.0f);
	EditableRectangle* self = this;
	positionHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("position"), mouse, rectBox);
	positionHandle->onDrag.connect("position", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		auto castPosition = MV::cast<MV::PointPrecision>(deltaPosition);
		handle->translate(castPosition);
		elementToEdit->translate(castPosition);
		topLeftSizeHandle->translate(castPosition);
		topRightSizeHandle->translate(castPosition);
		bottomLeftSizeHandle->translate(castPosition);
		bottomRightSizeHandle->translate(castPosition);
		onChange(self);
	});

	topLeftSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("topLeft"), mouse, MV::BoxAABB<>(rectBox.topLeftPoint(), rectBox.topLeftPoint() - (handleSize * MV::point(1.0f, 1.0f))));
	topLeftSizeHandle->color({SIZE_HANDLES});
	topLeftSizeHandle->onDrag.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topRightSizeHandle->position(MV::point(topRightSizeHandle->position().x, topLeftSizeHandle->position().y));
		bottomLeftSizeHandle->position(MV::point(topLeftSizeHandle->position().x, bottomLeftSizeHandle->position().y));

		dragUpdateFromHandles();
	});
	topLeftSizeHandle->onRelease.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle){
		resetHandles();
	});

	topRightSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("topRight"), mouse, MV::BoxAABB<>(rectBox.topRightPoint(), rectBox.topRightPoint() + (handleSize * MV::point(1.0f, -1.0f))));
	topRightSizeHandle->color({SIZE_HANDLES});
	topRightSizeHandle->onDrag.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topLeftSizeHandle->position(MV::point(topLeftSizeHandle->position().x, topRightSizeHandle->position().y));
		bottomRightSizeHandle->position(MV::point(topRightSizeHandle->position().x, bottomRightSizeHandle->position().y));

		dragUpdateFromHandles();
	});
	topRightSizeHandle->onRelease.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle){
		resetHandles();
	});

	bottomLeftSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("bottomLeft"), mouse, MV::BoxAABB<>(rectBox.bottomLeftPoint(), rectBox.bottomLeftPoint() - (handleSize * MV::point(1.0f, -1.0f))));
	bottomLeftSizeHandle->color({SIZE_HANDLES});
	bottomLeftSizeHandle->onDrag.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topLeftSizeHandle->position(MV::point(bottomLeftSizeHandle->position().x, topLeftSizeHandle->position().y));
		bottomRightSizeHandle->position(MV::point(bottomRightSizeHandle->position().x, bottomLeftSizeHandle->position().y));

		dragUpdateFromHandles();
	});
	bottomLeftSizeHandle->onRelease.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle){
		resetHandles();
	});

	bottomRightSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("bottomRight"), mouse, MV::BoxAABB<>(rectBox.bottomRightPoint(), rectBox.bottomRightPoint() + (handleSize * MV::point(1.0f, 1.0f))));
	bottomRightSizeHandle->color({SIZE_HANDLES});
	bottomRightSizeHandle->onDrag.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topRightSizeHandle->position(MV::point(bottomRightSizeHandle->position().x, topRightSizeHandle->position().y));
		bottomLeftSizeHandle->position(MV::point(bottomLeftSizeHandle->position().x, bottomRightSizeHandle->position().y));

		dragUpdateFromHandles();
	});
	bottomRightSizeHandle->onRelease.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle){
		resetHandles();
	});
}

EditableRectangle::EditableRectangle(std::shared_ptr<MV::Scene::Rectangle> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_controlContainer, MV::MouseState *a_mouse):
	elementToEdit(a_elementToEdit),
	controlContainer(a_controlContainer),
	mouse(a_mouse) {
	resetHandles();
}

void EditableRectangle::removeHandles() {
	controlContainer->remove(positionHandle);
	controlContainer->remove(topLeftSizeHandle);
	controlContainer->remove(topRightSizeHandle);
	controlContainer->remove(bottomLeftSizeHandle);
	controlContainer->remove(bottomRightSizeHandle);

	positionHandle.reset();
	topLeftSizeHandle.reset();
	topRightSizeHandle.reset();
	bottomLeftSizeHandle.reset();
	bottomRightSizeHandle.reset();
}

void EditableRectangle::dragUpdateFromHandles() {
	auto box = MV::BoxAABB<int>(topLeftSizeHandle->screenAABB().bottomRightPoint(), bottomRightSizeHandle->screenAABB().topLeftPoint());

	elementToEdit->position({});
	auto corners = elementToEdit->localFromScreen(box);

	elementToEdit->size(corners.size());
	positionHandle->size(corners.size());

	elementToEdit->position(corners.minPoint);
	positionHandle->position(corners.minPoint);

	onChange(this);
}

void EditableRectangle::position(MV::Point<> a_newPosition) {
	elementToEdit->position(a_newPosition);
	resetHandles();
}

MV::Point<> EditableRectangle::position() const {
	return elementToEdit->position();
}

void EditableRectangle::size(MV::Size<> a_newSize){
	elementToEdit->size(a_newSize);
	resetHandles();
}

MV::Size<> EditableRectangle::size(){
	return elementToEdit->localAABB().size();
}



//EDITABLE EMITTER

void EditableEmitter::resetHandles() {
	removeHandles();
	auto rectBox = MV::cast<MV::PointPrecision>(elementToEdit->screenAABB());

	auto handleSize = MV::point(8.0f, 8.0f);
	EditableEmitter* self = this;
	positionHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("position"), mouse, rectBox);
	positionHandle->onDrag.connect("position", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		auto castPosition = MV::cast<MV::PointPrecision>(deltaPosition);
		handle->translate(castPosition);
		elementToEdit->translate(castPosition);
		topLeftSizeHandle->translate(castPosition);
		topRightSizeHandle->translate(castPosition);
		bottomLeftSizeHandle->translate(castPosition);
		bottomRightSizeHandle->translate(castPosition);
		onChange(self);
	});

	topLeftSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("topLeft"), mouse, MV::BoxAABB<>(rectBox.topLeftPoint(), rectBox.topLeftPoint() - (handleSize * MV::point(1.0f, 1.0f))));
	topLeftSizeHandle->color({SIZE_HANDLES});
	topLeftSizeHandle->onDrag.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topRightSizeHandle->position(MV::point(topRightSizeHandle->position().x, topLeftSizeHandle->position().y));
		bottomLeftSizeHandle->position(MV::point(topLeftSizeHandle->position().x, bottomLeftSizeHandle->position().y));

		dragUpdateFromHandles();
	});
	topLeftSizeHandle->onRelease.connect("topLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle){
		resetHandles();
	});

	topRightSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("topRight"), mouse, MV::BoxAABB<>(rectBox.topRightPoint(), rectBox.topRightPoint() + (handleSize * MV::point(1.0f, -1.0f))));
	topRightSizeHandle->color({SIZE_HANDLES});
	topRightSizeHandle->onDrag.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topLeftSizeHandle->position(MV::point(topLeftSizeHandle->position().x, topRightSizeHandle->position().y));
		bottomRightSizeHandle->position(MV::point(topRightSizeHandle->position().x, bottomRightSizeHandle->position().y));

		dragUpdateFromHandles();
	});
	topRightSizeHandle->onRelease.connect("topRight", [&](std::shared_ptr<MV::Scene::Clickable> handle){
		resetHandles();
	});

	bottomLeftSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("bottomLeft"), mouse, MV::BoxAABB<>(rectBox.bottomLeftPoint(), rectBox.bottomLeftPoint() - (handleSize * MV::point(1.0f, -1.0f))));
	bottomLeftSizeHandle->color({SIZE_HANDLES});
	bottomLeftSizeHandle->onDrag.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topLeftSizeHandle->position(MV::point(bottomLeftSizeHandle->position().x, topLeftSizeHandle->position().y));
		bottomRightSizeHandle->position(MV::point(bottomRightSizeHandle->position().x, bottomLeftSizeHandle->position().y));

		dragUpdateFromHandles();
	});
	bottomLeftSizeHandle->onRelease.connect("bottomLeft", [&](std::shared_ptr<MV::Scene::Clickable> handle){
		resetHandles();
	});

	bottomRightSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("bottomRight"), mouse, MV::BoxAABB<>(rectBox.bottomRightPoint(), rectBox.bottomRightPoint() + (handleSize * MV::point(1.0f, 1.0f))));
	bottomRightSizeHandle->color({SIZE_HANDLES});
	bottomRightSizeHandle->onDrag.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->translate(MV::cast<MV::PointPrecision>(deltaPosition));
		topRightSizeHandle->position(MV::point(bottomRightSizeHandle->position().x, topRightSizeHandle->position().y));
		bottomLeftSizeHandle->position(MV::point(bottomLeftSizeHandle->position().x, bottomRightSizeHandle->position().y));

		dragUpdateFromHandles();
	});
	bottomRightSizeHandle->onRelease.connect("bottomRight", [&](std::shared_ptr<MV::Scene::Clickable> handle){
		resetHandles();
	});
}

EditableEmitter::EditableEmitter(std::shared_ptr<MV::Scene::Emitter> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_controlContainer, MV::MouseState *a_mouse):
	elementToEdit(a_elementToEdit),
	controlContainer(a_controlContainer),
	mouse(a_mouse) {
	resetHandles();
}

void EditableEmitter::removeHandles() {
	controlContainer->remove(positionHandle);
	controlContainer->remove(topLeftSizeHandle);
	controlContainer->remove(topRightSizeHandle);
	controlContainer->remove(bottomLeftSizeHandle);
	controlContainer->remove(bottomRightSizeHandle);

	positionHandle.reset();
	topLeftSizeHandle.reset();
	topRightSizeHandle.reset();
	bottomLeftSizeHandle.reset();
	bottomRightSizeHandle.reset();
}

void EditableEmitter::dragUpdateFromHandles() {
	auto box = MV::boxaabb(topLeftSizeHandle->screenAABB().bottomRightPoint(), bottomRightSizeHandle->screenAABB().topLeftPoint());

	elementToEdit->position({});
	auto corners = elementToEdit->localFromScreen(box);

	elementToEdit->properties().minimumPosition = {0.0f, 0.0f};
	elementToEdit->properties().maximumPosition = MV::toPoint(corners.size());

	positionHandle->size(corners.size());

	elementToEdit->position(corners.minPoint);
	positionHandle->position(corners.minPoint);

	onChange(this);
}

void EditableEmitter::position(MV::Point<> a_newPosition) {
	elementToEdit->position(a_newPosition);
	resetHandles();
}

MV::Point<> EditableEmitter::position() const {
	return elementToEdit->position();
}

void EditableEmitter::size(MV::Size<> a_newSize){
	elementToEdit->properties().minimumPosition = {0.0f, 0.0f};
	elementToEdit->properties().maximumPosition = MV::toPoint(a_newSize);
	resetHandles();
}

MV::Size<> EditableEmitter::size(){
	return MV::BoxAABB<>(elementToEdit->properties().minimumPosition, elementToEdit->properties().maximumPosition).size();
}
