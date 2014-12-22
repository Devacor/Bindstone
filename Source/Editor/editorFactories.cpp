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

	auto button = a_parent->make(MV::toString(a_text) + boost::lexical_cast<std::string>(buttonId++))->attach<MV::Scene::Button>(a_mouse)->size(a_size);
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

std::shared_ptr<MV::Scene::Text> makeLabel(EditorPanel *a_panel, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_textLibrary, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents){
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

std::shared_ptr<MV::Scene::Node> makeDraggableBox(const std::string &a_id, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::Size<> a_boxSize, MV::MouseState &a_mouse) {
	auto box = a_parent->make(a_id);

	float headerSize = 20.0f;
	auto boxContents = box->make("contents")->position({ 0.0f, headerSize });

	auto boxHeader = box->attach<MV::Scene::Clickable>(a_mouse)->size({ a_boxSize.width, headerSize })->color({ BOX_HEADER })->show();

	boxHeader->onDrag.connect("DragSignal", [](std::shared_ptr<MV::Scene::Clickable> a_boxHeader, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		a_boxHeader->owner()->translate(MV::cast<MV::PointPrecision>(deltaPosition));
	});

	return boxContents;
}
