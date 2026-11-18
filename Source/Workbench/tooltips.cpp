#include "tooltips.h"
#include "widgets.h"
#include <algorithm>

namespace Workbench {

	namespace {
		const MV::Color tipFill(0x1b1b1b);
	}

	Tooltips::Tooltips(MV::Services& a_services, const Theme& a_theme) :
		services(a_services),
		theme(a_theme) {
	}

	void Tooltips::attach(const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const std::string& a_text) {
		std::weak_ptr<MV::Scene::Clickable> weakClickable = a_clickable;
		a_clickable->onHoverEnter.connect("tip", [this, weakClickable, a_text](const std::shared_ptr<MV::Scene::Clickable>&) {
			hide();
			pending = weakClickable;
			pendingText = a_text;
			rested = 0.0;
		});
		a_clickable->onHoverLeave.connect("tip", [this, weakClickable](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (pending.lock() == weakClickable.lock()) {
				pending.reset();
				hide();
			}
		});
	}

	void Tooltips::dragNote(const std::string& a_text) {
		if (a_text == noteText) {
			return;
		}
		if (notePopup) {
			notePopup->removeFromParent();
			notePopup.reset();
		}
		noteText = a_text;
		if (!noteText.empty() && hostNode) {
			notePopup = makePopup("dragNote", noteText, noteSize);
		}
	}

	void Tooltips::update(double a_dt) {
		auto& mouse = *services.get<MV::TapDevice>();
		if (notePopup) {
			placeBesidePointer(notePopup, noteSize);
		}
		auto resting = pending.lock();
		if (!resting || !resting->hovered() || mouse.leftDown() || mouse.rightDown() || mouse.middleDown()) {
			if (!resting || !resting->hovered()) {
				pending.reset();
			}
			rested = 0.0;
			hide();
			return;
		}
		if (mouse.position() != restedAt) {
			restedAt = mouse.position();
			rested = 0.0;
			return;
		}
		rested += a_dt;
		if (rested >= restSeconds && shownText.empty()) {
			show();
		}
	}

	void Tooltips::show() {
		if (!hostNode || pendingText.empty()) {
			return;
		}
		hide();
		MV::Size<> size;
		popup = makePopup("tooltip", pendingText, size);
		shownText = pendingText;
	}

	std::shared_ptr<MV::Scene::Node> Tooltips::makePopup(const std::string& a_id, const std::string& a_text, MV::Size<>& a_size) {
		const float inset = theme.px(7.0f);
		a_size = MV::Size<>(labelTextWidth(services, theme, a_text) + inset * 2.0f, theme.fieldHeight);
		auto result = hostNode->make(a_id);
		attachOutlinedRect(result, services, theme, a_size, tipFill, theme.fieldOutline());
		auto label = makeLabel(result, services, theme, "text", MV::size(a_size.width - inset * 2.0f, a_size.height), a_text);
		label->owner()->position(MV::point(inset, 0.0f));
		placeBesidePointer(result, a_size);
		return result;
	}

	void Tooltips::placeBesidePointer(const std::shared_ptr<MV::Scene::Node>& a_popup, const MV::Size<>& a_size) {
		auto& mouse = *services.get<MV::TapDevice>();
		auto world = hostNode->renderer().worldFromScreen(mouse.position());
		auto worldSize = hostNode->renderer().world().size();
		MV::Point<> at(world.x + theme.px(12.0f), world.y + theme.px(18.0f));
		at.x = std::clamp(at.x, 0.0f, std::max(0.0f, worldSize.width - a_size.width));
		if (at.y + a_size.height > worldSize.height) {
			at.y = world.y - a_size.height - theme.px(6.0f);
		}
		a_popup->position(MV::point(std::round(at.x), std::round(at.y)));
	}

	void Tooltips::hide() {
		if (popup) {
			popup->removeFromParent();
			popup.reset();
		}
		shownText.clear();
	}

}
