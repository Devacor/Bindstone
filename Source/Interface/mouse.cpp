#include "mouse.h"

namespace MV{

	MouseState::MouseState():
		onLeftMouseDown(onLeftMouseDownSlot),
		onLeftMouseUp(onLeftMouseUpSlot),
		onMiddleMouseDown(onMiddleMouseDownSlot),
		onMiddleMouseUp(onMiddleMouseUpSlot),
		onRightMouseDown(onRightMouseDownSlot),
		onRightMouseUp(onRightMouseUpSlot),
		onMove(onMoveSlot) {
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
		updateButtonState(middle, newMiddle, onLeftMouseDownSlot, onLeftMouseUpSlot);
		updateButtonState(right, newRight, onLeftMouseDownSlot, onLeftMouseUpSlot);
	}

	void MouseState::updateButtonState(bool &oldState, bool newState, Slot<CallbackSignature> &onDown, Slot<CallbackSignature> &onUp) {
		if(newState != oldState){
			oldState = newState;
			if(newState){
				onDown(*this);
			} else{
				onUp(*this);
			}
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

};