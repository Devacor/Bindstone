#include "button.h"
#include "cereal/archives/json.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Clickable);

namespace MV {
	namespace Scene {
		/*************************\
		| -------Clickable------- |
		\*************************/

		Clickable::~Clickable(){
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState *a_mouse) {
			auto clipped = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			clipped->hookUpSlots();
			return clipped;
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState *a_mouse, const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight) {
			auto clipped = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			clipped->setTwoCorners(a_topLeft, a_bottomRight);
			clipped->hookUpSlots();
			return clipped;
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState *a_mouse, const Point<> &a_topLeft, const Point<> &a_bottomRight) {
			auto clipped = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			clipped->setTwoCorners(a_topLeft, a_bottomRight);
			clipped->hookUpSlots();
			return clipped;
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState *a_mouse, const Point<> &a_point, const Size<> &a_size, bool a_center) {
			auto clipped = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			if(a_center){
				clipped->setSizeAndCenterPoint(a_point, a_size);
			} else{
				clipped->setSizeAndCornerPoint(a_point, a_size);
			}
			clipped->hookUpSlots();
			return clipped;
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size) {
			auto clipped = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			clipped->setSize(a_size);
			clipped->hookUpSlots();
			return clipped;
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState *a_mouse, const BoxAABB &a_boxAABB) {
			auto clipped = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			clipped->setTwoCorners(a_boxAABB);
			return clipped;
		}

		void Clickable::hookUpSlots() {
			std::weak_ptr<Node> thisWeakPtr = shared_from_this();

			onMouseButtonBeginHandle = mouse->onMouseButtonBegin.connect([&, thisWeakPtr](MouseState& a_mouse){
				if(!thisWeakPtr.expired()){
					auto baseself = thisWeakPtr.lock();
					auto self = std::dynamic_pointer_cast<Clickable>(baseself);
					if(self->mouseInBounds(a_mouse)){
						if(self->eatTouches){
							self->alertParent(BlockInteraction::make(self));
						}
					}
				}
			});

			onMouseButtonEndHandle = mouse->onMouseButtonEnd.connect([&, thisWeakPtr](MouseState& a_mouse){
				if(!thisWeakPtr.expired()){
					auto self = std::static_pointer_cast<Clickable>(thisWeakPtr.lock());
					self->unblockInput();
				}
			});

			onMouseDownHandle = mouse->onLeftMouseDown.connect([&, thisWeakPtr](MouseState& a_mouse){
				if(!thisWeakPtr.expired() && mouseInBounds(a_mouse)){
					auto self = std::static_pointer_cast<Clickable>(thisWeakPtr.lock());
					self->isInPressEvent = true;
					self->onPressSlot(self);

					self->objectLocationBeforeDrag = self->position();
					self->dragStartPosition = a_mouse.position();
					self->priorMousePosition = self->dragStartPosition;

					self->onMouseMoveHandle = a_mouse.onMove.connect([&, thisWeakPtr](MouseState& a_mouseInner){
						if(!thisWeakPtr.expired()){
							auto self = std::static_pointer_cast<Clickable>(thisWeakPtr.lock());
							self->onDragSlot(self, self->dragStartPosition, a_mouseInner.position() - self->priorMousePosition);
							self->priorMousePosition = a_mouseInner.position();
						}
					});
				}
			});

			onMouseUpHandle = mouse->onLeftMouseUp.connect([&, thisWeakPtr](MouseState& a_mouse){
				if(!thisWeakPtr.expired()){
					auto self = std::static_pointer_cast<Clickable>(thisWeakPtr.lock());
					self->onMouseMoveHandle = nullptr;
					if(self->inPressEvent()){
						self->isInPressEvent = false;
						if(self->mouseInBounds(a_mouse)){
							self->onAcceptSlot(self);
						} else{
							self->onCancelSlot(self);
						}
						if(!thisWeakPtr.expired()){
							self->onReleaseSlot(self);
						}
					}
				}
			});

			hookedUp = true;
		}

		void Clickable::blockInput() {
			if(!inPressEvent()){
				onMouseDownHandle->block();
				onMouseUpHandle->block();
				onMouseMoveHandle = nullptr;
			}
		}

		void Clickable::unblockInput() {
			onMouseDownHandle->unblock();
			onMouseUpHandle->unblock();
		}

		void Clickable::handleBegin(std::shared_ptr<BlockInteraction>) {
			blockInput();
		}

		Clickable::Clickable(Draw2D *a_renderer, MouseState *a_mouse):
			Rectangle(a_renderer),
			mouse(a_mouse),
			eatTouches(true),
			isInPressEvent(false),
			shouldUseChildrenInHitDetection(false),
			onPress(onPressSlot),
			onRelease(onReleaseSlot),
			onCancel(onCancelSlot),
			onAccept(onAcceptSlot),
			onDrag(onDragSlot),
			hookedUp(false){
			//Default to transparent. Allows us to toggle it back visible for testing purposes, or if we want to render a button image directly in the Clickable node.
			//NOT calling setColor because that relies on shared_from_this.
			auto alpha = MV::Color(1, 1, 1, 0);
			for(auto point : points){
				point = alpha;
			}
		}

		bool Clickable::inPressEvent() const {
			return isInPressEvent;
		}

		void Clickable::startEatingTouches() {
			eatTouches = true;
		}

		void Clickable::stopEatingTouches() {
			eatTouches = false;
		}

		bool Clickable::isEatingTouches() const {
			return eatTouches;
		}

		MouseState* Clickable::getMouse() const {
			return mouse;
		}

		bool Clickable::mouseInBounds(const MouseState& a_state) {
			if(visible()){
				return screenAABB(shouldUseChildrenInHitDetection).pointContained(castPoint<double>(a_state.position()));
			} else{
				return false;
			}
		}

		Point<> Clickable::locationBeforeDrag() const {
			return objectLocationBeforeDrag;
		}

		void Clickable::drawImplementation() {
			if(!hookedUp){
				hookUpSlots();
			}
			Rectangle::drawImplementation();
		}

		/*************************\
		| ---------Button-------- |
		\*************************/

		std::shared_ptr<Button> Button::make(Draw2D* a_renderer, MouseState *a_mouse) {
			auto button = std::shared_ptr<Button>(new Button(a_renderer, a_mouse));
			return button;
		}

		std::shared_ptr<Button> Button::make(Draw2D* a_renderer, MouseState *a_mouse, const Point<> &a_topLeft, const Point<> &a_bottomRight) {
			auto button = std::shared_ptr<Button>(new Button(a_renderer, a_mouse));
			button->clickable->setTwoCorners(a_topLeft, a_bottomRight);
			return button;
		}

		std::shared_ptr<Button> Button::make(Draw2D* a_renderer, MouseState *a_mouse, const Point<> &a_point, const Size<> &a_size, bool a_center) {
			auto button = std::shared_ptr<Button>(new Button(a_renderer, a_mouse));
			if(a_center){
				button->clickable->setSizeAndCenterPoint(a_point, a_size);
			} else{
				button->clickable->setSizeAndCornerPoint(a_point, a_size);
			}
			return button;
		}

		std::shared_ptr<Button> Button::make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size) {
			auto button = std::shared_ptr<Button>(new Button(a_renderer, a_mouse));
			button->setClickableSize(a_size);
			return button;
		}

		std::shared_ptr<Button> Button::make(Draw2D* a_renderer, MouseState *a_mouse, const BoxAABB &a_boxAABB) {
			auto button = std::shared_ptr<Button>(new Button(a_renderer, a_mouse));
			button->setClickableTwoCorners(a_boxAABB);
			return button;
		}

		MouseState* Button::getMouse() const {
			return clickable->getMouse();
		}

		std::shared_ptr<Node> Button::activeScene() const {
			return activeSceneNode;
		}

		std::shared_ptr<Node> Button::idleScene() const {
			return idleSceneNode;
		}

		std::shared_ptr<Node> Button::idleScene(std::shared_ptr<Node> a_idleSceneNode) {
			if(a_idleSceneNode){
				idleSceneNode = a_idleSceneNode;
				idleSceneNode->removeFromParent();
				idleSceneNode->parent(this);
			}
			return idleSceneNode;
		}

		std::shared_ptr<Node> Button::activeScene(std::shared_ptr<Node> a_activeSceneNode) {
			if(a_activeSceneNode){
				activeSceneNode = a_activeSceneNode;
				activeSceneNode->removeFromParent();
				activeSceneNode->parent(this);
			}
			return activeSceneNode;
		}

		void Button::setClickableSize(const Size<> &a_size){
			clickable->setSize(a_size);
		}

		void Button::setClickableTwoCorners(const Point<> &a_topLeft, const Point<> &a_bottomRight){
			clickable->setTwoCorners(a_topLeft, a_bottomRight);
		}
		void Button::setClickableTwoCorners(const BoxAABB &a_bounds){
			clickable->setTwoCorners(a_bounds);
		}

		void Button::setClickableSizeAndCenterPoint(const Point<> &a_centerPoint, const Size<> &a_size){
			clickable->setSizeAndCenterPoint(a_centerPoint, a_size);
		}
		void Button::setClickableSizeAndCornerPoint(const Point<> &a_cornerPoint, const Size<> &a_size){
			clickable->setSizeAndCornerPoint(a_cornerPoint, a_size);
		}

		Button::Button(Draw2D *a_renderer, MouseState *a_mouse):
			Node(a_renderer),
			clickable(Clickable::make(a_renderer, a_mouse)),
			idleSceneNode(Node::make(a_renderer)),
			activeSceneNode(Node::make(a_renderer)),
			onPress(clickable->onPressSlot),
			onRelease(clickable->onReleaseSlot),
			onDrag(clickable->onDragSlot),
			onAccept(clickable->onAcceptSlot),
			onCancel(clickable->onCancelSlot){

			clickable->parent(this);
			idleSceneNode->parent(this);
			activeSceneNode->parent(this);
		}

		void Button::drawImplementation() {
			clickable->draw(); //ensures slots are hooked up and the node gets its update.
			if(clickable->inPressEvent()){
				activeSceneNode->draw();
			} else {
				idleSceneNode->draw();
			}
		}

		BoxAABB Button::getWorldAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return clickable->getWorldAABBImplementation(a_includeChildren, a_nestedCall).expandWith(
				(clickable->inPressEvent()) ?
				activeSceneNode->getWorldAABBImplementation(a_includeChildren, a_nestedCall) :
				idleSceneNode->getWorldAABBImplementation(a_includeChildren, a_nestedCall)
				);
		}
		BoxAABB Button::getScreenAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return clickable->getScreenAABBImplementation(a_includeChildren, a_nestedCall).expandWith(
				(clickable->inPressEvent()) ?
				activeSceneNode->getScreenAABBImplementation(a_includeChildren, a_nestedCall) :
				idleSceneNode->getScreenAABBImplementation(a_includeChildren, a_nestedCall)
				);
		}
		BoxAABB Button::getLocalAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return clickable->getLocalAABBImplementation(a_includeChildren, a_nestedCall).expandWith(
				(clickable->inPressEvent()) ?
				activeSceneNode->getLocalAABBImplementation(a_includeChildren, a_nestedCall) :
				idleSceneNode->getLocalAABBImplementation(a_includeChildren, a_nestedCall)
				);
		}
		BoxAABB Button::getBasicAABBImplementation() const{
			return clickable->getBasicAABBImplementation().expandWith(
				(clickable->inPressEvent()) ?
				activeSceneNode->getBasicAABBImplementation() :
				idleSceneNode->getBasicAABBImplementation()
				);
		}

	}
}
