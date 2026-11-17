#include "scroller.h"

#include "text.h"

#include <algorithm>

#include <jaiscript/core/dynamic_binder.hpp>

namespace MV {
	namespace Scene {
		void Scroller::jai_auto_bind(jai::dynamic_binder<Scroller>& a_builder) {
			a_builder.method("content", static_cast<std::shared_ptr<Node>(Scroller::*)() const>(&Scroller::content));
			a_builder.method("content", static_cast<std::shared_ptr<Scroller>(Scroller::*)(const std::shared_ptr<Node>&)>(&Scroller::content), jai::serial);
			a_builder.method("focusOnChild", &Scroller::focusOnChild, jai::serial);
		}


		Scroller::Scroller(const std::weak_ptr<Node> &a_owner, TapDevice &a_mouse) :
			jai::property_owner<Scroller, Clickable>(a_owner, a_mouse) {
			stopEatingTouches();
			
			appendClickPriority = 1000;

			onDrag.connect("_INTERNAL_SCROLL", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) mutable {
				if(contentView){
					if (!isDragging && a_clickable->dragTime() <= cancelTimeThreshold) {
						if (a_clickable->totalDragDistance() > dragStartThreshold) {
							isDragging = true;
							auto buttons = contentView->componentsInChildren<MV::Scene::Clickable>(false, false);
							MV::visit(buttons, [&](const MV::Scene::SafeComponent<MV::Scene::Clickable> &a_button) {
								a_button->cancelPress();
							});
						} else if (a_clickable->dragTime() > cancelTimeThreshold) {
							cancelPress();
							return;
						}
					}
					if (isDragging) {
						shiftContentByDelta(deltaPosition);
					}
				}
			});
		}

		namespace {
			// The box around a box of one node's space, in another's.
			BoxAABB<> boxInSpaceOf(const std::shared_ptr<Node>& a_space, const std::shared_ptr<Node>& a_from, const BoxAABB<>& a_box) {
				const Point<> corners[] {
					a_box.minPoint,
					Point<>(a_box.maxPoint.x, a_box.minPoint.y, a_box.minPoint.z),
					a_box.maxPoint,
					Point<>(a_box.minPoint.x, a_box.maxPoint.y, a_box.maxPoint.z) };
				BoxAABB<> result;
				bool first = true;
				for (auto&& corner : corners) {
					const Point<> placed = a_space->localFromWorld(a_from->worldFromLocal(corner));
					if (first) {
						result = BoxAABB<>(placed, placed);
						first = false;
					} else {
						result.expandWith(placed);
					}
				}
				return result;
			}

			// Content that fits the view stays where it was put. Content longer than the view covers it from end to end.
			PointPrecision shiftKeptCovering(PointPrecision a_shift, PointPrecision a_contentLow, PointPrecision a_contentHigh, PointPrecision a_viewLow, PointPrecision a_viewHigh) {
				if (a_contentHigh - a_contentLow <= a_viewHigh - a_viewLow) {
					return 0.0f;
				}
				return std::clamp(a_shift, a_viewHigh - a_contentHigh, a_viewLow - a_contentLow);
			}

			PointPrecision shiftToMiddle(PointPrecision a_low, PointPrecision a_high, PointPrecision a_viewLow, PointPrecision a_viewHigh) {
				return (a_viewLow + a_viewHigh - a_low - a_high) * 0.5f;
			}
		}

		void Scroller::shiftContentByDelta(const MV::Point<int> & deltaPosition) {
			if (deltaPosition.x == 0 && deltaPosition.y == 0) {
				return;
			}
			auto self = owner();
			shiftContent(self->localFromScreen(deltaPosition) - self->localFromScreen(MV::Point<int>()));
		}

		void Scroller::shiftContent(Point<> a_delta) {
			auto self = owner();
			std::shared_ptr<Node> content = contentView;
			auto contentBox = boxInSpaceOf(self, content, content->bounds());
			auto view = bounds();
			a_delta.x = horizontalAllowed ? shiftKeptCovering(a_delta.x, contentBox.minPoint.x, contentBox.maxPoint.x, view.minPoint.x, view.maxPoint.x) : 0.0f;
			a_delta.y = verticalAllowed ? shiftKeptCovering(a_delta.y, contentBox.minPoint.y, contentBox.maxPoint.y, view.minPoint.y, view.maxPoint.y) : 0.0f;
			a_delta.z = 0.0f;
			if (a_delta.x == 0.0f && a_delta.y == 0.0f) {
				return;
			}
			auto contentParent = content->parent();
			if (contentParent && contentParent != self) {
				a_delta = contentParent->localFromWorld(self->worldFromLocal(a_delta)) - contentParent->localFromWorld(self->worldFromLocal(Point<>()));
			}
			content->translate(a_delta);
		}

		void Scroller::focusOnChild(const std::shared_ptr<Node>& a_child) {
			std::shared_ptr<Node> content = contentView;
			if (!content || !a_child || !ownerIsAlive()) {
				return;
			}
			bool underContent = false;
			for (auto node = a_child; node && !underContent; node = node->parent()) {
				underContent = node == content;
			}
			auto childBox = a_child->bounds();
			if (!underContent || childBox.empty()) {
				return;
			}
			childBox = boxInSpaceOf(owner(), a_child, childBox);
			auto view = bounds();
			shiftContent(Point<>(
				shiftToMiddle(childBox.minPoint.x, childBox.maxPoint.x, view.minPoint.x, view.maxPoint.x),
				shiftToMiddle(childBox.minPoint.y, childBox.maxPoint.y, view.minPoint.y, view.maxPoint.y)));
		}

		std::shared_ptr<Scroller> Scroller::content(const std::shared_ptr<Node> &a_content) {
			contentView = a_content;
			shiftContentByDelta({0, 0});
			return std::static_pointer_cast<Scroller>(shared_from_this());
		}


		std::shared_ptr<Component> Scroller::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Clickable::cloneHelper(a_clone);
			auto scrollerClone = std::static_pointer_cast<Scroller>(a_clone);
			if (contentView) {
				auto foundHandle = scrollerClone->owner()->get(contentView->id());
				scrollerClone->content(foundHandle);
			}
			return a_clone;
		}

		void Scroller::updateImplementation(double a_delta) {
			Clickable::updateImplementation(a_delta);
// 			if (contentView && outOfBounds) {
// 				auto localContentPosition = owner()->localFromWorld(contentView->worldPosition());
// 				auto localContentBounds = owner()->localFromWorld(contentView->worldBounds());
// 				auto ourBounds = bounds();
// 
// 				if (localContentBounds.minPoint.x < ourBounds.minPoint.x) {
// 					localContentPosition.x += (ourBounds.minPoint.x - localContentBounds.minPoint.x);
// 				}
// 				if (localContentBounds.minPoint.y > ourBounds.minPoint.y) {
// 					localContentPosition.y -= ourBounds.minPoint
// 				}
// 			}
		}

	}
}
