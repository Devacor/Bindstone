#include "editorFactories.h"
#include "editorDefines.h"
#include "editorPanels.h"

void colorTopAndBottom(const std::shared_ptr<MV::Scene::Rectangle> &a_rect, const MV::Color &a_top, const MV::Color &a_bot){
	a_rect->applyToCorners(a_top, a_top, a_bot, a_bot);
}

std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier){
	static long buttonId = 0;
	auto button = a_parent->make<MV::Scene::Button>(MV::wideToString(a_text) + boost::lexical_cast<std::string>(buttonId++), &a_mouse, a_size);
	auto activeScene = MV::Scene::Rectangle::make(a_parent->getRenderer(), a_size);
	colorTopAndBottom(activeScene, {BUTTON_TOP_ACTIVE}, {BUTTON_BOTTOM_ACTIVE});

	auto idleScene = MV::Scene::Rectangle::make(a_parent->getRenderer(), a_size);
	colorTopAndBottom(idleScene, {BUTTON_TOP_IDLE}, {BUTTON_BOTTOM_IDLE});

	auto activeBox = activeScene->make<MV::Scene::Text>("TextBox", &a_library, a_size, a_fontIdentifier);
	activeBox->justification(MV::TextJustification::CENTER);
	activeBox->wrapping(MV::TextWrapMethod::HARD)->setMinimumLineHeight(a_size.height)->text(a_text);
	
	auto idleBox = idleScene->make<MV::Scene::Text>("TextBox", &a_library, a_size, a_fontIdentifier);
	idleBox->justification(MV::TextJustification::CENTER);
	idleBox->wrapping(MV::TextWrapMethod::HARD)->setMinimumLineHeight(a_size.height)->text(a_text);

	if(a_text.length() > 15){
		//ABC DEF GHI JKL MNO PQR STU VWX YZ
		//idleBox.formattedText.removeCharacters(strlen("A")-1, 1);
	}

	button->activeScene(activeScene);
	button->idleScene(idleScene);

	return button;
}

std::shared_ptr<MV::Scene::Text> makeInputField(EditorPanel *a_panel, MV::MouseState &a_mouse, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_textLibrary, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents){
	auto box = a_parent->make<MV::Scene::Text>(a_name, &a_textLibrary, a_size, "small")->justification(MV::TextJustification::CENTER)->text(a_startContents);
	auto background = box->make<MV::Scene::Rectangle>("Background", a_size)->depth(-1);
	auto clickable = box->make<MV::Scene::Clickable>("Clickable", &a_mouse, a_size);
	clickable->clickSignals["register"] = clickable->onAccept.connect([=](std::shared_ptr<MV::Scene::Clickable> a_clickable){
		a_panel->activate(box);
	});
	box->setMinimumLineHeight(a_size.height);
	box->makeSingleLine();
	colorTopAndBottom(background, {TEXTBOX_TOP}, {TEXTBOX_BOTTOM});
	return box;
}
