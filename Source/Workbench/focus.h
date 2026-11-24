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
			// what is there is selected, so typing replaces it and an arrow key keeps it
			a_text->selectAll();
			MV::startTextInput();
		}

		// Leaves the field as a press elsewhere would: committing it, or putting an Enter-only field back.
		void leave() {
			leaveActive();
		}

		std::shared_ptr<MV::Scene::Text> active() const {
			return activeText.lock();
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
			if (a_event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
				// a right press is for the field's menu, which works on what is being typed
				if (a_event.button.button == SDL_BUTTON_LEFT) {
					leaveActive();
				}
				return false;
			}
			if (a_event.type != SDL_EVENT_KEY_DOWN && a_event.type != SDL_EVENT_KEY_UP && a_event.type != SDL_EVENT_TEXT_INPUT && a_event.type != SDL_EVENT_TEXT_EDITING) {
				return false;
			}
			if (a_event.type == SDL_EVENT_KEY_DOWN && a_event.key.key == SDLK_ESCAPE) {
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
			MV::stopTextInput();
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
