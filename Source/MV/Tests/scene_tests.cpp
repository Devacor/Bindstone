// Bindstone (MV) scene-graph transform regression tests, using JaiScript's Foundry framework.
// Guards the correctness + perf invariants of the Clipped override-draw dirty-flag optimization:
//   - an override (Clipped) draw must NOT pollute persistent world transforms
//   - dynamic transform changes still apply and survive override draws
//   - PERF GATE: a static clipped subtree must not recompute matrices every frame
//   - worldTransform equals the manual chain of ancestor local transforms (structural sanity)
// Built into mv_tests.exe (see build_tests.bat); links the real engine lib; auto-registers.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Render/render.h"
#include "MV/Render/Scene/node.h"

#include <random>
#include <cstring>
#include <vector>
#include <memory>
#include <array>
#include <string>

using namespace jai::foundry;
using namespace MV;
using MV::Scene::Node;

namespace mv_tests {

static bool sameMatrix(const Matrix<4, 4>& a, const Matrix<4, 4>& b) {
    return std::memcmp(a.getMatrixArray().data(), b.getMatrixArray().data(), 16 * sizeof(float)) == 0;
}

static void buildClipSubtree(std::shared_ptr<Node> parent, int count, int& idc,
                             std::vector<std::shared_ptr<Node>>& all) {
    std::vector<std::shared_ptr<Node>> frontier{ parent };
    int made = 0; size_t cur = 0;
    while (made < count) {
        auto p = frontier[cur % frontier.size()]; ++cur;
        for (int k = 0; k < 4 && made < count; ++k) {
            auto n = p->make("n" + std::to_string(idc++));
            n->position(Point<>(float((made % 11) - 5) * 1.4f, float((made % 7) - 3) * 2.0f, 0.3f));
            n->rotation(AxisAngles(float(made % 80), float(made % 50), float(made % 20)));
            n->scale(Scale(1.1f, 0.9f, 1.0f));
            all.push_back(n); frontier.push_back(n); ++made;
        }
    }
}

class scene_suite : public suite {
public:
    scene_suite() : suite("MV Scene Transform") {}

    void forge_tests() override {
        test("Clipped override draw does not pollute persistent world transforms", [] {
            Draw2D r; r.makeHeadless();
            auto root = Node::make(r, "root");
            root->position(Point<>(3, -2, 1)); root->rotation(AxisAngles(5, 10, 15));
            auto clip = root->make("clip"); clip->position(Point<>(100, 50, 0));
            int idc = 0; std::vector<std::shared_ptr<Node>> all;
            buildClipSubtree(clip, 200, idc, all);          // 2+ levels deep -> exercises grandchild pollution path
            TransformMatrix origin; origin.translate(Point<>(-100, -50, 0));

            std::vector<std::array<float, 16>> before(all.size());
            for (size_t i = 0; i < all.size(); ++i) {
                TransformMatrix w = all[i]->worldTransform();
                std::memcpy(before[i].data(), w.getMatrixArray().data(), 16 * sizeof(float));
            }
            clip->drawChildren(origin);
            for (size_t i = 0; i < all.size(); ++i) {
                TransformMatrix w = all[i]->worldTransform();
                check(std::memcmp(before[i].data(), w.getMatrixArray().data(), 16 * sizeof(float)) == 0,
                      "override draw polluted the world transform of node " + std::to_string(i));
            }
        });

        test("dynamic transform changes apply and survive override draws", [] {
            Draw2D r; r.makeHeadless();
            auto root = Node::make(r, "root");
            auto clip = root->make("clip"); clip->position(Point<>(100, 50, 0));
            int idc = 0; std::vector<std::shared_ptr<Node>> all;
            buildClipSubtree(clip, 120, idc, all);
            TransformMatrix origin; origin.translate(Point<>(-100, -50, 0));

            auto probe = all[all.size() / 2];
            TransformMatrix refClean = probe->worldTransform();
            for (int k = 0; k < 5; ++k) clip->drawChildren(origin);
            check(sameMatrix(probe->worldTransform(), refClean), "static node drifted across override draws");

            probe->position(probe->position() + Point<>(17, -23, 5));
            TransformMatrix movedClean = probe->worldTransform();
            for (int k = 0; k < 5; ++k) clip->drawChildren(origin);
            check(sameMatrix(probe->worldTransform(), movedClean), "moved node not preserved through override draws");
            check(!sameMatrix(movedClean, refClean), "move did not take effect");
        });

        test("PERF GATE: static clipped subtree does not recompute matrices every frame", [] {
            Draw2D r; r.makeHeadless();
            auto root = Node::make(r, "root");
            auto clip = root->make("clip"); clip->position(Point<>(100, 50, 0));
            int idc = 0; std::vector<std::shared_ptr<Node>> all;
            buildClipSubtree(clip, 300, idc, all);
            TransformMatrix origin; origin.translate(Point<>(-100, -50, 0));

            clip->drawChildren(origin);                                  // warm up (settle dirty flags)
            int64_t base = Node::recalculateMatrixCalls;
            for (int f = 0; f < 500; ++f) clip->drawChildren(origin);
            int64_t recomputes = Node::recalculateMatrixCalls - base;
            check_eq((int64_t)0, recomputes,
                     "static clipped subtree recomputed world matrices over 500 idle frames "
                     "(dirty-flag regression: the override draw is re-dirtying or rebuilding the world matrix)");
        });

        test("worldTransform equals the manual chain of ancestor local transforms", [] {
            Draw2D r; r.makeHeadless();
            auto root = Node::make(r, "root"); root->position(Point<>(2, 3, 1)); root->rotation(AxisAngles(11, 22, 33));
            auto a = root->make("a"); a->position(Point<>(5, -4, 2)); a->rotation(AxisAngles(7, 0, 0)); a->scale(Scale(1.5f, 2.0f, 1.0f));
            auto b = a->make("b"); b->position(Point<>(-3, 6, 0)); b->rotation(AxisAngles(0, 9, 0));
            auto c = b->make("c"); c->position(Point<>(1, 1, 1)); c->rotation(AxisAngles(0, 0, 13)); c->scale(Scale(0.5f, 0.5f, 0.5f));

            TransformMatrix manual = affineMultiply(
                affineMultiply(affineMultiply(root->localTransform(), a->localTransform()), b->localTransform()),
                c->localTransform());
            check(sameMatrix(manual, c->worldTransform()),
                  "worldTransform diverged from the manual ancestor-chain product");
        });
    }
};

} // namespace mv_tests

using scene_suite = mv_tests::scene_suite;
FOUNDRY_REGISTER(scene_suite)
