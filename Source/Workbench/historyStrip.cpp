#include "historyStrip.h"
#include "widgets.h"
#include "MV/Interface/tapDevice.h"
#include <algorithm>
#include <cmath>

namespace Workbench {

	namespace {
		const MV::Color rowFill(0.11f, 0.11f, 0.11f, 0.92f);
		const MV::Color trackFill(0.06f, 0.06f, 0.06f, 1.0f);
		const MV::Color appliedPip(0.82f, 0.82f, 0.82f, 1.0f);
		const MV::Color undonePip(0.32f, 0.32f, 0.32f, 1.0f);
		const MV::Color savedColor(0.36f, 0.78f, 0.45f, 1.0f);
	}

	HistoryStrip::HistoryStrip(const std::shared_ptr<MV::Scene::Node>& a_host, MV::Services& a_services, const Theme& a_theme, CommandHistory& a_history) :
		host(a_host),
		services(a_services),
		theme(a_theme),
		history(a_history) {
	}

	void HistoryStrip::expanded(bool a_expanded) {
		if (isExpanded != a_expanded) {
			isExpanded = a_expanded;
			rebuild();
		}
	}

	void HistoryStrip::availableWidth(float a_width) {
		if (widthLimit != a_width) {
			widthLimit = a_width;
			rebuild();
		}
	}

	void HistoryStrip::refresh() {
		if (!isExpanded) {
			return;
		}
		if (pips.size() != history.length()) {
			rebuild();
		} else {
			showPosition();
		}
	}

	void HistoryStrip::rebuild() {
		if (row) {
			row->removeFromParent();
			row.reset();
		}
		pips.clear();
		track.reset();
		handle.reset();
		savedMark.reset();
		captionText.reset();
		rowSize = MV::Size<>();
		if (!isExpanded) {
			return;
		}

		const float pad = theme.px(6.0f);
		const float rowHeight = theme.px(30.0f);
		const float trackHeight = theme.px(18.0f);
		const float characterWidth = theme.px(7.4f);
		const float rowWidth = std::clamp(widthLimit, theme.px(120.0f), theme.px(560.0f));
		// the caption gives way first: a narrow view keeps the pips
		const float captionWidth = rowWidth >= theme.px(360.0f) ? theme.px(240.0f) : 0.0f;
		captionCharacters = static_cast<size_t>(captionWidth / characterWidth);
		rowSize = MV::size(rowWidth, rowHeight);
		trackInset = theme.px(8.0f);
		trackWidth = rowWidth - pad * 2.0f - (captionWidth > 0.0f ? captionWidth + pad : 0.0f);

		row = host->make("historyRow");
		attachOutlinedRect(row, services, theme, rowSize, rowFill, theme.fieldOutline());

		track = row->make("track");
		track->position(MV::point(pad, std::round((rowHeight - trackHeight) * 0.5f)));
		MV::Size<> trackSize(trackWidth, trackHeight);
		attachOutlinedRect(track, services, theme, trackSize, trackFill, theme.fieldOutline());

		const size_t steps = history.length();
		const float usable = trackWidth - trackInset * 2.0f;
		pipSpacing = steps == 0 ? 0.0f : std::min(theme.px(12.0f), usable / static_cast<float>(steps));
		const bool roundPips = pipSpacing >= theme.px(11.0f);
		const float pipWidth = roundPips ? theme.px(8.0f) : std::max(theme.px(1.0f), std::min(theme.px(6.0f), pipSpacing - theme.px(1.0f)));
		const float pipHeight = theme.px(8.0f);
		auto pipHost = track->make("pips");
		for (size_t step = 0; step < steps; ++step) {
			auto pipNode = pipHost->make(MV::guid("pip_"));
			pipNode->position(MV::point(std::round(positionX(step) + (pipSpacing - pipWidth) * 0.5f), std::round((trackHeight - pipHeight) * 0.5f)));
			auto pip = pipNode->attach<MV::Scene::Sprite>()->bounds(MV::size(pipWidth, pipHeight));
			if (roundPips) {
				pip->texture(roundedRectHandle(services, theme));
			}
			pips.push_back(pip);
		}

		savedMark = track->make("saved");
		savedMark->attach<MV::Scene::Sprite>()->bounds(MV::size(theme.px(4.0f), theme.px(3.0f)))->color(savedColor);

		handle = track->make("handle");
		handle->attach<MV::Scene::Sprite>()->bounds(MV::size(theme.px(2.0f), trackHeight + theme.px(4.0f)))->color(theme.accent());
		auto grip = handle->make("grip");
		grip->position(MV::point(-theme.px(3.0f), -theme.px(2.0f)));
		attachRoundedRect(grip, services, theme, MV::size(theme.px(8.0f), theme.px(8.0f)), theme.accent());

		if (captionWidth > 0.0f) {
			captionText = makeLabel(row, services, theme, "caption", MV::size(captionWidth, rowHeight), "");
			captionText->owner()->position(MV::point(pad + trackWidth + pad, 0.0f));
		}

		auto& mouse = *services.get<MV::TapDevice>();
		auto press = track->attach<MV::Scene::Clickable>(mouse)->bounds(trackSize);
		press->onPress.connect("jump", [this](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
			moveToScreenPoint(a_clickable->mouse().position());
		});
		press->onDrag.connect("scrub", [this](const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const MV::Point<int>&, const MV::Point<int>&) {
			moveToScreenPoint(a_clickable->mouse().position());
		});
		showPosition();
	}

	void HistoryStrip::showPosition() {
		if (!row) {
			return;
		}
		const size_t position = history.position();
		for (size_t step = 0; step < pips.size(); ++step) {
			pips[step]->color(step < position ? appliedPip : undonePip);
		}
		handle->position(MV::point(std::round(positionX(position)) - theme.px(1.0f), -theme.px(2.0f)));
		if (auto saved = history.savedPosition()) {
			savedMark->show();
			savedMark->position(MV::point(std::round(positionX(*saved)) - theme.px(2.0f), theme.px(18.0f) - theme.px(4.0f)));
		} else {
			savedMark->hide();
		}

		shownCaption = std::to_string(position) + " / " + std::to_string(history.length());
		shownCaption += position == 0 ? (history.length() == 0 ? "   no steps yet" : "   start") : "   " + history.labelAt(position - 1);
		if (captionText) {
			std::string fitted = shownCaption.size() > captionCharacters && captionCharacters > 3 ? shownCaption.substr(0, captionCharacters - 3) + "..." : shownCaption;
			captionText->text(fitted);
		}
	}

	void HistoryStrip::moveToScreenPoint(const MV::Point<int>& a_screenPoint) {
		if (!track || pipSpacing <= 0.0f) {
			return;
		}
		float along = (track->localFromScreen(a_screenPoint).x - trackInset) / pipSpacing;
		auto target = static_cast<size_t>(std::clamp(std::lround(along), 0L, static_cast<long>(history.length())));
		history.moveTo(target);
	}

	MV::Point<int> HistoryStrip::positionScreenPoint(size_t a_position) const {
		return track->screenFromLocal(MV::point(positionX(a_position), theme.px(9.0f)));
	}

}
