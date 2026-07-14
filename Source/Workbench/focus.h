#ifndef _WORKBENCH_FOCUS_H_
#define _WORKBENCH_FOCUS_H_

#include <functional>
#include <memory>
#include "MV/Render/package.h"

namespace Workbench {

	// Single-focus keyboard router: one active text field at a time; the field's commit
	// callback runs on Enter AND on focus loss (click-away or focusing another field).
	class FocusRouter {
	public:
		void activate(const std::shared_ptr<MV::Scene::Text>& a_text, std::function<void()> a_commit = nullptr) {
			commitActive();
			activeText = a_text;
			activeCommit = std::move(a_commit);
			a_text->enableCursor();
			SDL_StartTextInput();
		}

		void deactivate() {
			commitActive();
		}

		// Drops focus without running the commit (Escape).
		void cancel() {
			if (auto locked = activeText.lock()) {
				locked->disableCursor();
			}
			activeText.reset();
			activeCommit = nullptr;
			SDL_StopTextInput();
		}

		bool hasActive() const {
			return !activeText.expired();
		}

		// Returns true when the event was consumed by the focused field. Only keyboard/text
		// events are consumed; a mouse press commits (click-away) but still routes normally.
		bool handleEvent(SDL_Event& a_event) {
			auto locked = activeText.lock();
			if (!locked) {
				return false;
			}
			if (a_event.type == SDL_MOUSEBUTTONDOWN) {
				deactivate();
				return false;
			}
			if (a_event.type != SDL_KEYDOWN && a_event.type != SDL_KEYUP && a_event.type != SDL_TEXTINPUT && a_event.type != SDL_TEXTEDITING) {
				return false;
			}
			if (a_event.type == SDL_KEYDOWN && a_event.key.keysym.sym == SDLK_ESCAPE) {
				cancel();
				return true;
			}
			locked->text(a_event);
			return true;
		}

	private:
		void commitActive() {
			if (auto locked = activeText.lock()) {
				locked->disableCursor();
				if (activeCommit) {
					auto commit = std::move(activeCommit);
					activeCommit = nullptr;
					commit();
				}
			}
			activeText.reset();
			activeCommit = nullptr;
			SDL_StopTextInput();
		}

		std::weak_ptr<MV::Scene::Text> activeText;
		std::function<void()> activeCommit;
	};

}

#endif
