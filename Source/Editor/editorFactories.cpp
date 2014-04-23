#include "editorFactories.h"
#include "editorDefines.h"

void colorTopAndBottom(const std::shared_ptr<MV::Scene::Rectangle> &a_rect, const MV::Color &a_top, const MV::Color &a_bot){
	a_rect->applyToCorners(a_top, a_top, a_bot, a_bot);
}

std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier){
	static long buttonId = 0;
	auto button = a_parent->make<MV::Scene::Button>(MV::wideToString(a_text) + boost::lexical_cast<std::string>(buttonId++), &a_mouse, a_size);
	auto activeScene = MV::Scene::Rectangle::make(a_parent->getRenderer(), MV::Point<>(), a_size, false);
	colorTopAndBottom(activeScene, {BUTTON_TOP_ACTIVE}, {BUTTON_BOTTOM_ACTIVE});

	auto idleScene = MV::Scene::Rectangle::make(a_parent->getRenderer(), MV::Point<>(), a_size, false);
	colorTopAndBottom(idleScene, {BUTTON_TOP_IDLE}, {BUTTON_BOTTOM_IDLE});

	MV::TextBox activeBox(&a_library, a_fontIdentifier, a_size), idleBox(&a_library, a_fontIdentifier, a_size);
	activeBox.justification(MV::CENTER);
	activeBox.setMinimumLineHeight(a_size.height);
	activeBox.setText(a_text);
	
	idleBox.justification(MV::CENTER);
	idleBox.setMinimumLineHeight(a_size.height);
	idleBox.setText(a_text);

	activeBox.scene()->position(activeBox.scene()->basicAABB().centerPoint() - activeScene->basicAABB().centerPoint());
	activeScene->add("text", activeBox.scene());
	idleScene->add("text", idleBox.scene());

	button->activeScene(activeScene);
	button->idleScene(idleScene);

	return button;
}

std::shared_ptr<MV::TextBox> makeInputField(MV::TextLibrary &a_textLibrary, const MV::Size<> &a_size, const MV::UtfString &a_startContents) {
	auto box = std::make_shared<MV::TextBox>(&a_textLibrary, "small", a_startContents, a_size);
	box->justification(MV::CENTER);
	box->setMinimumLineHeight(a_size.height);
	box->makeSingleLine();
	auto background = box->scene()->make<MV::Scene::Rectangle>("background", a_size);
	colorTopAndBottom(background, {TEXTBOX_TOP}, {TEXTBOX_BOTTOM});
	background->setSortDepth(-100.0);
	return box;
}
