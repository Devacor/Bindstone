// MV utility regression tests (generalUtility.h, stringUtility.h, boxaabb.h), Foundry framework.
// Built into mv_tests.exe (see build_tests.bat); auto-registers with the Foundry runner.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Utility/generalUtility.h"
#include "MV/Utility/stringUtility.h"
#include "MV/Render/points.h"
#include "MV/Render/boxaabb.h"

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
        test("wrappingDistance is the shorter way around the range and says when it wraps", [this]() {
            bool wrapped = false;
            check_near(20.0f, wrappingDistance(0.0f, 360.0f, 10.0f, 350.0f, &wrapped), 1e-4f);
            check_true(wrapped);
            check_near(40.0f, wrappingDistance(0.0f, 360.0f, 10.0f, 50.0f, &wrapped), 1e-4f);
            check_false(wrapped);
            check_near(10.0f, wrappingDistance(0.0f, 360.0f, 370.0f, 20.0f, &wrapped), 1e-4f);
            check_false(wrapped);
            check_near(20.0f, wrappingDistance(0.0f, 360.0f, -10.0f, 10.0f, &wrapped), 1e-4f);
            check_true(wrapped);
            check_near(20.0f, wrappingDistance(360.0f, 0.0f, 350.0f, 10.0f, &wrapped), 1e-4f);
            check_true(wrapped);
        });
        test("wrappingDistance honors a nonzero lower bound and integer ranges", [this]() {
            bool wrapped = false;
            check_near(20.0, wrappingDistance(100.0, 200.0, 110.0, 190.0, &wrapped), 1e-9);
            check_true(wrapped);
            check_eq(2, wrappingDistance(0, 8, 1, 7, &wrapped));
            check_true(wrapped);
            check_eq(0, wrappingDistance(0, 8, 3, 3, &wrapped));
            check_false(wrapped);
        });
        test("wrappingMix (Collider::interpolateDrawAngle) goes the short way around, from wrapped angles", [this]() {
            auto offZero = [](float a_angle) { return wrappingDistance(0.0f, 360.0f, a_angle, 0.0f); };
            check_near(0.0f, offZero(wrappingMix(0.0f, 360.0f, 350.0f, 10.0f, 0.5f)), 1e-4f);
            check_near(0.0f, offZero(wrappingMix(0.0f, 360.0f, 10.0f, 350.0f, 0.5f)), 1e-4f);
            check_near(355.0f, wrappingMix(0.0f, 360.0f, 350.0f, 10.0f, 0.25f), 1e-4f);
            check_near(5.0f, wrappingMix(0.0f, 360.0f, 10.0f, 350.0f, 0.25f), 1e-4f);
            check_near(20.0f, wrappingMix(0.0f, 360.0f, 10.0f, 50.0f, 0.25f), 1e-4f);
            check_near(40.0f, wrappingMix(0.0f, 360.0f, 50.0f, 10.0f, 0.25f), 1e-4f);
            check_near(350.0f, wrappingMix(0.0f, 360.0f, 350.0f, 10.0f, 0.0f), 1e-4f);
            check_near(10.0f, wrappingMix(0.0f, 360.0f, 350.0f, 10.0f, 1.0f), 1e-4f);
            check_near(0.0f, offZero(wrappingMix(0.0f, 360.0f, -5.0f, 5.0f, 0.5f)), 1e-4f);
            check_near(0.0f, offZero(wrappingMix(0.0f, 360.0f, 355.0f, 365.0f, 0.5f)), 1e-4f);
            check_near(0.0f, offZero(wrappingMix(0.0f, 360.0f, 725.0f, 715.0f, 0.5f)), 1e-4f);
            check_near(90.0f, wrappingMix(0.0f, 360.0f, 450.0f, 450.0f, 0.5f), 1e-4f);
            check_near(105.0, wrappingMix(100.0, 200.0, 190.0, 120.0, 0.5), 1e-9);
        });
        test("startsWith is true exactly when the string begins with the prefix", [this]() {
            check_true(startsWith("Assets/Interface", "Assets/"));
            check_true(startsWith("abc", "abc"));
            check_true(startsWith("abc", ""));
            check_false(startsWith("abc", "abd"));
            check_false(startsWith("ab", "abc"));
            check_false(startsWith("xabc", "abc"));
        });
        test("BoxAABB centerPoint is the midpoint of its corners", [this]() {
            BoxAABB<> box(Point<>(10.0f, 20.0f, 0.0f), Point<>(30.0f, 60.0f, 4.0f));
            Point<> c = box.centerPoint();
            check_near(20.0f, c.x, 1e-6f);
            check_near(40.0f, c.y, 1e-6f);
            check_near(2.0f, c.z, 1e-6f);
            BoxAABB<int> ibox(Point<int>(-4, 2), Point<int>(6, 12));
            Point<int> ic = ibox.centerPoint();
            check_eq(1, ic.x);
            check_eq(7, ic.y);
        });
    }
};

} // namespace mv_tests

using utility_suite = mv_tests::utility_suite;
FOUNDRY_REGISTER(utility_suite)
