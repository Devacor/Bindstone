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

	class TapDevice {
	public:
		TapDevice();

		typedef void CallbackSignature(TapDevice&);
		typedef std::shared_ptr<jai::receiver<CallbackSignature>> SignalType;

		typedef void TouchCallbackSignature(const MV::Point<int> &center, float zoom, float radians);
		typedef std::shared_ptr<jai::receiver<TouchCallbackSignature>> TouchSignalType;

		void update();

		void updateTouch(SDL_Event a_event, MV::Size<int> screenSize);

		MV::Point<int> position() const;
		MV::Point<int> oldPosition() const;

		bool leftDown() const;
		bool rightDown() const;
		bool middleDown() const;

		void queueExclusiveAction(const ExclusiveTapAction &a_node);

		void runExclusiveActions();
	private:
		std::vector<ExclusiveTapAction> nodesToExecute;
		bool left = false;
		bool middle = false;
		bool right = false;
		MV::Point<int> mousePosition;
		MV::Point<int> oldMousePosition;
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

		jai::signal<TouchCallbackSignature> onPinchZoom;
	};
}

#endif
