#include "scroller.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

#include "text.h"

CEREAL_REGISTER_TYPE(MV::Scene::Scroller);

namespace MV {
	namespace Scene {

		Scroller::Scroller(const std::weak_ptr<Node> &a_owner, MouseState &a_mouse) :
			Clickable(a_owner, a_mouse) {
			stopEatingTouches();
			
			appendClickPriority = 1000;

			onDrag.connect("_INTERNAL_SCROLL", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) mutable {
				if(contentView){
					if (!isDragging) {
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

		void Scroller::shiftContentByDelta(const MV::Point<int> & deltaPosition) {
			auto localContentPosition = owner()->localFromWorld(contentView->worldPosition());
			auto localContentBounds = owner()->localFromWorld(contentView->worldBounds());
			auto contentBoundsToPositionDelta = localContentPosition - localContentBounds.minPoint;
			auto localDelta = owner()->localFromScreen(deltaPosition) - owner()->localFromScreen(MV::Point<int>());
			if (!horizontalAllowed) {
				localDelta.x = 0;
			}
			if (!verticalAllowed) {
				localDelta.y = 0;
			}

			localContentPosition += localDelta;
			localContentBounds += localDelta;
			auto ourBounds = bounds();
			
			if (horizontalAllowed) {
				if (localContentBounds.width() < ourBounds.width()) {
					if (localContentBounds.minPoint.x < ourBounds.minPoint.x) {
						localContentPosition.x = ourBounds.minPoint.x + contentBoundsToPositionDelta.x;
					} else if (localContentBounds.maxPoint.x > ourBounds.maxPoint.x) {
						localContentPosition.x = ourBounds.maxPoint.x - localContentBounds.width() + contentBoundsToPositionDelta.x;
					}
				} else {
					if (localContentBounds.minPoint.x > ourBounds.minPoint.x) {
						localContentPosition.x = ourBounds.minPoint.x + contentBoundsToPositionDelta.x;
					} else if (localContentBounds.maxPoint.x < ourBounds.maxPoint.x) {
						localContentPosition.x = ourBounds.maxPoint.x - localContentBounds.width() + contentBoundsToPositionDelta.x;
					}
				}
			}
			if (verticalAllowed) {
				if (localContentBounds.height() < ourBounds.height()) {
					if (localContentBounds.minPoint.y < ourBounds.minPoint.y) {
						localContentPosition.y = ourBounds.minPoint.y + contentBoundsToPositionDelta.y;
					} else if (localContentBounds.maxPoint.y > ourBounds.maxPoint.y) {
						localContentPosition.y = ourBounds.maxPoint.y - localContentBounds.height() + contentBoundsToPositionDelta.y;
					}
				} else {
					if (localContentBounds.minPoint.y > ourBounds.minPoint.y) {
						localContentPosition.y = ourBounds.minPoint.y + contentBoundsToPositionDelta.y;
					} else if (localContentBounds.maxPoint.y < ourBounds.maxPoint.y) {
						localContentPosition.y = ourBounds.maxPoint.y - localContentBounds.height() + contentBoundsToPositionDelta.y;
					}
				}
			}

			contentView->worldPosition(owner()->worldFromLocal(localContentPosition));
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
