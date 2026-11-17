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
		onFocusQuery(onFocusQuerySignal),
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

	std::vector<std::shared_ptr<void>> TapDevice::hoveredSubjects() const {
		std::vector<std::shared_ptr<void>> result;
		for (auto&& candidate : hovered) {
			if (auto subject = candidate.subject.lock()) {
				result.push_back(std::move(subject));
			}
		}
		return result;
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

	namespace {
		bool sameFocusSubject(const FocusCandidate& a_left, const FocusCandidate& a_right) {
			return !a_left.subject.owner_before(a_right.subject) && !a_right.subject.owner_before(a_left.subject);
		}
	}

	void TapDevice::queueFocusCandidate(FocusCandidate a_candidate) {
		focusCandidates.push_back(std::move(a_candidate));
	}

	std::vector<FocusCandidate> TapDevice::askForFocusCandidates(const void* a_scope) {
		focusCandidates.clear();
		focusScopeToken = a_scope;
		onFocusQuerySignal(*this);
		focusScopeToken = nullptr;
		std::stable_sort(focusCandidates.begin(), focusCandidates.end(), [](const FocusCandidate& a_left, const FocusCandidate& a_right) {
			return a_left.order != a_right.order ? a_left.order < a_right.order : a_left.treePath < a_right.treePath;
		});
		return std::move(focusCandidates);
	}

	void TapDevice::focus(FocusCandidate a_candidate, bool a_pressOnFocus) {
		if (hasFocus() && sameFocusSubject(focused, a_candidate)) {
			return;
		}
		// in place before the callbacks: either may move the focus on again
		auto previous = std::move(focused);
		focused = std::move(a_candidate);
		auto taken = focused;
		if (!previous.subject.expired() && previous.leave) {
			previous.leave();
		}
		if (taken.enter) {
			taken.enter();
		}
		if (a_pressOnFocus && taken.pressOnFocus && taken.activate) {
			taken.activate();
		}
	}

	bool TapDevice::focusFollowLink(FocusLink a_link, const void* a_scope) {
		if (!hasFocus() || !focused.linked) {
			return false;
		}
		FocusCandidate neighbour;
		auto linked = focused.linked;
		if (!linked(a_link, a_scope, neighbour)) {
			return false;
		}
		focus(std::move(neighbour));
		return true;
	}

	bool TapDevice::focusStep(int a_step, const void* a_scope) {
		if (focusFollowLink(a_step > 0 ? FocusLink::NEXT : FocusLink::PREVIOUS, a_scope)) {
			return true;
		}
		auto candidates = askForFocusCandidates(a_scope);
		if (candidates.empty()) {
			return false;
		}
		const int count = static_cast<int>(candidates.size());
		int next = a_step > 0 ? 0 : count - 1;
		if (hasFocus()) {
			for (int index = 0; index < count; ++index) {
				if (sameFocusSubject(candidates[static_cast<size_t>(index)], focused)) {
					next = (index + a_step + count) % count;
					break;
				}
			}
		}
		focus(std::move(candidates[static_cast<size_t>(next)]));
		return true;
	}

	bool TapDevice::focusNext(const void* a_scope) {
		return focusStep(1, a_scope);
	}

	bool TapDevice::focusPrevious(const void* a_scope) {
		return focusStep(-1, a_scope);
	}

	namespace {
		struct FocusWay {
			FocusLink link;
			float x;
			float y;
		};

		// y runs down the screen
		FocusWay focusWay(FocusDirection a_direction) {
			const float diagonal = 0.70710678f;
			switch (a_direction) {
				case FocusDirection::LEFT:
					return { FocusLink::LEFT, -1.0f, 0.0f };
				case FocusDirection::RIGHT:
					return { FocusLink::RIGHT, 1.0f, 0.0f };
				case FocusDirection::UP:
					return { FocusLink::UP, 0.0f, -1.0f };
				case FocusDirection::DOWN:
					return { FocusLink::DOWN, 0.0f, 1.0f };
				case FocusDirection::UP_LEFT:
					return { FocusLink::UP_LEFT, -diagonal, -diagonal };
				case FocusDirection::UP_RIGHT:
					return { FocusLink::UP_RIGHT, diagonal, -diagonal };
				case FocusDirection::DOWN_LEFT:
					return { FocusLink::DOWN_LEFT, -diagonal, diagonal };
				default:
					return { FocusLink::DOWN_RIGHT, diagonal, diagonal };
			}
		}
	}

	bool TapDevice::focusMoveOneWay(FocusDirection a_direction, const void* a_scope) {
		const auto way = focusWay(a_direction);
		if (focusFollowLink(way.link, a_scope)) {
			return true;
		}
		auto candidates = askForFocusCandidates(a_scope);
		if (candidates.empty()) {
			return false;
		}
		const FocusCandidate* from = nullptr;
		for (auto&& candidate : candidates) {
			if (hasFocus() && sameFocusSubject(candidate, focused)) {
				from = &candidate;
			}
		}
		if (!from) {
			focus(std::move(candidates.front()));
			return true;
		}
		auto centre = [](const FocusCandidate& a_candidate) {
			return MV::Point<float>((a_candidate.screenMin.x + a_candidate.screenMax.x) * 0.5f, (a_candidate.screenMin.y + a_candidate.screenMax.y) * 0.5f);
		};
		const auto start = centre(*from);
		const bool diagonal = way.x != 0.0f && way.y != 0.0f;
		const FocusCandidate* best = nullptr;
		float bestScore = 0.0f;
		for (auto&& candidate : candidates) {
			if (&candidate == from) {
				continue;
			}
			const auto at = centre(candidate);
			const float along = (at.x - start.x) * way.x + (at.y - start.y) * way.y;
			const float across = std::abs((at.x - start.x) * way.y - (at.y - start.y) * way.x);
			// that way, and more that way than sideways: a diagonal keeps to a narrower cone so it never takes what
			// lies straight along a row or a column. Sideways distance counts double, so a row or column is followed.
			if (along <= 0.5f || across > along * (diagonal ? 0.8f : 2.0f)) {
				continue;
			}
			const float score = along + across * 2.0f;
			if (!best || score < bestScore) {
				best = &candidate;
				bestScore = score;
			}
		}
		if (!best) {
			return false;
		}
		focus(*best);
		return true;
	}

	bool TapDevice::focusMove(FocusDirection a_direction, const void* a_scope) {
		if (focusMoveOneWay(a_direction, a_scope)) {
			return true;
		}
		switch (a_direction) {
			case FocusDirection::UP_LEFT:
				return focusMoveOneWay(FocusDirection::LEFT, a_scope) || focusMoveOneWay(FocusDirection::UP, a_scope);
			case FocusDirection::UP_RIGHT:
				return focusMoveOneWay(FocusDirection::RIGHT, a_scope) || focusMoveOneWay(FocusDirection::UP, a_scope);
			case FocusDirection::DOWN_LEFT:
				return focusMoveOneWay(FocusDirection::LEFT, a_scope) || focusMoveOneWay(FocusDirection::DOWN, a_scope);
			case FocusDirection::DOWN_RIGHT:
				return focusMoveOneWay(FocusDirection::RIGHT, a_scope) || focusMoveOneWay(FocusDirection::DOWN, a_scope);
			default:
				return false;
		}
	}

	bool TapDevice::focusNearest(const MV::Point<int>& a_screenPoint, const void* a_scope) {
		auto candidates = askForFocusCandidates(a_scope);
		const FocusCandidate* best = nullptr;
		int64_t bestDistance = 0;
		for (auto&& candidate : candidates) {
			const int64_t dx = std::max<int64_t>({ candidate.screenMin.x - a_screenPoint.x, a_screenPoint.x - candidate.screenMax.x, 0 });
			const int64_t dy = std::max<int64_t>({ candidate.screenMin.y - a_screenPoint.y, a_screenPoint.y - candidate.screenMax.y, 0 });
			const int64_t distance = dx * dx + dy * dy;
			if (!best || distance < bestDistance) {
				best = &candidate;
				bestDistance = distance;
			}
		}
		if (!best) {
			return false;
		}
		focus(*best);
		return true;
	}

	bool TapDevice::focusedScreenCentre(MV::Point<int>& a_centre) {
		if (!hasFocus()) {
			return false;
		}
		for (auto&& candidate : askForFocusCandidates(nullptr)) {
			if (sameFocusSubject(candidate, focused)) {
				a_centre = MV::Point<int>((candidate.screenMin.x + candidate.screenMax.x) / 2, (candidate.screenMin.y + candidate.screenMax.y) / 2);
				return true;
			}
		}
		return false;
	}

	bool TapDevice::focusActivate() {
		if (!hasFocus() || !focused.activate) {
			return false;
		}
		auto activate = focused.activate;
		activate();
		return true;
	}

	void TapDevice::focusClear() {
		auto previous = std::move(focused);
		focused = FocusCandidate {};
		if (!previous.subject.expired() && previous.leave) {
			previous.leave();
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
