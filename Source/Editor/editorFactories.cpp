#include "editorFactories.h"
#include "editorDefines.h"
#include "editorPanels.h"

#include "Render/package.h"

void colorTopAndBottom(const std::shared_ptr<MV::Scene::Rectangle> &a_rect, const MV::Color &a_top, const MV::Color &a_bot){
	a_rect->applyToCorners(a_top, a_top, a_bot, a_bot);
}

std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier){
	static long buttonId = 0;
	auto button = a_parent->make<MV::Scene::Button>(MV::wideToString(a_text) + boost::lexical_cast<std::string>(buttonId++), &a_mouse, a_size);
	auto activeScene = MV::Scene::Rectangle::make(a_parent->getRenderer(), a_size);
	colorTopAndBottom(activeScene, {InterfaceColors::BUTTON_TOP_ACTIVE}, {InterfaceColors::BUTTON_BOTTOM_ACTIVE});

	auto idleScene = MV::Scene::Rectangle::make(a_parent->getRenderer(), a_size);
	colorTopAndBottom(idleScene, {InterfaceColors::BUTTON_TOP_IDLE}, {InterfaceColors::BUTTON_BOTTOM_IDLE});

	auto activeBox = activeScene->make<MV::Scene::Text>("TextBox", &a_library, a_size, a_fontIdentifier);
	activeBox->justification(MV::TextJustification::CENTER);
	activeBox->wrapping(MV::TextWrapMethod::HARD)->setMinimumLineHeight(a_size.height)->text(a_text);
	
	auto idleBox = idleScene->make<MV::Scene::Text>("TextBox", &a_library, a_size, a_fontIdentifier);
	idleBox->justification(MV::TextJustification::CENTER);
	idleBox->wrapping(MV::TextWrapMethod::HARD)->setMinimumLineHeight(a_size.height)->text(a_text);

	button->activeScene(activeScene);
	button->idleScene(idleScene);

	return button;
}

std::shared_ptr<MV::Scene::Text> makeInputField(EditorPanel *a_panel, MV::MouseState &a_mouse, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_textLibrary, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents){
	auto box = a_parent->make<MV::Scene::Text>(a_name, &a_textLibrary, a_size, "small")->justification(MV::TextJustification::CENTER)->text(a_startContents);
	auto background = box->make<MV::Scene::Rectangle>("Background", a_size)->depth(-1);
	auto clickable = box->make<MV::Scene::Clickable>("Clickable", &a_mouse, a_size);
	std::weak_ptr<MV::Scene::Text> weakBox = box;
	clickable->onAccept.connect("register", [=](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		if(!weakBox.expired()){
			a_panel->activate(weakBox.lock());
		} else{
			std::cerr << "Error: onAccept failure in box." << std::endl;
		}
	});
	box->onEnter.connect("unregister", [=](std::shared_ptr<MV::Scene::Text> a_text){
		a_panel->activate(nullptr);
	});
	box->setMinimumLineHeight(a_size.height);
	box->makeSingleLine();
	colorTopAndBottom(background, {InterfaceColors::TEXTBOX_TOP}, {InterfaceColors::TEXTBOX_BOTTOM});
	return box;
}

std::shared_ptr<MV::Scene::Text> makeLabel(EditorPanel *a_panel, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_textLibrary, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents){
	auto box = a_parent->make<MV::Scene::Text>(a_name, &a_textLibrary, a_size, "small")->justification(MV::TextJustification::CENTER)->text(a_startContents);
	auto background = box->make<MV::Scene::Rectangle>("Background", a_size)->depth(-1);
	box->setMinimumLineHeight(a_size.height);
	box->makeSingleLine();

	colorTopAndBottom(background, {InterfaceColors::LABEL_TOP}, {InterfaceColors::LABEL_BOTTOM});
	return box;
}

std::shared_ptr<MV::Scene::Slider> makeSlider(MV::MouseState &a_mouse, const std::shared_ptr<MV::Scene::Node> &a_parent, const std::function <void(std::shared_ptr<MV::Scene::Slider>)> &a_method, float a_startPercent){
	auto slider = a_parent->make<MV::Scene::Slider>(&a_mouse, MV::Size<>(110.0f, 10.0f))->percent(a_startPercent);
	slider->area()->color({.25f, .25f, .25f, 1.0f});
	slider->onPercentChange.connect("action", a_method);
	slider->percent(a_startPercent);
	return slider;
}

std::shared_ptr<MV::Scene::Slider> makeSlider(MV::Draw2D &a_renderer, MV::MouseState &a_mouse, const std::function <void(std::shared_ptr<MV::Scene::Slider>)> &a_method, float a_startPercent){
	auto slider = MV::Scene::Slider::make(&a_renderer, &a_mouse, MV::Size<>(110.0f, 10.0f))->percent(a_startPercent);
	slider->area()->color({.25f, .25f, .25f, 1.0f});
	slider->onPercentChange.connect("action", a_method);
	slider->percent(a_startPercent);
	return slider;
}

std::shared_ptr<MV::Scene::Slider> makeSlider(MV::Draw2D &a_renderer, MV::MouseState &a_mouse, float a_startPercent){
	auto slider = MV::Scene::Slider::make(&a_renderer, &a_mouse, MV::Size<>(110.0f, 10.0f))->percent(a_startPercent);
	slider->area()->color({.25f, .25f, .25f, 1.0f});
	slider->percent(a_startPercent);
	return slider;
}

std::shared_ptr<MV::Scene::Node> makeDraggableBox(const std::string &a_id, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::Size<> a_boxSize, MV::MouseState &a_mouse) {
	auto box = a_parent->make<MV::Scene::Node>(a_id);

	float headerSize = 20.0f;
	auto boxContents = box->make<MV::Scene::Node>()->position({0.0f, headerSize});

	auto boxHeader = box->make<MV::Scene::Clickable>("ContextMenuHandle", &a_mouse, MV::size(a_boxSize.width, headerSize))->color({BOX_HEADER});

	boxHeader->onDrag.connect("DragSignal", [](std::shared_ptr<MV::Scene::Clickable> boxHeader, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition){
		boxHeader->parent()->translate(MV::cast<MV::PointPrecision>(deltaPosition));
	});

	return boxContents;
}
