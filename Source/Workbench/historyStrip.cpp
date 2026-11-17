#include "historyStrip.h"
#include "widgets.h"
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
		const MV::Color pointedPip(1.0f, 1.0f, 1.0f, 1.0f);
		const MV::Color handleColor(0.92f, 0.92f, 0.92f, 1.0f);
	}

	HistoryStrip::HistoryStrip(const std::shared_ptr<MV::Scene::Node>& a_host, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, CommandHistory& a_history) :
		host(a_host),
		services(a_services),
		theme(a_theme),
		focus(a_focus),
		history(a_history) {

		pointerReceiver = services.get<MV::TapDevice>()->onMove.connect([this](MV::TapDevice& a_mouse) {
			pointerMoved(a_mouse.position());
		});
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
		if (isExpanded) {
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
		bars.clear();
		barHost.reset();
		track.reset();
		trackPress.reset();
		handle.reset();
		savedMark.reset();
		scrollThumb.reset();
		scrollThumbFace.reset();
		backButton.reset();
		forwardButton.reset();
		positionField.reset();
		totalText.reset();
		stepText.reset();
		pointedStep.reset();
		shownBars = 0;
		rowSize = MV::Size<>();
		if (!isExpanded) {
			return;
		}

		const float pad = theme.px(6.0f);
		const float rowHeight = theme.px(30.0f);
		const float buttonSize = IconButton::size(theme);
		const float fieldWidth = theme.px(44.0f);
		const float totalWidth = theme.px(52.0f);
		const float rowWidth = std::max(widthLimit, theme.px(260.0f));
		// the step's name gives way first: a narrow view keeps the bars, the arrows and the position
		const float stepWidth = rowWidth >= theme.px(520.0f) ? theme.px(220.0f) : 0.0f;
		rowSize = MV::size(rowWidth, rowHeight);
		trackHeight = theme.px(18.0f);
		trackInset = theme.px(8.0f);

		row = host->make("historyRow");
		attachOutlinedRect(row, services, theme, rowSize, rowFill, theme.fieldOutline());

		float x = theme.px(2.0f);
		const float buttonY = std::round((rowHeight - buttonSize) * 0.5f);
		backButton = std::make_unique<IconButton>(row, services, theme, "back", Icon::StepBack, [this]() { history.undo(); }, "Undo one step (Ctrl+Z)");
		backButton->node()->position(MV::point(x, buttonY));
		x += buttonSize + theme.px(2.0f);

		trackWidth = rowWidth - x - theme.px(2.0f) - buttonSize - pad - fieldWidth - theme.px(4.0f) - totalWidth - stepWidth - pad;
		const float trackY = std::round((rowHeight - trackHeight) * 0.5f) - theme.px(1.0f);
		track = row->make("track");
		track->position(MV::point(x, trackY));
		MV::Size<> trackSize(trackWidth, trackHeight);
		attachOutlinedRect(track, services, theme, trackSize, trackFill, theme.fieldOutline());
		barHost = track->make("bars");
		x += trackWidth + theme.px(2.0f);

		forwardButton = std::make_unique<IconButton>(row, services, theme, "forward", Icon::StepForward, [this]() { history.redo(); }, "Redo one step (Ctrl+Y)");
		forwardButton->node()->position(MV::point(x, buttonY));
		x += buttonSize + pad;

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
		x += totalWidth;
		if (stepWidth > 0.0f) {
			stepText = makeLabel(row, services, theme, "step", MV::size(stepWidth, rowHeight), "");
			stepText->owner()->position(MV::point(x, 0.0f));
		}

		auto& mouse = *services.get<MV::TapDevice>();
		trackPress = track->attach<MV::Scene::Clickable>(mouse)->bounds(trackSize);
		trackPress->onPress.connect("jump", [this](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
			moveToScreenPoint(a_clickable->mouse().position());
		});
		trackPress->onDrag.connect("scrub", [this](const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const MV::Point<int>&, const MV::Point<int>&) {
			moveToScreenPoint(a_clickable->mouse().position());
		});
		trackPress->onHoverEnter.connect("point", [this](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
			pointerMoved(a_clickable->mouse().position());
		});
		trackPress->onHoverLeave.connect("unpoint", [this](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (pointedStep) {
				pointedStep.reset();
				showPosition();
			}
		});

		auto thumbNode = row->make("scroller");
		thumbNode->position(MV::point(track->position().x + trackInset, trackY + trackHeight + theme.px(1.0f)));
		scrollThumb = thumbNode->attach<MV::Scene::Clickable>(mouse).self();
		scrollThumb->onDrag.connect("scroll", [this](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<int>&, const MV::Point<int>& a_delta) {
			scrollBy(static_cast<float>(a_delta.x));
		});
		scrollThumbFace = thumbNode->make("face")->attach<MV::Scene::Sprite>().self();
		scrollThumbFace->color(theme.scrollThumb());

		followedPosition = history.position() + 1;
		showPosition();
	}

	void HistoryStrip::showPosition() {
		if (!row) {
			return;
		}
		const size_t steps = history.length();
		const size_t position = history.position();
		const float usable = usableWidth();
		// a bar is never thinner than a logical pixel with one between: past that the row scrolls
		const float minimumSpacing = theme.px(2.0f);
		pipSpacing = steps == 0 ? 0.0f : std::clamp(usable / static_cast<float>(steps), minimumSpacing, theme.px(7.0f));
		pipWidth = std::max(theme.px(1.0f), std::min(theme.px(3.0f), pipSpacing - theme.px(1.0f)));
		const float contentWidth = pipSpacing * static_cast<float>(steps);
		maximumScroll = std::max(0.0f, contentWidth - usable);
		if (position != followedPosition) {
			followedPosition = position;
			const float handleAt = pipSpacing * static_cast<float>(position);
			if (handleAt < scrollOffset) {
				scrollOffset = handleAt - usable * 0.25f;
			} else if (handleAt > scrollOffset + usable) {
				scrollOffset = handleAt - usable * 0.75f;
			}
		}
		scrollOffset = std::round(std::clamp(scrollOffset, 0.0f, maximumScroll));
		if (pointedStep && *pointedStep >= steps) {
			pointedStep.reset();
		}

		const float barHeight = theme.px(10.0f);
		const float tallHeight = theme.px(14.0f);
		const float leftEdge = trackInset - 0.5f;
		const float rightEdge = trackWidth - trackInset + 0.5f;
		const size_t first = pipSpacing > 0.0f ? static_cast<size_t>(scrollOffset / pipSpacing) : 0;
		shownBars = 0;
		for (size_t step = first; step < steps; ++step) {
			const float left = std::round(positionX(step) + (pipSpacing - pipWidth) * 0.5f);
			if (left + pipWidth > rightEdge) {
				break;
			}
			if (left < leftEdge) {
				continue;
			}
			if (bars.size() <= shownBars) {
				bars.push_back(barHost->make(MV::guid("bar_"))->attach<MV::Scene::Sprite>().self());
			}
			auto& bar = bars[shownBars++];
			const bool current = step + 1 == position;
			const bool pointed = pointedStep && *pointedStep == step;
			const float height = current || pointed ? tallHeight : barHeight;
			bar->owner()->show();
			bar->owner()->position(MV::point(left, std::round((trackHeight - height) * 0.5f)));
			bar->bounds(MV::size(pipWidth, height));
			bar->color(pointed ? pointedPip : current ? currentPip : step < position ? appliedPip : undonePip);
		}
		for (size_t unused = shownBars; unused < bars.size(); ++unused) {
			bars[unused]->owner()->hide();
		}

		auto placeMark = [&](const std::shared_ptr<MV::Scene::Node>& a_mark, float a_x, float a_y) {
			if (a_x >= leftEdge - theme.px(1.0f) && a_x <= rightEdge + theme.px(1.0f)) {
				a_mark->show();
				a_mark->position(MV::point(a_x, a_y));
			} else {
				a_mark->hide();
			}
		};
		placeMark(handle, std::round(positionX(position)), -theme.px(2.0f));
		if (auto saved = history.savedPosition()) {
			placeMark(savedMark, std::round(positionX(*saved)) - theme.px(2.0f), trackHeight - theme.px(3.0f));
		} else {
			savedMark->hide();
		}

		if (maximumScroll > 0.0f) {
			const float thumbWidth = std::max(theme.px(20.0f), usable * usable / contentWidth);
			const float thumbHeight = theme.px(3.0f);
			scrollThumb->owner()->position(MV::point(track->position().x + trackInset + (usable - thumbWidth) * (scrollOffset / maximumScroll), scrollThumb->owner()->position().y));
			scrollThumb->bounds(MV::size(thumbWidth, thumbHeight + theme.px(2.0f)));
			scrollThumbFace->bounds(MV::size(thumbWidth, thumbHeight));
			scrollThumbFace->owner()->show();
		} else {
			scrollThumb->bounds(MV::size(0.0f, 0.0f));   // a hidden Clickable still takes presses
			scrollThumbFace->owner()->hide();
		}

		if (!focus.activeWithin(positionField->owner())) {
			positionField->text(std::to_string(position));
		}
		totalText->text("/ " + std::to_string(steps));
		if (stepText) {
			std::string step;
			if (pointedStep) {
				step = "step " + std::to_string(*pointedStep + 1) + ": " + history.labelAt(*pointedStep);
			} else {
				step = position == 0 ? std::string(steps == 0 ? "no steps yet" : "start") : history.labelAt(position - 1);
			}
			setLabelText(stepText, services, theme, step);
		}
	}

	std::string HistoryStrip::caption() const {
		if (!row) {
			return {};
		}
		std::string result = std::string(positionField->text()) + " " + std::string(totalText->text());
		return stepText ? result + "   " + std::string(stepText->text()) : result;
	}

	void HistoryStrip::pointerMoved(const MV::Point<int>& a_screenPoint) {
		if (!trackPress || !trackPress->hovered() || pipSpacing <= 0.0f) {
			return;
		}
		float along = (track->localFromScreen(a_screenPoint).x - trackInset + scrollOffset) / pipSpacing;
		std::optional<size_t> pointed;
		// a press is moving the handle: the row names the step it is at
		if (!trackPress->inPressEvent() && along >= 0.0f && along < static_cast<float>(history.length())) {
			pointed = static_cast<size_t>(along);
		}
		if (pointed != pointedStep) {
			pointedStep = pointed;
			showPosition();
		}
	}

	void HistoryStrip::scrollBy(float a_trackPixels) {
		if (maximumScroll > 0.0f) {
			const float thumbTravel = usableWidth() - scrollThumb->bounds().width();
			scrollOffset += thumbTravel > 0.0f ? a_trackPixels * maximumScroll / thumbTravel : 0.0f;
			showPosition();
		}
	}

	void HistoryStrip::moveToScreenPoint(const MV::Point<int>& a_screenPoint) {
		if (!track || pipSpacing <= 0.0f) {
			return;
		}
		float along = (track->localFromScreen(a_screenPoint).x - trackInset + scrollOffset) / pipSpacing;
		auto target = static_cast<size_t>(std::clamp(std::lround(along), 0L, static_cast<long>(history.length())));
		// the pointer picks a place in what is on screen: the view does not jump under it
		followedPosition = target;
		pointedStep.reset();
		history.moveTo(target);
	}

	void HistoryStrip::moveToTyped(const std::string& a_text) {
		size_t typed = 0;
		auto parsed = std::from_chars(a_text.data(), a_text.data() + a_text.size(), typed);
		if (parsed.ec == std::errc() && parsed.ptr == a_text.data() + a_text.size()) {
			history.moveTo(typed);
		}
		if (positionField) {
			positionField->text(std::to_string(history.position()));
		}
	}

	MV::Point<int> HistoryStrip::positionScreenPoint(size_t a_position) const {
		return track->screenFromLocal(MV::point(positionX(a_position), trackHeight * 0.5f));
	}

	MV::Point<int> HistoryStrip::barScreenPoint(size_t a_step) const {
		return track->screenFromLocal(MV::point(positionX(a_step) + pipSpacing * 0.5f, trackHeight * 0.5f));
	}

	MV::Point<int> HistoryStrip::stepButtonScreenPoint(bool a_forward) const {
		return (a_forward ? forwardButton : backButton)->node()->screenBounds().centerPoint();
	}

	MV::Point<int> HistoryStrip::fieldScreenPoint() const {
		return positionField->owner()->screenBounds().centerPoint();
	}

}
