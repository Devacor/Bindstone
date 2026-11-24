#include "controllerDevice.h"
#include <algorithm>
#include <cmath>
#include <jaiscript/signals/signal_impl.hpp>

namespace MV {

	namespace {
		const float TRIGGER_AS_BUTTON = 0.5f;
		// how fast the d-pad carries the pointer, as a share of the stick's full speed
		const float CURSOR_NUDGE = 0.35f;
		// the left stick starts a navigation move past the first and lets go under the second, so a stick resting
		// near the edge of one does not flicker
		const float NAVIGATION_ENGAGE = 0.6f;
		const float NAVIGATION_RELEASE = 0.4f;
		// half the width, in degrees, of the slice of the stick's circle that counts as a diagonal
		const float NAVIGATION_DIAGONAL_HALF_WIDTH = 15.0f;

		struct ButtonSource {
			ControllerButton button;
			SDL_GamepadButton source;
		};
		const ButtonSource BUTTON_SOURCES[] {
			{ ControllerButton::SOUTH, SDL_GAMEPAD_BUTTON_SOUTH },
			{ ControllerButton::EAST, SDL_GAMEPAD_BUTTON_EAST },
			{ ControllerButton::WEST, SDL_GAMEPAD_BUTTON_WEST },
			{ ControllerButton::NORTH, SDL_GAMEPAD_BUTTON_NORTH },
			{ ControllerButton::BACK, SDL_GAMEPAD_BUTTON_BACK },
			{ ControllerButton::GUIDE, SDL_GAMEPAD_BUTTON_GUIDE },
			{ ControllerButton::START, SDL_GAMEPAD_BUTTON_START },
			{ ControllerButton::LEFT_STICK, SDL_GAMEPAD_BUTTON_LEFT_STICK },
			{ ControllerButton::RIGHT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK },
			{ ControllerButton::LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },
			{ ControllerButton::RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
			{ ControllerButton::DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_UP },
			{ ControllerButton::DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN },
			{ ControllerButton::DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT },
			{ ControllerButton::DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT }
		};

		struct AxisSource {
			ControllerAxis axis;
			SDL_GamepadAxis source;
		};
		const AxisSource AXIS_SOURCES[] {
			{ ControllerAxis::LEFT_X, SDL_GAMEPAD_AXIS_LEFTX },
			{ ControllerAxis::LEFT_Y, SDL_GAMEPAD_AXIS_LEFTY },
			{ ControllerAxis::RIGHT_X, SDL_GAMEPAD_AXIS_RIGHTX },
			{ ControllerAxis::RIGHT_Y, SDL_GAMEPAD_AXIS_RIGHTY },
			{ ControllerAxis::LEFT_TRIGGER, SDL_GAMEPAD_AXIS_LEFT_TRIGGER },
			{ ControllerAxis::RIGHT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER }
		};

		std::optional<FocusDirection> directionFromParts(int a_x, int a_y) {
			if (a_x < 0) {
				return a_y < 0 ? FocusDirection::UP_LEFT : a_y > 0 ? FocusDirection::DOWN_LEFT : FocusDirection::LEFT;
			}
			if (a_x > 0) {
				return a_y < 0 ? FocusDirection::UP_RIGHT : a_y > 0 ? FocusDirection::DOWN_RIGHT : FocusDirection::RIGHT;
			}
			if (a_y < 0) {
				return FocusDirection::UP;
			}
			if (a_y > 0) {
				return FocusDirection::DOWN;
			}
			return std::nullopt;
		}
	}

	ControllerDevice::ControllerDevice() :
		onButtonDown(onButtonDownSignal),
		onButtonUp(onButtonUpSignal),
		onConnect(onConnectSignal),
		onDisconnect(onDisconnectSignal) {
	}

	ControllerDevice::~ControllerDevice() {
		// a device that outlives SDL has nothing left to close
		if (SDL_WasInit(SDL_INIT_GAMEPAD)) {
			for (auto&& controller : opened) {
				SDL_CloseGamepad(controller);
			}
		}
	}

	void ControllerDevice::simulate(const ControllerState& a_state) {
		simulated = true;
		simulatedState = a_state;
	}

	void ControllerDevice::stopSimulating() {
		simulated = false;
	}

	ControllerDevice& ControllerDevice::deadZone(float a_deadZone) {
		stickDeadZone = std::clamp(a_deadZone, 0.0f, 0.95f);
		return *this;
	}

	ControllerDevice& ControllerDevice::navigation(bool a_enabled) {
		navigationEnabled = a_enabled;
		return *this;
	}

	ControllerDevice& ControllerDevice::navigationRepeat(double a_firstDelay, double a_interval) {
		navigationFirstDelay = std::max(0.0, a_firstDelay);
		navigationInterval = std::max(0.01, a_interval);
		return *this;
	}

	MV::Point<> ControllerDevice::stick(ControllerAxis a_x, ControllerAxis a_y) const {
		const float x = axis(a_x);
		const float y = axis(a_y);
		const float length = std::sqrt(x * x + y * y);
		if (length <= stickDeadZone) {
			return MV::Point<>();
		}
		const float scaled = std::min(1.0f, (length - stickDeadZone) / (1.0f - stickDeadZone)) / length;
		return MV::point(x * scaled, y * scaled);
	}

	// A headless run starts SDL without its controller subsystem, and then there is no hardware to read.
	ControllerState ControllerDevice::readHardware() {
		ControllerState result;
		if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
			opened.clear();
			return result;
		}
		opened.erase(std::remove_if(opened.begin(), opened.end(), [](SDL_Gamepad* a_controller) {
			if (SDL_GamepadConnected(a_controller)) {
				return false;
			}
			SDL_CloseGamepad(a_controller);
			return true;
		}), opened.end());
		int gamepadCount = 0;
		SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepadCount);
		for (int index = 0; index < gamepadCount; ++index) {
			const SDL_JoystickID instance = gamepads[index];
			const bool alreadyOpen = std::any_of(opened.begin(), opened.end(), [instance](SDL_Gamepad* a_controller) {
				return SDL_GetGamepadID(a_controller) == instance;
			});
			if (!alreadyOpen) {
				if (auto* controller = SDL_OpenGamepad(instance)) {
					opened.push_back(controller);
				}
			}
		}
		SDL_free(gamepads);
		for (auto&& controller : opened) {
			for (auto&& source : BUTTON_SOURCES) {
				if (SDL_GetGamepadButton(controller, source.source)) {
					result.press(source.button);
				}
			}
			for (auto&& source : AXIS_SOURCES) {
				// SDL reads -32768 to 32767
				const float value = std::clamp(static_cast<float>(SDL_GetGamepadAxis(controller, source.source)) / 32767.0f, -1.0f, 1.0f);
				float& furthest = result.axes[static_cast<size_t>(source.axis)];
				if (std::abs(value) > std::abs(furthest)) {
					furthest = value;
				}
			}
		}
		return result;
	}

	void ControllerDevice::update() {
		ControllerState now = simulated ? simulatedState : readHardware();
		const size_t nowAttached = simulated ? std::max<size_t>(1, opened.size()) : opened.size();
		now.buttons[static_cast<size_t>(ControllerButton::LEFT_TRIGGER)] = now.buttons[static_cast<size_t>(ControllerButton::LEFT_TRIGGER)] || now.axes[static_cast<size_t>(ControllerAxis::LEFT_TRIGGER)] >= TRIGGER_AS_BUTTON;
		now.buttons[static_cast<size_t>(ControllerButton::RIGHT_TRIGGER)] = now.buttons[static_cast<size_t>(ControllerButton::RIGHT_TRIGGER)] || now.axes[static_cast<size_t>(ControllerAxis::RIGHT_TRIGGER)] >= TRIGGER_AS_BUTTON;

		const size_t wasAttached = attachedCount;
		attachedCount = nowAttached;
		if (wasAttached == 0 && nowAttached > 0) {
			onConnectSignal(*this);
		}

		// in place before the callbacks, so one that asks sees this frame
		const ControllerState before = current;
		current = now;
		for (size_t index = 0; index < current.buttons.size(); ++index) {
			if (now.buttons[index] != before.buttons[index]) {
				auto button = static_cast<ControllerButton>(index);
				if (now.buttons[index]) {
					onButtonDownSignal(*this, button);
				} else {
					onButtonUpSignal(*this, button);
				}
			}
		}

		if (wasAttached > 0 && nowAttached == 0) {
			onDisconnectSignal(*this);
		}
	}

	std::optional<FocusDirection> ControllerDevice::navigationDirection() const {
		if (cursorEnabled) {
			return std::nullopt;
		}
		const int padX = (down(ControllerButton::DPAD_RIGHT) ? 1 : 0) - (down(ControllerButton::DPAD_LEFT) ? 1 : 0);
		const int padY = (down(ControllerButton::DPAD_DOWN) ? 1 : 0) - (down(ControllerButton::DPAD_UP) ? 1 : 0);
		if (padX != 0 || padY != 0) {
			return directionFromParts(padX, padY);
		}
		const float x = axis(ControllerAxis::LEFT_X);
		const float y = axis(ControllerAxis::LEFT_Y);
		const float length = std::sqrt(x * x + y * y);
		if (length < (navigationStickEngaged ? NAVIGATION_RELEASE : NAVIGATION_ENGAGE)) {
			return std::nullopt;
		}
		// a narrow slice for each diagonal and a wide one for each of the four, so a stick pushed roughly sideways
		// goes sideways
		const float degrees = std::atan2(std::abs(y), std::abs(x)) * 57.29578f;
		const bool sideways = degrees < 45.0f + NAVIGATION_DIAGONAL_HALF_WIDTH;
		const bool upOrDown = degrees > 45.0f - NAVIGATION_DIAGONAL_HALF_WIDTH;
		return directionFromParts(sideways ? (x < 0.0f ? -1 : 1) : 0, upOrDown ? (y < 0.0f ? -1 : 1) : 0);
	}

	bool ControllerDevice::navigationPressed(ControllerButton a_button) {
		const size_t index = static_cast<size_t>(a_button);
		const bool pressed = current.buttons[index] && !navigationSeen.buttons[index];
		navigationSeen.buttons[index] = current.buttons[index];
		return pressed;
	}

	ControllerDevice& ControllerDevice::cursor(bool a_enabled) {
		cursorEnabled = a_enabled;
		return *this;
	}

	ControllerDevice& ControllerDevice::cursorToggle(std::optional<ControllerButton> a_button) {
		cursorToggleButton = a_button;
		return *this;
	}

	void ControllerDevice::showCursorAt(const MV::Point<int>& a_position) {
		cursorShowing = true;
		cursorX = static_cast<float>(a_position.x);
		cursorY = static_cast<float>(a_position.y);
	}

	void ControllerDevice::changeMode(TapDevice& a_focus, const void* a_scope) {
		cursorModeEntered = cursorEnabled;
		if (cursorEnabled) {
			MV::Point<int> start = a_focus.position();
			a_focus.focusedScreenCentre(start);
			a_focus.focusClear();
			if (connected()) {
				showCursorAt(start);
			}
		} else if (cursorShowing) {
			const auto last = cursorPosition();
			hideCursor(a_focus);
			a_focus.focusNearest(last, a_scope);
		}
	}

	MV::Point<int> ControllerDevice::cursorPosition() const {
		return MV::Point<int>(static_cast<int>(std::lround(cursorX)), static_cast<int>(std::lround(cursorY)));
	}

	ControllerDevice& ControllerDevice::cursorBounds(const MV::Size<int>& a_size) {
		cursorArea = a_size;
		return *this;
	}

	ControllerDevice& ControllerDevice::cursorSpeed(float a_pixelsPerSecond) {
		cursorPixelsPerSecond = std::max(0.0f, a_pixelsPerSecond);
		return *this;
	}

	void ControllerDevice::hideCursor(TapDevice& a_pointer) {
		if (cursorShowing) {
			cursorShowing = false;
			a_pointer.stopSimulating();
		}
	}

	bool ControllerDevice::moveCursor(TapDevice& a_pointer, double a_dt) {
		if (!cursorEnabled || !connected()) {
			hideCursor(a_pointer);
			return false;
		}
		bool mouseMoved = false;
		if (SDL_WasInit(SDL_INIT_VIDEO)) {
			float hardwareX = 0.0f;
			float hardwareY = 0.0f;
			SDL_GetMouseState(&hardwareX, &hardwareY);
			MV::Point<int> hardware(static_cast<int>(hardwareX), static_cast<int>(hardwareY));
			mouseMoved = hardwareMouse && *hardwareMouse != hardware;
			hardwareMouse = hardware;
		}
		const auto push = leftStick();
		const float pushLength = std::sqrt(push.x * push.x + push.y * push.y);
		const float nudgeX = static_cast<float>((down(ControllerButton::DPAD_RIGHT) ? 1 : 0) - (down(ControllerButton::DPAD_LEFT) ? 1 : 0));
		const float nudgeY = static_cast<float>((down(ControllerButton::DPAD_DOWN) ? 1 : 0) - (down(ControllerButton::DPAD_UP) ? 1 : 0));
		const bool moving = pushLength > 0.0f || nudgeX != 0.0f || nudgeY != 0.0f;
		if (moving || down(ControllerButton::SOUTH)) {
			if (!cursorShowing) {
				showCursorAt(a_pointer.position());
			}
			const float fullSpeed = cursorPixelsPerSecond > 0.0f ? cursorPixelsPerSecond : cursorArea.height > 0 ? cursorArea.height * 0.8f : 800.0f;
			// the push counts twice over: slow near the middle for a small target, quick at the rim
			const float stickStep = fullSpeed * pushLength * static_cast<float>(a_dt);
			const float nudgeStep = fullSpeed * CURSOR_NUDGE * static_cast<float>(a_dt);
			cursorX += push.x * stickStep + nudgeX * nudgeStep;
			cursorY += push.y * stickStep + nudgeY * nudgeStep;
			cursorX = std::max(0.0f, cursorArea.width > 0 ? std::min(cursorX, static_cast<float>(cursorArea.width - 1)) : cursorX);
			cursorY = std::max(0.0f, cursorArea.height > 0 ? std::min(cursorY, static_cast<float>(cursorArea.height - 1)) : cursorY);
		} else if (mouseMoved) {
			hideCursor(a_pointer);
		}
		if (cursorShowing) {
			a_pointer.simulate(cursorPosition(), down(ControllerButton::SOUTH));
		}
		return cursorShowing && moving;
	}

	bool ControllerDevice::navigate(TapDevice& a_focus, double a_dt, const void* a_scope) {
		if (!navigationEnabled) {
			hideCursor(a_focus);
			// the pointer comes back when navigation does, if cursor mode is still the mode
			cursorModeEntered = false;
			// a direction held when navigation returns waits out a repeat instead of moving at once
			navigationHeld = navigationDirection();
			navigationWait = navigationFirstDelay;
			navigationSeen = current;
			return false;
		}
		if (cursorToggleButton && navigationPressed(*cursorToggleButton)) {
			cursorEnabled = !cursorEnabled;
		}
		if (cursorEnabled != cursorModeEntered) {
			changeMode(a_focus, a_scope);
		}
		bool result = moveCursor(a_focus, a_dt);
		// with a pad attached and snapping, the focus is put right as windows come and go
		if (!cursorEnabled && connected()) {
			a_focus.focusContext(a_scope);
		}
		const auto direction = navigationDirection();
		navigationStickEngaged = direction.has_value();
		if (!direction) {
			navigationHeld.reset();
		} else if (!navigationHeld) {
			navigationHeld = direction;
			navigationWait = navigationFirstDelay;
			result = a_focus.focusMoveWithin(*direction, a_scope);
		} else {
			// a direction that changes while held waits its turn, so a stick wandering between two does not run
			navigationHeld = direction;
			navigationWait -= a_dt;
			if (navigationWait <= 0.0) {
				navigationWait = navigationInterval;
				result = a_focus.focusMoveWithin(*direction, a_scope);
			}
		}
		// in cursor mode SOUTH is the pointer's press and the triggers rest; each is still watched, so one held
		// through a change of mode is not taken for a new press
		const bool south = navigationPressed(ControllerButton::SOUTH);
		const bool back = navigationPressed(ControllerButton::EAST);
		const bool nextTab = navigationPressed(ControllerButton::RIGHT_SHOULDER);
		const bool previousTab = navigationPressed(ControllerButton::LEFT_SHOULDER);
		const bool nextWindow = navigationPressed(ControllerButton::RIGHT_TRIGGER);
		const bool previousWindow = navigationPressed(ControllerButton::LEFT_TRIGGER);
		if (back) {
			result = a_focus.focusBack(a_scope) || result;
		}
		if (nextTab) {
			result = a_focus.focusTabNext(a_scope) || result;
		}
		if (previousTab) {
			result = a_focus.focusTabPrevious(a_scope) || result;
		}
		if (!cursorEnabled) {
			if (south) {
				result = a_focus.focusActivate() || result;
			}
			if (nextWindow) {
				result = a_focus.focusWindowNext(a_scope) || result;
			}
			if (previousWindow) {
				result = a_focus.focusWindowPrevious(a_scope) || result;
			}
		}
		return result;
	}

}
