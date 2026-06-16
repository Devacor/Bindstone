// Bindstone (MV) texture-atlas UV regression tests, using JaiScript's Foundry framework.
// Guards the half-texel inset in TexturePack::percentBounds that prevents atlas bleed
// ("shows pixels from one over") under GL_LINEAR filtering with edge-to-edge packing.
// Pure CPU UV math — no GPU needed, so it runs headless.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Render/render.h"
#include "MV/Render/texturePacker.h"
#include "MV/Render/textures.h"

#include <memory>

using namespace jai::foundry;
using namespace MV;

namespace mv_tests {

class atlas_suite : public suite {
public:
    atlas_suite() : suite("MV Texture Atlas") {}

    void forge_tests() override {
        test("percentBounds applies a half-texel inset on every edge (atlas bleed guard)", [] {
            Draw2D renderer; renderer.makeHeadless();
            auto pack = TexturePack::make("test_pack", &renderer, Color(0.0f, 0.0f, 0.0f, 0.0f), Size<int>(256, 256));

            // One 32x32 entry sets contentExtent -> roundUpPowerOfTwo(32) = 32 texels per axis.
            auto tex = DynamicTextureDefinition::make("a", Size<int>(32, 32), Color());
            check(pack->add("a", tex), "TexturePack::add should succeed for a 32x32 entry");

            // A degenerate zero-rect exposes exactly the inset amount (0.5 / POT) per axis.
            auto inset = pack->percentBounds(BoxAABB<int>(Point<int>(), Point<int>()));
            const PointPrecision halfU = inset.minPoint.x;
            const PointPrecision halfV = inset.minPoint.y;
            check(halfU > 0.0f && halfV > 0.0f,
                  "percentBounds must inset by a positive half-texel — without it, exact-edge UVs bleed the neighbour");

            // For a [0,32] sub-rect: min edge pulled in by +half, max edge by -half.
            // One texel == 2*half, so 32 texels == 64*half; max edge == 64*half - half == 63*half.
            auto uv = pack->percentBounds(BoxAABB<int>(Point<int>(0, 0), Point<int>(32, 32)));
            check_near(halfU, uv.minPoint.x, 1e-5f);
            check_near(halfV, uv.minPoint.y, 1e-5f);
            check_near(63.0f * halfU, uv.maxPoint.x, 1e-4f);
            check_near(63.0f * halfV, uv.maxPoint.y, 1e-4f);
            check(uv.maxPoint.x > uv.minPoint.x && uv.maxPoint.y > uv.minPoint.y,
                  "inset UV box must remain non-degenerate");
        });

        test("a packed shape's UVs are inset away from the atlas pixel boundary", [] {
            Draw2D renderer; renderer.makeHeadless();
            auto pack = TexturePack::make("test_pack", &renderer, Color(0.0f, 0.0f, 0.0f, 0.0f), Size<int>(256, 256));
            auto tex = DynamicTextureDefinition::make("a", Size<int>(32, 32), Color());
            pack->add("a", tex);

            // First entry lands at the atlas origin (0,0); its UVs must not start exactly at 0,
            // otherwise the bilinear edge tap reaches the neighbouring packed pixels.
            auto shapeUV = pack->percentBounds(pack->shape("a").bounds);
            check(shapeUV.minPoint.x > 0.0f && shapeUV.minPoint.y > 0.0f,
                  "packed shape UVs must be inset from the atlas edge");
        });
    }
};

} // namespace mv_tests

using atlas_suite = mv_tests::atlas_suite;
FOUNDRY_REGISTER(atlas_suite)
