/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

// BatchingDevice — the Device Draw2D hands out. It forwards every call to the backend it wraps and counts the draws
// the backend issues each frame.

#ifndef _MV_RENDER_BATCHINGDEVICE_H_
#define _MV_RENDER_BATCHINGDEVICE_H_

#include "MV/Render/device.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MV {
	namespace Render {

		struct DrawStatistics {
			uint32_t draws = 0;          // draws the backend issued
		};

		class BatchingDevice : public Device {
		public:
			explicit BatchingDevice(Device& a_backend);
			~BatchingDevice() override;

			const DrawStatistics& frame() const { return currentFrame; }
			const DrawStatistics& lastFrame() const { return previousFrame; }

			Device& backend() { return wrapped; }

			bool initialize(const SwapchainDesc& a_desc) override { return wrapped.initialize(a_desc); }
			void shutdown() override { wrapped.shutdown(); }
			const DeviceCaps& caps() const override { return wrapped.caps(); }

			BoundBuffer createBuffer(BufferUsage a_usage, BufferUpdateHint a_hint, const void* a_data, size_t a_bytes) override { return wrapped.createBuffer(a_usage, a_hint, a_data, a_bytes); }
			void        updateBuffer(BoundBuffer a_buffer, const void* a_data, size_t a_bytes, size_t a_offset) override { wrapped.updateBuffer(a_buffer, a_data, a_bytes, a_offset); }
			void        destroyBuffer(BoundBuffer a_buffer) override { wrapped.destroyBuffer(a_buffer); }

			BoundTexture createTexture(const TextureDesc& a_desc, const void* a_initialPixels) override { return wrapped.createTexture(a_desc, a_initialPixels); }
			void         updateTexture(BoundTexture a_texture, int a_mip, const Rect& a_rect, const void* a_pixels) override { wrapped.updateTexture(a_texture, a_mip, a_rect, a_pixels); }
			void         generateMips(BoundTexture a_texture) override { wrapped.generateMips(a_texture); }
			void         destroyTexture(BoundTexture a_texture) override { wrapped.destroyTexture(a_texture); }
			void         readTexture(BoundTexture a_texture, std::vector<uint8_t>& a_out) override { wrapped.readTexture(a_texture, a_out); }
			uint32_t     nativeTextureId(BoundTexture a_texture) const override { return wrapped.nativeTextureId(a_texture); }

			BoundSampler      createSampler(const SamplerDesc& a_desc) override { return wrapped.createSampler(a_desc); }
			BoundShaderModule createShaderModule(const ShaderModuleDesc& a_desc) override { return wrapped.createShaderModule(a_desc); }
			BoundPipeline     createPipeline(const PipelineDesc& a_desc) override { return wrapped.createPipeline(a_desc); }
			void              destroyPipeline(BoundPipeline a_pipeline) override { wrapped.destroyPipeline(a_pipeline); }
			BoundRenderTarget createRenderTarget(const RenderTargetDesc& a_desc) override { return wrapped.createRenderTarget(a_desc); }
			void              destroyRenderTarget(BoundRenderTarget a_target) override { wrapped.destroyRenderTarget(a_target); }

			BoundUniformSet createUniformSet(BoundPipeline a_pipeline) override { return wrapped.createUniformSet(a_pipeline); }
			void destroyUniformSet(BoundUniformSet a_set) override { wrapped.destroyUniformSet(a_set); }
			void setUniform(BoundUniformSet a_set, const std::string& a_name, const float* a_data, uint32_t a_floatCount) override { wrapped.setUniform(a_set, a_name, a_data, a_floatCount); }
			void setUniformMatrix(BoundUniformSet a_set, const std::string& a_name, const float* a_columnMajor16) override { wrapped.setUniformMatrix(a_set, a_name, a_columnMajor16); }
			void setTexture(BoundUniformSet a_set, const std::string& a_name, BoundTexture a_texture, BoundSampler a_sampler) override { wrapped.setTexture(a_set, a_name, a_texture, a_sampler); }

			void beginFrame() override { wrapped.beginFrame(); }
			void beginPass(BoundRenderTarget a_target, const RenderTargetDesc& a_clearInfo) override { wrapped.beginPass(a_target, a_clearInfo); }
			void beginDefaultPass(const float a_clearColor[4]) override { wrapped.beginDefaultPass(a_clearColor); }
			void setViewport(const Viewport& a_viewport) override { wrapped.setViewport(a_viewport); }
			void setScissor(const Rect& a_rect) override { wrapped.setScissor(a_rect); }
			void setStencilState(const StencilState& a_state) override { wrapped.setStencilState(a_state); }
			void clearStencil(uint8_t a_value) override { wrapped.clearStencil(a_value); }
			void draw(const DrawItem& a_item) override;
			void endPass() override { wrapped.endPass(); }
			void endFrame() override;
			void onSurfaceResized(int a_drawableWidthPixels, int a_drawableHeightPixels) override { wrapped.onSurfaceResized(a_drawableWidthPixels, a_drawableHeightPixels); }

		private:
			Device& wrapped;

			DrawStatistics currentFrame;
			DrawStatistics previousFrame;
		};

	}
}

#endif
