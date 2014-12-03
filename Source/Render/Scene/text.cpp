#include "text.h"
#include "cereal/archives/json.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Text);
namespace MV{
	namespace Scene {

		const double Text::BLINK_DURATION = .35;

	}
}