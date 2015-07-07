#include "mouse.h"
#include "cereal/archives/json.hpp"

namespace MV{

	MouseState::MouseState():
		onLeftMouseDown(onLeftMouseDownSignal),
		onLeftMouseUp(onLeftMouseUpSignal),
		onMiddleMouseDown(onMiddleMouseDownSignal),
		onMiddleMouseUp(onMiddleMouseUpSignal),
		onRightMouseDown(onRightMouseDownSignal),
		onRightMouseUp(onRightMouseUpSignal),
		onLeftMouseDownEnd(onLeftMouseDownEndSignal),
		onLeftMouseUpEnd(onLeftMouseUpEndSignal),
		onMiddleMouseDownEnd(onMiddleMouseDownEndSignal),
		onMiddleMouseUpEnd(onMiddleMouseUpEndSignal),
		onRightMouseDownEnd(onRightMouseDownEndSignal),
		onRightMouseUpEnd(onRightMouseUpEndSignal),
		onMove(onMoveSignal){
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
			onMoveSignal(*this);
		}

		updateButtonState(left, newLeft, onLeftMouseDownSignal, onLeftMouseUpSignal, onLeftMouseDownEndSignal, onLeftMouseUpEndSignal);
		updateButtonState(middle, newMiddle, onMiddleMouseDownSignal, onMiddleMouseUpSignal, onMiddleMouseDownEndSignal, onMiddleMouseUpEndSignal);
		updateButtonState(right, newRight, onRightMouseDownSignal, onRightMouseUpSignal, onRightMouseDownEndSignal, onRightMouseUpEndSignal);

		runExclusiveActions();
	}

	void MouseState::updateButtonState(bool &oldState, bool newState, Signal<CallbackSignature> &onDown, Signal<CallbackSignature> &onUp, Signal<CallbackSignature> &onDownEnd, Signal<CallbackSignature> &onUpEnd) {
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
		std::sort(nodesToExecute.rbegin(), nodesToExecute.rend());
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
