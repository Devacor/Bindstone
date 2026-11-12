#include "slider.h"
#include <memory>

#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/signals/signal.hpp>

namespace MV {
	namespace Scene {
		void Slider::jai_auto_bind(jai::dynamic_binder<Slider>& a_builder) {
			a_builder.property("onPercentChange", &Slider::onPercentChange);

			a_builder.method("percent", static_cast<PointPrecision(Slider::*)() const>(&Slider::percent));
			a_builder.method("percent", static_cast<std::shared_ptr<Slider>(Slider::*)(PointPrecision, bool)>(&Slider::percent), jai::serial);

			a_builder.method("handle", static_cast<std::shared_ptr<Node>(Slider::*)() const>(&Slider::handle));
			a_builder.method("handle", static_cast<std::shared_ptr<Slider>(Slider::*)(const std::shared_ptr<Node>&)>(&Slider::handle), jai::serial);
		}


		MV::PointPrecision Slider::percent() const {
			return dragPercent;
		}

		std::shared_ptr<Slider> Slider::percent(PointPrecision a_newPercent, bool a_notify /*= true*/) {
			float oldPercent = dragPercent;
			dragPercent = std::min(std::max(a_newPercent, 0.0f), 1.0f);
			auto self = std::static_pointer_cast<Slider>(shared_from_this());
			if (a_notify && oldPercent != dragPercent) {
				onPercentChangeSignal(self);
			}
			updateHandlePosition();
			return self;
		}

		std::shared_ptr<Slider> Slider::handle(const std::shared_ptr<Node> &a_handleNode) {
			if (a_handleNode) {
				dragHandle = a_handleNode;
				if (dragHandle->parent() != owner()) {
					dragHandle->parent(owner());
				}
				updateHandlePosition();
			}
			return std::static_pointer_cast<Slider>(shared_from_this());
		}

		std::shared_ptr<Node> Slider::handle() const {
			return dragHandle;
		}

		Slider::Slider(const std::weak_ptr<Node> &a_owner, TapDevice &a_mouse) :
			jai::property_owner<Slider, Clickable>(a_owner, a_mouse),
			onPercentChange(onPercentChangeSignal) {
			shouldDraw = true;
		}

		void Slider::acceptDownClick() {
			Clickable::acceptDownClick();

			updateDragFromMouse();
		}

		void Slider::updateDragFromMouse() {
			float oldPercent = dragPercent;
			dragPercent = calculatePercentFromPosition(owner()->localFromScreen(mouse().position()));
			auto self = std::static_pointer_cast<Slider>(shared_from_this()); //keep alive
			if (dragPercent != oldPercent) {
				onPercentChangeSignal(self);
			}
			updateHandlePosition();
		}

		MV::PointPrecision Slider::calculatePercentFromPosition(const Point<> &a_localPoint) {
			auto bounds = owner()->bounds();
			if (bounds.width() >= bounds.height()) {
				return std::min(std::max((a_localPoint.x - bounds.minPoint.x) / bounds.width(), 0.0f), 1.0f);
			} else {
				return std::min(std::max((a_localPoint.y - bounds.minPoint.y) / bounds.height(), 0.0f), 1.0f);
			}
		}

		void Slider::updateHandlePosition() {
			if (dragHandle) {
				auto areaBounds = owner()->bounds();
				auto handleBounds = dragHandle->bounds();

				PointPrecision firstX = areaBounds.minPoint.x + (areaBounds.width() - handleBounds.width());
				PointPrecision secondY = areaBounds.minPoint.y + (areaBounds.height() - handleBounds.height());
				if (areaBounds.width() >= areaBounds.height()) {
					dragHandle->position({ mix(areaBounds.minPoint.x, firstX, dragPercent), mix(areaBounds.minPoint.y, secondY, .5f) });
				} else {
					dragHandle->position({ mix(areaBounds.minPoint.x, firstX, .5f), mix(areaBounds.minPoint.y, secondY, dragPercent) });
				}
			}
		}

		void Slider::initialize() {
			Clickable::initialize();
			Clickable::onDrag.connect("HandleDrag", [](const std::shared_ptr<Clickable> &a_clickable, const Point<int> &startPosition, const Point<int> &deltaPosition) {
				auto self = std::static_pointer_cast<Slider>(a_clickable);
				self->updateDragFromMouse();
			});
		}

		std::shared_ptr<Component> Slider::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Clickable::cloneHelper(a_clone);
			auto sliderClone = std::static_pointer_cast<Slider>(a_clone);
			if (dragHandle) {
				auto foundHandle = sliderClone->owner()->get(dragHandle->id());
				sliderClone->handle(foundHandle);
			}
			return a_clone;
		}

	}
}
