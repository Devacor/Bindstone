#ifndef __MV_EDITOR_FACTORIES_H__
#define __MV_EDITOR_FACTORIES_H__

#include <memory>
#include "Render/package.h"
class EditorPanel;
std::shared_ptr<MV::Scene::Text> makeInputField(EditorPanel* a_panel, MV::TapDevice &a_mouse, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_textLibrary, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents = UTF_CHAR_STR("0"));
std::shared_ptr<MV::Scene::Text> makeInputField(EditorPanel *a_panel, MV::Services &a_services, const std::shared_ptr<MV::Scene::Node> &a_parent, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents = UTF_CHAR_STR("0"));
std::shared_ptr<MV::Scene::Text> makeLabel(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_textLibrary, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents = UTF_CHAR_STR("--"));
std::shared_ptr<MV::Scene::Text> makeLabel(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::Services &a_services, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_startContents = UTF_CHAR_STR("--"));
std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::TapDevice &a_mouse, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier = MV::DEFAULT_ID);
std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::Services &a_services, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier = MV::DEFAULT_ID);
std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::Services &a_services, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier = MV::DEFAULT_ID);
std::shared_ptr<MV::Scene::Button> makeColorButton(const std::shared_ptr<MV::Scene::Node> &a_parent, std::weak_ptr<MV::Scene::Node> a_colorPaletteParent, MV::TextLibrary &a_library, MV::TapDevice &a_mouse, const MV::Size<> &a_size, const MV::Color &a_color, std::function<void(const MV::Color& a_newColor)> a_callback, const MV::UtfString &a_text = UTF_CHAR_STR("Color"));
MV::Scene::SafeComponent<MV::Scene::Button> makeColorButton(MV::Draw2D &a_renderer, std::weak_ptr<MV::Scene::Node> a_colorPaletteParent, MV::TextLibrary &a_library, MV::TapDevice &a_mouse, const MV::Size<> &a_size, const MV::Color &a_color, std::function<void(const MV::Color& a_newColor)> a_callback, const MV::UtfString &a_text = UTF_CHAR_STR("Color"));
MV::Scene::SafeComponent<MV::Scene::Button> makeColorButton(MV::Services &a_services, std::weak_ptr<MV::Scene::Node> a_colorPaletteParent, const MV::Size<> &a_size, const MV::Color &a_color, std::function<void(const MV::Color& a_newColor)> a_callback, const MV::UtfString &a_text = UTF_CHAR_STR("Color"));
void renameButton(const MV::Scene::SafeComponent<MV::Scene::Button> &a_button, const MV::UtfString &a_text);
std::shared_ptr<MV::Scene::Button> makeSceneButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::TapDevice &a_mouse, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier = "small");
std::shared_ptr<MV::Scene::Button> makeSceneButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::Services &a_services, const std::string &a_name, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier = "small");
std::shared_ptr<MV::Scene::Slider> makeSlider(MV::TapDevice &a_mouse, const std::shared_ptr<MV::Scene::Node> &a_parent, const std::function <void(std::shared_ptr<MV::Scene::Slider>)> &a_method, float a_startPercent = 1.0f);
std::shared_ptr<MV::Scene::Node> makeSlider(MV::Draw2D &a_renderer, MV::TapDevice &a_mouse, const std::function <void(std::shared_ptr<MV::Scene::Slider>)> &a_method, float a_startPercent = 1.0f);
std::shared_ptr<MV::Scene::Node> makeSlider(MV::Draw2D &a_renderer, MV::TapDevice &a_mouse, float a_startPercent = 1.0f);
std::shared_ptr<MV::Scene::Slider> makeSlider(MV::Services &a_services, const std::shared_ptr<MV::Scene::Node> &a_parent, const std::function <void(std::shared_ptr<MV::Scene::Slider>)> &a_method, float a_startPercent = 1.0f);
std::shared_ptr<MV::Scene::Node> makeSlider(MV::Services &a_services, const std::function <void(std::shared_ptr<MV::Scene::Slider>)> &a_method, float a_startPercent = 1.0f);
std::shared_ptr<MV::Scene::Node> makeSlider(MV::Services &a_services, float a_startPercent = 1.0f);
void colorTopAndBottom(const std::shared_ptr<MV::Scene::Sprite> &a_rect, const MV::Color &a_top, const MV::Color &a_bot);

std::shared_ptr<MV::Scene::Node> makeDraggableBox(const std::string &a_id, const std::shared_ptr<MV::Scene::Node> &a_parent, MV::Size<> a_boxSize, MV::TapDevice &a_mouse);
void applyColorToColorButton(std::shared_ptr<MV::Scene::Button> a_button, const MV::Color& a_color);

std::shared_ptr<MV::Scene::Node> makeToggle(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TapDevice &a_mouse, const std::string &a_name, bool a_defaultValue, const std::function<void ()> a_on, const std::function<void ()> a_off, const MV::Size<> &a_size = MV::Size<>(27.0f, 27.0f));
std::shared_ptr<MV::Scene::Node> makeToggle(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::Services &a_services, const std::string &a_name, bool a_defaultValue, const std::function<void()> a_on, const std::function<void()> a_off, const MV::Size<> &a_size = MV::Size<>(27.0f, 27.0f));
#endif
