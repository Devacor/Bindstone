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
			auto clickable = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			a_renderer->registerShader(clickable);
			return clickable->hookUpSlots();
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, bool a_center) {
			auto clickable = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			a_renderer->registerShader(clickable);
			return clickable->size(a_size, a_center)->hookUpSlots();
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, const Point<> &a_centerPoint) {
			auto clickable = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			a_renderer->registerShader(clickable);
			return clickable->size(a_size, a_centerPoint)->hookUpSlots();
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState *a_mouse, const BoxAABB &a_boxAABB) {
			auto clickable = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			a_renderer->registerShader(clickable);
			return clickable->bounds(a_boxAABB)->hookUpSlots();
		}
		
		std::shared_ptr<Clickable> Clickable::hookUpSlots() {
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
			return std::static_pointer_cast<Clickable>(shared_from_this());
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
			for(auto &point : points){
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
				return screenAABB(shouldUseChildrenInHitDetection).pointContained(castPoint<PointPrecision>(a_state.position()));
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
			a_renderer->registerShader(button);
			return button;
		}
		
		std::shared_ptr<Button> Button::make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, bool a_center) {
			auto button = std::shared_ptr<Button>(new Button(a_renderer, a_mouse));
			a_renderer->registerShader(button);
			return button->clickSize(a_size, a_center);
		}

		std::shared_ptr<Button> Button::make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, const Point<> &a_centerPoint) {
			auto button = std::shared_ptr<Button>(new Button(a_renderer, a_mouse));
			a_renderer->registerShader(button);
			return button->clickSize(a_size, a_centerPoint);
		}

		std::shared_ptr<Button> Button::make(Draw2D* a_renderer, MouseState *a_mouse, const BoxAABB &a_boxAABB) {
			auto button = std::shared_ptr<Button>(new Button(a_renderer, a_mouse));
			a_renderer->registerShader(button);
			return button->clickBounds(a_boxAABB);
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

		std::shared_ptr<Button> Button::idleScene(std::shared_ptr<Node> a_idleSceneNode) {
			if(a_idleSceneNode){
				idleSceneNode = a_idleSceneNode;
				idleSceneNode->removeFromParent();
				idleSceneNode->parent(this);
			}
			return std::static_pointer_cast<Button>(shared_from_this());
		}

		std::shared_ptr<Button> Button::activeScene(std::shared_ptr<Node> a_activeSceneNode) {
			if(a_activeSceneNode){
				activeSceneNode = a_activeSceneNode;
				activeSceneNode->removeFromParent();
				activeSceneNode->parent(this);
			}
			return std::static_pointer_cast<Button>(shared_from_this());
		}

		std::shared_ptr<Button> Button::clickSize(const Size<> &a_size, bool a_center){
			clickable->size(a_size, a_center);
			return std::static_pointer_cast<Button>(shared_from_this());
		}
		std::shared_ptr<Button> Button::clickSize(const Size<> &a_size, const Point<> &a_centerPoint){
			clickable->size(a_size, a_centerPoint);
			return std::static_pointer_cast<Button>(shared_from_this());
		}

		std::shared_ptr<Button> Button::clickBounds(const BoxAABB &a_bounds){
			clickable->bounds(a_bounds);
			return std::static_pointer_cast<Button>(shared_from_this());
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

		BoxAABB Button::worldAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return clickable->worldAABBImplementation(a_includeChildren, a_nestedCall).expandWith(
				(clickable->inPressEvent()) ?
				activeSceneNode->worldAABBImplementation(a_includeChildren, a_nestedCall) :
				idleSceneNode->worldAABBImplementation(a_includeChildren, a_nestedCall)
			);
		}
		BoxAABB Button::screenAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return clickable->screenAABBImplementation(a_includeChildren, a_nestedCall).expandWith(
				(clickable->inPressEvent()) ?
				activeSceneNode->screenAABBImplementation(a_includeChildren, a_nestedCall) :
				idleSceneNode->screenAABBImplementation(a_includeChildren, a_nestedCall)
			);
		}
		BoxAABB Button::localAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return clickable->localAABBImplementation(a_includeChildren, a_nestedCall).expandWith(
				(clickable->inPressEvent()) ?
				activeSceneNode->localAABBImplementation(a_includeChildren, a_nestedCall) :
				idleSceneNode->localAABBImplementation(a_includeChildren, a_nestedCall)
			);
		}
		BoxAABB Button::basicAABBImplementation() const{
			return clickable->basicAABBImplementation().expandWith(
				(clickable->inPressEvent()) ?
				activeSceneNode->basicAABBImplementation() :
				idleSceneNode->basicAABBImplementation()
			);
		}

	}
}
