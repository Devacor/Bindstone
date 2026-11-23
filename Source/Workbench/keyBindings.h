#ifndef _WORKBENCH_KEYBINDINGS_H_
#define _WORKBENCH_KEYBINDINGS_H_

#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>
#include <SDL.h>

namespace Workbench {

	// A key with the modifiers held for it. The empty chord is bound to nothing.
	struct KeyChord {
		SDL_Keycode key = SDLK_UNKNOWN;
		bool control = false;
		bool shift = false;
		bool alt = false;

		bool empty() const { return key == SDLK_UNKNOWN; }
		bool operator==(const KeyChord&) const = default;
		// "Ctrl+Shift+Z"; "None" for the empty chord.
		std::string text() const;
		// The text form back; none for text that names no key.
		static std::optional<KeyChord> parse(const std::string& a_text);
		// A key-down event with the modifiers held now.
		static KeyChord of(const SDL_Event& a_event);
		static bool modifierKey(SDL_Keycode a_key);
	};

	// The editor's keyboard shortcuts by action, each with the key it starts with. Actions are grouped by where they
	// apply: two in one group should not share a chord, two in different groups may.
	class KeyBindings {
	public:
		struct Binding {
			std::string id;
			std::string label;
			std::string group;
			KeyChord standard;
			KeyChord chord;
		};

		void add(const std::string& a_id, const std::string& a_label, const std::string& a_group, const KeyChord& a_standard);
		const std::vector<Binding>& all() const { return bindings; }
		std::vector<std::string> groups() const;
		const Binding* find(const std::string& a_id) const;

		KeyChord chord(const std::string& a_id) const;
		bool set(const std::string& a_id, const KeyChord& a_chord);
		void reset(const std::string& a_id);
		void resetAll();
		// The action in a group a key-down event is bound to.
		std::optional<std::string> actionFor(const std::string& a_group, const SDL_Event& a_event) const;
		bool matches(const std::string& a_id, const SDL_Event& a_event) const;
		// The other actions in a binding's group sharing its chord.
		std::vector<std::string> clashes(const std::string& a_id) const;

		// For the settings file: a "key.<id> = <chord>" line for each binding that is not its standard one, and one
		// such line read back (false for a name that is not a binding's).
		void write(std::ostream& a_out) const;
		bool read(const std::string& a_name, const std::string& a_value);

		// After a binding changes.
		std::function<void()> onChanged;

	private:
		Binding* findBinding(const std::string& a_id);

		std::vector<Binding> bindings;
	};

	// The Workbench's own shortcuts and the mesh editor's, with their standard keys.
	void addStandardBindings(KeyBindings& a_bindings);

}

#endif
