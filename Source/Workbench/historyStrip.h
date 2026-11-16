#ifndef _WORKBENCH_HISTORYSTRIP_H_
#define _WORKBENCH_HISTORYSTRIP_H_

#include <memory>
#include <string>
#include <vector>
#include "MV/Render/package.h"
#include "commands.h"
#include "theme.h"

namespace Workbench {

	// The history as a row of pips, one per step, with a handle where the scene is: steps left of it are applied,
	// steps right of it are undone. A press on the row moves the handle there and a drag carries it, undoing and
	// redoing as it passes each step.
	class HistoryStrip {
	public:
		HistoryStrip(const std::shared_ptr<MV::Scene::Node>& a_host, MV::Services& a_services, const Theme& a_theme, CommandHistory& a_history);

		bool expanded() const { return isExpanded; }
		void expanded(bool a_expanded);
		// The row never grows past this, so it stays inside the view it sits on.
		void availableWidth(float a_width);
		// The history's position, its steps or its saved mark moved.
		void refresh();

		MV::Size<> size() const { return isExpanded ? rowSize : MV::Size<>(); }
		size_t pipCount() const { return pips.size(); }
		const std::string& caption() const { return shownCaption; }
		// Where on screen the handle sits when a_position steps are applied; meaningful while expanded.
		MV::Point<int> positionScreenPoint(size_t a_position) const;

	private:
		void rebuild();
		void showPosition();
		void moveToScreenPoint(const MV::Point<int>& a_screenPoint);
		float positionX(size_t a_position) const { return trackInset + pipSpacing * static_cast<float>(a_position); }

		std::shared_ptr<MV::Scene::Node> host;
		MV::Services& services;
		const Theme& theme;
		CommandHistory& history;

		bool isExpanded = false;
		float widthLimit = 0.0f;
		MV::Size<> rowSize;

		std::shared_ptr<MV::Scene::Node> row;
		std::shared_ptr<MV::Scene::Node> track;
		std::vector<std::shared_ptr<MV::Scene::Sprite>> pips;
		std::shared_ptr<MV::Scene::Node> handle;
		std::shared_ptr<MV::Scene::Node> savedMark;
		std::shared_ptr<MV::Scene::Text> captionText;
		std::string shownCaption;
		float trackInset = 0.0f;
		float trackWidth = 0.0f;
		float pipSpacing = 0.0f;
		size_t captionCharacters = 0;
	};

}

#endif
