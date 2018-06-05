#include "mouse.h"
#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

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
		onMove(onMoveSignal),
		onPinchZoom(onPinchZoomSignal),
		onRotate(onRotateSignal){
		update();
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

	void MouseState::updateTouch(SDL_Event a_event, MV::Size<int> a_screenSize) {
		if (a_event.type == SDL_MULTIGESTURE){
			bool pinchZoomAboveThreshold = fabs(a_event.mgesture.dDist) > 0.002;
			bool rotationAboveThreshold = fabs(a_event.mgesture.dTheta) > MV::PIEf / 180.0f;
			if (pinchZoomAboveThreshold || rotationAboveThreshold)
			{
				MV::Point<int> position(a_event.mgesture.x * a_screenSize.width, a_event.mgesture.y * a_screenSize.height);
				if (pinchZoomAboveThreshold) {
					onPinchZoomSignal(position, a_event.mgesture.dDist);
				}
				if (rotationAboveThreshold) {
					onRotateSignal(position, a_event.mgesture.dTheta);
				}
			}
		}
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
