#include "settingsPanel.h"
#include "widgets.h"

namespace Workbench {

	void SettingsPanel::build() {
		const float labelWidth = theme->px(120.0f);
		const float fieldWidth = theme->px(80.0f);
		auto rowGrid = contentNode->attach<MV::Scene::Grid>()->columns(1)->layoutPolicy(MV::Scene::Grid::AutoLayoutPolicy::Child);
		rowGrid->hide();
		rowGrid->cellSize(MV::size(labelWidth + theme->pad + fieldWidth, theme->fieldHeight))->padding(MV::Point<>(), MV::point(0.0f, theme->px(4.0f)));
		for (size_t index = 0; index < rows.size(); ++index) {
			auto row = contentNode->make(MV::guid("setting_"));
			makeLabel(row, *services, *theme, "label", MV::size(labelWidth, theme->fieldHeight), rows[index].label);
			auto fieldHost = row->make("field");
			fieldHost->position(MV::point(labelWidth + theme->pad, 0.0f));
			fields.push_back(makeTextField(fieldHost, *services, *theme, *focus, "value", MV::size(fieldWidth, theme->fieldHeight), rows[index].value(), [this, index](const std::string& a_text) {
				rows[index].apply(a_text);
				fields[index]->text(rows[index].value());
			}, true));
		}
		rowGrid->layoutCells();
		rowsHeight = rows.empty() ? 0.0f : rows.size() * (theme->fieldHeight + theme->px(4.0f));
		builtHeight = rowsHeight;
		buildBindings();
	}

	std::string SettingsPanel::bindingText(const std::string& a_id) const {
		if (capturingId == a_id) {
			return "Press a key...";
		}
		auto chord = keys->chord(a_id);
		return (keys->clashes(a_id).empty() ? std::string() : std::string("! ")) + chord.text();
	}

	void SettingsPanel::buildBindings() {
		if (!keys) {
			return;
		}
		if (bindingHost) {
			bindingHost->removeFromParent();
		}
		bindingButtons.clear();
		resetButtons.clear();
		bindingsShown.clear();
		bindingHost = contentNode->make("keyBindings");
		const float gap = theme->px(4.0f);
		const float labelWidth = theme->px(170.0f);
		const float keyWidth = theme->px(110.0f);
		const float resetWidth = theme->px(52.0f);
		const float height = theme->fieldHeight;
		// the panel's Grid places this host below the rows, as it does each child of the content
		float y = theme->px(10.0f);
		auto title = makeLabel(bindingHost, *services, *theme, "title", MV::size(labelWidth, height), "Key bindings");
		title->owner()->position(MV::point(0.0f, y));
		auto resetAll = makeButton(bindingHost, *services, *theme, "resetAll", MV::size(keyWidth, height), "Reset all", [this]() {
			capturingId.clear();
			keys->resetAll();
			buildBindings();
		});
		resetAll->owner()->position(MV::point(labelWidth + gap, y));
		resetAllButton = resetAll->owner();
		y += height + gap * 2.0f;
		for (auto&& group : keys->groups()) {
			auto heading = makeLabel(bindingHost, *services, *theme, "group_" + group, MV::size(labelWidth + keyWidth, height), group);
			heading->owner()->position(MV::point(0.0f, y));
			y += height + gap;
			for (auto&& binding : keys->all()) {
				if (binding.group != group) {
					continue;
				}
				const std::string id = binding.id;
				auto label = makeLabel(bindingHost, *services, *theme, "label_" + id, MV::size(labelWidth - theme->px(10.0f), height), binding.label);
				label->owner()->position(MV::point(theme->px(10.0f), y));
				const auto shown = bindingText(id);
				bindingsShown[id] = shown;
				auto keyButton = makeButton(bindingHost, *services, *theme, "key_" + id, MV::size(keyWidth, height), shown, [this, id]() {
					if (capturingId == id) {
						capturingId.clear();
						buildBindings();
					} else {
						beginCapture(id);
					}
				});
				keyButton->owner()->position(MV::point(labelWidth + gap, y));
				bindingButtons[id] = keyButton->owner();
				if (!(binding.chord == binding.standard)) {
					auto reset = makeButton(bindingHost, *services, *theme, "reset_" + id, MV::size(resetWidth, height), "Reset", [this, id]() {
						capturingId.clear();
						keys->reset(id);
						buildBindings();
					});
					reset->owner()->position(MV::point(labelWidth + keyWidth + gap * 2.0f, y));
					resetButtons[id] = reset->owner();
				}
				y += height + gap;
			}
			y += gap * 2.0f;
		}
		builtHeight = rowsHeight + y;
	}

	void SettingsPanel::beginCapture(const std::string& a_id) {
		if (!keys || !keys->find(a_id)) {
			return;
		}
		if (focus->active()) {
			focus->leave();
		}
		capturingId = a_id;
		buildBindings();
	}

	void SettingsPanel::cancelCapture() {
		if (!capturingId.empty()) {
			capturingId.clear();
			buildBindings();
		}
	}

	bool SettingsPanel::captureKey(const SDL_Event& a_event) {
		if (capturingId.empty() || !keys) {
			return false;
		}
		if (a_event.type != SDL_KEYDOWN) {
			return a_event.type == SDL_KEYUP || a_event.type == SDL_TEXTINPUT;
		}
		const auto symbol = a_event.key.keysym.sym;
		if (KeyChord::modifierKey(symbol)) {
			return true;
		}
		const auto id = capturingId;
		capturingId.clear();
		if (symbol != SDLK_ESCAPE || (SDL_GetModState() & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT))) {
			keys->set(id, KeyChord::of(a_event));
			auto clashes = keys->clashes(id);
			if (!clashes.empty() && onWarning) {
				std::string names;
				for (auto&& other : clashes) {
					names += (names.empty() ? "" : ", ") + keys->find(other)->label;
				}
				onWarning(keys->chord(id).text() + " is now " + keys->find(id)->label + " and also " + names + " (" + keys->find(id)->group + ")");
			}
		}
		buildBindings();
		return true;
	}

	std::optional<MV::Point<int>> SettingsPanel::bindingScreenPoint(const std::string& a_id) const {
		auto found = bindingButtons.find(a_id);
		auto node = found == bindingButtons.end() ? nullptr : found->second.lock();
		return node ? std::optional<MV::Point<int>>(node->screenBounds().centerPoint()) : std::nullopt;
	}

	std::optional<MV::Point<int>> SettingsPanel::resetScreenPoint(const std::string& a_id) const {
		auto found = resetButtons.find(a_id);
		auto node = found == resetButtons.end() ? nullptr : found->second.lock();
		return node ? std::optional<MV::Point<int>>(node->screenBounds().centerPoint()) : std::nullopt;
	}

	std::optional<MV::Point<int>> SettingsPanel::resetAllScreenPoint() const {
		auto node = resetAllButton.lock();
		return node ? std::optional<MV::Point<int>>(node->screenBounds().centerPoint()) : std::nullopt;
	}

	std::string SettingsPanel::bindingShown(const std::string& a_id) const {
		auto found = bindingsShown.find(a_id);
		return found == bindingsShown.end() ? std::string() : found->second;
	}

	void SettingsPanel::refresh() {
		for (size_t index = 0; index < fields.size(); ++index) {
			if (!focus->activeWithin(fields[index]->owner())) {
				fields[index]->text(rows[index].value());
			}
		}
		if (keys) {
			for (auto&& binding : keys->all()) {
				auto found = bindingsShown.find(binding.id);
				if (found == bindingsShown.end() || found->second != bindingText(binding.id)) {
					buildBindings();
					break;
				}
			}
		}
	}

	std::shared_ptr<MV::Scene::Text> SettingsPanel::field(const std::string& a_label) const {
		for (size_t index = 0; index < fields.size(); ++index) {
			if (rows[index].label == a_label) {
				return fields[index];
			}
		}
		return nullptr;
	}

	MV::Point<int> SettingsPanel::fieldScreenPoint(const std::string& a_label) const {
		auto found = field(a_label);
		return found ? found->owner()->screenBounds().centerPoint() : MV::Point<int>();
	}

	std::string SettingsPanel::shown(const std::string& a_label) const {
		auto found = field(a_label);
		return found ? std::string(found->text()) : std::string();
	}

}
