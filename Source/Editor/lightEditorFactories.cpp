#include "lightEditorFactories.h"
#include "Editor/editorDefines.h"

#include "MV/Render/package.h"

std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::TextLibrary& a_library, MV::TapDevice& a_mouse, const std::string& a_name, const MV::Size<>& a_size, const MV::UtfString& a_text, const std::string& a_fontIdentifier /*= MV::DEFAULT_ID*/) {
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

std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const MV::Size<>& a_size, const MV::UtfString& a_text, const std::string& a_fontIdentifier /*= MV::DEFAULT_ID*/) {
	return makeButton(a_parent, *a_services.get<MV::TextLibrary>(), *a_services.get<MV::TapDevice>(), a_text, a_size, a_text, a_fontIdentifier);
}

std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const std::string& a_name, const MV::Size<>& a_size, const MV::UtfString& a_text, const std::string& a_fontIdentifier /*= MV::DEFAULT_ID*/) {
	return makeButton(a_parent, *a_services.get<MV::TextLibrary>(), *a_services.get<MV::TapDevice>(), a_name, a_size, a_text, a_fontIdentifier);
}
