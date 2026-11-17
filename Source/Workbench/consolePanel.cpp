#include "consolePanel.h"
#include "widgets.h"
#include <algorithm>

namespace Workbench {

	namespace {
		constexpr size_t rowsShown = 200;
	}

	std::string ConsolePanel::levelMarkup(MessageLevel a_level) {
		switch (a_level) {
			case MessageLevel::WARNING:
				return "[[c|0.96:0.78:0.26]]";
			case MessageLevel::FAILURE:
				return "[[c|0.98:0.42:0.38]]";
			default:
				return "[[c|0.78:0.78:0.78]]";
		}
	}

	void ConsolePanel::build() {
		auto clear = makeButton(contentNode, *services, *theme, "clear", MV::size(theme->px(64.0f), theme->rowHeight), "Clear", [this]() {
			log->clear();
		});
		tooltip(*services, clear, "Empty the console");
		clearNode = clear->owner();
		rowHost = contentNode->make("rows");
		rowHost->position(MV::point(0.0f, theme->rowHeight + theme->px(6.0f)));
		auto rowGrid = rowHost->attach<MV::Scene::Grid>()->columns(1)->layoutPolicy(MV::Scene::Grid::AutoLayoutPolicy::Child);
		rowGrid->hide();
		stale = true;
	}

	void ConsolePanel::resized(const MV::Size<>& a_size) {
		if (a_size.width != width) {
			width = a_size.width;
			stale = true;
		}
	}

	void ConsolePanel::update(double) {
		if (stale && rowHost) {
			stale = false;
			rebuildRows();
		}
	}

	void ConsolePanel::rebuildRows() {
		rowHost->clear();
		rowTexts.clear();
		auto rowGrid = rowHost->component<MV::Scene::Grid>(true, false);
		const float rowWidth = std::max(theme->px(120.0f), width);
		rowGrid->cellSize(MV::size(rowWidth, theme->fieldHeight))->padding(MV::Point<>(), MV::point(0.0f, theme->px(2.0f)));
		auto&& messages = log->messages();
		size_t shown = 0;
		for (auto message = messages.rbegin(); message != messages.rend() && shown < rowsShown; ++message, ++shown) {
			std::string text = message->text + (message->repeats > 1 ? " (x" + std::to_string(message->repeats) + ")" : "");
			rowTexts.push_back(text);
			auto row = rowHost->make(MV::guid("message_"));
			if (shown % 2 == 1) {
				row->attach<MV::Scene::Sprite>()->bounds(MV::size(rowWidth, theme->fieldHeight))->color(theme->panelAlt());
			}
			// fitted before it is coloured: the markup is not text to measure
			auto label = makeLabel(row, *services, *theme, "text", MV::size(rowWidth - theme->px(8.0f), theme->fieldHeight), levelMarkup(message->level) + fitLabelText(*services, *theme, text, rowWidth - theme->px(8.0f)));
			label->owner()->position(MV::point(theme->px(4.0f), 0.0f));
		}
		rowGrid->layoutCells();
		builtHeight = theme->rowHeight + theme->px(6.0f) + (shown > 0 ? rowGrid->bounds().height() : 0.0f);
	}

	MV::Point<int> ConsolePanel::clearButtonScreenPoint() const {
		return clearNode ? clearNode->screenBounds().centerPoint() : MV::Point<int>();
	}

}
