#include "editorSelection.h"
#include "editorPanels.h"

long Selection::gid = 0;

void Selection::callback(std::function<void(const MV::BoxAABB &)> a_callback){
	selectedCallback = a_callback;
}

void Selection::enable(std::function<void(const MV::BoxAABB &)> a_callback){
	selectedCallback = a_callback;
	enable();
}

void Selection::enable(){
	onMouseDownHandle = mouse.onLeftMouseDown.connect([&](MV::MouseState &mouse){
		inSelection = true;
		selection.initialize(MV::castPoint<double>(mouse.position()));
		visibleSelection = scene->make<MV::Scene::Rectangle>("Selection_" + boost::lexical_cast<std::string>(id), selection.minPoint, MV::Size<>());
		visibleSelection->color(MV::Color(1.0, 1.0, 0.0, .25));
		auto originalPosition = visibleSelection->localFromScreen(mouse.position());
		onMouseMoveHandle = mouse.onMove.connect([&, originalPosition](MV::MouseState &mouse){
			visibleSelection->setTwoCorners(originalPosition, visibleSelection->localFromScreen(mouse.position()));
		});
	});

	onMouseUpHandle = mouse.onLeftMouseUp.connect([&](MV::MouseState &mouse){
		if(!inSelection){
			return;
		}
		SCOPE_EXIT{
			exitSelection(); //callback might throw, let's be safe.
		};

		selection.expandWith(MV::castPoint<double>(mouse.position()));
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

void EditableElement::resetHandles() {
	removeHandles();
	auto rectBox = elementToEdit->screenAABB();

	auto handleSize = MV::point(8.0, 8.0);

	positionHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("position"), mouse, rectBox);
	dragSignals["position"] = positionHandle->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		auto castPosition = MV::castPoint<double>(deltaPosition);
		handle->translate(castPosition);
		elementToEdit->translate(castPosition);
		topLeftSizeHandle->translate(castPosition);
		topRightSizeHandle->translate(castPosition);
		bottomLeftSizeHandle->translate(castPosition);
		bottomRightSizeHandle->translate(castPosition);
	});

	topLeftSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("topLeft"), mouse, MV::BoxAABB(rectBox.topLeftPoint(), rectBox.topLeftPoint() - (handleSize * MV::point(1.0, 1.0))));
	topLeftSizeHandle->color({SIZE_HANDLES});
	dragSignals["topLeft"] = topLeftSizeHandle->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->translate(MV::castPoint<double>(deltaPosition));
		topRightSizeHandle->position(MV::point(topRightSizeHandle->position().x, topLeftSizeHandle->position().y));
		bottomLeftSizeHandle->position(MV::point(topLeftSizeHandle->position().x, bottomLeftSizeHandle->position().y));

		dragUpdateFromHandles();
	});

	topRightSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("topRight"), mouse, MV::BoxAABB(rectBox.topRightPoint(), rectBox.topRightPoint() + (handleSize * MV::point(1.0, -1.0))));
	topRightSizeHandle->color({SIZE_HANDLES});
	dragSignals["topRight"] = topRightSizeHandle->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->translate(MV::castPoint<double>(deltaPosition));
		topLeftSizeHandle->position(MV::point(topLeftSizeHandle->position().x, topRightSizeHandle->position().y));
		bottomRightSizeHandle->position(MV::point(topRightSizeHandle->position().x, bottomRightSizeHandle->position().y));

		dragUpdateFromHandles();
	});

	bottomLeftSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("bottomLeft"), mouse, MV::BoxAABB(rectBox.bottomLeftPoint(), rectBox.bottomLeftPoint() - (handleSize * MV::point(1.0, -1.0))));
	bottomLeftSizeHandle->color({SIZE_HANDLES});
	dragSignals["bottomLeft"] = bottomLeftSizeHandle->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->translate(MV::castPoint<double>(deltaPosition));
		topLeftSizeHandle->position(MV::point(bottomLeftSizeHandle->position().x, topLeftSizeHandle->position().y));
		bottomRightSizeHandle->position(MV::point(bottomRightSizeHandle->position().x, bottomLeftSizeHandle->position().y));

		dragUpdateFromHandles();
	});

	bottomRightSizeHandle = controlContainer->make<MV::Scene::Clickable>(MV::guid("bottomRight"), mouse, MV::BoxAABB(rectBox.bottomRightPoint(), rectBox.bottomRightPoint() + (handleSize * MV::point(1.0, 1.0))));
	bottomRightSizeHandle->color({SIZE_HANDLES});
	dragSignals["bottomRight"] = bottomRightSizeHandle->onDrag.connect([&](std::shared_ptr<MV::Scene::Clickable> handle, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		handle->translate(MV::castPoint<double>(deltaPosition));
		topRightSizeHandle->position(MV::point(bottomRightSizeHandle->position().x, topRightSizeHandle->position().y));
		bottomLeftSizeHandle->position(MV::point(bottomLeftSizeHandle->position().x, bottomRightSizeHandle->position().y));

		dragUpdateFromHandles();
	});
}

EditableElement::EditableElement(std::shared_ptr<MV::Scene::Rectangle> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_controlContainer, MV::MouseState *a_mouse):
	elementToEdit(a_elementToEdit),
	controlContainer(a_controlContainer),
	mouse(a_mouse) {
	resetHandles();
}

void EditableElement::removeHandles() {
	controlContainer->remove(positionHandle);
	controlContainer->remove(topLeftSizeHandle);
	controlContainer->remove(topRightSizeHandle);
	controlContainer->remove(bottomLeftSizeHandle);
	controlContainer->remove(bottomRightSizeHandle);
}

void EditableElement::dragUpdateFromHandles() {
	if((topLeftSizeHandle->position().x - .5) > bottomRightSizeHandle->position().x || (topLeftSizeHandle->position().y - .5) > bottomRightSizeHandle->position().y){
		resetHandles();
	}

	auto box = MV::BoxAABB(topLeftSizeHandle->screenAABB().bottomRightPoint(), bottomRightSizeHandle->screenAABB().topLeftPoint());

	elementToEdit->position({});
	positionHandle->position({});

	auto corners = elementToEdit->localFromScreen(box);

	elementToEdit->setSizeAndCornerPoint(corners.minPoint, corners.size());
	positionHandle->setSizeAndCornerPoint(corners.minPoint, corners.size());
}
