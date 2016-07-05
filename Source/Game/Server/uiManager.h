#ifndef _MV_UIMANAGER_H_
#define _MV_UIMANAGER_H_

#include "Render/package.h"
#include "Utility/package.h"

namespace MV {
	class Interface {
	public:
		Interface(const std::string &a_filename) :
			fileName(a_filename){
			root = MV::Scene::Node::load(fileContents(a_filename));
		}

	private:
		std::string fileName;
		std::shared_ptr<MV::Scene::Node> root;
	};

	class InterfaceManager {
	public:

	private:
	};
}
#endif
