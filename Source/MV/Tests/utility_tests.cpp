// MV utility regression tests (generalUtility.h, stringUtility.h, boxaabb.h), Foundry framework.
// Built into mv_tests.exe (see build_tests.bat); auto-registers with the Foundry runner.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Utility/generalUtility.h"
#include "MV/Render/points.h"

using namespace jai::foundry;
using namespace MV;

namespace mv_tests {

class utility_suite : public suite {
public:
    utility_suite() : suite("MV Utility") {}

    void forge_tests() override {
        test("angle2D measures the direction from the first point to the second in [0, 360)", [this]() {
            check_near(45.0f, angle2D(0.0f, 0.0f, 1.0f, 1.0f), 1e-4f);
            check_near(135.0f, angle2D(0.0f, 0.0f, -1.0f, 1.0f), 1e-4f);
            check_near(270.0f, angle2D(0.0f, 0.0f, 0.0f, -2.0f), 1e-4f);
            check_near(315.0, angle2D(1.0, 1.0, 2.0, 0.0), 1e-9);
        });
        test("angle2D int overload uses x2 - x1", [this]() {
            check_near(45.0f, angle2D(0, 0, 1, 1), 1e-4f);
            check_near(90.0f, angle2D(3, 2, 3, 7), 1e-4f);
            check_near(180.0f, angle2D(5, 5, 1, 5), 1e-4f);
            check_near(angle2D(2.0f, 1.0f, 7.0f, 4.0f), angle2D(2, 1, 7, 4), 1e-4f);
        });
        test("angle2DRad int overload uses x2 - x1", [this]() {
            check_near(std::atan2(3.0f, 5.0f), angle2DRad(2, 1, 7, 4), 1e-6f);
            check_near(angle2DRad(2.0f, 1.0f, 7.0f, 4.0f), angle2DRad(2, 1, 7, 4), 1e-6f);
        });
        test("angle2D on points agrees with the scalar form", [this]() {
            check_near(45.0f, angle2D(Point<>(0.0f, 0.0f), Point<>(3.0f, 3.0f)), 1e-4f);
            check_near(45.0f, angle2D(Point<int>(0, 0), Point<int>(3, 3)), 1e-4f);
        });
    }
};

} // namespace mv_tests

using utility_suite = mv_tests::utility_suite;
FOUNDRY_REGISTER(utility_suite)
