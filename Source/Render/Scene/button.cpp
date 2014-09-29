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
			clickable->registerShader();
			return clickable->hookUpSlots();
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, bool a_center) {
			auto clickable = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			clickable->registerShader();
			return clickable->size(a_size, a_center)->hookUpSlots();
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, const Point<> &a_centerPoint) {
			auto clickable = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			clickable->registerShader();
			return clickable->size(a_size, a_centerPoint)->hookUpSlots();
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState *a_mouse, const BoxAABB<> &a_boxAABB) {
			auto clickable = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			clickable->registerShader();
			return clickable->bounds(a_boxAABB)->hookUpSlots();
		}

		std::shared_ptr<Clickable> Clickable::hookUpSlots() {
			onLeftMouseDownHandle = mouse->onLeftMouseDown.connect([&](MouseState& a_mouse){
				if(mouseInBounds(a_mouse)){
					a_mouse.queueExclusiveAction({eatTouches, parentIndexList(100), [&](){
						acceptDownClick();
					}, [](){}});
				}
			});

			onLeftMouseUpHandle = mouse->onLeftMouseUp.connect([&](MouseState& a_mouse){
				acceptUpClick();
			});

			hookedUp = true;
			return std::static_pointer_cast<Clickable>(shared_from_this());
		}

		void Clickable::blockInput() {
			if(!inPressEvent()){
				onLeftMouseUpHandle->block();
				onLeftMouseUpHandle->block();
				onMouseMoveHandle = nullptr;
				blocked = true;
			}
		}

		void Clickable::unblockInput() {
			onLeftMouseDownHandle->unblock();
			onLeftMouseUpHandle->unblock();
			blocked = false;
		}

		bool Clickable::handleBegin(std::shared_ptr<BlockInteraction>) {
			blockInput();
			return true;
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
				return screenAABB(shouldUseChildrenInHitDetection).contains(a_state.position());
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

		void Clickable::acceptDownClick() {
			auto protectFromDismissal = std::static_pointer_cast<Clickable>(shared_from_this());
			isInPressEvent = true;
			onPressSlot(protectFromDismissal);

			objectLocationBeforeDrag = position();
			dragStartPosition = mouse->position();
			priorMousePosition = dragStartPosition;

			onMouseMoveHandle = mouse->onMove.connect([&](MouseState& a_mouseInner){
				auto protectFromDismissal = std::static_pointer_cast<Clickable>(shared_from_this());
				onDragSlot(protectFromDismissal, dragStartPosition, a_mouseInner.position() - priorMousePosition);
				priorMousePosition = a_mouseInner.position();
			});
		}

		void Clickable::acceptUpClick() {
			if(inPressEvent()){
				auto protectFromDismissal = std::static_pointer_cast<Clickable>(shared_from_this());
				onMouseMoveHandle = nullptr;

				isInPressEvent = false;
				if(mouseInBounds(*mouse)){
					onAcceptSlot(protectFromDismissal);
				} else{
					onCancelSlot(protectFromDismissal);
				}
				onReleaseSlot(protectFromDismissal);
			}
		}

		/*************************\
		| ---------Button-------- |
		\*************************/

		std::shared_ptr<Button> Button::make(Draw2D* a_renderer, MouseState *a_mouse) {
			auto button = std::shared_ptr<Button>(new Button(a_renderer, a_mouse));
			button->registerShader();
			return button;
		}
		
		std::shared_ptr<Button> Button::make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, bool a_center) {
			auto button = std::shared_ptr<Button>(new Button(a_renderer, a_mouse));
			button->registerShader();
			return button->clickSize(a_size, a_center);
		}

		std::shared_ptr<Button> Button::make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, const Point<> &a_centerPoint) {
			auto button = std::shared_ptr<Button>(new Button(a_renderer, a_mouse));
			button->registerShader();
			return button->clickSize(a_size, a_centerPoint);
		}

		std::shared_ptr<Button> Button::make(Draw2D* a_renderer, MouseState *a_mouse, const BoxAABB<> &a_boxAABB) {
			auto button = std::shared_ptr<Button>(new Button(a_renderer, a_mouse));
			button->registerShader();
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

		std::shared_ptr<Button> Button::clickBounds(const BoxAABB<> &a_bounds){
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

		BoxAABB<> Button::worldAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return clickable->worldAABBImplementation(a_includeChildren, a_nestedCall).expandWith(
				(clickable->inPressEvent()) ?
				activeSceneNode->worldAABBImplementation(a_includeChildren, a_nestedCall) :
				idleSceneNode->worldAABBImplementation(a_includeChildren, a_nestedCall)
			);
		}
		BoxAABB<int> Button::screenAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return clickable->screenAABBImplementation(a_includeChildren, a_nestedCall).expandWith(
				(clickable->inPressEvent()) ?
				activeSceneNode->screenAABBImplementation(a_includeChildren, a_nestedCall) :
				idleSceneNode->screenAABBImplementation(a_includeChildren, a_nestedCall)
			);
		}
		BoxAABB<> Button::localAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return clickable->localAABBImplementation(a_includeChildren, a_nestedCall).expandWith(
				(clickable->inPressEvent()) ?
				activeSceneNode->localAABBImplementation(a_includeChildren, a_nestedCall) :
				idleSceneNode->localAABBImplementation(a_includeChildren, a_nestedCall)
			);
		}
		BoxAABB<> Button::basicAABBImplementation() const{
			return clickable->basicAABBImplementation().expandWith(
				(clickable->inPressEvent()) ?
				activeSceneNode->basicAABBImplementation() :
				idleSceneNode->basicAABBImplementation()
			);
		}

		void Button::setRenderer(Draw2D* a_renderer, bool includeChildren /*= true*/, bool includeParents /*= true*/) {
			Node::setRenderer(a_renderer, includeChildren, includeParents);
			idleSceneNode->setRenderer(a_renderer, includeChildren, false);
			idleSceneNode->setRenderer(a_renderer, includeChildren, false);
		}

	}
}
