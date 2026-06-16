/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

// GLDevice — the OpenGL reference implementation of MV::Render::Device, the A/B baseline
// for the Vulkan/Metal bring-up.

#ifndef _MV_RENDER_GLDEVICE_H_
#define _MV_RENDER_GLDEVICE_H_

#include "MV/Render/device.h"

#include <memory>

namespace MV {
	namespace Render {

		class GLDevice : public Device {
		public:
			GLDevice();
			~GLDevice() override;

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
			uint32_t     nativeTextureId(BoundTexture) const override;

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
			void setStencilState(const StencilState&) override;
			void clearStencil(uint8_t value) override;
			void draw(const DrawItem&) override;
			void endPass() override;
			void endFrame() override;
			void onSurfaceResized(int drawableWidthPixels, int drawableHeightPixels) override;

		private:
			void* sdlWindow = nullptr; // SDL_Window* held opaquely so this header stays GL/SDL-free.
			DeviceCaps deviceCaps;
			// GL object tables, PIMPL'd so the GLuint/GLenum payloads stay out of this header.
			struct State;
			std::unique_ptr<State> state;
		};

	} // namespace Render
} // namespace MV

#endif
