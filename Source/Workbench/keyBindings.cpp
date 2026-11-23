#include "keyBindings.h"
#include <algorithm>
#include <ostream>
#include <sstream>

namespace Workbench {

	namespace {
		std::string trimmed(std::string a_text) {
			a_text.erase(0, a_text.find_first_not_of(" \t\r"));
			a_text.erase(a_text.find_last_not_of(" \t\r") + 1);
			return a_text;
		}

		bool sameText(std::string a_first, std::string a_second) {
			auto lower = [](std::string& a_text) {
				std::transform(a_text.begin(), a_text.end(), a_text.begin(), [](unsigned char a_character) { return static_cast<char>(std::tolower(a_character)); });
			};
			lower(a_first);
			lower(a_second);
			return a_first == a_second;
		}

		KeyChord chordOf(SDL_Keycode a_key, bool a_control = false, bool a_shift = false, bool a_alt = false) {
			KeyChord result;
			result.key = a_key;
			result.control = a_control;
			result.shift = a_shift;
			result.alt = a_alt;
			return result;
		}
	}

	std::string KeyChord::text() const {
		if (empty()) {
			return "None";
		}
		std::string result;
		if (control) {
			result += "Ctrl+";
		}
		if (shift) {
			result += "Shift+";
		}
		if (alt) {
			result += "Alt+";
		}
		std::string name = SDL_GetKeyName(key);
		return result + (name.empty() ? "?" : name);
	}

	std::optional<KeyChord> KeyChord::parse(const std::string& a_text) {
		const auto text = trimmed(a_text);
		if (sameText(text, "None") || text.empty()) {
			return KeyChord();
		}
		KeyChord result;
		std::string rest = text;
		// the key itself may be "+" or "Keypad +", so modifiers come off the front one at a time
		for (bool more = true; more;) {
			more = false;
			for (auto&& [prefix, flag] : { std::pair<const char*, bool*>{ "Ctrl+", &result.control }, { "Shift+", &result.shift }, { "Alt+", &result.alt } }) {
				const std::string lead = prefix;
				if (rest.size() > lead.size() && sameText(rest.substr(0, lead.size()), lead)) {
					*flag = true;
					rest = rest.substr(lead.size());
					more = true;
				}
			}
		}
		result.key = SDL_GetKeyFromName(rest.c_str());
		if (result.key == SDLK_UNKNOWN) {
			return std::nullopt;
		}
		return result;
	}

	KeyChord KeyChord::of(const SDL_Event& a_event) {
		const auto held = static_cast<Uint16>(SDL_GetModState()) | a_event.key.keysym.mod;
		return chordOf(a_event.key.keysym.sym, (held & KMOD_CTRL) != 0, (held & KMOD_SHIFT) != 0, (held & KMOD_ALT) != 0);
	}

	bool KeyChord::modifierKey(SDL_Keycode a_key) {
		switch (a_key) {
			case SDLK_LCTRL:
			case SDLK_RCTRL:
			case SDLK_LSHIFT:
			case SDLK_RSHIFT:
			case SDLK_LALT:
			case SDLK_RALT:
			case SDLK_LGUI:
			case SDLK_RGUI:
			case SDLK_CAPSLOCK:
				return true;
			default:
				return false;
		}
	}

	void KeyBindings::add(const std::string& a_id, const std::string& a_label, const std::string& a_group, const KeyChord& a_standard) {
		if (auto* existing = findBinding(a_id)) {
			*existing = Binding{ a_id, a_label, a_group, a_standard, a_standard };
			return;
		}
		bindings.push_back(Binding{ a_id, a_label, a_group, a_standard, a_standard });
	}

	std::vector<std::string> KeyBindings::groups() const {
		std::vector<std::string> result;
		for (auto&& binding : bindings) {
			if (std::find(result.begin(), result.end(), binding.group) == result.end()) {
				result.push_back(binding.group);
			}
		}
		return result;
	}

	const KeyBindings::Binding* KeyBindings::find(const std::string& a_id) const {
		for (auto&& binding : bindings) {
			if (binding.id == a_id) {
				return &binding;
			}
		}
		return nullptr;
	}

	KeyBindings::Binding* KeyBindings::findBinding(const std::string& a_id) {
		return const_cast<Binding*>(static_cast<const KeyBindings*>(this)->find(a_id));
	}

	KeyChord KeyBindings::chord(const std::string& a_id) const {
		auto* binding = find(a_id);
		return binding ? binding->chord : KeyChord();
	}

	bool KeyBindings::set(const std::string& a_id, const KeyChord& a_chord) {
		auto* binding = findBinding(a_id);
		if (!binding) {
			return false;
		}
		if (!(binding->chord == a_chord)) {
			binding->chord = a_chord;
			if (onChanged) {
				onChanged();
			}
		}
		return true;
	}

	void KeyBindings::reset(const std::string& a_id) {
		if (auto* binding = find(a_id)) {
			set(a_id, binding->standard);
		}
	}

	void KeyBindings::resetAll() {
		bool changed = false;
		for (auto&& binding : bindings) {
			changed = changed || !(binding.chord == binding.standard);
			binding.chord = binding.standard;
		}
		if (changed && onChanged) {
			onChanged();
		}
	}

	std::optional<std::string> KeyBindings::actionFor(const std::string& a_group, const SDL_Event& a_event) const {
		if (a_event.type != SDL_KEYDOWN) {
			return std::nullopt;
		}
		const auto pressed = KeyChord::of(a_event);
		for (auto&& binding : bindings) {
			if (binding.group == a_group && !binding.chord.empty() && binding.chord == pressed) {
				return binding.id;
			}
		}
		return std::nullopt;
	}

	bool KeyBindings::matches(const std::string& a_id, const SDL_Event& a_event) const {
		auto* binding = find(a_id);
		return binding && a_event.type == SDL_KEYDOWN && !binding->chord.empty() && binding->chord == KeyChord::of(a_event);
	}

	std::vector<std::string> KeyBindings::clashes(const std::string& a_id) const {
		std::vector<std::string> result;
		auto* binding = find(a_id);
		if (!binding || binding->chord.empty()) {
			return result;
		}
		for (auto&& other : bindings) {
			if (other.id != a_id && other.group == binding->group && other.chord == binding->chord) {
				result.push_back(other.id);
			}
		}
		return result;
	}

	void KeyBindings::write(std::ostream& a_out) const {
		for (auto&& binding : bindings) {
			if (!(binding.chord == binding.standard)) {
				a_out << "key." << binding.id << " = " << binding.chord.text() << "\n";
			}
		}
	}

	bool KeyBindings::read(const std::string& a_name, const std::string& a_value) {
		if (a_name.rfind("key.", 0) != 0) {
			return false;
		}
		auto* binding = findBinding(a_name.substr(4));
		auto chord = KeyChord::parse(a_value);
		if (!binding || !chord) {
			return false;
		}
		binding->chord = *chord;
		return true;
	}

	void addStandardBindings(KeyBindings& a_bindings) {
		const std::string editor = "Workbench";
		a_bindings.add("tool.select", "Select tool", editor, chordOf(SDLK_q));
		a_bindings.add("tool.move", "Move tool", editor, chordOf(SDLK_w));
		a_bindings.add("tool.rotate", "Rotate tool", editor, chordOf(SDLK_e));
		a_bindings.add("tool.scale", "Scale tool", editor, chordOf(SDLK_r));
		a_bindings.add("view.frame", "Frame selection", editor, chordOf(SDLK_f));
		a_bindings.add("node.delete", "Delete", editor, chordOf(SDLK_DELETE));
		a_bindings.add("node.duplicate", "Duplicate", editor, chordOf(SDLK_d, true));
		a_bindings.add("edit.undo", "Undo", editor, chordOf(SDLK_z, true));
		a_bindings.add("edit.redo", "Redo", editor, chordOf(SDLK_y, true));
		a_bindings.add("edit.redoAlternate", "Redo (second key)", editor, chordOf(SDLK_z, true, true));
		a_bindings.add("file.open", "Open", editor, chordOf(SDLK_o, true));
		a_bindings.add("file.save", "Save", editor, chordOf(SDLK_s, true));
		a_bindings.add("file.saveAs", "Save As", editor, chordOf(SDLK_s, true, true));

		const std::string mesh = "Mesh editor";
		a_bindings.add("mesh.toolSelect", "Select tool", mesh, chordOf(SDLK_1));
		a_bindings.add("mesh.toolPolygon", "Polygon tool", mesh, chordOf(SDLK_2));
		a_bindings.add("mesh.toolRibbon", "Ribbon tool", mesh, chordOf(SDLK_3));
		a_bindings.add("mesh.toolPaint", "Paint tool", mesh, chordOf(SDLK_4));
		a_bindings.add("mesh.move", "Move", mesh, chordOf(SDLK_g));
		a_bindings.add("mesh.rotate", "Rotate", mesh, chordOf(SDLK_r));
		a_bindings.add("mesh.scale", "Scale", mesh, chordOf(SDLK_s));
		a_bindings.add("mesh.extrude", "Extrude", mesh, chordOf(SDLK_e));
		a_bindings.add("mesh.fill", "Fill", mesh, chordOf(SDLK_f));
		a_bindings.add("mesh.remove", "Remove", mesh, chordOf(SDLK_x));
		a_bindings.add("mesh.removeAlternate", "Remove (second key)", mesh, chordOf(SDLK_DELETE));
		a_bindings.add("mesh.merge", "Merge", mesh, chordOf(SDLK_m));
		a_bindings.add("mesh.duplicate", "Duplicate", mesh, chordOf(SDLK_d, false, true));
		a_bindings.add("mesh.all", "Pick all or none", mesh, chordOf(SDLK_a));
		a_bindings.add("mesh.island", "Pick island under pointer", mesh, chordOf(SDLK_l));
		a_bindings.add("mesh.colour", "Colour the pick", mesh, chordOf(SDLK_c));

		const std::string meshMoving = "Mesh editor, while moving";
		a_bindings.add("mesh.lockX", "Keep to X", meshMoving, chordOf(SDLK_x));
		a_bindings.add("mesh.lockY", "Keep to Y", meshMoving, chordOf(SDLK_y));
	}

}
