#include "mouse.h"
#include "cereal/archives/json.hpp"

namespace MV{

	MouseState::MouseState():
		onLeftMouseDown(onLeftMouseDownSlot),
		onLeftMouseUp(onLeftMouseUpSlot),
		onMiddleMouseDown(onMiddleMouseDownSlot),
		onMiddleMouseUp(onMiddleMouseUpSlot),
		onRightMouseDown(onRightMouseDownSlot),
		onRightMouseUp(onRightMouseUpSlot),
		onLeftMouseDownEnd(onLeftMouseDownEndSlot),
		onLeftMouseUpEnd(onLeftMouseUpEndSlot),
		onMiddleMouseDownEnd(onMiddleMouseDownEndSlot),
		onMiddleMouseUpEnd(onMiddleMouseUpEndSlot),
		onRightMouseDownEnd(onRightMouseDownEndSlot),
		onRightMouseUpEnd(onRightMouseUpEndSlot),
		onMove(onMoveSlot){
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

		updateButtonState(left, newLeft, onLeftMouseDownSlot, onLeftMouseUpSlot, onLeftMouseDownEndSlot, onLeftMouseUpEndSlot);
		updateButtonState(middle, newMiddle, onMiddleMouseDownSlot, onMiddleMouseUpSlot, onMiddleMouseDownEndSlot, onMiddleMouseUpEndSlot);
		updateButtonState(right, newRight, onRightMouseDownSlot, onRightMouseUpSlot, onRightMouseDownEndSlot, onRightMouseUpEndSlot);

		runExclusiveActions();
	}

	void MouseState::updateButtonState(bool &oldState, bool newState, Slot<CallbackSignature> &onDown, Slot<CallbackSignature> &onUp, Slot<CallbackSignature> &onDownEnd, Slot<CallbackSignature> &onUpEnd) {
		if(newState != oldState){
			oldState = newState;
			if(newState){
				onDown(*this);
				onDownEnd(*this);
			} else{
				onUp(*this);
				onUpEnd(*this);
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

	void MouseState::runExclusiveActions() {
		bool touchesEaten = false;
		std::sort(nodesToExecute.begin(), nodesToExecute.end());
		for(auto it = nodesToExecute.begin(); it != nodesToExecute.end(); ++it){
			if(!touchesEaten){
				it->enabled();
				touchesEaten = it->exclusive;
			} else{
				it->disabled();
			}
		}
		nodesToExecute.clear();
	}

	void MouseState::queueExclusiveAction(const ExclusiveMouseAction &a_node) {
		nodesToExecute.push_back(a_node);
	}

}
