#ifndef _MV_CONTROLLERDEVICE_H_
#define _MV_CONTROLLERDEVICE_H_

#include <SDL.h>
#include <array>
#include <memory>
#include <optional>
#include <vector>
#include "MV/Render/points.h"
#include "MV/Interface/tapDevice.h"
#include <jaiscript/signals/signal_decl.hpp>

namespace MV {
	// Face buttons are named by place, not by the letter a pad prints on them. The triggers count as buttons once
	// pulled halfway.
	enum class ControllerButton {
		SOUTH, EAST, WEST, NORTH,
		BACK, GUIDE, START,
		LEFT_STICK, RIGHT_STICK,
		LEFT_SHOULDER, RIGHT_SHOULDER,
		DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT,
		LEFT_TRIGGER, RIGHT_TRIGGER,
		COUNT
	};

	// Sticks run -1 to 1 with y down the screen; triggers run 0 to 1.
	enum class ControllerAxis { LEFT_X, LEFT_Y, RIGHT_X, RIGHT_Y, LEFT_TRIGGER, RIGHT_TRIGGER, COUNT };

	struct ControllerState {
		std::array<bool, static_cast<size_t>(ControllerButton::COUNT)> buttons {};
		std::array<float, static_cast<size_t>(ControllerAxis::COUNT)> axes {};

		ControllerState& press(ControllerButton a_button, bool a_down = true) {
			buttons[static_cast<size_t>(a_button)] = a_down;
			return *this;
		}
		ControllerState& axis(ControllerAxis a_axis, float a_value) {
			axes[static_cast<size_t>(a_axis)] = a_value;
			return *this;
		}
	};

	// Every attached game controller read as one pad: a button is down when it is down on any of them, and an axis
	// reads from whichever pushes it furthest. Polled once a frame like TapDevice.
	class ControllerDevice {
	public:
		ControllerDevice();
		~ControllerDevice();
		ControllerDevice(const ControllerDevice&) = delete;
		ControllerDevice& operator=(const ControllerDevice&) = delete;

		typedef void CallbackSignature(ControllerDevice&);
		typedef std::shared_ptr<jai::receiver<CallbackSignature>> SignalType;
		typedef void ButtonCallbackSignature(ControllerDevice&, ControllerButton);
		typedef std::shared_ptr<jai::receiver<ButtonCallbackSignature>> ButtonSignalType;

		void update();

		// Automation: while simulating, update() reads this instead of the hardware and a pad counts as attached
		void simulate(const ControllerState& a_state);
		void stopSimulating();
		bool simulating() const { return simulated; }

		bool connected() const { return attachedCount > 0; }
		size_t connectedCount() const { return attachedCount; }

		bool down(ControllerButton a_button) const { return current.buttons[static_cast<size_t>(a_button)]; }
		// As read, before any dead zone.
		float axis(ControllerAxis a_axis) const { return current.axes[static_cast<size_t>(a_axis)]; }
		// A stick with the dead zone taken out: nothing inside it, then growing from zero at its edge to full at the rim.
		MV::Point<> leftStick() const { return stick(ControllerAxis::LEFT_X, ControllerAxis::LEFT_Y); }
		MV::Point<> rightStick() const { return stick(ControllerAxis::RIGHT_X, ControllerAxis::RIGHT_Y); }

		float deadZone() const { return stickDeadZone; }
		ControllerDevice& deadZone(float a_deadZone);

		// The pad as focus moves on a_focus, once a frame after update(): the d-pad or the left stick moves the
		// focus (eight ways; a held direction repeats), SOUTH presses the focused Clickable, and the shoulders step
		// to the previous and next. True when the focus moved or something was pressed.
		bool navigate(TapDevice& a_focus, double a_dt, const void* a_scope = nullptr);
		// How long a held direction waits before it repeats, and between repeats after that.
		ControllerDevice& navigationRepeat(double a_firstDelay, double a_interval);
		// The way the d-pad or left stick is held for navigation right now.
		std::optional<FocusDirection> navigationDirection() const;

	private:
		MV::Point<> stick(ControllerAxis a_x, ControllerAxis a_y) const;
		ControllerState readHardware();
		bool navigationPressed(ControllerButton a_button);

		ControllerState current;
		size_t attachedCount = 0;
		float stickDeadZone = 0.2f;

		bool simulated = false;
		ControllerState simulatedState;

		std::vector<SDL_GameController*> opened;

		std::optional<FocusDirection> navigationHeld;
		bool navigationStickEngaged = false;
		double navigationWait = 0.0;
		double navigationFirstDelay = 0.4;
		double navigationInterval = 0.12;
		ControllerState navigationSeen;

		jai::signal_emitter<ButtonCallbackSignature> onButtonDownSignal;
		jai::signal_emitter<ButtonCallbackSignature> onButtonUpSignal;
		jai::signal_emitter<CallbackSignature> onConnectSignal;
		jai::signal_emitter<CallbackSignature> onDisconnectSignal;
	public:
		jai::signal<ButtonCallbackSignature> onButtonDown;
		jai::signal<ButtonCallbackSignature> onButtonUp;
		// The first pad attached, and the last one taken away.
		jai::signal<CallbackSignature> onConnect;
		jai::signal<CallbackSignature> onDisconnect;
	};
}

#endif
