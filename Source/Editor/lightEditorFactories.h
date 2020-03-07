#ifndef __MV_EDITOR_FACTORIES_H__
#define __MV_EDITOR_FACTORIES_H__

#include <memory>
#include "MV/Render/package.h"

std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::TextLibrary& a_library, MV::TapDevice& a_mouse, const std::string& a_name, const MV::Size<>& a_size, const MV::UtfString& a_text, const std::string& a_fontIdentifier = MV::DEFAULT_ID);
std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const MV::Size<>& a_size, const MV::UtfString& a_text, const std::string& a_fontIdentifier = MV::DEFAULT_ID);
std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const std::string& a_name, const MV::Size<>& a_size, const MV::UtfString& a_text, const std::string& a_fontIdentifier = MV::DEFAULT_ID);


#endif