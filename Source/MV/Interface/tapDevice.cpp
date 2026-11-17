#include "tapDevice.h"
#include <algorithm>
#include <jaiscript/signals/signal_impl.hpp>

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
		onHoverQuery(onHoverQuerySignal),
		onPinchZoom(onPinchZoomSignal){
		update();
	}

	void TapDevice::simulate(const MV::Point<int>& a_position, bool a_left, bool a_right, bool a_middle) {
		simulated = true;
		simulatedPosition = a_position;
		simulatedLeft = a_left;
		simulatedRight = a_right;
		simulatedMiddle = a_middle;
	}

	void TapDevice::stopSimulating() {
		simulated = false;
	}

	void TapDevice::update() {
		MV::Point<int> newPosition;
		bool newLeft, newMiddle, newRight;
		if (simulated) {
			newPosition = simulatedPosition;
			newLeft = simulatedLeft;
			newMiddle = simulatedMiddle;
			newRight = simulatedRight;
		} else {
			uint32_t state = SDL_GetMouseState(&newPosition.x, &newPosition.y);
			newLeft = (state & SDL_BUTTON(SDL_BUTTON_LEFT)) != false;
			newMiddle = (state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != false;
			newRight = (state & SDL_BUTTON(SDL_BUTTON_RIGHT)) != false;
		}

		if(newPosition != mousePosition){
			oldMousePosition = mousePosition;
			mousePosition = newPosition;
			hoverStale = true;
			onMoveSignal(*this);
		}

		hoverStale = hoverStale || newLeft != left;
		updateButtonState(left, newLeft, onLeftMouseDownSignal, onLeftMouseUpSignal, onLeftMouseDownEndSignal, onLeftMouseUpEndSignal);
		updateButtonState(middle, newMiddle, onMiddleMouseDownSignal, onMiddleMouseUpSignal, onMiddleMouseDownEndSignal, onMiddleMouseUpEndSignal);
		updateButtonState(right, newRight, onRightMouseDownSignal, onRightMouseUpSignal, onRightMouseDownEndSignal, onRightMouseUpEndSignal);

		runExclusiveActions();

		if (hoverTracked && hoverStale && !left) {
			resolveHover();
		}
	}

	void TapDevice::trackHover(bool a_track) {
		hoverTracked = a_track;
		hoverStale = a_track;
		if (!a_track) {
			auto left = std::move(hovered);
			hovered.clear();
			for (auto&& was : left) {
				if (!was.subject.expired()) {
					was.leave();
				}
			}
		}
	}

	void TapDevice::queueHoverCandidate(HoverCandidate a_candidate) {
		hoverCandidates.push_back(std::move(a_candidate));
	}

	void TapDevice::resolveHover() {
		hoverStale = false;
		hoverCandidates.clear();
		onHoverQuerySignal(*this);
		std::sort(hoverCandidates.begin(), hoverCandidates.end(), [](const HoverCandidate& a_left, const HoverCandidate& a_right) {
			return std::lexicographical_compare(a_right.fitness.begin(), a_right.fitness.end(), a_left.fitness.begin(), a_left.fitness.end());
		});
		std::vector<HoverCandidate> now;
		for (auto&& candidate : hoverCandidates) {
			now.push_back(std::move(candidate));
			if (now.back().exclusive) {
				break;
			}
		}
		hoverCandidates.clear();

		auto sameSubject = [](const HoverCandidate& a_left, const HoverCandidate& a_right) {
			return !a_left.subject.owner_before(a_right.subject) && !a_right.subject.owner_before(a_left.subject);
		};
		auto among = [&](const std::vector<HoverCandidate>& a_list, const HoverCandidate& a_one) {
			return std::any_of(a_list.begin(), a_list.end(), [&](const HoverCandidate& a_other) { return sameSubject(a_other, a_one); });
		};
		// the callbacks may rebuild the scene and ask again, so the new list is in place before any runs
		auto before = std::move(hovered);
		hovered = now;
		for (auto&& was : before) {
			if (!was.subject.expired() && !among(now, was)) {
				was.leave();
			}
		}
		for (auto&& is : now) {
			if (!is.subject.expired() && !among(before, is)) {
				is.enter();
			}
		}
	}

	void TapDevice::updateTouch(SDL_Event a_event, MV::Size<int> a_screenSize) {
		if (a_event.type == SDL_MULTIGESTURE){
			MV::Point<int> position(static_cast<int>(a_event.mgesture.x * a_screenSize.width), static_cast<int>(a_event.mgesture.y * a_screenSize.height));
			onPinchZoomSignal(position, a_event.mgesture.dDist, a_event.mgesture.dTheta);
		}
	}

	void TapDevice::updateButtonState(bool &oldState, bool newState, jai::signal_emitter<CallbackSignature> &onDown, jai::signal_emitter<CallbackSignature> &onUp, jai::signal_emitter<CallbackSignature> &onDownEnd, jai::signal_emitter<CallbackSignature> &onUpEnd) {
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
