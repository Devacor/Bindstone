#include "guiFactories.h"
#include "Utility/generalUtility.h"

enum GameInterfaceColors : uint32_t {
	POSITION_HANDLE_CENTER = 0xFFFFFFFF,
	POSITION_HANDLE = 0x44FFFF00,
	ROTATION_HANDLE = 0x8800FFFF,
	BUTTON_TOP_IDLE = 0x707070,
	BUTTON_BOTTOM_IDLE = 0x636363,
	BUTTON_TOP_ACTIVE = 0x3d3d3d,
	BUTTON_BOTTOM_ACTIVE = 0x323232,
	SCENE_BUTTON_TOP_IDLE = 0x494949,
	SCENE_BUTTON_BOTTOM_IDLE = 0x3e3e3e,
	SCENE_BUTTON_TOP_ACTIVE = 0x595959,
	SCENE_BUTTON_BOTTOM_ACTIVE = 0x4e4e4e,
	TEXTBOX_TOP = 0x303030,
	TEXTBOX_BOTTOM = 0x414141,
	LABEL_TOP = 0x414141,
	LABEL_BOTTOM = 0x717171,
	BOX_BACKGROUND = 0x4f4f4f,
	BOX_HEADER = 0x2d2d2d,
	CREATED_DEFAULT = 0xdfdfdf,
	SIZE_HANDLES = 0x44ffb400,
	SLIDER_BACKGROUND = 0x2c2e36,
	SIDER_HANDLE = 0x516191
};

std::shared_ptr<MV::Scene::Button> button(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const MV::Size<> &a_size, const MV::UtfString &a_text) {
	auto fontIdentifier = MV::DEFAULT_ID;
	static long buttonId = 0;
	auto button = a_parent->make(MV::guid(MV::to_string(a_text)))->attach<MV::Scene::Button>(a_mouse)->size(a_size);
	std::vector<MV::Color> boxActiveColors = { { GameInterfaceColors::BUTTON_TOP_ACTIVE },{ GameInterfaceColors::BUTTON_BOTTOM_ACTIVE },{ GameInterfaceColors::BUTTON_BOTTOM_ACTIVE },{ GameInterfaceColors::BUTTON_TOP_ACTIVE } };
	std::vector<MV::Color> boxIdleColors = { { GameInterfaceColors::BUTTON_TOP_IDLE },{ GameInterfaceColors::BUTTON_BOTTOM_IDLE },{ GameInterfaceColors::BUTTON_BOTTOM_IDLE },{ GameInterfaceColors::BUTTON_TOP_IDLE } };

	auto activeScene = button->owner()->make("active")->attach<MV::Scene::Sprite>()->size(a_size)->colors(boxActiveColors)->owner();

	auto idleScene = button->owner()->make("idle")->attach<MV::Scene::Sprite>()->size(a_size)->colors(boxIdleColors)->owner();

	auto activeBox = activeScene->attach<MV::Scene::Text>(a_library, a_size, fontIdentifier);
	activeBox->justification(MV::TextJustification::CENTER);
	activeBox->wrapping(MV::TextWrapMethod::HARD)->minimumLineHeight(a_size.height)->text(a_text);

	auto idleBox = idleScene->attach<MV::Scene::Text>(a_library, a_size, fontIdentifier);
	idleBox->justification(MV::TextJustification::CENTER);
	idleBox->wrapping(MV::TextWrapMethod::HARD)->minimumLineHeight(a_size.height)->text(a_text);

	button->activeNode(activeScene);
	button->idleNode(idleScene);

	return button;
}