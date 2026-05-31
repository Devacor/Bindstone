// Scene-graph dirty-flag benchmark + correctness check for the Clipped override-parent draw path.
//
// Builds a HEADLESS MV::Draw2D and a subtree mimicking what Clipped::refreshTexture renders
// (clipParent->drawChildren(renderOrigin)), then:
//   (1) CORRECTNESS: snapshots every node's world transform in a clean state, runs an override
//       draw, and asserts the persistent world transforms are UNCHANGED (no clip-space pollution).
//   (2) PERF: over K static refresh frames, reports Node::recalculateMatrixCalls + wall-clock.
//
// Links the built engine static lib. Build (VS x64 dev shell), from this dir:
//   see build_scene.bat
//
// Run: bench_scene.exe [framesK] [width] [depth]

#include "MV/Render/render.h"
#include "MV/Render/Scene/node.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <memory>
#include <chrono>
#include <array>

using namespace MV;
using MV::Scene::Node;

static volatile double sink = 0.0;

int main(int argc, char** argv) {
    int framesK = (argc > 1) ? std::atoi(argv[1]) : 2000;
    int width   = (argc > 2) ? std::atoi(argv[2]) : 60;   // children under the clip parent
    int depth   = (argc > 3) ? std::atoi(argv[3]) : 4;    // grandchildren per child

    Draw2D renderer;
    renderer.makeHeadless();

    auto root = Node::make(renderer, "root");
    root->position(Point<>(3.0f, -2.0f, 1.0f));
    root->rotation(AxisAngles(5.0f, 10.0f, 15.0f));

    auto clipParent = root->make("clip");
    clipParent->position(Point<>(100.0f, 50.0f, 0.0f));

    std::vector<std::shared_ptr<Node>> allUnderClip;
    int id = 0;
    for (int i = 0; i < width; ++i) {
        auto child = clipParent->make("c" + std::to_string(id++));
        child->position(Point<>(float(i) * 1.7f, float(i) * -0.9f, 0.3f));
        child->rotation(AxisAngles(float(i) * 3.0f, float(i) * 1.1f, float(i) * 0.5f));
        child->scale(Scale(1.1f, 0.9f, 1.0f));
        allUnderClip.push_back(child);
        for (int j = 0; j < depth; ++j) {
            auto gc = child->make("g" + std::to_string(id++));
            gc->position(Point<>(float(j) * 0.5f, float(j) * 0.7f, 0.1f));
            gc->rotation(AxisAngles(float(j) * 7.0f, float(j) * 2.0f, float(j) * 4.0f));
            gc->scale(Scale(0.8f, 1.2f, 1.0f));
            allUnderClip.push_back(gc);
        }
    }
    const size_t N = allUnderClip.size();

    TransformMatrix renderOrigin;
    renderOrigin.translate(Point<>(-100.0f, -50.0f, 0.0f));   // clip-local origin (translate-only, affine)

    auto snapshot = [&](std::vector<std::array<float,16>>& out){
        out.resize(N);
        for (size_t i = 0; i < N; ++i) {
            TransformMatrix w = allUnderClip[i]->worldTransform();   // clean normal recalc
            std::memcpy(out[i].data(), w.getMatrixArray().data(), 16*sizeof(float));
        }
    };

    // (1) CORRECTNESS — world transforms must be identical before and after an override draw.
    std::vector<std::array<float,16>> before, after;
    snapshot(before);                       // clean baseline (real-world transforms)
    clipParent->drawChildren(renderOrigin); // override-parent draw (the Clipped path)
    snapshot(after);                        // query again — must be unpolluted

    int polluted = 0, firstBad = -1;
    for (size_t i = 0; i < N; ++i) {
        if (std::memcmp(before[i].data(), after[i].data(), 16*sizeof(float)) != 0) {
            if (firstBad < 0) firstBad = (int)i;
            ++polluted;
        }
    }
    std::printf("scene: %zu nodes under clip (width=%d depth=%d)\n", N, width, depth);
    std::printf("correctness: world transforms after override draw: %s (%d/%zu polluted)\n\n",
                polluted == 0 ? "UNCHANGED (no pollution)" : "POLLUTED", polluted, N);

    // (1b) DYNAMIC CORRECTNESS — a moved node must update correctly and survive override draws.
    {
        auto eqMat = [](const TransformMatrix& a, const TransformMatrix& b){
            for (int e = 0; e < 16; ++e) if (a[e] != b[e]) return false;   // numeric (signed-zero tolerant)
            return true;
        };
        auto& probe = allUnderClip[N/2];                 // a deep node
        TransformMatrix refClean = probe->worldTransform();
        for (int k = 0; k < 5; ++k) clipParent->drawChildren(renderOrigin);
        bool staticOk = eqMat(probe->worldTransform(), refClean);

        probe->position(probe->position() + Point<>(17.0f, -23.0f, 5.0f));   // move it (markMatrixDirty)
        TransformMatrix movedClean = probe->worldTransform();                // correct recompute
        for (int k = 0; k < 5; ++k) clipParent->drawChildren(renderOrigin);  // stress override interleave
        bool movedPreserved = eqMat(probe->worldTransform(), movedClean);
        bool moveTookEffect = !eqMat(movedClean, refClean);

        std::printf("dynamic correctness: static-after-draw=%s, move-applied=%s, move-survives-draw=%s\n\n",
                    staticOk ? "OK" : "FAIL", moveTookEffect ? "OK" : "FAIL", movedPreserved ? "OK" : "FAIL");
    }

    // (2a) PERF — pure override-draw refreshes on a STATIC scene (no transform changes).
    clipParent->drawChildren(renderOrigin);            // warm up (clean any remaining dirty)
    Node::recalculateMatrixCalls = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < framesK; ++f) {
        clipParent->drawChildren(renderOrigin);
    }
    auto t1 = std::chrono::steady_clock::now();
    int64_t recalcRefresh = Node::recalculateMatrixCalls;
    double msRefresh = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // (2b) PERF — refresh + per-frame world queries on all nodes (hit-test-like reads interleaved).
    clipParent->drawChildren(renderOrigin);
    Node::recalculateMatrixCalls = 0;
    auto t2 = std::chrono::steady_clock::now();
    for (int f = 0; f < framesK; ++f) {
        clipParent->drawChildren(renderOrigin);
        for (size_t i = 0; i < N; ++i) {
            TransformMatrix w = allUnderClip[i]->worldTransform();
            sink += w[12] + w[13];
        }
    }
    auto t3 = std::chrono::steady_clock::now();
    int64_t recalcQuery = Node::recalculateMatrixCalls;
    double msQuery = std::chrono::duration<double, std::milli>(t3 - t2).count();

    std::printf("over %d static refresh frames (%zu nodes):\n", framesK, N);
    std::printf("  refresh only        : recalcMatrixCalls=%lld (%.2f/frame), %.3f ms total, %.4f ms/frame\n",
                (long long)recalcRefresh, double(recalcRefresh)/framesK, msRefresh, msRefresh/framesK);
    std::printf("  refresh + world queries: recalcMatrixCalls=%lld (%.2f/frame), %.3f ms total, %.4f ms/frame\n",
                (long long)recalcQuery, double(recalcQuery)/framesK, msQuery, msQuery/framesK);
    std::printf("\n[sink=%.3f]\n", (double)sink);
    return 0;
}
