#include "mouse.h"

namespace MV{

	MouseState::MouseState():
		onLeftMouseDown(onLeftMouseDownSlot),
		onLeftMouseUp(onLeftMouseUpSlot),
		onMiddleMouseDown(onMiddleMouseDownSlot),
		onMiddleMouseUp(onMiddleMouseUpSlot),
		onRightMouseDown(onRightMouseDownSlot),
		onRightMouseUp(onRightMouseUpSlot),
		onMove(onMoveSlot),
		onMouseButtonBegin(onMouseButtonBeginSlot),
		onMouseButtonEnd(onMouseButtonEndSlot){
	}

	void MouseState::update() {
		MV::Point<int> newPosition;
		uint32_t state = SDL_GetMouseState(&newPosition.x, &newPosition.y);
		bool newLeft = (state & SDL_BUTTON(SDL_BUTTON_LEFT)) != false;
		bool newMiddle = (state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != false;
		bool newRight = (state & SDL_BUTTON(SDL_BUTTON_RIGHT)) != false;

		if(newPosition != mousePosition){
			oldMousePosition = mousePosition;
			mousePosition = newPosition;
			onMoveSlot(*this);
		}

		updateButtonState(left, newLeft, onLeftMouseDownSlot, onLeftMouseUpSlot);
		updateButtonState(middle, newMiddle, onMiddleMouseDownSlot, onMiddleMouseUpSlot);
		updateButtonState(right, newRight, onRightMouseDownSlot, onRightMouseUpSlot);
	}

	void MouseState::updateButtonState(bool &oldState, bool newState, Slot<CallbackSignature> &onDown, Slot<CallbackSignature> &onUp) {
		if(newState != oldState){
			oldState = newState;
			onMouseButtonBeginSlot(*this);
			if(newState){
				onDown(*this);
			} else{
				onUp(*this);
			}
			onMouseButtonEndSlot(*this);
		}
	}

	MV::Point<int> MouseState::position() const {
		return mousePosition;
	}

	MV::Point<int> MouseState::oldPosition() const {
		return oldMousePosition;
	}

	bool MouseState::leftDown() const {
		return left;
	}

	bool MouseState::rightDown() const {
		return right;
	}

	bool MouseState::middleDown() const {
		return middle;
	}


	namespace Scene {
		int Clickable::counter = 0;

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState &a_mouse) {
			auto clipped = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			clipped->hookUpSlots();
			return clipped;
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState &a_mouse, const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight) {
			auto clipped = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			clipped->setTwoCorners(a_topLeft, a_bottomRight);
			clipped->hookUpSlots();
			return clipped;
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState &a_mouse, const Point<> &a_topLeft, const Point<> &a_bottomRight) {
			auto clipped = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			clipped->setTwoCorners(a_topLeft, a_bottomRight);
			clipped->hookUpSlots();
			return clipped;
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState &a_mouse, const Point<> &a_point, Size<> &a_size, bool a_center) {
			auto clipped = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			if(a_center){
				clipped->setSizeAndCenterPoint(a_point, a_size);
			} else{
				clipped->setSizeAndCornerPoint(a_point, a_size);
			}
			clipped->hookUpSlots();
			return clipped;
		}

		std::shared_ptr<Clickable> Clickable::make(Draw2D* a_renderer, MouseState &a_mouse, const Size<> &a_size) {
			auto clipped = std::shared_ptr<Clickable>(new Clickable(a_renderer, a_mouse));
			clipped->setSize(a_size);
			clipped->hookUpSlots();
			return clipped;
		}

		void Clickable::hookUpSlots() {
			thisSharedPtr = std::static_pointer_cast<Clickable>(shared_from_this());
			onMouseButtonBeginHandle = mouse.onMouseButtonBegin.connect([&](MouseState& a_mouse){
				if(mouseInBounds(a_mouse)){
					if(eatTouches){
						alertParent(BlockInteraction::make(thisSharedPtr.lock()));
					}
				}
			});

			onMouseButtonEndHandle = mouse.onMouseButtonEnd.connect([&](MouseState& a_mouse){
				unblockInput();
			});

			onMouseDownHandle = mouse.onLeftMouseDown.connect([&](MouseState& a_mouse){
				if(mouseInBounds(a_mouse)){
					std::cerr << "ON PRESS (" << ourId << "): " << a_mouse.position() << std::endl;
					isInPressEvent = true;
					onPressSlot(thisSharedPtr.lock());

					objectLocationBeforeDrag = getPosition();
					dragStartPosition = a_mouse.position();
					priorMousePosition = dragStartPosition;

					onMouseMoveHandle = a_mouse.onMove.connect([&](MouseState& a_mouseInner){
						//std::cerr << "ON DRAG (" << ourId << "): " << dragStartPosition << " -> " << a_mouseInner.position() << std::endl;
						onDragSlot(thisSharedPtr.lock(), dragStartPosition, a_mouseInner.position() - priorMousePosition);
						priorMousePosition = a_mouseInner.position();
					});
				}
			});

			onMouseUpHandle = mouse.onLeftMouseUp.connect([&](MouseState& a_mouse){
				onMouseMoveHandle = nullptr;
				if(inPressEvent()){
					isInPressEvent = false;
					if(mouseInBounds(a_mouse)){
						std::cerr << "ON RELEASE (" << ourId << "): " << a_mouse.position() << std::endl;
						onAcceptSlot(thisSharedPtr.lock());
					} else{
						std::cerr << "ON CANCEL (" << ourId << "): " << a_mouse.position() << std::endl;
						onCancelSlot(thisSharedPtr.lock());
					}
					onReleaseSlot(thisSharedPtr.lock());
				}
			});
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

		Clickable::Clickable(Draw2D *a_renderer, MouseState &a_mouse):
			Rectangle(a_renderer),
			mouse(a_mouse),
			eatTouches(true),
			isInPressEvent(false),
			onPress(onPressSlot),
			onRelease(onReleaseSlot),
			onCancel(onCancelSlot),
			onAccept(onAcceptSlot),
			onDrag(onDragSlot),
			ourId(counter++){
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

		const MouseState& Clickable::getMouse() const {
			return mouse;
		}

		bool Clickable::mouseInBounds(const MouseState& a_state) {
			return getScreenAABB().pointContained(castPoint<double>(a_state.position()));
		}

		Point<> Clickable::locationBeforeDrag() const {
			return objectLocationBeforeDrag;
		}

	}

}