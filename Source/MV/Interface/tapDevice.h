#ifndef _MV_MOUSE_H_
#define _MV_MOUSE_H_

#include <SDL.h>
#include <memory>
#include "MV/Render/points.h"
#include <jaiscript/signals/signal_decl.hpp>

namespace MV{
	struct ExclusiveTapAction{
		ExclusiveTapAction(bool a_exclusive, const std::vector<int64_t> &a_fitness, const std::function<void()> &a_enabledCallback, const std::function<void()> &a_disableCallback, const std::string &a_name = ""):
			exclusive(a_exclusive),
			fitness(a_fitness),
			disabled(a_disableCallback),
			enabled(a_enabledCallback),
			name(a_name){
		}

		bool operator<(const ExclusiveTapAction &a_rhs) const{
			for(size_t i = 0; i < fitness.size() && i < a_rhs.fitness.size(); ++i){
				if(fitness[i] < a_rhs.fitness[i]){
					return true;
				} else if(fitness[i] > a_rhs.fitness[i]){
					return false;
				}
			}
			return fitness.size() < a_rhs.fitness.size();
		}

		std::string name;
		bool exclusive;
		std::vector<int64_t> fitness;
		std::function<void()> disabled;
		std::function<void()> enabled;
	};

	// A Clickable under the pointer offering itself as hovered. The device keeps the top-most one, with any above it
	// that let touches through.
	struct HoverCandidate {
		std::vector<int64_t> fitness;
		bool exclusive = true;
		std::weak_ptr<void> subject;
		std::function<void()> enter;
		std::function<void()> leave;
	};

	enum class FocusDirection { LEFT, RIGHT, UP, DOWN, UP_LEFT, UP_RIGHT, DOWN_LEFT, DOWN_RIGHT };
	// The moves a Clickable can name a neighbour for; one it does not name is found for it.
	enum class FocusLink { NEXT, PREVIOUS, LEFT, RIGHT, UP, DOWN, UP_LEFT, UP_RIGHT, DOWN_LEFT, DOWN_RIGHT };

	// A focusable Clickable answering a focus query: where it comes in the order, where it is on screen, and what
	// taking, losing and using the focus does to it.
	struct FocusCandidate {
		int64_t order = 0;
		std::vector<int64_t> treePath;
		MV::Point<int> screenMin;
		MV::Point<int> screenMax;
		std::weak_ptr<void> subject;
		std::function<void()> enter;
		std::function<void()> leave;
		std::function<void()> activate;
		// The neighbour this one names for a link, when it names one that can take the focus within the scope.
		std::function<bool(FocusLink, const void* a_scope, FocusCandidate& a_neighbour)> linked;
		bool pressOnFocus = false;
	};

	class TapDevice {
	public:
		TapDevice();

		typedef void CallbackSignature(TapDevice&);
		typedef std::shared_ptr<jai::receiver<CallbackSignature>> SignalType;

		typedef void TouchCallbackSignature(const MV::Point<int> &center, float zoom, float radians);
		typedef std::shared_ptr<jai::receiver<TouchCallbackSignature>> TouchSignalType;

		void update();

		// Automation: while simulating, update() reads this instead of the hardware mouse
		void simulate(const MV::Point<int>& a_position, bool a_left, bool a_right = false, bool a_middle = false);
		void stopSimulating();
		bool simulating() const { return simulated; }

		void updateTouch(SDL_Event a_event, MV::Size<int> screenSize);

		MV::Point<int> position() const;
		MV::Point<int> oldPosition() const;

		bool leftDown() const;
		bool rightDown() const;
		bool middleDown() const;

		void queueExclusiveAction(const ExclusiveTapAction &a_node);

		void runExclusiveActions();

		// Off until asked for: while on, every Clickable tests the pointer each time it moves. Hover holds through a
		// press and is settled again on release.
		void trackHover(bool a_track);
		bool trackingHover() const { return hoverTracked; }
		// Asks again who is under the pointer, for when the scene moved and the pointer did not.
		void refreshHover() { hoverStale = true; }
		void queueHoverCandidate(HoverCandidate a_candidate);
		// The Clickables under the pointer as hover last settled them, top-most first.
		std::vector<std::shared_ptr<void>> hoveredSubjects() const;

		// Keyboard and controller focus. One focusable Clickable holds it at a time. A move first follows the
		// neighbour the focused Clickable names for it (Clickable::focusLink). Without one, focusNext and
		// focusPrevious walk by focus order and then by place in the scene, and focusMove goes to the nearest
		// focusable that way on screen; a diagonal with nothing that way tries its sideways part, then its
		// up-or-down part, so four directions serve a layout as well as eight. a_scope, a Node's address, keeps a
		// move to Clickables under that node. Each returns false when there is nowhere to go.
		bool focusNext(const void* a_scope = nullptr);
		bool focusPrevious(const void* a_scope = nullptr);
		bool focusMove(FocusDirection a_direction, const void* a_scope = nullptr);
		// Gives the focus to the focusable Clickable at a point on screen, or failing that the one nearest it.
		bool focusNearest(const MV::Point<int>& a_screenPoint, const void* a_scope = nullptr);
		// The middle on screen of what holds the focus, as it is now; false when nothing does.
		bool focusedScreenCentre(MV::Point<int>& a_centre);
		// Presses the focused Clickable, as Enter or a controller's accept button would.
		bool focusActivate();
		void focusClear();
		bool hasFocus() const { return !focused.subject.expired(); }
		// What holds the focus: the Clickable that answered the query, for a caller that knows the type.
		std::shared_ptr<void> focusedSubject() const { return focused.subject.lock(); }
		// What a focus query is limited to; null while there is no query or no limit.
		const void* focusScope() const { return focusScopeToken; }
		void queueFocusCandidate(FocusCandidate a_candidate);
		// Gives the focus to a_candidate; a_pressOnFocus false when a pointer press is what brought it.
		void focus(FocusCandidate a_candidate, bool a_pressOnFocus = true);
	private:
		std::vector<FocusCandidate> askForFocusCandidates(const void* a_scope);
		bool focusStep(int a_step, const void* a_scope);
		bool focusFollowLink(FocusLink a_link, const void* a_scope);
		bool focusMoveOneWay(FocusDirection a_direction, const void* a_scope);

		std::vector<FocusCandidate> focusCandidates;
		FocusCandidate focused;
		const void* focusScopeToken = nullptr;

		void resolveHover();

		bool hoverTracked = false;
		bool hoverStale = false;
		std::vector<HoverCandidate> hoverCandidates;
		std::vector<HoverCandidate> hovered;

		std::vector<ExclusiveTapAction> nodesToExecute;
		bool left = false;
		bool middle = false;
		bool right = false;
		MV::Point<int> mousePosition;
		MV::Point<int> oldMousePosition;
		bool simulated = false;
		MV::Point<int> simulatedPosition;
		bool simulatedLeft = false;
		bool simulatedRight = false;
		bool simulatedMiddle = false;
		MV::Point<int> touchPosition;

		void updateButtonState(bool &oldState, bool newState, jai::signal_emitter<CallbackSignature> &onDown, jai::signal_emitter<CallbackSignature> &onUp, jai::signal_emitter<CallbackSignature> &onDownEnd, jai::signal_emitter<CallbackSignature> &onUpEnd);

		jai::signal_emitter<CallbackSignature> onLeftMouseDownSignal;
		jai::signal_emitter<CallbackSignature> onLeftMouseUpSignal;

		jai::signal_emitter<CallbackSignature> onLeftMouseDownEndSignal;
		jai::signal_emitter<CallbackSignature> onLeftMouseUpEndSignal;

		jai::signal_emitter<CallbackSignature> onRightMouseDownSignal;
		jai::signal_emitter<CallbackSignature> onRightMouseUpSignal;

		jai::signal_emitter<CallbackSignature> onRightMouseDownEndSignal;
		jai::signal_emitter<CallbackSignature> onRightMouseUpEndSignal;

		jai::signal_emitter<CallbackSignature> onMiddleMouseDownSignal;
		jai::signal_emitter<CallbackSignature> onMiddleMouseUpSignal;

		jai::signal_emitter<CallbackSignature> onMiddleMouseDownEndSignal;
		jai::signal_emitter<CallbackSignature> onMiddleMouseUpEndSignal;

		jai::signal_emitter<CallbackSignature> onMoveSignal;
		jai::signal_emitter<CallbackSignature> onHoverQuerySignal;
		jai::signal_emitter<CallbackSignature> onFocusQuerySignal;

		jai::signal_emitter<TouchCallbackSignature> onPinchZoomSignal;
	public:
		jai::signal<CallbackSignature> onLeftMouseDown;
		jai::signal<CallbackSignature> onLeftMouseUp;

		jai::signal<CallbackSignature> onLeftMouseDownEnd;
		jai::signal<CallbackSignature> onLeftMouseUpEnd;

		jai::signal<CallbackSignature> onRightMouseDown;
		jai::signal<CallbackSignature> onRightMouseUp;

		jai::signal<CallbackSignature> onRightMouseDownEnd;
		jai::signal<CallbackSignature> onRightMouseUpEnd;

		jai::signal<CallbackSignature> onMiddleMouseDown;
		jai::signal<CallbackSignature> onMiddleMouseUp;

		jai::signal<CallbackSignature> onMiddleMouseDownEnd;
		jai::signal<CallbackSignature> onMiddleMouseUpEnd;

		jai::signal<CallbackSignature> onMove;
		// Emitted while hover is tracked: whoever is under the pointer answers with queueHoverCandidate.
		jai::signal<CallbackSignature> onHoverQuery;
		// Emitted to find who can take the focus: every focusable Clickable in scope answers with queueFocusCandidate.
		jai::signal<CallbackSignature> onFocusQuery;

		jai::signal<TouchCallbackSignature> onPinchZoom;
	};
}

#endif
