#ifndef _WORKBENCH_HISTORYSTRIP_H_
#define _WORKBENCH_HISTORYSTRIP_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "MV/Render/package.h"
#include "MV/Interface/tapDevice.h"
#include "commands.h"
#include "focus.h"
#include "icons.h"
#include "theme.h"

namespace Workbench {

	// The history as a row of bars, one per step, with a handle where the scene is: steps left of it are applied,
	// steps right of it are undone. A press on the row moves the handle there and a drag carries it, undoing and
	// redoing as it passes each step. A step back and a step forward sit at the ends of the bars; beside them the
	// position is a field over the total. The row is as wide as the view it sits on; when the bars would get thinner
	// than they can be drawn, they keep their width and a thin scroller appears under them.
	class HistoryStrip {
	public:
		HistoryStrip(const std::shared_ptr<MV::Scene::Node>& a_host, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, CommandHistory& a_history);

		bool expanded() const { return isExpanded; }
		void expanded(bool a_expanded);
		void availableWidth(float a_width);
		// The history's position, its steps or its saved mark moved.
		void refresh();

		MV::Size<> size() const { return isExpanded ? rowSize : MV::Size<>(); }
		// Bars on screen: every step, or the ones the scroller has in view.
		size_t pipCount() const { return shownBars; }
		bool scrolling() const { return maximumScroll > 0.0f; }
		// The row's text as shown: the position field, the total, then the step the scene is at ("2 / 4   move Sky"),
		// or the step under the pointer while it is over the bars.
		std::string caption() const;
		// Where on screen the handle sits when a_position steps are applied, and the middle of a step's bar;
		// meaningful while expanded and while that part of the history is in view.
		MV::Point<int> positionScreenPoint(size_t a_position) const;
		MV::Point<int> barScreenPoint(size_t a_step) const;
		MV::Point<int> stepButtonScreenPoint(bool a_forward) const;
		MV::Point<int> fieldScreenPoint() const;

	private:
		void rebuild();
		void showPosition();
		void moveToScreenPoint(const MV::Point<int>& a_screenPoint);
		void moveToTyped(const std::string& a_text);
		void pointerMoved(const MV::Point<int>& a_screenPoint);
		void scrollBy(float a_trackPixels);
		float positionX(size_t a_position) const { return trackInset + pipSpacing * static_cast<float>(a_position) - scrollOffset; }
		float usableWidth() const { return trackWidth - trackInset * 2.0f; }

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
		std::shared_ptr<MV::Scene::Node> barHost;
		std::vector<std::shared_ptr<MV::Scene::Sprite>> bars;
		std::shared_ptr<MV::Scene::Clickable> trackPress;
		std::shared_ptr<MV::Scene::Node> handle;
		std::shared_ptr<MV::Scene::Node> savedMark;
		std::shared_ptr<MV::Scene::Clickable> scrollThumb;
		std::shared_ptr<MV::Scene::Sprite> scrollThumbFace;
		std::unique_ptr<IconButton> backButton;
		std::unique_ptr<IconButton> forwardButton;
		std::shared_ptr<MV::Scene::Text> positionField;
		std::shared_ptr<MV::Scene::Text> totalText;
		std::shared_ptr<MV::Scene::Text> stepText;
		MV::TapDevice::SignalType pointerReceiver;

		float trackInset = 0.0f;
		float trackWidth = 0.0f;
		float trackHeight = 0.0f;
		float pipSpacing = 0.0f;
		float pipWidth = 0.0f;
		float scrollOffset = 0.0f;
		float maximumScroll = 0.0f;
		size_t shownBars = 0;
		size_t followedPosition = 0;
		std::optional<size_t> pointedStep;
	};

}

#endif
