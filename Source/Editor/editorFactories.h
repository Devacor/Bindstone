#ifndef __MV_EDITOR_FACTORIES_H__
#define __MV_EDITOR_FACTORIES_H__

#include <memory>
#include "Render/package.h"
class EditorPanel;
std::shared_ptr<MV::Scene::Text> makeInputField(EditorPanel* a_panel, MV::MouseState &a_mouse, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_textLibrary, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents = UTF_CHAR_STR("0"));
std::shared_ptr<MV::Scene::Text> makeLabel(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_textLibrary, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents = UTF_CHAR_STR("--"));
std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier = MV::DEFAULT_ID);
std::shared_ptr<MV::Scene::Button> makeSceneButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier = "small");
std::shared_ptr<MV::Scene::Slider> makeSlider(MV::MouseState &a_mouse, const std::shared_ptr<MV::Scene::Node> &a_parent, const std::function <void(std::shared_ptr<MV::Scene::Slider>)> &a_method, float a_startPercent = 1.0f);
std::shared_ptr<MV::Scene::Node> makeSlider(MV::Draw2D &a_renderer, MV::MouseState &a_mouse, const std::function <void(std::shared_ptr<MV::Scene::Slider>)> &a_method, float a_startPercent = 1.0f);
std::shared_ptr<MV::Scene::Node> makeSlider(MV::Draw2D &a_renderer, MV::MouseState &a_mouse, float a_startPercent = 1.0f);
void colorTopAndBottom(const std::shared_ptr<MV::Scene::Sprite> &a_rect, const MV::Color &a_top, const MV::Color &a_bot);

std::shared_ptr<MV::Scene::Node> makeDraggableBox(const std::string &a_id, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::Size<> a_boxSize, MV::MouseState &a_mouse);

#endif
