#include "state.h"
#include "player.h"

void JsonNodeLoadBinder::operator()(cereal::JSONInputArchive& a_archive) {
	a_archive.add(
		cereal::make_nvp("mouse", &mouse),
		cereal::make_nvp("renderer", &managers.renderer),
		cereal::make_nvp("textLibrary", &managers.textLibrary),
		cereal::make_nvp("pool", &managers.pool),
		cereal::make_nvp("texture", &managers.textures),
		cereal::make_nvp("script", &script)
	);
}

void BinaryNodeLoadBinder::operator()(cereal::PortableBinaryInputArchive& a_archive) {
	a_archive.add(
		cereal::make_nvp("mouse", &mouse),
		cereal::make_nvp("renderer", &managers.renderer),
		cereal::make_nvp("textLibrary", &managers.textLibrary),
		cereal::make_nvp("pool", &managers.pool),
		cereal::make_nvp("texture", &managers.textures),
		cereal::make_nvp("script", &script)
	);
}
