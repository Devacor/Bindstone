#include "historyStrip.h"
#include "widgets.h"
#include "MV/Interface/tapDevice.h"
#include <algorithm>
#include <charconv>
#include <cmath>

namespace Workbench {

	namespace {
		const MV::Color rowFill(0.11f, 0.11f, 0.11f, 0.92f);
		const MV::Color trackFill(0.06f, 0.06f, 0.06f, 1.0f);
		const MV::Color appliedPip(0.96f, 0.56f, 0.13f, 1.0f);
		const MV::Color undonePip(0.36f, 0.36f, 0.36f, 1.0f);
		const MV::Color currentPip(0.36f, 0.82f, 0.42f, 1.0f);
		const MV::Color handleColor(0.92f, 0.92f, 0.92f, 1.0f);
	}

	HistoryStrip::HistoryStrip(const std::shared_ptr<MV::Scene::Node>& a_host, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, CommandHistory& a_history) :
		host(a_host),
		services(a_services),
		theme(a_theme),
		focus(a_focus),
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
			if (focus.activeWithin(row)) {
				focus.cancel();
			}
			row->removeFromParent();
			row.reset();
		}
		pips.clear();
		track.reset();
		handle.reset();
		savedMark.reset();
		backButton.reset();
		forwardButton.reset();
		positionField.reset();
		totalText.reset();
		stepText.reset();
		rowSize = MV::Size<>();
		if (!isExpanded) {
			return;
		}

		const float pad = theme.px(6.0f);
		const float rowHeight = theme.px(30.0f);
		const float characterWidth = theme.px(7.4f);
		const float buttonSize = IconButton::size(theme);
		const float fieldWidth = theme.px(44.0f);
		const float totalWidth = theme.px(46.0f);
		const float rowWidth = std::clamp(widthLimit, theme.px(260.0f), theme.px(680.0f));
		// the step's name gives way first: a narrow view keeps the bars, the arrows and the position
		const float stepWidth = rowWidth >= theme.px(480.0f) ? theme.px(200.0f) : 0.0f;
		stepCharacters = static_cast<size_t>(stepWidth / characterWidth);
		rowSize = MV::size(rowWidth, rowHeight);
		trackHeight = theme.px(18.0f);
		trackInset = theme.px(8.0f);

		row = host->make("historyRow");
		attachOutlinedRect(row, services, theme, rowSize, rowFill, theme.fieldOutline());

		float x = theme.px(2.0f);
		const float buttonY = std::round((rowHeight - buttonSize) * 0.5f);
		backButton = std::make_unique<IconButton>(row, services, theme, "back", Icon::StepBack, [this]() { history.undo(); });
		backButton->node()->position(MV::point(x, buttonY));
		x += buttonSize;
		forwardButton = std::make_unique<IconButton>(row, services, theme, "forward", Icon::StepForward, [this]() { history.redo(); });
		forwardButton->node()->position(MV::point(x, buttonY));
		x += buttonSize + pad;

		const float trackWidth = rowWidth - x - pad - fieldWidth - totalWidth - stepWidth - pad;
		track = row->make("track");
		track->position(MV::point(x, std::round((rowHeight - trackHeight) * 0.5f)));
		MV::Size<> trackSize(trackWidth, trackHeight);
		attachOutlinedRect(track, services, theme, trackSize, trackFill, theme.fieldOutline());
		x += trackWidth + pad;

		const size_t steps = history.length();
		const float usable = trackWidth - trackInset * 2.0f;
		pipSpacing = steps == 0 ? 0.0f : std::min(theme.px(7.0f), usable / static_cast<float>(steps));
		pipWidth = std::max(theme.px(1.0f), std::min(theme.px(3.0f), pipSpacing - theme.px(1.0f)));
		auto pipHost = track->make("pips");
		for (size_t step = 0; step < steps; ++step) {
			pips.push_back(pipHost->make(MV::guid("pip_"))->attach<MV::Scene::Sprite>().self());
		}

		savedMark = track->make("saved");
		savedMark->attach<MV::Scene::Sprite>()->bounds(MV::size(theme.px(5.0f), theme.px(2.0f)))->color(theme.accent());

		handle = track->make("handle");
		handle->attach<MV::Scene::Sprite>()->bounds(MV::size(theme.px(1.0f), trackHeight + theme.px(4.0f)))->color(handleColor);
		auto grip = handle->make("grip");
		grip->position(MV::point(-theme.px(3.0f), -theme.px(3.0f)));
		grip->attach<MV::Scene::Sprite>()->bounds(MV::size(theme.px(7.0f), theme.px(4.0f)))->color(handleColor);

		auto fieldHost = row->make("position");
		fieldHost->position(MV::point(x, std::round((rowHeight - theme.fieldHeight) * 0.5f)));
		positionField = makeTextField(fieldHost, services, theme, focus, "value", MV::size(fieldWidth, theme.fieldHeight), "", [this](const std::string& a_text) {
			moveToTyped(a_text);
		}, true);
		x += fieldWidth + theme.px(4.0f);
		totalText = makeLabel(row, services, theme, "total", MV::size(totalWidth, rowHeight), "");
		totalText->owner()->position(MV::point(x, 0.0f));
		x += totalWidth - theme.px(4.0f);
		if (stepWidth > 0.0f) {
			stepText = makeLabel(row, services, theme, "step", MV::size(stepWidth, rowHeight), "");
			stepText->owner()->position(MV::point(x, 0.0f));
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
		const float barHeight = theme.px(10.0f);
		const float currentHeight = theme.px(14.0f);
		for (size_t step = 0; step < pips.size(); ++step) {
			const bool current = step + 1 == position;
			const float height = current ? currentHeight : barHeight;
			pips[step]->owner()->position(MV::point(std::round(positionX(step) + (pipSpacing - pipWidth) * 0.5f), std::round((trackHeight - height) * 0.5f)));
			pips[step]->bounds(MV::size(pipWidth, height));
			pips[step]->color(current ? currentPip : step < position ? appliedPip : undonePip);
		}
		handle->position(MV::point(std::round(positionX(position)), -theme.px(2.0f)));
		if (auto saved = history.savedPosition()) {
			savedMark->show();
			savedMark->position(MV::point(std::round(positionX(*saved)) - theme.px(2.0f), trackHeight - theme.px(3.0f)));
		} else {
			savedMark->hide();
		}

		if (!focus.activeWithin(positionField->owner())) {
			positionField->text(std::to_string(position));
		}
		totalText->text("/ " + std::to_string(history.length()));
		if (stepText) {
			std::string step = position == 0 ? std::string(history.length() == 0 ? "no steps yet" : "start") : history.labelAt(position - 1);
			stepText->text(step.size() > stepCharacters && stepCharacters > 3 ? step.substr(0, stepCharacters - 3) + "..." : step);
		}
	}

	std::string HistoryStrip::caption() const {
		if (!row) {
			return {};
		}
		std::string result = std::string(positionField->text()) + " " + std::string(totalText->text());
		return stepText ? result + "   " + std::string(stepText->text()) : result;
	}

	void HistoryStrip::moveToScreenPoint(const MV::Point<int>& a_screenPoint) {
		if (!track || pipSpacing <= 0.0f) {
			return;
		}
		float along = (track->localFromScreen(a_screenPoint).x - trackInset) / pipSpacing;
		auto target = static_cast<size_t>(std::clamp(std::lround(along), 0L, static_cast<long>(history.length())));
		history.moveTo(target);
	}

	void HistoryStrip::moveToTyped(const std::string& a_text) {
		size_t typed = 0;
		auto parsed = std::from_chars(a_text.data(), a_text.data() + a_text.size(), typed);
		if (parsed.ec == std::errc() && parsed.ptr == a_text.data() + a_text.size()) {
			history.moveTo(typed);
		}
		// the field is still focused while its commit runs, so the refresh a move makes leaves what was typed
		if (positionField) {
			positionField->text(std::to_string(history.position()));
		}
	}

	MV::Point<int> HistoryStrip::positionScreenPoint(size_t a_position) const {
		return track->screenFromLocal(MV::point(positionX(a_position), trackHeight * 0.5f));
	}

	MV::Point<int> HistoryStrip::stepButtonScreenPoint(bool a_forward) const {
		return (a_forward ? forwardButton : backButton)->node()->screenBounds().centerPoint();
	}

	MV::Point<int> HistoryStrip::fieldScreenPoint() const {
		return positionField->owner()->screenBounds().centerPoint();
	}

}
