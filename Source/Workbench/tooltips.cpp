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

	void Tooltips::update(double a_dt) {
		auto& mouse = *services.get<MV::TapDevice>();
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
		auto& mouse = *services.get<MV::TapDevice>();
		const float inset = theme.px(7.0f);
		MV::Size<> size(labelTextWidth(services, theme, pendingText) + inset * 2.0f, theme.fieldHeight);
		auto world = hostNode->renderer().worldFromScreen(mouse.position());
		auto worldSize = hostNode->renderer().world().size();
		MV::Point<> at(world.x + theme.px(12.0f), world.y + theme.px(18.0f));
		at.x = std::clamp(at.x, 0.0f, std::max(0.0f, worldSize.width - size.width));
		if (at.y + size.height > worldSize.height) {
			at.y = world.y - size.height - theme.px(6.0f);
		}
		popup = hostNode->make("tooltip");
		popup->position(MV::point(std::round(at.x), std::round(at.y)));
		attachOutlinedRect(popup, services, theme, size, tipFill, theme.fieldOutline());
		auto label = makeLabel(popup, services, theme, "text", MV::size(size.width - inset * 2.0f, size.height), pendingText);
		label->owner()->position(MV::point(inset, 0.0f));
		shownText = pendingText;
	}

	void Tooltips::hide() {
		if (popup) {
			popup->removeFromParent();
			popup.reset();
		}
		shownText.clear();
	}

}
