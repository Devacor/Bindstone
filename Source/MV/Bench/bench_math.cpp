// MV matrix/point math micro-benchmark.
//
// Exercises the hot operations of MV/Render/matrix.hpp + points.h + boxaabb.h by
// including the REAL headers (so re-compiling after a header change measures the change).
//
// Build (from a VS x64 dev shell), single TU, header-only deps:
//   cl /nologo /O2 /Ob2 /Oi /Ot /std:c++20 /EHsc /DNDEBUG ^
//      /I"D:\git\Bindstone\Source" /I"D:\git\Bindstone\External\cereal\include" ^
//      /I"D:\git\Bindstone\Source\JaiScript\include" bench_math.cpp /Fe:bench_math.exe
//
// Run:  bench_math.exe [repetitions]   (default 11)
//
// Anti-DCE: every result folds into a global double `sink` printed at the end; all
// inputs come from a runtime-seeded array indexed by (i & MASK) so nothing is loop-invariant.

#define NDEBUG
#include "MV/Render/matrix.hpp"
#include "MV/Render/boxaabb.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <array>
#include <chrono>
#include <algorithm>
#include <random>
#include <string>

using MV::Matrix;
using MV::TransformMatrix;
using MV::Point;
using MV::Scale;
using MV::BoxAABB;

static volatile double sink = 0.0;

// ---- input pools (runtime-filled, so not constant-foldable) ----
static constexpr size_t POOL = 1024;          // power of two
static constexpr size_t MASK = POOL - 1;

static std::vector<TransformMatrix> gAffine;   // random affine TRS matrices
static std::vector<TransformMatrix> gWorld;    // random "parent world" matrices
static std::vector<Matrix<4,4>>     gDense;    // fully-dense random matrices (for inverse/general)
static std::vector<Point<>>         gPoints;
static std::vector<Point<>>         gPoints2;
static std::vector<Point<>>         gAngles;   // rotation angles (radians) packed as Point
static std::vector<Scale>           gScales;
static std::vector<float>           gFloats;

static void buildPools() {
    std::mt19937 rng(12345u);
    std::uniform_real_distribution<float> d(-3.0f, 3.0f);
    std::uniform_real_distribution<float> s(0.25f, 4.0f);
    std::uniform_real_distribution<float> ang(-3.14159f, 3.14159f);
    gAffine.resize(POOL); gWorld.resize(POOL); gDense.resize(POOL);
    gPoints.resize(POOL); gPoints2.resize(POOL); gAngles.resize(POOL);
    gScales.resize(POOL); gFloats.resize(POOL);
    for (size_t i = 0; i < POOL; ++i) {
        TransformMatrix a;                 // identity
        a.position(d(rng), d(rng), d(rng));
        a.setRotationXYZ(ang(rng), ang(rng), ang(rng));
        a.scale(s(rng), s(rng), s(rng));
        gAffine[i] = a;
        TransformMatrix w;
        w.position(d(rng), d(rng), d(rng));
        w.setRotationXYZ(ang(rng), ang(rng), ang(rng));
        w.scale(s(rng), s(rng), s(rng));
        gWorld[i] = w;
        Matrix<4,4> m(MV::MatrixInitialize::NoFill);
        for (size_t k = 0; k < 16; ++k) m[k] = d(rng);
        gDense[i] = m;
        gPoints[i]  = Point<>(d(rng), d(rng), d(rng));
        gPoints2[i] = Point<>(d(rng), d(rng), d(rng));
        gAngles[i]  = Point<>(ang(rng), ang(rng), ang(rng));
        gScales[i]  = Scale(s(rng), s(rng), s(rng));
        gFloats[i]  = s(rng);
    }
}

// Fold ALL 16 elements so the optimizer cannot dead-code-eliminate unused outputs (real
// consumers store the whole matrix, e.g. node.cpp worldMatrixTransform = ...).
static inline double sum16(const Matrix<4,4>& m) {
    double a = 0;
    for (int i = 0; i < 16; ++i) a += m[i];
    return a;
}

// ---- verbatim copies of the PRE-CHANGE implementations, so old-vs-new is measured in the
//      SAME binary (no cross-build noise). These mirror matrix.hpp before this optimization. ----
static inline Matrix<4,4> oldTranspose(const Matrix<4,4>& in) {
    Matrix<4,4> result(in);                       // old: full copy of *this, then overwrite all
    for (size_t x = 0; x < 4; ++x)
        for (size_t y = 0; y < 4; ++y)
            result.accessTransposed(x, y) = in.access(x, y);
    return result;
}
static inline void oldRotateZSupplyCosSin(TransformMatrix& self, float cosR, float sinR) {
    TransformMatrix rotation;                     // old: build identity rotation...
    rotation.access(0, 0) = cosR;
    rotation.access(1, 0) = -sinR;
    rotation.access(0, 1) = sinR;
    rotation.access(1, 1) = cosR;
    self = self * rotation;                       // ...then full 4x4 multiply
}

struct Stat { double minNs, medianNs; };

template <typename F>
static Stat run(const char* /*name*/, int reps, uint64_t opsPerRep, F&& body) {
    // warmup
    { double w = body(); sink += w; }
    std::vector<double> samples;
    samples.reserve(reps);
    for (int r = 0; r < reps; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        double acc = body();
        auto t1 = std::chrono::steady_clock::now();
        sink += acc;
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        samples.push_back(ns / static_cast<double>(opsPerRep));
    }
    std::sort(samples.begin(), samples.end());
    return Stat{ samples.front(), samples[samples.size()/2] };
}

int main(int argc, char** argv) {
    int reps = (argc > 1) ? std::atoi(argv[1]) : 11;
    if (reps < 3) reps = 3;
    buildPools();

    // ---- correctness self-checks: optimized paths must be BIT-IDENTICAL to the generic
    //      multiply for these (affine) inputs, else the timings below are meaningless. ----
    {
        // Classify each element pair: numericDiff = values actually differ (real error);
        // signZeroDiff = bits differ but values compare equal (only -0.0f vs +0.0f, benign).
        auto compare = [](const Matrix<4,4>& a, const Matrix<4,4>& b, int& numericDiff, int& signZeroDiff){
            for (int e = 0; e < 16; ++e) {
                if (a[e] == b[e]) {
                    if (std::memcmp(&a[e], &b[e], sizeof(float)) != 0) ++signZeroDiff;  // -0.0 vs +0.0
                } else {
                    ++numericDiff;
                }
            }
        };
        int affNum = 0, affSz = 0, rotNum = 0, rotSz = 0;
        for (size_t i = 0; i < POOL; ++i) {
            // affineMultiply vs generic operator*
            Matrix<4,4> g = gWorld[i] * gAffine[i];
            TransformMatrix a = MV::affineMultiply(gWorld[i], gAffine[i]);
            compare(g, a, affNum, affSz);
            // in-place rotate*SupplyCosSin vs generic *this * R (the previous behavior).
            // Shared (c,s) isolates the multiply change from the trig path (unchanged by this edit).
            for (int axis = 0; axis < 3; ++axis) {
                float ang = (&gAngles[i].x)[axis];
                float c = std::cos(ang), s = std::sin(ang);
                TransformMatrix R; // identity
                TransformMatrix live = gWorld[i];
                if (axis == 0) { R.access(1,1)=c; R.access(2,1)=-s; R.access(1,2)=s; R.access(2,2)=c; live.rotateXSupplyCosSin(c,s); }
                else if (axis == 1) { R.access(0,0)=c; R.access(2,0)=s; R.access(0,2)=-s; R.access(2,2)=c; live.rotateYSupplyCosSin(c,s); }
                else { R.access(0,0)=c; R.access(1,0)=-s; R.access(0,1)=s; R.access(1,1)=c; live.rotateZSupplyCosSin(c,s); }
                Matrix<4,4> ref = gWorld[i] * R;
                compare(ref, live, rotNum, rotSz);
            }
        }
        std::printf("correctness self-check over %zu affine inputs:\n", POOL);
        std::printf("  affineMultiply : %s  (numeric diffs=%d, benign signed-zero=%d)\n",
                    affNum == 0 ? "OK" : "FAIL", affNum, affSz);
        std::printf("  in-place rotate: %s  (numeric diffs=%d, benign signed-zero=%d)\n\n",
                    rotNum == 0 ? "OK (numerically identical)" : "FAIL", rotNum, rotSz);
    }

    struct Row { const char* name; Stat st; };
    std::vector<Row> rows;

    const uint64_t N = 4'000'000;   // ops per timed sample for cheap ops

    // 1. Matrix4x4 * Matrix4x4 (hand-unrolled specialization) — world*local composition
    rows.push_back({ "mat4 * mat4", run("mat4*mat4", reps, N, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < N; ++i) {
            const auto& A = gWorld[i & MASK];
            const auto& B = gAffine[i & MASK];
            Matrix<4,4> R = A * B;
            acc += sum16(R);
        }
        return acc;
    })});

    // 1b. affineMultiply (same operands as #1) — affine-specialized, bit-identical for affine inputs
    rows.push_back({ "mat4 * mat4 (affineMultiply)", run("affmul", reps, N, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < N; ++i) {
            const auto& A = gWorld[i & MASK];
            const auto& B = gAffine[i & MASK];
            TransformMatrix R = MV::affineMultiply(A, B);
            acc += sum16(R);
        }
        return acc;
    })});

    // 2. local TRS build (recalculateMatrix local part)
    rows.push_back({ "TRS build (identity+pos+rot+scale)", run("trs", reps, N, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < N; ++i) {
            size_t k = i & MASK;
            TransformMatrix m;
            m.position(gPoints[k]);
            m.setRotationXYZ(gAngles[k]);
            m.scale(gScales[k].x, gScales[k].y, gScales[k].z);
            acc += sum16(m);
        }
        return acc;
    })});

    // 3. full local->world per node: TRS build + parent*local
    rows.push_back({ "node recalc (TRS + parent*local)", run("recalc", reps, N, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < N; ++i) {
            size_t k = i & MASK;
            TransformMatrix m;
            m.position(gPoints[k]);
            m.setRotationXYZ(gAngles[k]);
            m.scale(gScales[k].x, gScales[k].y, gScales[k].z);
            Matrix<4,4> world = gWorld[k] * m;
            acc += sum16(world);
        }
        return acc;
    })});

    // 3b. node recalc using affineMultiply (TRS build + affine parent*local)
    rows.push_back({ "node recalc (TRS + affineMultiply)", run("recalcA", reps, N, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < N; ++i) {
            size_t k = i & MASK;
            TransformMatrix m;
            m.position(gPoints[k]);
            m.setRotationXYZ(gAngles[k]);
            m.scale(gScales[k].x, gScales[k].y, gScales[k].z);
            TransformMatrix world = MV::affineMultiply(gWorld[k], m);
            acc += sum16(world);
        }
        return acc;
    })});

    // 4. operator*(Matrix4, Point<>)  (3-component result)
    rows.push_back({ "mat4 * Point<> (3-comp)", run("matpt3", reps, N, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < N; ++i) {
            size_t k = i & MASK;
            Point<> r = gWorld[k] * gPoints[k];
            acc += r.x + r.y + r.z;
        }
        return acc;
    })});

    // 5. fullMatrixPointMultiply (projection, 4-component)
    rows.push_back({ "fullMatrixPointMultiply", run("fullmp", reps, N, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < N; ++i) {
            size_t k = i & MASK;
            auto r = MV::fullMatrixPointMultiply(gWorld[k], gPoints[k]);
            acc += r[0] + r[1] + r[2] + r[3];
        }
        return acc;
    })});

    // 6. inverse(Matrix4x4)
    {
        const uint64_t M = 1'000'000;
        rows.push_back({ "inverse(mat4)", run("inv", reps, M, [&]{
            double acc = 0;
            for (uint64_t i = 0; i < M; ++i) {
                size_t k = i & MASK;
                float det;
                TransformMatrix r = MV::inverse(gWorld[k], det);
                acc += sum16(r);
            }
            return acc;
        })});
    }

    // 7. transpose()  — old (copy+overwrite) vs new (NoFill), same binary
    rows.push_back({ "transpose (old: copy+overwrite)", run("transO", reps, N, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < N; ++i) {
            Matrix<4,4> r = oldTranspose(gWorld[i & MASK]);
            acc += sum16(r);
        }
        return acc;
    })});
    rows.push_back({ "transpose (new: NoFill)", run("transN", reps, N, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < N; ++i) {
            Matrix<4,4> r = gWorld[i & MASK].transpose();
            acc += sum16(r);
        }
        return acc;
    })});

    // 8. rotateZ right-multiply — old (build identity + full mul) vs new (in-place 2 cols),
    //    same binary. Uses SupplyCosSin with precomputed cos/sin so the trig cost (unchanged)
    //    does not mask the multiply delta.
    rows.push_back({ "rotateZ mul (old: build+mul)", run("rotzO", reps, N, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < N; ++i) {
            size_t k = i & MASK;
            TransformMatrix m = gWorld[k];
            oldRotateZSupplyCosSin(m, gScales[k].x /*cos-ish*/, gScales[k].y /*sin-ish*/);
            acc += sum16(m);
        }
        return acc;
    })});
    rows.push_back({ "rotateZ mul (new: in-place)", run("rotzN", reps, N, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < N; ++i) {
            size_t k = i & MASK;
            TransformMatrix m = gWorld[k];
            m.rotateZSupplyCosSin(gScales[k].x, gScales[k].y);
            acc += sum16(m);
        }
        return acc;
    })});

    const uint64_t P = 16'000'000; // cheap point ops need more iterations

    // 9a. Point += Point
    rows.push_back({ "Point += Point", run("pt+=", reps, P, [&]{
        Point<> a = gPoints[0];
        for (uint64_t i = 0; i < P; ++i) { a += gPoints2[i & MASK]; }
        return (double)(a.x + a.y + a.z);
    })});

    // 9b. Point *= scalar
    rows.push_back({ "Point *= float", run("pt*=f", reps, P, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < P; ++i) {
            Point<> a = gPoints[i & MASK];
            a *= gFloats[i & MASK];
            acc += a.x + a.y + a.z;
        }
        return acc;
    })});

    // 9b2. Point /= float (operator/=(scalar) — currently contains the cerr branch)
    rows.push_back({ "Point /= float", run("pt/=f", reps, P, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < P; ++i) {
            Point<> a = gPoints[i & MASK];
            a /= gFloats[i & MASK];
            acc += a.x + a.y + a.z;
        }
        return acc;
    })});

    // 9c. Point /= Point (the divide-by-zero-guarded path)
    rows.push_back({ "Point /= Point (guarded)", run("pt/=", reps, P, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < P; ++i) {
            Point<> a = gPoints[i & MASK];
            a /= gPoints2[i & MASK];
            acc += a.x + a.y + a.z;
        }
        return acc;
    })});

    // 9d. magnitude
    rows.push_back({ "Point.magnitude()", run("mag", reps, P, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < P; ++i) { acc += gPoints[i & MASK].magnitude(); }
        return acc;
    })});

    // 9e. normalized
    rows.push_back({ "Point.normalized()", run("norm", reps, P, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < P; ++i) {
            Point<> r = gPoints[i & MASK].normalized();
            acc += r.x + r.y + r.z;
        }
        return acc;
    })});

    // 9f. distance
    rows.push_back({ "distance(Point,Point)", run("dist", reps, P, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < P; ++i) { acc += MV::distance(gPoints[i & MASK], gPoints2[i & MASK]); }
        return acc;
    })});

    // 10. AABB transform: project 4 corners by matrix, rebuild AABB via expandWith
    rows.push_back({ "AABB transform (4 corners + expand)", run("aabb", reps, N, [&]{
        double acc = 0;
        for (uint64_t i = 0; i < N; ++i) {
            size_t k = i & MASK;
            const auto& M = gWorld[k];
            BoxAABB<> box(gPoints[k]);
            Point<> c0 = M * gPoints[k];
            Point<> c1 = M * gPoints2[k];
            box.expandWith(c0); box.expandWith(c1);
            acc += box.minPoint.x + box.maxPoint.y;
        }
        return acc;
    })});

    // ---- report ----
    std::printf("%-40s %12s %12s\n", "operation", "min ns/op", "med ns/op");
    std::printf("%-40s %12s %12s\n", "----------------------------------------", "-----------", "-----------");
    for (auto& r : rows) {
        std::printf("%-40s %12.3f %12.3f\n", r.name, r.st.minNs, r.st.medianNs);
    }
    std::printf("\n[sink=%.3f reps=%d]\n", (double)sink, reps);
    return 0;
}
