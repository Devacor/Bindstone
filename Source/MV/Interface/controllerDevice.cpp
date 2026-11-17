#include "controllerDevice.h"
#include <algorithm>
#include <cmath>
#include <jaiscript/signals/signal_impl.hpp>

namespace MV {

	namespace {
		const float TRIGGER_AS_BUTTON = 0.5f;
		// the left stick starts a navigation move past the first and lets go under the second, so a stick resting
		// near the edge of one does not flicker
		const float NAVIGATION_ENGAGE = 0.6f;
		const float NAVIGATION_RELEASE = 0.4f;
		// half the width, in degrees, of the slice of the stick's circle that counts as a diagonal
		const float NAVIGATION_DIAGONAL_HALF_WIDTH = 15.0f;

		struct ButtonSource {
			ControllerButton button;
			SDL_GameControllerButton source;
		};
		const ButtonSource BUTTON_SOURCES[] {
			{ ControllerButton::SOUTH, SDL_CONTROLLER_BUTTON_A },
			{ ControllerButton::EAST, SDL_CONTROLLER_BUTTON_B },
			{ ControllerButton::WEST, SDL_CONTROLLER_BUTTON_X },
			{ ControllerButton::NORTH, SDL_CONTROLLER_BUTTON_Y },
			{ ControllerButton::BACK, SDL_CONTROLLER_BUTTON_BACK },
			{ ControllerButton::GUIDE, SDL_CONTROLLER_BUTTON_GUIDE },
			{ ControllerButton::START, SDL_CONTROLLER_BUTTON_START },
			{ ControllerButton::LEFT_STICK, SDL_CONTROLLER_BUTTON_LEFTSTICK },
			{ ControllerButton::RIGHT_STICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK },
			{ ControllerButton::LEFT_SHOULDER, SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
			{ ControllerButton::RIGHT_SHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
			{ ControllerButton::DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_UP },
			{ ControllerButton::DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN },
			{ ControllerButton::DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT },
			{ ControllerButton::DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT }
		};

		struct AxisSource {
			ControllerAxis axis;
			SDL_GameControllerAxis source;
		};
		const AxisSource AXIS_SOURCES[] {
			{ ControllerAxis::LEFT_X, SDL_CONTROLLER_AXIS_LEFTX },
			{ ControllerAxis::LEFT_Y, SDL_CONTROLLER_AXIS_LEFTY },
			{ ControllerAxis::RIGHT_X, SDL_CONTROLLER_AXIS_RIGHTX },
			{ ControllerAxis::RIGHT_Y, SDL_CONTROLLER_AXIS_RIGHTY },
			{ ControllerAxis::LEFT_TRIGGER, SDL_CONTROLLER_AXIS_TRIGGERLEFT },
			{ ControllerAxis::RIGHT_TRIGGER, SDL_CONTROLLER_AXIS_TRIGGERRIGHT }
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
		if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
			for (auto&& controller : opened) {
				SDL_GameControllerClose(controller);
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
		if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
			opened.clear();
			return result;
		}
		opened.erase(std::remove_if(opened.begin(), opened.end(), [](SDL_GameController* a_controller) {
			if (SDL_GameControllerGetAttached(a_controller)) {
				return false;
			}
			SDL_GameControllerClose(a_controller);
			return true;
		}), opened.end());
		const int joysticks = SDL_NumJoysticks();
		for (int index = 0; index < joysticks; ++index) {
			if (!SDL_IsGameController(index)) {
				continue;
			}
			const SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(index);
			const bool alreadyOpen = std::any_of(opened.begin(), opened.end(), [instance](SDL_GameController* a_controller) {
				return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(a_controller)) == instance;
			});
			if (!alreadyOpen) {
				if (auto* controller = SDL_GameControllerOpen(index)) {
					opened.push_back(controller);
				}
			}
		}
		for (auto&& controller : opened) {
			for (auto&& source : BUTTON_SOURCES) {
				if (SDL_GameControllerGetButton(controller, source.source)) {
					result.press(source.button);
				}
			}
			for (auto&& source : AXIS_SOURCES) {
				// SDL reads -32768 to 32767
				const float value = std::clamp(static_cast<float>(SDL_GameControllerGetAxis(controller, source.source)) / 32767.0f, -1.0f, 1.0f);
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
		const int padX = (down(ControllerButton::DPAD_RIGHT) ? 1 : 0) - (down(ControllerButton::DPAD_LEFT) ? 1 : 0);
		const int padY = (down(ControllerButton::DPAD_DOWN) ? 1 : 0) - (down(ControllerButton::DPAD_UP) ? 1 : 0);
		if (padX != 0 || padY != 0) {
			return directionFromParts(padX, padY);
		}
		if (cursorEnabled) {
			return std::nullopt;
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
			MV::Point<int> hardware;
			SDL_GetMouseState(&hardware.x, &hardware.y);
			mouseMoved = hardwareMouse && *hardwareMouse != hardware;
			hardwareMouse = hardware;
		}
		const auto push = leftStick();
		const float pushLength = std::sqrt(push.x * push.x + push.y * push.y);
		if (pushLength > 0.0f) {
			if (!cursorShowing) {
				cursorShowing = true;
				const auto from = a_pointer.position();
				cursorX = static_cast<float>(from.x);
				cursorY = static_cast<float>(from.y);
			}
			const float fullSpeed = cursorPixelsPerSecond > 0.0f ? cursorPixelsPerSecond : cursorArea.height > 0 ? cursorArea.height * 0.8f : 800.0f;
			// the push counts twice over: slow near the middle for a small target, quick at the rim
			const float step = fullSpeed * pushLength * static_cast<float>(a_dt);
			cursorX += push.x * step;
			cursorY += push.y * step;
			cursorX = std::max(0.0f, cursorArea.width > 0 ? std::min(cursorX, static_cast<float>(cursorArea.width - 1)) : cursorX);
			cursorY = std::max(0.0f, cursorArea.height > 0 ? std::min(cursorY, static_cast<float>(cursorArea.height - 1)) : cursorY);
		} else if (mouseMoved) {
			hideCursor(a_pointer);
		}
		if (cursorShowing) {
			a_pointer.simulate(cursorPosition(), down(ControllerButton::SOUTH));
		}
		return cursorShowing && pushLength > 0.0f;
	}

	bool ControllerDevice::navigate(TapDevice& a_focus, double a_dt, const void* a_scope) {
		bool result = moveCursor(a_focus, a_dt);
		const auto direction = navigationDirection();
		navigationStickEngaged = direction.has_value();
		if (!direction) {
			navigationHeld.reset();
		} else if (!navigationHeld) {
			navigationHeld = direction;
			navigationWait = navigationFirstDelay;
			result = a_focus.focusMove(*direction, a_scope);
		} else {
			// a direction that changes while held waits its turn, so a stick wandering between two does not run
			navigationHeld = direction;
			navigationWait -= a_dt;
			if (navigationWait <= 0.0) {
				navigationWait = navigationInterval;
				result = a_focus.focusMove(*direction, a_scope);
			}
		}
		// under the controller's pointer SOUTH is that pointer's press
		if (navigationPressed(ControllerButton::SOUTH) && !cursorShowing) {
			result = a_focus.focusActivate() || result;
		}
		if (navigationPressed(ControllerButton::RIGHT_SHOULDER)) {
			result = a_focus.focusNext(a_scope) || result;
		}
		if (navigationPressed(ControllerButton::LEFT_SHOULDER)) {
			result = a_focus.focusPrevious(a_scope) || result;
		}
		return result;
	}

}
