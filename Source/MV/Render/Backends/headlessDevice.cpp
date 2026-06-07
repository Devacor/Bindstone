/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

#include "MV/Render/Backends/headlessDevice.h"

#include <memory>

namespace MV {
	namespace Render {

		std::unique_ptr<Device> Device::makeHeadless() {
			return std::make_unique<HeadlessDevice>();
		}

		HeadlessDevice::HeadlessDevice() {
			deviceCaps.deviceName = "Headless (null backend)";
			deviceCaps.clipSpace = ClipSpace::GL_NegOneToOne;
			deviceCaps.flipViewportY = false;
		}

		HeadlessDevice::~HeadlessDevice() {
		}

		bool HeadlessDevice::initialize(const SwapchainDesc&) { return true; }
		void HeadlessDevice::shutdown() {
			buffers.clear(); textures.clear(); samplers.clear();
			shaderModules.clear(); pipelines.clear(); renderTargets.clear(); uniformSets.clear();
		}
		const DeviceCaps& HeadlessDevice::caps() const { return deviceCaps; }

		BoundBuffer HeadlessDevice::createBuffer(BufferUsage, BufferUpdateHint, const void*, size_t) { return buffers.create({}); }
		void        HeadlessDevice::updateBuffer(BoundBuffer, const void*, size_t, size_t) {}
		void        HeadlessDevice::destroyBuffer(BoundBuffer a_handle) { Empty e; buffers.remove(a_handle, e); }

		BoundTexture HeadlessDevice::createTexture(const TextureDesc&, const void*) { return textures.create({}); }
		void         HeadlessDevice::updateTexture(BoundTexture, int, const Rect&, const void*) {}
		void         HeadlessDevice::generateMips(BoundTexture) {}
		void         HeadlessDevice::destroyTexture(BoundTexture a_handle) { Empty e; textures.remove(a_handle, e); }
		void         HeadlessDevice::readTexture(BoundTexture, std::vector<uint8_t>&) {}

		BoundSampler      HeadlessDevice::createSampler(const SamplerDesc&) { return samplers.create({}); }
		BoundShaderModule HeadlessDevice::createShaderModule(const ShaderModuleDesc&) { return shaderModules.create({}); }
		BoundPipeline     HeadlessDevice::createPipeline(const PipelineDesc&) { return pipelines.create({}); }
		void              HeadlessDevice::destroyPipeline(BoundPipeline a_handle) { Empty e; pipelines.remove(a_handle, e); }
		BoundRenderTarget HeadlessDevice::createRenderTarget(const RenderTargetDesc&) { return renderTargets.create({}); }
		void              HeadlessDevice::destroyRenderTarget(BoundRenderTarget a_handle) { Empty e; renderTargets.remove(a_handle, e); }

		BoundUniformSet HeadlessDevice::createUniformSet(BoundPipeline) { return uniformSets.create({}); }
		void HeadlessDevice::setUniform(BoundUniformSet, const std::string&, const float*, uint32_t) {}
		void HeadlessDevice::setUniformMatrix(BoundUniformSet, const std::string&, const float*) {}
		void HeadlessDevice::setTexture(BoundUniformSet, const std::string&, BoundTexture, BoundSampler) {}

		void HeadlessDevice::beginFrame() {}
		void HeadlessDevice::beginPass(BoundRenderTarget, const RenderTargetDesc&) {}
		void HeadlessDevice::beginDefaultPass(const float[4]) {}
		void HeadlessDevice::setViewport(const Viewport&) {}
		void HeadlessDevice::setScissor(const Rect&) {}
		void HeadlessDevice::setStencilState(const StencilState&) {}
		void HeadlessDevice::clearStencil(uint8_t) {}
		void HeadlessDevice::draw(const DrawItem&) {}
		void HeadlessDevice::endPass() {}
		void HeadlessDevice::endFrame() {}
		void HeadlessDevice::onSurfaceResized(int, int) {}

	} // namespace Render
} // namespace MV
