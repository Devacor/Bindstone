#ifndef __MV_GAME_GUI_FACTORIES_H__
#define __MV_GAME_GUI_FACTORIES_H__

#include <memory>
#include "Render/package.h"

std::shared_ptr<MV::Scene::Button> button(const std::shared_ptr<MV::Scene::Node> &a_parent, MV::TextLibrary &a_library, MV::MouseState &a_mouse, const MV::Size<> &a_size, const MV::UtfString &a_text);

#endif