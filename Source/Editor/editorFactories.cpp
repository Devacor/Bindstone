#include "editorFactories.h"
#include "editorDefines.h"
#include "componentPanels.h"

#include "Render/package.h"

void colorTopAndBottom(const std::shared_ptr<MV::Scene::Sprite> &a_rect, const MV::Color &a_top, const MV::Color &a_bot){
	a_rect->corners(a_top, a_top, a_bot, a_bot);
}

std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier /*= MV::DEFAULT_ID*/) {
	static long buttonId = 0;
	auto button = a_parent->make(MV::to_string(a_text) + std::to_string(buttonId++))->attach<MV::Scene::Button>(a_mouse)->bounds(a_size);	
	std::vector<MV::Color> boxActiveColors = { { InterfaceColors::BUTTON_TOP_ACTIVE },{ InterfaceColors::BUTTON_BOTTOM_ACTIVE },{ InterfaceColors::BUTTON_BOTTOM_ACTIVE },{ InterfaceColors::BUTTON_TOP_ACTIVE } };
	std::vector<MV::Color> boxIdleColors = { { InterfaceColors::BUTTON_TOP_IDLE },{ InterfaceColors::BUTTON_BOTTOM_IDLE },{ InterfaceColors::BUTTON_BOTTOM_IDLE },{ InterfaceColors::BUTTON_TOP_IDLE } };

	auto activeScene = button->owner()->make("active")->attach<MV::Scene::Sprite>()->bounds(a_size)->colors(boxActiveColors)->owner();

	auto idleScene = button->owner()->make("idle")->attach<MV::Scene::Sprite>()->bounds(a_size)->colors(boxIdleColors)->owner();

	auto activeBox = activeScene->attach<MV::Scene::Text>(a_library, a_fontIdentifier)->bounds({ MV::Point<>(), a_size });
	activeBox->justification(MV::TextJustification::CENTER);
	activeBox->wrapping(MV::TextWrapMethod::HARD)->minimumLineHeight(a_size.height)->text(a_text);
	
	auto idleBox = idleScene->attach<MV::Scene::Text>(a_library, a_fontIdentifier)->bounds({ MV::Point<>(), a_size });
	idleBox->justification(MV::TextJustification::CENTER);
	idleBox->wrapping(MV::TextWrapMethod::HARD)->minimumLineHeight(a_size.height)->text(a_text);

	button->activeNode(activeScene);
	button->idleNode(idleScene);

	return button;
}

std::shared_ptr<MV::Scene::Node> makeToggle(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::MouseState &a_mouse, const std::string &a_name, bool a_defaultValue, const std::function<void()> a_on, const std::function<void()> a_off, const MV::Size<> &a_size) {
	auto node = a_parent->make(a_name);
	auto toggle = node->make("toggle")->attach<MV::Scene::Sprite>()->bounds({ MV::toPoint(a_size / 4.0f), a_size / 2.0f })->color({ InterfaceColors::TOGGLE_CENTER })->owner();
	if (!a_defaultValue) {
		toggle->hide();
	}
	node->attach<MV::Scene::Clickable>(a_mouse)->bounds(a_size)->color({ InterfaceColors::TOGGLE_BACKGROUND })->show()->onAccept.connect("CLICKY", [=](auto self){
		if (toggle->visible()) {
			toggle->hide();
			a_off();
		} else {
			toggle->show();
			a_on();
		}
	});
	return node;
}

void applyColorToColorButton(std::shared_ptr<MV::Scene::Button> a_button, const MV::Color& a_color) {
	std::vector<MV::Color> boxIdleColors = { a_color, a_color / 2.0f, a_color / 2.0f, a_color };
	std::vector<MV::Color> boxActiveColors = { a_color / 2.0f, a_color / 4.0f, a_color / 4.0f, a_color / 2.0f };

	for (size_t i = 0; i < 4; ++i) {
		boxActiveColors[i].A = a_color.A;
		boxIdleColors[i].A = a_color.A;
	}

	a_button->idleNode()->component<MV::Scene::Sprite>()->colors(boxIdleColors);
	a_button->activeNode()->component<MV::Scene::Sprite>()->colors(boxActiveColors);
}

std::shared_ptr<MV::Scene::Button> makeColorButton(const std::shared_ptr<MV::Scene::Node> &a_parent, std::weak_ptr<MV::Scene::Node> a_colorPaletteParent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const MV::Size<> &a_size, const MV::Color &a_color, std::function<void (const MV::Color& a_newColor)> a_callback, const MV::UtfString &a_text) {
	auto button = makeColorButton(a_parent->renderer(), a_colorPaletteParent, a_library, a_mouse, a_size, a_color, a_callback, a_text);
	a_parent->add(button.owner());
	return button.self();
}

MV::Scene::SafeComponent<MV::Scene::Button> makeColorButton(MV::Draw2D &a_renderer, std::weak_ptr<MV::Scene::Node> a_colorPaletteParent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const MV::Size<> &a_size, const MV::Color &a_color, std::function<void(const MV::Color& a_newColor)> a_callback, const MV::UtfString &a_text) {
	auto buttonId = MV::guid("color_button");
	auto button = MV::Scene::Node::make(a_renderer, buttonId)->attach<MV::Scene::Button>(a_mouse)->bounds(a_size)->safe();

	auto activeScene = button->owner()->make("active")->attach<MV::Scene::Sprite>()->bounds(a_size)->owner();
	auto idleScene = button->owner()->make("idle")->attach<MV::Scene::Sprite>()->bounds(a_size)->owner();

	auto activeBox = activeScene->attach<MV::Scene::Text>(a_library)->bounds({ MV::Point<>(), a_size });
	activeBox->justification(MV::TextJustification::CENTER);
	activeBox->wrapping(MV::TextWrapMethod::HARD)->minimumLineHeight(a_size.height)->text(a_text);

	auto idleBox = idleScene->attach<MV::Scene::Text>(a_library)->bounds({ MV::Point<>(), a_size });
	idleBox->justification(MV::TextJustification::CENTER);
	idleBox->wrapping(MV::TextWrapMethod::HARD)->minimumLineHeight(a_size.height)->text(a_text);

	button->activeNode(activeScene);
	button->idleNode(idleScene);

	applyColorToColorButton(button.self(), a_color);

	std::weak_ptr<MV::Scene::Button> weakButton = button.self();
	button->onAccept.connect("OpenPicker", [weakButton, buttonId, a_color, a_colorPaletteParent, a_callback](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
		auto pickerNode = a_colorPaletteParent.lock()->makeOrGet(buttonId + "_picker");
		pickerNode->position({ 200.0f, 0.0f });
		auto pickerComponent = pickerNode->component<MV::Scene::Palette>(true, false);
		if (!pickerComponent) {
			pickerComponent = pickerNode->attach<MV::Scene::Palette>(a_clickable->mouse())->bounds(MV::size(256.0f, 256.0f));
		}

		pickerComponent->color(weakButton.lock()->idleNode()->component<MV::Scene::Sprite>()->point(0).color());
		pickerComponent->onColorChange.connect("change", [weakButton, a_color, a_callback](std::shared_ptr<MV::Scene::Palette> a_palette) {
			applyColorToColorButton(weakButton.lock(), a_palette->color());
			a_callback(a_palette->color());
		});
		pickerComponent->onSwatchClicked.connect("close", [](std::shared_ptr<MV::Scene::Palette> a_palette) {
			a_palette->owner()->removeFromParent();
		});
	});

	return button;
}

void renameButton(const MV::Scene::SafeComponent<MV::Scene::Button> &a_button, const MV::UtfString &a_text) {
	a_button->activeNode()->componentInChildren<MV::Scene::Text>()->text(a_text);
	a_button->idleNode()->componentInChildren<MV::Scene::Text>()->text(a_text);
}

std::shared_ptr<MV::Scene::Button> makeSceneButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier /*= MV::DEFAULT_ID*/) {
	static long buttonId = 0;
	std::vector<MV::Color> boxActiveColors = { { InterfaceColors::BUTTON_TOP_ACTIVE },{ InterfaceColors::BUTTON_BOTTOM_ACTIVE },{ InterfaceColors::BUTTON_BOTTOM_ACTIVE },{ InterfaceColors::BUTTON_TOP_ACTIVE } };
	std::vector<MV::Color> boxIdleColors = { { InterfaceColors::BUTTON_TOP_IDLE },{ InterfaceColors::BUTTON_BOTTOM_IDLE },{ InterfaceColors::BUTTON_BOTTOM_IDLE },{ InterfaceColors::BUTTON_TOP_IDLE } };

	auto button = a_parent->make(a_text + std::to_string(buttonId++))->attach<MV::Scene::Button>(a_mouse)->bounds(a_size);
	auto activeScene = button->owner()->make("active")->attach<MV::Scene::Sprite>()->bounds(a_size)->colors(boxActiveColors)->owner();

	auto idleScene = button->owner()->make("idle")->attach<MV::Scene::Sprite>()->bounds(a_size)->colors(boxIdleColors)->owner();

	auto activeBox = activeScene->attach<MV::Scene::Text>(a_library, a_fontIdentifier)->
		justification(MV::TextJustification::LEFT)->
		wrapping(MV::TextWrapMethod::NONE, a_size.width)->minimumLineHeight(a_size.height)->text(a_text);

	auto idleBox = idleScene->attach<MV::Scene::Text>(a_library, a_fontIdentifier)->
		justification(MV::TextJustification::LEFT)->
		wrapping(MV::TextWrapMethod::NONE, a_size.width)->minimumLineHeight(a_size.height)->text(a_text);

	button->activeNode(activeScene);
	button->idleNode(idleScene);

	return button;
}

std::shared_ptr<MV::Scene::Text> makeInputField(EditorPanel *a_panel, MV::MouseState &a_mouse, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_textLibrary, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents) {
	auto box = a_parent->make(a_name)->
		attach<MV::Scene::Sprite>()->bounds(a_size)->colors({ {InterfaceColors::TEXTBOX_TOP}, { InterfaceColors::TEXTBOX_BOTTOM }, { InterfaceColors::TEXTBOX_BOTTOM }, { InterfaceColors::TEXTBOX_TOP } })->owner();
	auto text = box->attach<MV::Scene::Text>(a_textLibrary, "small")->justification(MV::TextJustification::CENTER)->wrapping(MV::TextWrapMethod::NONE, a_size.width)->minimumLineHeight(a_size.height)->text(a_startContents);
	auto clickable = box->attach<MV::Scene::Clickable>(a_mouse)->bounds(a_size);
	std::weak_ptr<MV::Scene::Text> weakText = text;
	clickable->onAccept.connect("register", [=](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		if(!weakText.expired()){
			a_panel->activateText(weakText.lock());
		} else{
			std::cerr << "Error: onAccept failure in box." << std::endl;
		}
	});
	text->onEnter.connect("unregister", [=](std::shared_ptr<MV::Scene::Text> a_text){
		a_panel->deactivateText();
	});
	return text;
}

std::shared_ptr<MV::Scene::Text> makeLabel(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_textLibrary, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents){
	auto box = a_parent->make(a_name)->attach<MV::Scene::Sprite>()->bounds(a_size)->colors({ { InterfaceColors::LABEL_TOP },{ InterfaceColors::LABEL_BOTTOM },{ InterfaceColors::LABEL_BOTTOM },{ InterfaceColors::LABEL_TOP } })->owner();
	auto text = box->attach<MV::Scene::Text>(a_textLibrary, "small")->justification(MV::TextJustification::CENTER)->minimumLineHeight(a_size.height)->wrapping(MV::TextWrapMethod::NONE, a_size.width)->text(a_startContents);

	return text;
}

std::shared_ptr<MV::Scene::Slider> makeSlider(MV::MouseState &a_mouse, const std::shared_ptr<MV::Scene::Node> &a_parent, const std::function <void(std::shared_ptr<MV::Scene::Slider>)> &a_method, float a_startPercent){
	auto slider = a_parent->make(MV::guid("slider_"))->attach<MV::Scene::Slider>(a_mouse)->bounds(MV::Size<>(110.0f, 10.0f))->percent(a_startPercent);
	slider->color({.25f, .25f, .25f, 1.0f});
	slider->handle(MV::Scene::Node::make(slider->owner()->renderer(), "handle")->attach<MV::Scene::Sprite>()->bounds(MV::size(10.0f, 10.0f))->owner());
	slider->onPercentChange.connect("action", a_method);
	slider->percent(a_startPercent);
	return slider;
}

std::shared_ptr<MV::Scene::Node> makeSlider(MV::Draw2D &a_renderer, MV::MouseState &a_mouse, const std::function <void(std::shared_ptr<MV::Scene::Slider>)> &a_method, float a_startPercent){
	auto sliderNode = MV::Scene::Node::make(a_renderer, MV::guid("slider_"));
	auto slider = sliderNode->attach<MV::Scene::Slider>(a_mouse)->bounds(MV::size(110.0f, 10.0f))->percent(a_startPercent);
	slider->color({ .25f, .25f, .25f, 1.0f });
	slider->handle(MV::Scene::Node::make(slider->owner()->renderer(), "handle")->attach<MV::Scene::Sprite>()->bounds(MV::size(10.0f, 10.0f))->owner());
	slider->onPercentChange.connect("action", a_method);
	slider->percent(a_startPercent);
	return sliderNode;
}

std::shared_ptr<MV::Scene::Node> makeSlider(MV::Draw2D &a_renderer, MV::MouseState &a_mouse, float a_startPercent){
	auto sliderNode = MV::Scene::Node::make(a_renderer, MV::guid("slider_"));
	auto slider = sliderNode->attach<MV::Scene::Slider>(a_mouse)->bounds(MV::size(110.0f, 10.0f))->percent(a_startPercent);
	slider->color({ .25f, .25f, .25f, 1.0f });
	slider->handle(MV::Scene::Node::make(slider->owner()->renderer(), "handle")->attach<MV::Scene::Sprite>()->bounds(MV::size(10.0f, 10.0f))->owner());
	slider->percent(a_startPercent);
	return sliderNode;
}

std::shared_ptr<MV::Scene::Node> makeDraggableBox(const std::string &a_id, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::Size<> a_boxSize, MV::MouseState &a_mouse) {
	auto box = a_parent->make(a_id);

	MV::PointPrecision headerSize = 20.0f;
	auto boxContents = box->make("contents")->position({ 0.0f, headerSize });

	std::weak_ptr<MV::Scene::Node> weakBoxContents = boxContents;
	auto boxScroller = box->attach<MV::Scene::Scroller>(a_mouse)->content(boxContents)->bounds({MV::Point<>(0.0f, headerSize), MV::Size<>(a_boxSize.width, a_boxSize.height - headerSize)});
	box->attach<MV::Scene::Stencil>()->anchors()
		.anchor({ MV::Point<>(0.0f, 0.0f), MV::Point<>(1.0f, 1.0f) })
		.offset({MV::Point<>(0.0f, -headerSize), MV::Point<>()})
		.parent(boxScroller);
	box->attach<MV::Scene::Clickable>(a_mouse)->color({ BOX_BACKGROUND })->show()->anchors()
		.anchor({ MV::Point<>(0.0f, 0.0f), MV::Point<>(1.0f, 1.0f) })
		.parent(boxScroller);

	auto boxHeader = box->make("headerContainer")->attach<MV::Scene::Clickable>(a_mouse);
// 	auto indexList = box->parentIndexList(boxHeader->globalPriority());
// 	indexList[1] += 10;
// 	boxHeader->overridePriority(indexList);
// 	indexList[1] += 10;
// 	boxScroller->overridePriority(indexList);
	boxHeader->bounds(MV::size(a_boxSize.width, headerSize));
	boxHeader->color({ BOX_HEADER });
	boxHeader->show();

	boxHeader->onDrag.connect("DragSignal", [](std::shared_ptr<MV::Scene::Clickable> a_boxHeader, const MV::Point<int> &startPosition, const MV::Point<int> &deltaPosition) {
		a_boxHeader->owner()->parent()->translate(a_boxHeader->owner()->renderer().worldFromScreen(deltaPosition));
	});
	return boxContents;
}


std::shared_ptr<MV::Scene::Node> makeColorPicker(MV::Draw2D &a_renderer, MV::MouseState &a_mouse, const MV::Color &a_startColor) {
	auto sliderNode = MV::Scene::Node::make(a_renderer, MV::guid("colorpicker_"));

	return sliderNode;
}