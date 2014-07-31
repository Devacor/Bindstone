#include "slider.h"
#include <memory>
#include "cereal/archives/json.hpp"
CEREAL_REGISTER_TYPE(MV::Scene::Slider);

namespace MV {
	namespace Scene {

		/*************************\
		| --------Slider--------- |
		\*************************/
		std::shared_ptr<Slider> Slider::make(Draw2D* a_renderer, MouseState *a_mouse){
			auto slider = std::shared_ptr<Slider>(new Slider(a_renderer));
			slider->dragArea = Clickable::make(a_renderer, a_mouse);
			slider->dragArea->parent(slider.get());
			slider->dragHandle->parent(slider.get());
			a_renderer->registerShader(slider);

			slider->initializeDragArea();
			return slider;
		}

		std::shared_ptr<Slider> Slider::make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, bool a_center){
			auto slider = std::shared_ptr<Slider>(new Slider(a_renderer));
			slider->dragArea = Clickable::make(a_renderer, a_mouse, a_size, a_center);
			slider->dragArea->parent(slider.get());
			slider->dragHandle->parent(slider.get());
			a_renderer->registerShader(slider);

			slider->initializeDragArea();
			return slider;
		}

		std::shared_ptr<Slider> Slider::make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, const Point<>& a_centerPoint){
			auto slider = std::shared_ptr<Slider>(new Slider(a_renderer));
			slider->dragArea = Clickable::make(a_renderer, a_mouse, a_size, a_centerPoint);
			slider->dragArea->parent(slider.get());
			slider->dragHandle->parent(slider.get());
			a_renderer->registerShader(slider);

			slider->initializeDragArea();
			return slider;
		}
		std::shared_ptr<Slider> Slider::make(Draw2D* a_renderer, MouseState *a_mouse, const BoxAABB &a_boxAABB){
			auto slider = std::shared_ptr<Slider>(new Slider(a_renderer));
			slider->dragArea = Clickable::make(a_renderer, a_mouse, a_boxAABB);
			slider->dragArea->parent(slider.get());
			slider->dragHandle->parent(slider.get());
			a_renderer->registerShader(slider);

			slider->initializeDragArea();
			return slider;
		}

		void Slider::initializeDragArea(){
			dragArea->onPress.connect("dragSlider", [&](std::shared_ptr<MV::Scene::Clickable> a_dragHandle){
				updateDragPercentForMousePosition(a_dragHandle->getMouse()->position());
			});
			dragArea->onDrag.connect("dragSlider", [&](std::shared_ptr<MV::Scene::Clickable> a_dragHandle, const MV::Point<int> &a_startPosition, const MV::Point<int> &a_deltaPosition){
				updateDragPercentForMousePosition(a_dragHandle->getMouse()->position());
			});
			dragHandle->MessageHandler<VisualChange>::customHandleBegin.connect("resized", [&](std::shared_ptr<VisualChange> a_message){
				if(a_message->changeShape){
					updateHandlePosition();
				}
			});
			auto areaAabb = dragArea->localAABB();
			dragHandle->position(areaAabb.minPoint);
			if(areaAabb.width() > areaAabb.height()){
				dragHandle->size({areaAabb.height(), areaAabb.height()});
			} else{
				dragHandle->size({areaAabb.width(), areaAabb.width()});
			}
		}

		void Slider::drawImplementation(){
			dragHandle->draw();
		}

		BoxAABB Slider::worldAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return dragArea->worldAABBImplementation(a_includeChildren, a_nestedCall).expandWith(
				dragHandle->worldAABBImplementation(a_includeChildren, a_nestedCall)
			);
		}
		BoxAABB Slider::screenAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return dragArea->screenAABBImplementation(a_includeChildren, a_nestedCall).expandWith(
				dragHandle->screenAABBImplementation(a_includeChildren, a_nestedCall)
			);
		}
		BoxAABB Slider::localAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return dragArea->localAABBImplementation(a_includeChildren, a_nestedCall).expandWith(
				dragHandle->localAABBImplementation(a_includeChildren, a_nestedCall)
			);
		}
		BoxAABB Slider::basicAABBImplementation() const{
			return dragArea->basicAABBImplementation().expandWith(
				dragHandle->basicAABBImplementation()
			);
		}

		void Slider::updateHandlePosition() {
			if(!inUpdateHandlePosition){
				inUpdateHandlePosition = true;
				auto areaAabb = dragArea->localAABB();
				areaAabb.minPoint -= position();
				areaAabb.maxPoint -= position();
				auto handleAabb = dragHandle->localAABB();
				
				if(areaAabb.width() > areaAabb.height()){
					dragHandle->position({mix(areaAabb.minPoint.x, areaAabb.minPoint.x + (areaAabb.width() - handleAabb.width()), dragPercent), mix(areaAabb.minPoint.y, areaAabb.minPoint.y + (areaAabb.height() - handleAabb.height()), .5f)});
				} else{
					dragHandle->position({mix(areaAabb.minPoint.x, areaAabb.minPoint.x + (areaAabb.width() - handleAabb.width()), .5f), mix(areaAabb.minPoint.y, areaAabb.minPoint.y + (areaAabb.height() - handleAabb.height()), dragPercent)});
				}

				inUpdateHandlePosition = false;
			}
		}

		void Slider::updateDragPercentForMousePosition(const Point<int> &a_screenPoint) {
			auto aabb = dragArea->localAABB();
			auto relativePoint = dragArea->localFromScreen(a_screenPoint) - aabb.minPoint;

			auto previousPercent = dragPercent;

			if(aabb.width() > aabb.height()){
				dragPercent = std::min(std::max(relativePoint.x, 0.0f), aabb.width()) / aabb.width();
			} else {
				dragPercent = std::min(std::max(relativePoint.y, 0.0f), aabb.height()) / aabb.height();
			}

			if(previousPercent != dragPercent){
				updateHandlePosition();
				onPercentChangeSlot(std::static_pointer_cast<Slider>(shared_from_this()));
			}
		}

	}
}