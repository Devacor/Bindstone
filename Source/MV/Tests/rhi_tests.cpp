// Bindstone (MV) RHI handle-lifetime regression tests, using JaiScript's Foundry framework.
// Exercises the generation-checked HandlePool behind MV::Render::Device via the HeadlessDevice
// null backend (no GPU needed): create -> a valid handle; destroy -> the slot recycles with a
// bumped generation so the prior handle is detectably stale; the null/default handle is never
// valid; null/double destroys are harmless. Built into mv_tests.exe (see build_tests.bat);
// auto-registers with the Foundry runner.

#include <jaiscript/testing/foundry.hpp>
#include "MV/Render/device.h"

#include <memory>
#include <string>

using namespace jai::foundry;
using namespace MV::Render;

namespace mv_tests {

class rhi_suite : public suite {
public:
    rhi_suite() : suite("MV RHI Handles") {}

    void forge_tests() override {
        test("makeHeadless yields an initialized null backend", [] {
            auto dev = Device::makeHeadless();
            check(dev != nullptr, "makeHeadless returned null");
            check(dev->initialize(SwapchainDesc{}), "headless initialize() should succeed");
            check(dev->caps().deviceName.find("Headless") != std::string::npos,
                  "headless caps deviceName should mention 'Headless'");
        });

        test("a fresh handle is null; a created handle is valid", [] {
            auto dev = Device::makeHeadless();
            dev->initialize(SwapchainDesc{});
            BoundTexture none;
            check(!none.valid(), "default-constructed handle must be invalid");
            check(none.index == 0 && none.generation == 0, "null handle must be {0,0}");
            BoundTexture tex = dev->createTexture(TextureDesc{}, nullptr);
            check(tex.valid(), "createTexture must return a valid handle");
            check(tex.index != 0 && tex.generation != 0, "a valid handle must be non-null");
        });

        test("distinct creates yield distinct handles", [] {
            auto dev = Device::makeHeadless();
            dev->initialize(SwapchainDesc{});
            BoundTexture a = dev->createTexture(TextureDesc{}, nullptr);
            BoundTexture b = dev->createTexture(TextureDesc{}, nullptr);
            check(a != b, "two live handles must differ");
        });

        test("destroy recycles the slot with a bumped generation (stale handle is detectable)", [] {
            auto dev = Device::makeHeadless();
            dev->initialize(SwapchainDesc{});
            BoundTexture a = dev->createTexture(TextureDesc{}, nullptr);
            dev->destroyTexture(a);
            BoundTexture b = dev->createTexture(TextureDesc{}, nullptr);
            check(b.valid(), "the recycled handle must be valid");
            check(b.index == a.index, "the LIFO free list should recycle the same slot index");
            check(b.generation != a.generation, "a recycled slot must bump its generation");
            check(a != b, "the stale handle must not equal the live recycled handle");
        });

        test("each resource type has an independent pool", [] {
            auto dev = Device::makeHeadless();
            dev->initialize(SwapchainDesc{});
            BoundBuffer  buf = dev->createBuffer(BufferUsage::Vertex, BufferUpdateHint::Static, nullptr, 0);
            BoundTexture tex = dev->createTexture(TextureDesc{}, nullptr);
            BoundSampler smp = dev->createSampler(SamplerDesc{});
            check(buf.valid() && tex.valid() && smp.valid(), "each pool must hand out a valid handle");
        });

        test("null and double destroys are harmless", [] {
            auto dev = Device::makeHeadless();
            dev->initialize(SwapchainDesc{});
            dev->destroyTexture(BoundTexture{});               // destroy the null handle
            BoundTexture a = dev->createTexture(TextureDesc{}, nullptr);
            dev->destroyTexture(a);
            dev->destroyTexture(a);                             // double-destroy: no-op
            dev->updateBuffer(BoundBuffer{}, nullptr, 0, 0);   // inert call, accepts a null handle
            BoundTexture b = dev->createTexture(TextureDesc{}, nullptr);
            check(b.valid(), "the device must still be usable after null/double destroys");
        });
    }
};

} // namespace mv_tests

using rhi_suite = mv_tests::rhi_suite;
FOUNDRY_REGISTER(rhi_suite)
