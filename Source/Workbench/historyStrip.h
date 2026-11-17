#ifndef _WORKBENCH_HISTORYSTRIP_H_
#define _WORKBENCH_HISTORYSTRIP_H_

#include <memory>
#include <string>
#include <vector>
#include "MV/Render/package.h"
#include "commands.h"
#include "focus.h"
#include "icons.h"
#include "theme.h"

namespace Workbench {

	// The history as a row of bars, one per step, with a handle where the scene is: steps left of it are applied,
	// steps right of it are undone. A press on the row moves the handle there and a drag carries it, undoing and
	// redoing as it passes each step. Beside it: a step back, a step forward, and the position as a field over the total.
	class HistoryStrip {
	public:
		HistoryStrip(const std::shared_ptr<MV::Scene::Node>& a_host, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, CommandHistory& a_history);

		bool expanded() const { return isExpanded; }
		void expanded(bool a_expanded);
		// The row never grows past this, so it stays inside the view it sits on.
		void availableWidth(float a_width);
		// The history's position, its steps or its saved mark moved.
		void refresh();

		MV::Size<> size() const { return isExpanded ? rowSize : MV::Size<>(); }
		size_t pipCount() const { return pips.size(); }
		// The row's text as shown: the position field, the total, then the step the scene is at ("2 / 4   move Sky").
		std::string caption() const;
		// Where on screen the handle sits when a_position steps are applied; meaningful while expanded.
		MV::Point<int> positionScreenPoint(size_t a_position) const;
		MV::Point<int> stepButtonScreenPoint(bool a_forward) const;
		MV::Point<int> fieldScreenPoint() const;

	private:
		void rebuild();
		void showPosition();
		void moveToScreenPoint(const MV::Point<int>& a_screenPoint);
		void moveToTyped(const std::string& a_text);
		float positionX(size_t a_position) const { return trackInset + pipSpacing * static_cast<float>(a_position); }

		std::shared_ptr<MV::Scene::Node> host;
		MV::Services& services;
		const Theme& theme;
		FocusRouter& focus;
		CommandHistory& history;

		bool isExpanded = false;
		float widthLimit = 0.0f;
		MV::Size<> rowSize;

		std::shared_ptr<MV::Scene::Node> row;
		std::shared_ptr<MV::Scene::Node> track;
		std::vector<std::shared_ptr<MV::Scene::Sprite>> pips;
		std::shared_ptr<MV::Scene::Node> handle;
		std::shared_ptr<MV::Scene::Node> savedMark;
		std::unique_ptr<IconButton> backButton;
		std::unique_ptr<IconButton> forwardButton;
		std::shared_ptr<MV::Scene::Text> positionField;
		std::shared_ptr<MV::Scene::Text> totalText;
		std::shared_ptr<MV::Scene::Text> stepText;
		float trackInset = 0.0f;
		float trackHeight = 0.0f;
		float pipSpacing = 0.0f;
		float pipWidth = 0.0f;
		size_t stepCharacters = 0;
	};

}

#endif
