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
		onFocusGroupQuery(onFocusGroupQuerySignal),
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
		auto candidates = askForReachable(a_scope);
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
		auto candidates = askForReachable(a_scope);
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
		// each rectangle as left, top, right, bottom, whichever way round its corners came
		struct Edges {
			float low[2];
			float high[2];
		};
		auto edges = [](const FocusCandidate& a_candidate) {
			return Edges {
				{ static_cast<float>(std::min(a_candidate.screenMin.x, a_candidate.screenMax.x)), static_cast<float>(std::min(a_candidate.screenMin.y, a_candidate.screenMax.y)) },
				{ static_cast<float>(std::max(a_candidate.screenMin.x, a_candidate.screenMax.x)), static_cast<float>(std::max(a_candidate.screenMin.y, a_candidate.screenMax.y)) } };
		};
		const auto start = centre(*from);
		const auto startEdges = edges(*from);
		const bool diagonal = way.x != 0.0f && way.y != 0.0f;
		// the axis a straight move runs along, and the one across it
		const int axis = way.x != 0.0f ? 0 : 1;
		const int crossAxis = 1 - axis;
		const bool forward = (axis == 0 ? way.x : way.y) > 0.0f;
		const FocusCandidate* best = nullptr;
		float bestScore = 0.0f;
		float bestTie = 0.0f;
		for (auto&& candidate : candidates) {
			if (&candidate == from) {
				continue;
			}
			const auto at = centre(candidate);
			const float along = (at.x - start.x) * way.x + (at.y - start.y) * way.y;
			const float across = std::abs((at.x - start.x) * way.y - (at.y - start.y) * way.x);
			if (along <= 0.5f) {
				continue;
			}
			// A straight move measures the gap between the facing edges and how far the two miss each other
			// sideways, so the nearest thing wins whatever its size, and something squarely ahead (wholly past
			// the edge and overlapping sideways) counts however far off its middle is. A diagonal goes by middles.
			float score = along + across * 2.0f;
			bool squarelyAhead = false;
			if (!diagonal) {
				const auto candidateEdges = edges(candidate);
				const float gap = forward ? candidateEdges.low[axis] - startEdges.high[axis] : startEdges.low[axis] - candidateEdges.high[axis];
				const float miss = std::max({ candidateEdges.low[crossAxis] - startEdges.high[crossAxis], startEdges.low[crossAxis] - candidateEdges.high[crossAxis], 0.0f });
				squarelyAhead = gap >= 0.0f && miss <= 0.0f;
				score = std::max(gap, 0.0f) + miss * 2.0f;
			}
			// that way, and more that way than sideways: a diagonal keeps to a narrower cone so it never takes what
			// lies straight along a row or a column. Sideways distance counts double, so a row or column is followed.
			if (!squarelyAhead && across > along * (diagonal ? 0.8f : 2.0f)) {
				continue;
			}
			const float tie = along + across * 2.0f;
			if (!best || score < bestScore - 0.01f || (score <= bestScore + 0.01f && tie < bestTie)) {
				best = &candidate;
				bestScore = score;
				bestTie = tie;
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
		auto candidates = askForReachable(a_scope);
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

	namespace {
		bool sameSubject(const std::weak_ptr<void>& a_left, const std::weak_ptr<void>& a_right) {
			return !a_left.expired() && !a_left.owner_before(a_right) && !a_right.owner_before(a_left);
		}

		// under the group's node, and not the group itself selected as a whole
		bool groupHolds(const FocusGroupCandidate& a_group, const FocusCandidate& a_candidate) {
			return a_candidate.under && a_candidate.under(a_group.scope) && !sameSubject(a_candidate.subject, a_group.subject);
		}

		// the modal window drawn last is the one on top
		const FocusGroupCandidate* topModalWindow(const std::vector<FocusGroupCandidate>& a_groups) {
			const FocusGroupCandidate* result = nullptr;
			for (auto&& group : a_groups) {
				if (!group.tabs && group.modal && (!result || result->treePath < group.treePath)) {
					result = &group;
				}
			}
			return result;
		}

		const FocusGroupCandidate* parentWindowOf(const std::vector<FocusGroupCandidate>& a_groups, const FocusGroupCandidate& a_child) {
			const FocusGroupCandidate* result = nullptr;
			for (auto&& group : a_groups) {
				if (!group.tabs && &group != &a_child && a_child.within(group.scope) && (!result || result->treePath.size() < group.treePath.size())) {
					result = &group;
				}
			}
			return result;
		}
	}

	void TapDevice::queueFocusGroupCandidate(FocusGroupCandidate a_candidate) {
		focusGroupCandidates.push_back(std::move(a_candidate));
	}

	std::vector<FocusGroupCandidate> TapDevice::askForFocusGroups(const void* a_scope) {
		focusGroupCandidates.clear();
		focusScopeToken = a_scope;
		onFocusGroupQuerySignal(*this);
		focusScopeToken = nullptr;
		std::stable_sort(focusGroupCandidates.begin(), focusGroupCandidates.end(), [](const FocusGroupCandidate& a_left, const FocusGroupCandidate& a_right) {
			return a_left.order != a_right.order ? a_left.order < a_right.order : a_left.treePath < a_right.treePath;
		});
		return std::move(focusGroupCandidates);
	}

	// What a move made at a scope can land on: everything focusable under it, less what is inside a window below
	// that is selected as a whole and has to be gone into.
	std::vector<FocusCandidate> TapDevice::askForReachable(const void* a_scope) {
		auto candidates = askForFocusCandidates(a_scope);
		auto groups = askForFocusGroups(a_scope);
		candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const FocusCandidate& a_candidate) {
			return std::any_of(groups.begin(), groups.end(), [&](const FocusGroupCandidate& a_group) {
				return a_group.explicitSelect && a_group.scope != a_scope && groupHolds(a_group, a_candidate);
			});
		}), candidates.end());
		return candidates;
	}

	const FocusGroupCandidate* TapDevice::windowHoldingFocus(const std::vector<FocusGroupCandidate>& a_groups) const {
		if (focused.subject.expired() || (focused.canTake && !focused.canTake(nullptr))) {
			return nullptr;
		}
		const FocusGroupCandidate* result = nullptr;
		for (auto&& group : a_groups) {
			if (!group.tabs && groupHolds(group, focused) && (!result || result->treePath.size() < group.treePath.size())) {
				result = &group;
			}
		}
		return result;
	}

	const FocusGroupCandidate* TapDevice::windowInUse(const std::vector<FocusGroupCandidate>& a_groups) const {
		const bool focusStands = !focused.subject.expired() && (!focused.canTake || focused.canTake(nullptr));
		const FocusGroupCandidate* modal = topModalWindow(a_groups);
		const FocusGroupCandidate* holding = windowHoldingFocus(a_groups);
		// the window last stepped to counts only while nothing holds the focus
		const FocusGroupCandidate* last = nullptr;
		if (!focusStands) {
			for (auto&& group : a_groups) {
				if (!group.tabs && sameSubject(group.subject, activeWindow)) {
					last = &group;
				}
			}
		}
		if (!modal) {
			return holding ? holding : last;
		}
		for (auto* inside : { holding, last }) {
			if (inside && inside != modal && inside->within(modal->scope)) {
				return inside;
			}
		}
		return modal;
	}

	void TapDevice::enterWindow(const FocusGroupCandidate& a_window, const std::vector<FocusGroupCandidate>& a_groups, bool a_inside) {
		if (!a_inside && a_window.explicitSelect && a_window.focusSelf) {
			// selected as a whole it is one of its parent's stops, so the parent is the window in use
			const FocusGroupCandidate* parent = parentWindowOf(a_groups, a_window);
			activeWindow = parent ? parent->subject : std::weak_ptr<void>();
			auto focusSelf = a_window.focusSelf;
			focusSelf();
			return;
		}
		activeWindow = a_window.subject;
		if (a_window.focusPreferred && a_window.focusPreferred()) {
			return;
		}
		auto candidates = askForReachable(a_window.scope);
		auto isTabHeader = [&](const FocusCandidate& a_candidate) {
			return std::any_of(a_groups.begin(), a_groups.end(), [&](const FocusGroupCandidate& a_group) {
				return a_group.tabs && groupHolds(a_group, a_candidate);
			});
		};
		auto first = std::find_if(candidates.begin(), candidates.end(), [&](const FocusCandidate& a_candidate) { return !isTabHeader(a_candidate); });
		if (first == candidates.end()) {
			first = candidates.begin();
		}
		if (first != candidates.end()) {
			focus(*first);
		} else if (!a_window.explicitSelect) {
			// a window selected as a whole with nothing in it stays selected
			focusClear();
		}
	}

	// Onto a control of a_window that is in no window under it: the one it remembers, else its first.
	bool TapDevice::focusOwnLevel(const FocusGroupCandidate& a_window, const std::vector<FocusGroupCandidate>& a_groups) {
		auto candidates = askForReachable(a_window.scope);
		auto ownLevel = [&](const FocusCandidate& a_candidate) {
			return std::none_of(a_groups.begin(), a_groups.end(), [&](const FocusGroupCandidate& a_group) {
				return &a_group != &a_window && a_group.within(a_window.scope) && groupHolds(a_group, a_candidate);
			});
		};
		std::weak_ptr<void> remembered = a_window.remembered ? a_window.remembered() : std::weak_ptr<void>();
		const FocusCandidate* chosen = nullptr;
		for (auto&& candidate : candidates) {
			if (!ownLevel(candidate)) {
				continue;
			}
			if (!chosen || sameSubject(candidate.subject, remembered)) {
				chosen = &candidate;
			}
		}
		if (!chosen) {
			return false;
		}
		activeWindow = a_window.subject;
		focus(*chosen);
		return true;
	}

	const void* TapDevice::focusContext(const void* a_scope) {
		auto groups = askForFocusGroups(a_scope);
		const bool focusStands = !focused.subject.expired() && (!focused.canTake || focused.canTake(nullptr));
		const bool focusLost = hasFocus() && !focusStands;
		// a window remembers what it holds itself, not what a window under it holds; a tab strip its headers
		const FocusGroupCandidate* holding = windowHoldingFocus(groups);
		if (focusStands) {
			for (auto&& group : groups) {
				if (group.tabs ? groupHolds(group, focused) : &group == holding) {
					group.remember(focused);
				}
			}
		}
		const FocusGroupCandidate* modal = topModalWindow(groups);
		if (!modal) {
			activeModal.reset();
		} else if (!sameSubject(modal->subject, activeModal)) {
			// a modal window newly on top: where the focus was is where it goes back to, unless that was a modal too
			if (activeModal.expired()) {
				windowBeforeModal = holding ? holding->subject : activeWindow;
			}
			activeModal = modal->subject;
		}
		const FocusGroupCandidate* inUse = windowInUse(groups);
		if (!inUse && focusLost) {
			// what held the focus went with its window: back to the one in use before it showed
			for (auto&& group : groups) {
				if (!group.tabs && sameSubject(group.subject, windowBeforeModal)) {
					inUse = &group;
				}
			}
			if (!inUse) {
				focusClear();
			}
		}
		if (!inUse) {
			activeWindow.reset();
			return a_scope;
		}
		const void* scope = inUse->scope;
		const bool outsideModal = modal && !(focusStands && groupHolds(*modal, focused));
		if (outsideModal || focusLost) {
			enterWindow(*inUse, groups, true);
		} else {
			activeWindow = inUse->subject;
		}
		return scope;
	}

	bool TapDevice::focusMoveWithin(FocusDirection a_direction, const void* a_scope) {
		auto groups = askForFocusGroups(a_scope);
		const FocusGroupCandidate* modal = topModalWindow(groups);
		const FocusGroupCandidate* level = windowInUse(groups);
		while (true) {
			if (focusMove(a_direction, level ? level->scope : a_scope)) {
				return true;
			}
			if (!level || level->explicitSelect || level == modal) {
				return false;
			}
			level = parentWindowOf(groups, *level);
		}
	}

	bool TapDevice::focusEnter(const void* a_windowNode) {
		auto groups = askForFocusGroups(nullptr);
		for (auto&& group : groups) {
			if (!group.tabs && group.scope == a_windowNode) {
				enterWindow(group, groups, true);
				return true;
			}
		}
		return false;
	}

	bool TapDevice::focusWindowStep(int a_step, const void* a_scope) {
		auto groups = askForFocusGroups(a_scope);
		const FocusGroupCandidate* modal = topModalWindow(groups);
		// a window selected as a whole steps from itself, though it is its parent that is in use
		const FocusGroupCandidate* level = nullptr;
		for (auto&& group : groups) {
			if (!group.tabs && hasFocus() && sameSubject(group.subject, focused.subject)) {
				level = &group;
			}
		}
		if (!level) {
			level = windowInUse(groups);
		}
		auto besideUnder = [&](const FocusGroupCandidate* a_parent) {
			std::vector<const FocusGroupCandidate*> result;
			for (auto&& group : groups) {
				if (!group.tabs && parentWindowOf(groups, group) == a_parent) {
					result.push_back(&group);
				}
			}
			return result;
		};
		if (!level) {
			auto tops = besideUnder(nullptr);
			if (tops.empty()) {
				return false;
			}
			enterWindow(a_step > 0 ? *tops.front() : *tops.back(), groups, false);
			return true;
		}
		// a window with none beside it steps the level above, as far up as the modal window when one shows
		while (level && level != modal) {
			const FocusGroupCandidate* parent = parentWindowOf(groups, *level);
			auto beside = besideUnder(parent);
			const int count = static_cast<int>(beside.size());
			if (count > 1) {
				const int at = static_cast<int>(std::find(beside.begin(), beside.end(), level) - beside.begin());
				enterWindow(*beside[static_cast<size_t>((at + a_step + count) % count)], groups, false);
				return true;
			}
			level = parent;
		}
		return false;
	}

	bool TapDevice::focusWindowNext(const void* a_scope) {
		return focusWindowStep(1, a_scope);
	}

	bool TapDevice::focusWindowPrevious(const void* a_scope) {
		return focusWindowStep(-1, a_scope);
	}

	bool TapDevice::focusTabStep(int a_step, const void* a_scope) {
		auto groups = askForFocusGroups(a_scope);
		const FocusGroupCandidate* inUse = windowInUse(groups);
		const FocusGroupCandidate* strip = nullptr;
		for (auto&& group : groups) {
			if (group.tabs && !strip && (!inUse || group.within(inUse->scope))) {
				strip = &group;
			}
		}
		if (!strip) {
			return false;
		}
		auto headers = askForFocusCandidates(strip->scope);
		const int count = static_cast<int>(headers.size());
		if (count < 2) {
			return false;
		}
		// with nothing chosen yet, the first tab is taken to be the one showing
		int current = 0;
		std::weak_ptr<void> chosen = strip->remembered();
		for (int index = 0; index < count; ++index) {
			if (sameSubject(headers[static_cast<size_t>(index)].subject, chosen)) {
				current = index;
			}
		}
		auto& header = headers[static_cast<size_t>((current + a_step + count) % count)];
		strip->remember(header);
		if (header.activate) {
			auto activate = header.activate;
			activate();
		}
		return true;
	}

	bool TapDevice::focusTabNext(const void* a_scope) {
		return focusTabStep(1, a_scope);
	}

	bool TapDevice::focusTabPrevious(const void* a_scope) {
		return focusTabStep(-1, a_scope);
	}

	bool TapDevice::focusBack(const void* a_scope) {
		auto groups = askForFocusGroups(a_scope);
		const FocusGroupCandidate* modal = topModalWindow(groups);
		const FocusGroupCandidate* level = windowInUse(groups);
		while (level) {
			if (level->explicitSelect) {
				auto back = level->back;
				enterWindow(*level, groups, false);
				if (back) {
					back();
				}
				return true;
			}
			if (level->handlesBack && level->handlesBack()) {
				auto back = level->back;
				back();
				return true;
			}
			if (level == modal) {
				return false;
			}
			const FocusGroupCandidate* parent = parentWindowOf(groups, *level);
			if (!parent) {
				return false;
			}
			if (focusOwnLevel(*parent, groups)) {
				return true;
			}
			level = parent;
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
