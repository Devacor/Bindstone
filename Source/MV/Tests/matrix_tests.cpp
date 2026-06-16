// Bindstone (MV) matrix-optimization regression tests, using JaiScript's Foundry framework.
// Guards the behavior the matrix.hpp optimizations rely on:
//   - affineMultiply == generic 4x4 multiply (bit-identical) for affine inputs
//   - in-place rotateX/Y/Z == the old build-identity-then-multiply form (numerically identical)
// Built into mv_tests.exe (see build_tests.bat); auto-registers with the Foundry runner.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Render/matrix.hpp"

#include <random>
#include <cstring>
#include <cmath>
#include <string>

using namespace jai::foundry;
using namespace MV;

namespace mv_tests {

static bool bitEq(const Matrix<4, 4>& a, const Matrix<4, 4>& b) {
    return std::memcmp(a.getMatrixArray().data(), b.getMatrixArray().data(), 16 * sizeof(float)) == 0;
}
// numeric equality: tolerates the benign -0.0f vs +0.0f in always-zero entries.
static bool numEq(const Matrix<4, 4>& a, const Matrix<4, 4>& b) {
    for (int i = 0; i < 16; ++i) if (a[i] != b[i]) return false;
    return true;
}
static TransformMatrix randomAffine(std::mt19937& rng) {
    std::uniform_real_distribution<float> d(-3.0f, 3.0f), s(0.25f, 4.0f), ang(-3.14159f, 3.14159f);
    TransformMatrix m;
    m.position(d(rng), d(rng), d(rng));
    m.setRotationXYZ(ang(rng), ang(rng), ang(rng));
    m.scale(s(rng), s(rng), s(rng));
    return m;
}

class matrix_suite : public suite {
public:
    matrix_suite() : suite("MV Matrix") {}

    void forge_tests() override {
        test("affineMultiply is bit-identical to the generic 4x4 multiply for affine inputs", [] {
            std::mt19937 rng(101);
            for (int i = 0; i < 5000; ++i) {
                TransformMatrix A = randomAffine(rng), B = randomAffine(rng);
                Matrix<4, 4> generic = A * B;
                TransformMatrix affine = affineMultiply(A, B);
                check(bitEq(generic, affine),
                      "affineMultiply diverged from the generic multiply at iter " + std::to_string(i));
            }
        });

        test("affineMultiply output stays affine (4th row == 0,0,0,1)", [] {
            std::mt19937 rng(202);
            for (int i = 0; i < 1000; ++i) {
                TransformMatrix a = affineMultiply(randomAffine(rng), randomAffine(rng));
                check(a[3] == 0.0f && a[7] == 0.0f && a[11] == 0.0f && a[15] == 1.0f,
                      "affineMultiply produced a non-affine 4th row");
            }
        });

        test("in-place rotateX/Y/Z match the generic (*this * R) product", [] {
            std::mt19937 rng(303);
            std::uniform_real_distribution<float> ang(-3.14159f, 3.14159f);
            for (int i = 0; i < 3000; ++i) {
                TransformMatrix M = randomAffine(rng);
                float t = ang(rng), c = std::cos(t), s = std::sin(t);
                for (int axis = 0; axis < 3; ++axis) {
                    TransformMatrix R;
                    TransformMatrix live = M;
                    if (axis == 0)      { R.access(1,1)=c; R.access(2,1)=-s; R.access(1,2)=s; R.access(2,2)=c; live.rotateXSupplyCosSin(c, s); }
                    else if (axis == 1) { R.access(0,0)=c; R.access(2,0)=s; R.access(0,2)=-s; R.access(2,2)=c; live.rotateYSupplyCosSin(c, s); }
                    else                { R.access(0,0)=c; R.access(1,0)=-s; R.access(0,1)=s; R.access(1,1)=c; live.rotateZSupplyCosSin(c, s); }
                    Matrix<4, 4> ref = M * R;
                    check(numEq(ref, live),
                          "in-place rotate diverged from generic multiply (axis " + std::to_string(axis) +
                          ", iter " + std::to_string(i) + ")");
                }
            }
        });

        test("rotate leaves the translation column / projective entry untouched", [] {
            std::mt19937 rng(404);
            TransformMatrix M = randomAffine(rng);
            TransformMatrix r = M;
            r.rotateZSupplyCosSin(0.5f, 0.3f);
            check(r[12] == M[12] && r[13] == M[13] && r[14] == M[14] && r[15] == M[15],
                  "rotateZ corrupted the translation column / projective entry");
        });

        test("rotateZ(+90deg) maps +X onto +Y", [] {
            TransformMatrix m;
            m.rotateZSupplyCosSin(0.0f, 1.0f); // cos=0, sin=1 -> +90 degrees
            Point<> p = m * Point<>(1.0f, 0.0f, 0.0f);
            check_near(0.0f, p.x, 1e-5f);
            check_near(1.0f, p.y, 1e-5f);
        });
    }
};

} // namespace mv_tests

using matrix_suite = mv_tests::matrix_suite;
FOUNDRY_REGISTER(matrix_suite)
