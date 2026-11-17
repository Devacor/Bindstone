#ifndef _WORKBENCH_FOCUS_H_
#define _WORKBENCH_FOCUS_H_

#include <functional>
#include <memory>
#include "MV/Render/package.h"

namespace Workbench {

	// Single-focus keyboard router: one active text field at a time; the field's commit
	// callback runs on Enter AND on focus loss (click-away or focusing another field).
	// A field given an abandon callback commits on Enter only: losing focus runs the abandon instead.
	class FocusRouter {
	public:
		void activate(const std::shared_ptr<MV::Scene::Text>& a_text, std::function<void()> a_commit = nullptr, std::function<void()> a_abandon = nullptr) {
			leaveActive();
			activeText = a_text;
			activeCommit = std::move(a_commit);
			activeAbandon = std::move(a_abandon);
			a_text->enableCursor();
			SDL_StartTextInput();
		}

		void deactivate() {
			commitActive();
		}

		// Drops focus without running the commit (Escape).
		void cancel() {
			activeCommit = nullptr;
			leaveActive();
		}

		bool hasActive() const {
			return !activeText.expired();
		}

		bool activeWithin(const std::shared_ptr<MV::Scene::Node>& a_ancestor) const {
			auto locked = activeText.lock();
			for (auto node = locked ? locked->owner() : nullptr; node; node = node->parent()) {
				if (node == a_ancestor) {
					return true;
				}
			}
			return false;
		}

		// Returns true when the event was consumed by the focused field. Only keyboard/text
		// events are consumed; a mouse press commits (click-away) but still routes normally.
		bool handleEvent(SDL_Event& a_event) {
			auto locked = activeText.lock();
			if (!locked) {
				return false;
			}
			if (a_event.type == SDL_MOUSEBUTTONDOWN) {
				leaveActive();
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
			activeAbandon = nullptr;
			leaveActive();
		}

		void leaveActive() {
			auto locked = activeText.lock();
			auto leave = std::move(activeAbandon ? activeAbandon : activeCommit);
			activeText.reset();
			activeCommit = nullptr;
			activeAbandon = nullptr;
			SDL_StopTextInput();
			if (locked) {
				// Text::disableCursor emits onEnter, which the fields route back here: nothing is active by now
				locked->disableCursor();
				if (leave) {
					leave();
				}
			}
		}

		std::weak_ptr<MV::Scene::Text> activeText;
		std::function<void()> activeCommit;
		std::function<void()> activeAbandon;
	};

}

#endif
