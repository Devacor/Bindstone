#include "task.h"

#include "cereal/cereal.hpp"
#include "cereal/types/list.hpp"
#include "cereal/types/memory.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/base_class.hpp"
#include "cereal/types/polymorphic.hpp"

#include "cereal/archives/portable_binary.hpp"
#include "cereal/archives/json.hpp"

CEREAL_REGISTER_TYPE(MV::BasicAction);
CEREAL_REGISTER_DYNAMIC_INIT(mv_task);
