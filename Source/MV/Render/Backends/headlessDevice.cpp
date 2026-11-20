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
		bool              HeadlessDevice::readsUniform(BoundPipeline, const std::string&) { return true; }
		BoundRenderTarget HeadlessDevice::createRenderTarget(const RenderTargetDesc&) { return renderTargets.create({}); }
		void              HeadlessDevice::destroyRenderTarget(BoundRenderTarget a_handle) { Empty e; renderTargets.remove(a_handle, e); }

		BoundUniformSet HeadlessDevice::createUniformSet(BoundPipeline) { return uniformSets.create({}); }
		void HeadlessDevice::destroyUniformSet(BoundUniformSet a_handle) { UniformValues released; uniformSets.remove(a_handle, released); }
		void HeadlessDevice::setUniform(BoundUniformSet a_handle, const std::string& a_name, const float* a_data, uint32_t a_floatCount) {
			if (auto* set = uniformSets.get(a_handle)) {
				set->values[a_name].assign(a_data, a_data + a_floatCount);
			}
		}
		void HeadlessDevice::setUniformMatrix(BoundUniformSet a_handle, const std::string& a_name, const float* a_columnMajor16) {
			setUniform(a_handle, a_name, a_columnMajor16, 16);
		}
		void HeadlessDevice::setTexture(BoundUniformSet, const std::string&, BoundTexture, BoundSampler) {}

		void HeadlessDevice::beginFrame() {}
		void HeadlessDevice::beginPass(BoundRenderTarget, const RenderTargetDesc&) {}
		void HeadlessDevice::beginDefaultPass(const float[4]) {}
		void HeadlessDevice::setViewport(const Viewport&) {}
		void HeadlessDevice::setScissor(const Rect&) {}
		void HeadlessDevice::setStencilState(const StencilState&) {}
		void HeadlessDevice::clearStencil(uint8_t) {}
		void HeadlessDevice::draw(const DrawItem& a_item) {
			if (!recordingDraws) {
				return;
			}
			DrawSeen seen;
			if (auto* set = uniformSets.get(a_item.uniforms)) {
				seen.uniforms = set->values;
			}
			seen.indexCount = a_item.indexCount;
			seenDraws.push_back(std::move(seen));
		}
		void HeadlessDevice::endPass() {}
		void HeadlessDevice::endFrame() {}
		void HeadlessDevice::onSurfaceResized(int, int) {}

	} // namespace Render
} // namespace MV
