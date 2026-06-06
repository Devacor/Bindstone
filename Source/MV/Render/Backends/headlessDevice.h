/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

// HeadlessDevice — the null implementation of MV::Render::Device (mv_tests, benchmarks). No GPU
// work, but it hands out real generation-checked handles so resource-lifetime logic stays testable.

#ifndef _MV_RENDER_HEADLESSDEVICE_H_
#define _MV_RENDER_HEADLESSDEVICE_H_

#include "MV/Render/device.h"
#include "MV/Render/Backends/handlePool.h"

namespace MV {
	namespace Render {

		class HeadlessDevice : public Device {
		public:
			HeadlessDevice();
			~HeadlessDevice() override;

			bool initialize(const SwapchainDesc&) override;
			void shutdown() override;
			const DeviceCaps& caps() const override;

			BoundBuffer createBuffer(BufferUsage, BufferUpdateHint, const void* data, size_t bytes) override;
			void        updateBuffer(BoundBuffer, const void* data, size_t bytes, size_t offset) override;
			void        destroyBuffer(BoundBuffer) override;

			BoundTexture createTexture(const TextureDesc&, const void* initialPixels) override;
			void         updateTexture(BoundTexture, int mip, const Rect&, const void* pixels) override;
			void         generateMips(BoundTexture) override;
			void         destroyTexture(BoundTexture) override;
			void         readTexture(BoundTexture, std::vector<uint8_t>& out) override;

			BoundSampler      createSampler(const SamplerDesc&) override;
			BoundShaderModule createShaderModule(const ShaderModuleDesc&) override;
			BoundPipeline     createPipeline(const PipelineDesc&) override;
			void              destroyPipeline(BoundPipeline) override;
			BoundRenderTarget createRenderTarget(const RenderTargetDesc&) override;
			void              destroyRenderTarget(BoundRenderTarget) override;

			BoundUniformSet createUniformSet(BoundPipeline) override;
			void setUniform(BoundUniformSet, const std::string& name, const float* data, uint32_t floatCount) override;
			void setUniformMatrix(BoundUniformSet, const std::string& name, const float* columnMajor16) override;
			void setTexture(BoundUniformSet, const std::string& name, BoundTexture, BoundSampler) override;

			void beginFrame() override;
			void beginPass(BoundRenderTarget, const RenderTargetDesc& clearInfo) override;
			void beginDefaultPass(const float clearColor[4]) override;
			void setViewport(const Viewport&) override;
			void setScissor(const Rect&) override;
			void draw(const DrawItem&) override;
			void endPass() override;
			void endFrame() override;
			void onSurfaceResized(int drawableWidthPixels, int drawableHeightPixels) override;

		private:
			// No native object; only the handle's {index, generation} identity matters.
			struct Empty {};
			HandlePool<BoundBuffer,       Empty> buffers;
			HandlePool<BoundTexture,      Empty> textures;
			HandlePool<BoundSampler,      Empty> samplers;
			HandlePool<BoundShaderModule, Empty> shaderModules;
			HandlePool<BoundPipeline,     Empty> pipelines;
			HandlePool<BoundRenderTarget, Empty> renderTargets;
			HandlePool<BoundUniformSet,   Empty> uniformSets;

			DeviceCaps deviceCaps;
		};

	} // namespace Render
} // namespace MV

#endif
