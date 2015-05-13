#include "editorFactories.h"
#include "editorDefines.h"
#include "editorPanels.h"

#include "Render/package.h"

void colorTopAndBottom(const std::shared_ptr<MV::Scene::Sprite> &a_rect, const MV::Color &a_top, const MV::Color &a_bot){
	a_rect->corners(a_top, a_top, a_bot, a_bot);
}

std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier /*= MV::DEFAULT_ID*/) {
	static long buttonId = 0;
	auto button = a_parent->make(MV::toString(a_text) + boost::lexical_cast<std::string>(buttonId++))->attach<MV::Scene::Button>(a_mouse)->size(a_size);	
	std::vector<MV::Color> boxActiveColors = { { InterfaceColors::BUTTON_TOP_ACTIVE },{ InterfaceColors::BUTTON_BOTTOM_ACTIVE },{ InterfaceColors::BUTTON_BOTTOM_ACTIVE },{ InterfaceColors::BUTTON_TOP_ACTIVE } };
	std::vector<MV::Color> boxIdleColors = { { InterfaceColors::BUTTON_TOP_IDLE },{ InterfaceColors::BUTTON_BOTTOM_IDLE },{ InterfaceColors::BUTTON_BOTTOM_IDLE },{ InterfaceColors::BUTTON_TOP_IDLE } };

	auto activeScene = button->owner()->make("active")->attach<MV::Scene::Sprite>()->size(a_size)->colors(boxActiveColors)->owner();

	auto idleScene = button->owner()->make("idle")->attach<MV::Scene::Sprite>()->size(a_size)->colors(boxIdleColors)->owner();

	auto activeBox = activeScene->attach<MV::Scene::Text>(a_library, a_size, a_fontIdentifier);
	activeBox->justification(MV::TextJustification::CENTER);
	activeBox->wrapping(MV::TextWrapMethod::HARD)->minimumLineHeight(a_size.height)->text(a_text);
	
	auto idleBox = idleScene->attach<MV::Scene::Text>(a_library, a_size, a_fontIdentifier);
	idleBox->justification(MV::TextJustification::CENTER);
	idleBox->wrapping(MV::TextWrapMethod::HARD)->minimumLineHeight(a_size.height)->text(a_text);

	button->activeNode(activeScene);
	button->idleNode(idleScene);

	return button;
}

std::shared_ptr<MV::Scene::Button> makeSceneButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier /*= MV::DEFAULT_ID*/) {
	static long buttonId = 0;
	std::vector<MV::Color> boxActiveColors = { { InterfaceColors::BUTTON_TOP_ACTIVE },{ InterfaceColors::BUTTON_BOTTOM_ACTIVE },{ InterfaceColors::BUTTON_BOTTOM_ACTIVE },{ InterfaceColors::BUTTON_TOP_ACTIVE } };
	std::vector<MV::Color> boxIdleColors = { { InterfaceColors::BUTTON_TOP_IDLE },{ InterfaceColors::BUTTON_BOTTOM_IDLE },{ InterfaceColors::BUTTON_BOTTOM_IDLE },{ InterfaceColors::BUTTON_TOP_IDLE } };

	auto button = a_parent->make(MV::toString(a_text) + std::to_string(buttonId++))->attach<MV::Scene::Button>(a_mouse)->size(a_size);
	auto activeScene = button->owner()->make("active")->attach<MV::Scene::Sprite>()->size(a_size)->colors(boxActiveColors)->owner();

	auto idleScene = button->owner()->make("idle")->attach<MV::Scene::Sprite>()->size(a_size)->colors(boxIdleColors)->owner();

	auto activeBox = activeScene->attach<MV::Scene::Text>(a_library, a_size - MV::size(10.0f, 0.0f), a_fontIdentifier)->
		justification(MV::TextJustification::LEFT)->
		wrapping(MV::TextWrapMethod::NONE)->minimumLineHeight(a_size.height)->text(a_text);

	auto idleBox = idleScene->attach<MV::Scene::Text>(a_library, a_size - MV::size(10.0f, 0.0f), a_fontIdentifier)->
		justification(MV::TextJustification::LEFT)->
		wrapping(MV::TextWrapMethod::NONE)->minimumLineHeight(a_size.height)->text(a_text);

	button->activeNode(activeScene);
	button->idleNode(idleScene);

	return button;
}

std::shared_ptr<MV::Scene::Text> makeInputField(EditorPanel *a_panel, MV::MouseState &a_mouse, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_textLibrary, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents) {
	auto box = a_parent->make(a_name)->
		attach<MV::Scene::Sprite>()->size(a_size)->colors({ {InterfaceColors::TEXTBOX_TOP}, { InterfaceColors::TEXTBOX_BOTTOM }, { InterfaceColors::TEXTBOX_BOTTOM }, { InterfaceColors::TEXTBOX_TOP } })->owner();
	auto text = box->attach<MV::Scene::Text>(a_textLibrary, a_size, "small")->justification(MV::TextJustification::CENTER)->wrapping(MV::TextWrapMethod::NONE)->minimumLineHeight(a_size.height)->text(a_startContents);
	auto clickable = box->attach<MV::Scene::Clickable>(a_mouse)->size(a_size);
	std::weak_ptr<MV::Scene::Text> weakText = text;
	clickable->onAccept.connect("register", [=](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		if(!weakText.expired()){
			a_panel->activate(weakText.lock());
		} else{
			std::cerr << "Error: onAccept failure in box." << std::endl;
		}
	});
	text->onEnter.connect("unregister", [=](std::shared_ptr<MV::Scene::Text> a_text){
		a_panel->activate(nullptr);
	});
	return text;
}

std::shared_ptr<MV::Scene::Text> makeLabel(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_textLibrary, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents){
	auto box = a_parent->make(a_name)->attach<MV::Scene::Sprite>()->size(a_size)->colors({ { InterfaceColors::LABEL_TOP },{ InterfaceColors::LABEL_BOTTOM },{ InterfaceColors::LABEL_BOTTOM },{ InterfaceColors::LABEL_TOP } })->owner();
	auto text = box->attach<MV::Scene::Text>(a_textLibrary, a_size, "small")->justification(MV::TextJustification::CENTER)->minimumLineHeight(a_size.height)->wrapping(MV::TextWrapMethod::NONE)->text(a_startContents);

	return text;
}

std::shared_ptr<MV::Scene::Slider> makeSlider(MV::MouseState &a_mouse, const std::shared_ptr<MV::Scene::Node> &a_parent, const std::function <void(std::shared_ptr<MV::Scene::Slider>)> &a_method, float a_startPercent){
	auto slider = a_parent->make(MV::guid("slider_"))->attach<MV::Scene::Slider>(a_mouse)->size(MV::Size<>(110.0f, 10.0f))->percent(a_startPercent);
	slider->color({.25f, .25f, .25f, 1.0f});
	slider->handle(MV::Scene::Node::make(slider->owner()->renderer(), "handle")->attach<MV::Scene::Sprite>()->size({ 10.0f, 10.0f })->owner());
	slider->onPercentChange.connect("action", a_method);
	slider->percent(a_startPercent);
	return slider;
}

std::shared_ptr<MV::Scene::Node> makeSlider(MV::Draw2D &a_renderer, MV::MouseState &a_mouse, const std::function <void(std::shared_ptr<MV::Scene::Slider>)> &a_method, float a_startPercent){
	auto sliderNode = MV::Scene::Node::make(a_renderer, MV::guid("slider_"));
	auto slider = sliderNode->attach<MV::Scene::Slider>(a_mouse)->size(MV::Size<>(110.0f, 10.0f))->percent(a_startPercent);
	slider->color({ .25f, .25f, .25f, 1.0f });
	slider->handle(MV::Scene::Node::make(slider->owner()->renderer(), "handle")->attach<MV::Scene::Sprite>()->size({ 10.0f, 10.0f })->owner());
	slider->onPercentChange.connect("action", a_method);
	slider->percent(a_startPercent);
	return sliderNode;
}

std::shared_ptr<MV::Scene::Node> makeSlider(MV::Draw2D &a_renderer, MV::MouseState &a_mouse, float a_startPercent){
	auto sliderNode = MV::Scene::Node::make(a_renderer, MV::guid("slider_"));
	auto slider = sliderNode->attach<MV::Scene::Slider>(a_mouse)->size(MV::Size<>(110.0f, 10.0f))->percent(a_startPercent);
	slider->color({ .25f, .25f, .25f, 1.0f });
	slider->handle(MV::Scene::Node::make(slider->owner()->renderer(), "handle")->attach<MV::Scene::Sprite>()->size({ 10.0f, 10.0f })->owner());
	slider->percent(a_startPercent);
	return sliderNode;
}

// std::shared_ptr<MV::Scene::Node> makeDraggableBox(const std::string &a_id, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::Size<> a_boxSize, MV::MouseState &a_mouse) {
// 	auto box = a_parent->make(a_id);
// 
// 	float headerSize = 20.0f;
// 	auto boxContents = box->make("contents")->position({ 0.0f, headerSize });
// 	std::weak_ptr<MV::Scene::Node> weakBoxContents = boxContents;
// 
// 	auto clippedView = box->attach<MV::Scene::Clipped>();
// 	box->onLocalBoundsChange.connect("boundsChange", [a_boxSize, &a_mouse, weakBoxContents](const std::shared_ptr<MV::Scene::Node> &a_self){
// 		auto clippedView = a_self->component<MV::Scene::Clipped>();
// 		auto selfSize = a_self->bounds().size();
// 		if (selfSize.height > 500.0f) {
// 			auto scrollbarNode = a_self->makeOrGet("scrollbar")->position({a_boxSize.width, 0.0f});
// 			auto scrollBarComponent = a_self->component<MV::Scene::Slider>(true, false);
// 			if (!scrollBarComponent) {
// 				scrollBarComponent = a_self->attach<MV::Scene::Slider>(a_mouse)->bounds(MV::size(10.0f, 500.0f));
// 				std::weak_ptr<MV::Scene::Clipped> weakClippedView = clippedView;
// 				scrollBarComponent->onPercentChange.connect("scroll", [weakBoxContents, weakClippedView](std::shared_ptr<MV::Scene::Slider> a_slider){
// 					float totalSize = weakBoxContents.lock()->bounds().size().height;
// 					weakClippedView.lock()->captureOffset({0.0f, a_slider->percent() * (totalSize - 500.0f) });
// 				});
// 			}
// 		} else {
// 			auto scrollbarNode = a_self->get("scrollbar", false);
// 			if (scrollbarNode) {
// 				scrollbarNode->removeFromParent();
// 			}
// 		}
// 		clippedView->bounds(selfSize);
// 		selfSize.height = std::min(selfSize.height, 500.0f);
// 		clippedView->captureBounds(selfSize);
// 	});
// 
// 	auto boxHeader = box->attach<MV::Scene::Clickable>(a_mouse)->size({ a_boxSize.width, headerSize })->color({ BOX_HEADER })->show();
// 
// 	boxHeader->onDrag.connect("DragSignal", [](std::shared_ptr<MV::Scene::Clickable> a_boxHeader, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
// 		a_boxHeader->owner()->translate(MV::cast<MV::PointPrecision>(deltaPosition));
// 	});
// 
// 	return boxContents;
// }

std::shared_ptr<MV::Scene::Node> makeDraggableBox(const std::string &a_id, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::Size<> a_boxSize, MV::MouseState &a_mouse) {
	auto box = a_parent->make(a_id);

	MV::PointPrecision headerSize = 20.0f;
	auto boxContents = box->make("contents")->position({ 0.0f, headerSize });

	std::weak_ptr<MV::Scene::Node> weakBoxContents = boxContents;
	box->attach<MV::Scene::Clipped>();

	box->onChange.connect("changeResize", [headerSize, weakBoxContents, &a_mouse](const std::shared_ptr<MV::Scene::Node> &a_self){
		auto clippedView = a_self->component<MV::Scene::Clipped>();
		clippedView->blockClippedChildTaps(a_mouse);
		auto newBounds = weakBoxContents.lock()->bounds();
		auto sizeOfNode = newBounds.size() + MV::size(0.0f, headerSize);
		if (sizeOfNode.height > 520.0f) {
			clippedView->bounds({ {0.0f, 0.0f}, MV::size(sizeOfNode.width, 520.0f) });
			auto scrollBarComponent = a_self->component<MV::Scene::Clickable>("Scrollbar", false);
			if (!scrollBarComponent) {
				scrollBarComponent = a_self->attach<MV::Scene::Clickable>(a_mouse)->id("Scrollbar")->safe();
				scrollBarComponent->stopEatingTouches();
				auto indexList = scrollBarComponent->owner()->parentIndexList(scrollBarComponent->globalPriority());
				std::vector<size_t> appendPriority{ 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10 };
				indexList.insert(indexList.end(), appendPriority.begin(), appendPriority.end());
				scrollBarComponent->overridePriority(indexList);
				scrollBarComponent->color({ 1.0f, 1.0f, 1.0f, .5f });
				scrollBarComponent->show();
				scrollBarComponent->bounds({ { 0.0f, headerSize }, MV::size(sizeOfNode.width, 500.0f) });
				int dragThreshold = 10;
				scrollBarComponent->onDrag.connect("scroll", [=, &a_mouse](std::shared_ptr<MV::Scene::Clickable> a_clickable, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) mutable {
					if (dragThreshold > 0) {
						dragThreshold -= std::abs(deltaPosition.y);
						if (dragThreshold <= 0) {
							auto buttons = a_clickable->owner()->componentsInChildren<MV::Scene::Clickable>(false, false);
							for (auto&& button : buttons) {
								button->cancelPress();
							}
						}
					} else {
						auto boxLocation = weakBoxContents.lock()->position();
						boxLocation.y = MV::clamp(boxLocation.y + deltaPosition.y, 20.0f, -(sizeOfNode.height - 540.0f));
						weakBoxContents.lock()->position(boxLocation);
					}
				});
				scrollBarComponent->onRelease.connect("drop", [=](std::shared_ptr<MV::Scene::Clickable> a_clickable, const MV::Point<float> &a_velocity) {
					if (!weakBoxContents.expired()) {
						MV::Task* existing = weakBoxContents.lock()->task().get("movingSCROLL", false);
						if (existing) {
							existing->cancel();
						}
						auto velocity = MV::point(0.0f, a_velocity.y, 0.0f);
						velocity.y = MV::clamp(velocity.y, -1000.0f, 1000.0f);
						weakBoxContents.lock()->task().also("movingSCROLL", [=](const MV::Task&, double a_dt) mutable {
							bool done = false;
							if (!weakBoxContents.expired()) {
								velocity.y = velocity.y - (velocity.y / (1.75f / static_cast<MV::PointPrecision>(a_dt)));
								done = std::abs(velocity.y) < 1.0f;
							}
							auto boxLocation = weakBoxContents.lock()->position();
							boxLocation.y = MV::clamp(boxLocation.y + velocity.y, 20.0f, -(sizeOfNode.height - 540.0f));
							if (boxLocation.y == 20.0f || boxLocation.y == -(sizeOfNode.height - 540.0f)) {
								velocity.y *= -1.0f;
							}
							weakBoxContents.lock()->position(boxLocation);
							return done;
						});
					}
				});
			}
		} else {
			clippedView->bounds({ { 0.0f, 0.0f }, sizeOfNode });
			weakBoxContents.lock()->position({ 0.0f, headerSize });
			a_self->detach("Scrollbar", false);
		}
	});

	auto boxHeader = box->attach<MV::Scene::Clickable>(a_mouse);
	auto indexList = box->parentIndexList(boxHeader->globalPriority());
	indexList[1] += 10;
	boxHeader->overridePriority(indexList);
	boxHeader->size({ a_boxSize.width, headerSize });
	boxHeader->color({ BOX_HEADER });
	boxHeader->show();

	boxHeader->onDrag.connect("DragSignal", [](std::shared_ptr<MV::Scene::Clickable> a_boxHeader, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		a_boxHeader->owner()->translate(MV::cast<MV::PointPrecision>(deltaPosition));
	});

	return boxContents;
}


std::shared_ptr<MV::Scene::Node> makeColorPicker(MV::Draw2D &a_renderer, MV::MouseState &a_mouse, const MV::Color &a_startColor) {
	auto sliderNode = MV::Scene::Node::make(a_renderer, MV::guid("colorpicker_"));
}