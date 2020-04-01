#include "tapDevice.h"
#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

namespace MV{

	TapDevice::TapDevice():
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
		onPinchZoom(onPinchZoomSignal){
		update();
	}

	void TapDevice::update() {
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

	void TapDevice::updateTouch(SDL_Event a_event, MV::Size<int> a_screenSize) {
		if (a_event.type == SDL_MULTIGESTURE){
			MV::Point<int> position(static_cast<int>(a_event.mgesture.x * a_screenSize.width), static_cast<int>(a_event.mgesture.y * a_screenSize.height));
			onPinchZoomSignal(position, a_event.mgesture.dDist, a_event.mgesture.dTheta);
		}
	}

	void TapDevice::updateButtonState(bool &oldState, bool newState, Signal<CallbackSignature> &onDown, Signal<CallbackSignature> &onUp, Signal<CallbackSignature> &onDownEnd, Signal<CallbackSignature> &onUpEnd) {
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

	MV::Point<int> TapDevice::position() const {
		return mousePosition;
	}

	MV::Point<int> TapDevice::oldPosition() const {
		return oldMousePosition;
	}

	bool TapDevice::leftDown() const {
		return left;
	}

	bool TapDevice::rightDown() const {
		return right;
	}

	bool TapDevice::middleDown() const {
		return middle;
	}

	void TapDevice::runExclusiveActions() {
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

	void TapDevice::queueExclusiveAction(const ExclusiveTapAction &a_node) {
		nodesToExecute.push_back(a_node);
	}


}
