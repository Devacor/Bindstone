// End-to-end per-frame scene-graph benchmark exercising BOTH optimizations together:
//   - affineMultiply in Node::recalculateMatrix (every dirty node's world recompute)
//   - the Clipped override-draw dirty-flag fix (static nodes under a clip skip recompute)
//
// Models a realistic frame: a "world" subtree drawn the normal way (worldTransform() queried per
// drawable node) plus a "UI" subtree refreshed through the Clipped override path
// (drawChildren(renderOrigin)). Each frame, a configurable FRACTION of nodes are animated
// (transform changes -> markMatrixDirty), the rest are static.
//
// Build: build_frame.bat <libdir>   (links the engine lib from <libdir>; default = optimized tree)
// Run:   bench_frame.exe [frames] [worldNodes] [uiNodes] [animatedPct]

#include "MV/Render/render.h"
#include "MV/Render/Scene/node.h"

#include <cstdio>
#include <cstdint>
#include <vector>
#include <memory>
#include <chrono>
#include <string>

using namespace MV;
using MV::Scene::Node;

static volatile double sink = 0.0;

// Build a balanced-ish subtree of about `count` nodes under `parent` (fanout 4), collecting them.
static void buildSubtree(std::shared_ptr<Node> parent, int count, int& idCounter,
                         std::vector<std::shared_ptr<Node>>& all) {
    std::vector<std::shared_ptr<Node>> frontier{ parent };
    int made = 0;
    size_t cursor = 0;
    while (made < count) {
        auto p = frontier[cursor % frontier.size()];
        ++cursor;
        for (int k = 0; k < 4 && made < count; ++k) {
            auto n = p->make("n" + std::to_string(idCounter++));
            n->position(Point<>(float((made % 13) - 6) * 1.3f, float((made % 7) - 3) * 2.1f, 0.2f));
            n->rotation(AxisAngles(float(made % 90), float(made % 45), float(made % 30)));
            n->scale(Scale(1.05f, 0.95f, 1.0f));
            all.push_back(n);
            frontier.push_back(n);
            ++made;
        }
    }
}

int main(int argc, char** argv) {
    int frames     = (argc > 1) ? std::atoi(argv[1]) : 1000;
    int worldNodes = (argc > 2) ? std::atoi(argv[2]) : 1500;
    int uiNodes    = (argc > 3) ? std::atoi(argv[3]) : 500;
    int animatedPct= (argc > 4) ? std::atoi(argv[4]) : 8;     // % of nodes animated each frame

    Draw2D renderer;
    renderer.makeHeadless();

    auto root = Node::make(renderer, "root");
    int idc = 0;

    auto worldRoot = root->make("world");
    std::vector<std::shared_ptr<Node>> worldAll;
    buildSubtree(worldRoot, worldNodes, idc, worldAll);

    auto uiClip = root->make("uiClip");          // stands in for a Clipped node's owner
    uiClip->position(Point<>(200.0f, 120.0f, 0.0f));
    std::vector<std::shared_ptr<Node>> uiAll;
    buildSubtree(uiClip, uiNodes, idc, uiAll);

    TransformMatrix renderOrigin;
    renderOrigin.translate(Point<>(-200.0f, -120.0f, 0.0f));

    // Pick the animated subset (every Nth node across both subtrees).
    std::vector<std::shared_ptr<Node>> animated;
    int stride = animatedPct > 0 ? std::max(1, 100 / animatedPct) : 1000000;
    for (size_t i = 0; i < worldAll.size(); ++i) if ((int)(i % stride) == 0) animated.push_back(worldAll[i]);
    for (size_t i = 0; i < uiAll.size(); ++i)    if ((int)(i % stride) == 0) animated.push_back(uiAll[i]);

    auto runFrames = [&](int n)->int64_t {
        Node::recalculateMatrixCalls = 0;
        for (int f = 0; f < n; ++f) {
            // 1) animate a fraction of nodes (transform churn -> markMatrixDirty)
            float a = float(f) * 0.01f;
            for (auto& nd : animated) nd->rotation(AxisAngles(a, a * 0.5f, a * 0.25f));
            // 2) normal draw: every drawable world node needs its world matrix
            for (auto& nd : worldAll) { auto w = nd->worldTransform(); sink += w[12]; }
            // 3) UI clip refresh through the override-parent path
            uiClip->drawChildren(renderOrigin);
        }
        return Node::recalculateMatrixCalls;
    };

    runFrames(50);                       // warmup
    auto t0 = std::chrono::steady_clock::now();
    int64_t recalcs = runFrames(frames);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    size_t total = worldAll.size() + uiAll.size();
    std::printf("scene: %zu world + %zu ui = %zu nodes, %zu animated/frame (%d%%)\n",
                worldAll.size(), uiAll.size(), total, animated.size(), animatedPct);
    std::printf("over %d frames: %.3f ms total, %.4f ms/frame, recalcMatrixCalls=%lld (%.0f/frame)\n",
                frames, ms, ms / frames, (long long)recalcs, double(recalcs) / frames);
    std::printf("[sink=%.3f]\n", (double)sink);
    return 0;
}
