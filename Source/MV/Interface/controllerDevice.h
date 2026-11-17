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

		// The pad at work on a_focus, once a frame after update(). It has two modes and BACK (the button a pad
		// labels Select, View or Share) swaps them.
		// Snapping, the mode it starts in: the d-pad or the left stick moves the focus from one Clickable to the
		// next (eight ways; a held direction repeats) inside the window in use (TapDevice::focusContext), SOUTH
		// presses the focused one, the triggers step to the previous and next window, the shoulders to the
		// previous and next tab, and EAST backs out of the window.
		// Cursor: see cursor() below.
		// True when the focus or the pointer moved or something was pressed.
		bool navigate(TapDevice& a_focus, double a_dt, const void* a_scope = nullptr);
		// Off while the pad is the game's own input, as in play away from any menu: navigate then moves no focus
		// and shows no pointer, and the buttons, sticks and signals above are all there is. Turned on again, a
		// button still held from play is not taken for a press.
		ControllerDevice& navigation(bool a_enabled);
		bool navigation() const { return navigationEnabled; }
		// How long a held direction waits before it repeats, and between repeats after that.
		ControllerDevice& navigationRepeat(double a_firstDelay, double a_interval);
		// The way the d-pad or the left stick is held for a focus move right now; none in cursor mode.
		std::optional<FocusDirection> navigationDirection() const;

		// Cursor mode: the pad moves a pointer of its own in place of the focus. The left stick moves it freely
		// (slow near the middle, quick at the rim), the d-pad nudges it, and SOUTH presses under it; the shoulders
		// still step tabs and EAST still backs out. navigate feeds
		// the pointer to the TapDevice as a mouse would be read, so hover, presses and drags follow it.
		// Going in, the pointer starts on whatever held the focus and the focus is let go, so the one thing
		// highlighted is what the pointer is over. Coming out, the focus snaps to the Clickable under the pointer
		// or nearest it. Moving the real mouse takes the pointer back until the pad is used again.
		// MV::Scene::ControllerCursor draws it.
		ControllerDevice& cursor(bool a_enabled);
		bool cursor() const { return cursorEnabled; }
		// The button that swaps the modes; none to leave the swap to cursor().
		ControllerDevice& cursorToggle(std::optional<ControllerButton> a_button);
		std::optional<ControllerButton> cursorToggle() const { return cursorToggleButton; }
		// Whether the controller's pointer is the one in use, and where it is on screen.
		bool cursorShown() const { return cursorShowing; }
		MV::Point<int> cursorPosition() const;
		// The area the pointer stays inside, usually the window.
		ControllerDevice& cursorBounds(const MV::Size<int>& a_size);
		// How far the pointer travels in a second at full push, in pixels; 0 is most of the area's height.
		ControllerDevice& cursorSpeed(float a_pixelsPerSecond);

	private:
		MV::Point<> stick(ControllerAxis a_x, ControllerAxis a_y) const;
		ControllerState readHardware();
		bool navigationPressed(ControllerButton a_button);
		bool moveCursor(TapDevice& a_pointer, double a_dt);
		void showCursorAt(const MV::Point<int>& a_position);
		void hideCursor(TapDevice& a_pointer);
		void changeMode(TapDevice& a_focus, const void* a_scope);

		ControllerState current;
		size_t attachedCount = 0;
		float stickDeadZone = 0.2f;

		bool simulated = false;
		ControllerState simulatedState;

		std::vector<SDL_GameController*> opened;

		bool navigationEnabled = true;
		std::optional<FocusDirection> navigationHeld;
		bool navigationStickEngaged = false;
		double navigationWait = 0.0;
		double navigationFirstDelay = 0.4;
		double navigationInterval = 0.12;
		ControllerState navigationSeen;

		bool cursorEnabled = false;
		// the mode navigate last worked in: a change is acted on there, where the TapDevice is at hand
		bool cursorModeEntered = false;
		std::optional<ControllerButton> cursorToggleButton = ControllerButton::BACK;
		bool cursorShowing = false;
		float cursorX = 0.0f;
		float cursorY = 0.0f;
		MV::Size<int> cursorArea;
		float cursorPixelsPerSecond = 0.0f;
		std::optional<MV::Point<int>> hardwareMouse;

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
