#ifndef __MV_EDITOR_FACTORIES_H__
#define __MV_EDITOR_FACTORIES_H__

#include <memory>
#include "Render/package.h"

std::shared_ptr<MV::TextBox> makeInputField(MV::TextLibrary &a_textLibrary, const MV::Size<> &a_size, const MV::UtfString &a_startContents = UTF_CHAR_STR("0"));
std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const MV::Size<> &a_size, const MV::UtfString &a_text, const std::string &a_fontIdentifier = "default");
void colorTopAndBottom(const std::shared_ptr<MV::Scene::Rectangle> &a_rect, const MV::Color &a_top, const MV::Color &a_bot);

#endif
