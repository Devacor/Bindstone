/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

// VulkanDevice — the native Vulkan implementation of MV::Render::Device. Resources + clear/present
// frame loop are live; shaders/pipelines/uniform sets/recorded draws still fail loudly (Phase 3).
//
// No link-time Vulkan dependency: VK_NO_PROTOTYPES + every entry point loaded through SDL's loader,
// so makeVulkan()/initialize() degrade gracefully (and Draw2D stays on GL) when Vulkan is absent.

#include "MV/Render/device.h"

#if __has_include(<vulkan/vulkan.h>)

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <SDL.h>
#include <SDL_vulkan.h>

#include "MV/Render/Backends/handlePool.h"
#include "MV/Utility/require.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

namespace MV {
	namespace Render {

		namespace {

			// ---- the function-pointer table (volk-style, but hand-rolled and minimal) -----
#define MV_VK_GLOBAL_FUNCS(X) \
	X(vkEnumerateInstanceExtensionProperties) \
	X(vkEnumerateInstanceLayerProperties) \
	X(vkCreateInstance)

#define MV_VK_INSTANCE_FUNCS(X) \
	X(vkDestroyInstance) \
	X(vkEnumeratePhysicalDevices) \
	X(vkGetPhysicalDeviceProperties) \
	X(vkGetPhysicalDeviceFeatures) \
	X(vkGetPhysicalDeviceMemoryProperties) \
	X(vkGetPhysicalDeviceQueueFamilyProperties) \
	X(vkGetPhysicalDeviceFormatProperties) \
	X(vkGetPhysicalDeviceSurfaceSupportKHR) \
	X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
	X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
	X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
	X(vkEnumerateDeviceExtensionProperties) \
	X(vkCreateDevice) \
	X(vkDestroySurfaceKHR) \
	X(vkGetDeviceProcAddr)

#define MV_VK_DEVICE_FUNCS(X) \
	X(vkDestroyDevice) \
	X(vkGetDeviceQueue) \
	X(vkDeviceWaitIdle) \
	X(vkQueueWaitIdle) \
	X(vkCreateSwapchainKHR) \
	X(vkDestroySwapchainKHR) \
	X(vkGetSwapchainImagesKHR) \
	X(vkAcquireNextImageKHR) \
	X(vkQueuePresentKHR) \
	X(vkQueueSubmit) \
	X(vkCreateImageView) \
	X(vkDestroyImageView) \
	X(vkCreateRenderPass) \
	X(vkDestroyRenderPass) \
	X(vkCreateFramebuffer) \
	X(vkDestroyFramebuffer) \
	X(vkCreateCommandPool) \
	X(vkDestroyCommandPool) \
	X(vkAllocateCommandBuffers) \
	X(vkFreeCommandBuffers) \
	X(vkBeginCommandBuffer) \
	X(vkEndCommandBuffer) \
	X(vkResetCommandBuffer) \
	X(vkCmdBeginRenderPass) \
	X(vkCmdEndRenderPass) \
	X(vkCmdSetViewport) \
	X(vkCmdSetScissor) \
	X(vkCmdPipelineBarrier) \
	X(vkCmdCopyBufferToImage) \
	X(vkCmdCopyImageToBuffer) \
	X(vkCmdCopyBuffer) \
	X(vkCmdBlitImage) \
	X(vkCreateSemaphore) \
	X(vkDestroySemaphore) \
	X(vkCreateFence) \
	X(vkDestroyFence) \
	X(vkWaitForFences) \
	X(vkResetFences) \
	X(vkCreateImage) \
	X(vkDestroyImage) \
	X(vkGetImageMemoryRequirements) \
	X(vkBindImageMemory) \
	X(vkCreateBuffer) \
	X(vkDestroyBuffer) \
	X(vkGetBufferMemoryRequirements) \
	X(vkBindBufferMemory) \
	X(vkAllocateMemory) \
	X(vkFreeMemory) \
	X(vkMapMemory) \
	X(vkUnmapMemory) \
	X(vkCreateSampler) \
	X(vkDestroySampler)

			struct VulkanFns {
#define MV_VK_DECL(n) PFN_##n n = nullptr;
				MV_VK_GLOBAL_FUNCS(MV_VK_DECL)
				MV_VK_INSTANCE_FUNCS(MV_VK_DECL)
				MV_VK_DEVICE_FUNCS(MV_VK_DECL)
#undef MV_VK_DECL
			};

			// ---- format / sampler enum mapping -----------------------------------------
			VkFormat toVkFormat(PixelFormat a_format) {
				switch (a_format) {
				case PixelFormat::RGBA8_UNORM:       return VK_FORMAT_R8G8B8A8_UNORM;
				case PixelFormat::RGBA8_SRGB:        return VK_FORMAT_R8G8B8A8_SRGB;
				case PixelFormat::BGRA8_UNORM:       return VK_FORMAT_B8G8R8A8_UNORM;
				case PixelFormat::BGRA8_SRGB:        return VK_FORMAT_B8G8R8A8_SRGB;
				case PixelFormat::R8_UNORM:          return VK_FORMAT_R8_UNORM;
				case PixelFormat::RG8_UNORM:         return VK_FORMAT_R8G8_UNORM;
				case PixelFormat::RGBA16_FLOAT:      return VK_FORMAT_R16G16B16A16_SFLOAT;
				case PixelFormat::D16_UNORM:         return VK_FORMAT_D16_UNORM;
				case PixelFormat::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
				case PixelFormat::D32_FLOAT:         return VK_FORMAT_D32_SFLOAT;
				case PixelFormat::D32_FLOAT_S8_UINT: return VK_FORMAT_D32_SFLOAT_S8_UINT;
				default:                             return VK_FORMAT_R8G8B8A8_UNORM;
				}
			}
			uint32_t bytesPerPixel(PixelFormat a_format) {
				switch (a_format) {
				case PixelFormat::R8_UNORM:    return 1;
				case PixelFormat::RG8_UNORM:
				case PixelFormat::D16_UNORM:   return 2;
				case PixelFormat::RGBA16_FLOAT:return 8;
				default:                       return 4; // RGBA8/BGRA8/sRGB/D32/...
				}
			}
			VkFilter toVkFilter(Filter a_filter) {
				return (a_filter == Filter::Linear) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
			}
			VkSamplerAddressMode toVkAddress(AddressMode a_mode) {
				switch (a_mode) {
				case AddressMode::Repeat:        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
				case AddressMode::MirrorRepeat:  return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
				case AddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
				case AddressMode::ClampToEdge:
				default:                         return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				}
			}
			VkSamplerMipmapMode toVkMipmapMode(MipFilter a_mip) {
				return (a_mip == MipFilter::Linear) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
			}

			VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
				VkDebugUtilsMessageSeverityFlagBitsEXT a_severity,
				VkDebugUtilsMessageTypeFlagsEXT,
				const VkDebugUtilsMessengerCallbackDataEXT *a_data,
				void *) {
				if (a_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && a_data && a_data->pMessage) {
					std::fprintf(stderr, "[Vulkan] %s\n", a_data->pMessage);
				}
				return VK_FALSE;
			}

			// ---- resource payloads stored in the HandlePools ---------------------------
			struct BufferRes {
				VkBuffer       buffer = VK_NULL_HANDLE;
				VkDeviceMemory memory = VK_NULL_HANDLE;
				VkDeviceSize   size = 0;
				void          *mapped = nullptr;   // host-visible+coherent, persistently mapped.
			};
			struct TextureRes {
				VkImage        image = VK_NULL_HANDLE;
				VkDeviceMemory memory = VK_NULL_HANDLE;
				VkImageView    view = VK_NULL_HANDLE;
				VkFormat       format = VK_FORMAT_UNDEFINED;
				PixelFormat    pixelFormat = PixelFormat::RGBA8_UNORM;
				int            width = 0, height = 0;
				uint32_t       mipLevels = 1;
				VkImageLayout  layout = VK_IMAGE_LAYOUT_UNDEFINED;
			};
			struct SamplerRes {
				VkSampler sampler = VK_NULL_HANDLE;
			};

			constexpr uint32_t kFramesInFlight = 2;

			class VulkanDevice : public Device {
			public:
				VulkanDevice() = default;
				~VulkanDevice() override { shutdown(); }

				bool initialize(const SwapchainDesc&) override;
				void shutdown() override;
				const DeviceCaps& caps() const override { return deviceCaps; }

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
				bool loadGlobalFunctions();
				bool createInstance();
				bool pickPhysicalDevice();
				bool createLogicalDevice();
				bool createRenderPass();
				bool createFrameResources();          // command pool + per-frame command buffers + sync
				bool createSwapchain(int width, int height);
				void destroySwapchain();
				bool recreateSwapchain();
				void beginSwapchainPass(const float clearColor[4]);

				uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
				bool oneTimeSubmit(const std::function<void(VkCommandBuffer)>& record);
				void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
				                     uint32_t baseMip, uint32_t mipCount);

				VulkanFns fns{};
				PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
				PFN_vkCreateDebugUtilsMessengerEXT  pfnCreateDebug = nullptr;
				PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyDebug = nullptr;

				SwapchainDesc swapDesc{};
				SDL_Window *window = nullptr;
				bool libraryLoaded = false;
				bool validationEnabled = false;

				VkInstance instance = VK_NULL_HANDLE;
				VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
				VkSurfaceKHR surface = VK_NULL_HANDLE;
				VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
				VkPhysicalDeviceMemoryProperties memProps{};
				uint32_t graphicsFamily = 0, presentFamily = 0;
				VkDevice device = VK_NULL_HANDLE;
				VkQueue graphicsQueue = VK_NULL_HANDLE, presentQueue = VK_NULL_HANDLE;

				VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
				VkColorSpaceKHR swapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
				VkExtent2D swapchainExtent{ 0, 0 };
				VkSwapchainKHR swapchain = VK_NULL_HANDLE;
				std::vector<VkImage>       swapchainImages;
				std::vector<VkImageView>   swapchainViews;
				std::vector<VkFramebuffer> framebuffers;
				std::vector<VkSemaphore>   renderFinished;   // per swapchain image
				std::vector<VkFence>       imagesInFlight;    // per swapchain image (borrowed, not owned)
				VkRenderPass renderPass = VK_NULL_HANDLE;

				VkCommandPool commandPool = VK_NULL_HANDLE;
				std::vector<VkCommandBuffer> commandBuffers;  // per frame in flight
				std::vector<VkSemaphore>     imageAvailable;  // per frame in flight
				std::vector<VkFence>         inFlightFences;  // per frame in flight
				uint32_t currentFrame = 0;
				uint32_t acquiredImage = 0;
				bool frameActive = false;

				HandlePool<BoundBuffer,  BufferRes>  buffers;
				HandlePool<BoundTexture, TextureRes> textures;
				HandlePool<BoundSampler, SamplerRes> samplers;

				DeviceCaps deviceCaps{};
			};

			// =====================================================================
			// lifecycle
			// =====================================================================
			bool VulkanDevice::initialize(const SwapchainDesc &a_desc) {
				swapDesc = a_desc;
				window = static_cast<SDL_Window*>(a_desc.sdlWindow);
				if (!window) { return false; }
				if ((SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN) == 0) {
					// The window must have been created with SDL_WINDOW_VULKAN. Until Draw2D learns to
					// create a Vulkan-flagged window, selecting this backend at run time is a no-op.
					std::fprintf(stderr, "[Vulkan] window was not created with SDL_WINDOW_VULKAN; staying on GL.\n");
					return false;
				}
				if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
					std::fprintf(stderr, "[Vulkan] SDL_Vulkan_LoadLibrary failed: %s\n", SDL_GetError());
					return false;
				}
				libraryLoaded = true;

				if (!loadGlobalFunctions()) { return false; }
				if (!createInstance()) { return false; }
				if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
					std::fprintf(stderr, "[Vulkan] SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
					return false;
				}
				if (!pickPhysicalDevice()) { return false; }
				if (!createLogicalDevice()) { return false; }
				if (!createRenderPass()) { return false; }
				if (!createFrameResources()) { return false; }

				int w = 0, h = 0;
				SDL_Vulkan_GetDrawableSize(window, &w, &h);
				if (!createSwapchain(w, h)) { return false; }

				VkPhysicalDeviceProperties props{};
				fns.vkGetPhysicalDeviceProperties(physicalDevice, &props);
				deviceCaps.deviceName = props.deviceName;
				deviceCaps.apiVersion = "Vulkan";
				deviceCaps.supportsAnisotropy = true;
				deviceCaps.clipSpace = ClipSpace::ZeroToOne_YDown;
				deviceCaps.flipViewportY = true;
				return true;
			}

			bool VulkanDevice::loadGlobalFunctions() {
				getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
				if (!getInstanceProcAddr) { return false; }
#define MV_VK_LOAD_GLOBAL(n) fns.n = reinterpret_cast<PFN_##n>(getInstanceProcAddr(VK_NULL_HANDLE, #n)); if (!fns.n) { return false; }
				MV_VK_GLOBAL_FUNCS(MV_VK_LOAD_GLOBAL)
#undef MV_VK_LOAD_GLOBAL
				return true;
			}

			bool VulkanDevice::createInstance() {
				unsigned int extCount = 0;
				if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr)) { return false; }
				std::vector<const char*> extensions(extCount);
				if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, extensions.data())) { return false; }

				// Validation only when both the layer and VK_EXT_debug_utils are present (the messenger
				// needs the extension; requesting it unsupported would fail vkCreateInstance).
				const char *validationLayer = "VK_LAYER_KHRONOS_validation";
				bool layerPresent = false;
				{
					uint32_t layerCount = 0;
					fns.vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
					std::vector<VkLayerProperties> layers(layerCount);
					if (layerCount) { fns.vkEnumerateInstanceLayerProperties(&layerCount, layers.data()); }
					for (const auto &l : layers) {
						if (std::strcmp(l.layerName, validationLayer) == 0) { layerPresent = true; break; }
					}
				}
				bool debugUtilsPresent = false;
				{
					uint32_t extPropCount = 0;
					fns.vkEnumerateInstanceExtensionProperties(nullptr, &extPropCount, nullptr);
					std::vector<VkExtensionProperties> extProps(extPropCount);
					if (extPropCount) { fns.vkEnumerateInstanceExtensionProperties(nullptr, &extPropCount, extProps.data()); }
					for (const auto &e : extProps) {
						if (std::strcmp(e.extensionName, "VK_EXT_debug_utils") == 0) { debugUtilsPresent = true; break; }
					}
				}
				validationEnabled = layerPresent && debugUtilsPresent;
				if (validationEnabled) { extensions.push_back("VK_EXT_debug_utils"); }

				VkApplicationInfo app{};
				app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
				app.pApplicationName = "MutedVision";
				app.pEngineName = "MutedVision";
				app.apiVersion = VK_API_VERSION_1_1;

				VkInstanceCreateInfo ci{};
				ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
				ci.pApplicationInfo = &app;
				ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
				ci.ppEnabledExtensionNames = extensions.data();
				if (validationEnabled) {
					ci.enabledLayerCount = 1;
					ci.ppEnabledLayerNames = &validationLayer;
				}
				if (fns.vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) { return false; }

#define MV_VK_LOAD_INSTANCE(n) fns.n = reinterpret_cast<PFN_##n>(getInstanceProcAddr(instance, #n)); if (!fns.n) { return false; }
				MV_VK_INSTANCE_FUNCS(MV_VK_LOAD_INSTANCE)
#undef MV_VK_LOAD_INSTANCE

				if (validationEnabled) {
					pfnCreateDebug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(getInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
					pfnDestroyDebug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(getInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
					if (pfnCreateDebug) {
						VkDebugUtilsMessengerCreateInfoEXT dci{};
						dci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
						dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
						dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
						dci.pfnUserCallback = debugCallback;
						pfnCreateDebug(instance, &dci, nullptr, &debugMessenger);
					}
				}
				return true;
			}

			bool VulkanDevice::pickPhysicalDevice() {
				uint32_t count = 0;
				fns.vkEnumeratePhysicalDevices(instance, &count, nullptr);
				if (count == 0) { return false; }
				std::vector<VkPhysicalDevice> devices(count);
				fns.vkEnumeratePhysicalDevices(instance, &count, devices.data());

				int bestScore = -1;
				for (VkPhysicalDevice dev : devices) {
					// Require a queue family that does graphics + can present to our surface.
					uint32_t qCount = 0;
					fns.vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
					std::vector<VkQueueFamilyProperties> qprops(qCount);
					fns.vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qprops.data());

					bool foundGraphics = false, foundPresent = false;
					uint32_t gFam = 0, pFam = 0;
					for (uint32_t i = 0; i < qCount; ++i) {
						if (!foundGraphics && (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { foundGraphics = true; gFam = i; }
						VkBool32 present = VK_FALSE;
						fns.vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present);
						if (!foundPresent && present) { foundPresent = true; pFam = i; }
					}
					if (!foundGraphics || !foundPresent) { continue; }

					// Require VK_KHR_swapchain.
					uint32_t extCount = 0;
					fns.vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
					std::vector<VkExtensionProperties> exts(extCount);
					if (extCount) { fns.vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data()); }
					bool hasSwapchain = false;
					for (const auto &e : exts) {
						if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) { hasSwapchain = true; break; }
					}
					if (!hasSwapchain) { continue; }

					VkPhysicalDeviceProperties props{};
					fns.vkGetPhysicalDeviceProperties(dev, &props);
					int score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? 1000 : 100;
					if (score > bestScore) {
						bestScore = score;
						physicalDevice = dev;
						graphicsFamily = gFam;
						presentFamily = pFam;
					}
				}
				if (physicalDevice == VK_NULL_HANDLE) { return false; }
				fns.vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
				return true;
			}

			bool VulkanDevice::createLogicalDevice() {
				const float priority = 1.0f;
				std::vector<VkDeviceQueueCreateInfo> queueInfos;
				auto addQueue = [&](uint32_t family) {
					for (const auto &qi : queueInfos) { if (qi.queueFamilyIndex == family) { return; } }
					VkDeviceQueueCreateInfo qi{};
					qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
					qi.queueFamilyIndex = family;
					qi.queueCount = 1;
					qi.pQueuePriorities = &priority;
					queueInfos.push_back(qi);
				};
				addQueue(graphicsFamily);
				addQueue(presentFamily);

				VkPhysicalDeviceFeatures features{};
				features.samplerAnisotropy = VK_TRUE;

				const char *deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

				VkDeviceCreateInfo dci{};
				dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
				dci.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
				dci.pQueueCreateInfos = queueInfos.data();
				dci.enabledExtensionCount = 1;
				dci.ppEnabledExtensionNames = deviceExtensions;
				dci.pEnabledFeatures = &features;
				if (fns.vkCreateDevice(physicalDevice, &dci, nullptr, &device) != VK_SUCCESS) { return false; }

#define MV_VK_LOAD_DEVICE(n) fns.n = reinterpret_cast<PFN_##n>(fns.vkGetDeviceProcAddr(device, #n)); if (!fns.n) { return false; }
				MV_VK_DEVICE_FUNCS(MV_VK_LOAD_DEVICE)
#undef MV_VK_LOAD_DEVICE

				fns.vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
				fns.vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
				return true;
			}

			bool VulkanDevice::createRenderPass() {
				// Choose the surface (color) format up front so the render pass and swapchain agree.
				uint32_t formatCount = 0;
				fns.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
				std::vector<VkSurfaceFormatKHR> formats(formatCount);
				if (formatCount) { fns.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data()); }
				swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
				swapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
				bool picked = false;
				for (const auto &f : formats) {
					if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
						swapchainFormat = f.format; swapchainColorSpace = f.colorSpace; picked = true; break;
					}
				}
				if (!picked && !formats.empty() && formats[0].format != VK_FORMAT_UNDEFINED) {
					swapchainFormat = formats[0].format; swapchainColorSpace = formats[0].colorSpace;
				}

				VkAttachmentDescription color{};
				color.format = swapchainFormat;
				color.samples = VK_SAMPLE_COUNT_1_BIT;
				color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

				VkAttachmentReference colorRef{};
				colorRef.attachment = 0;
				colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

				VkSubpassDescription subpass{};
				subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				subpass.colorAttachmentCount = 1;
				subpass.pColorAttachments = &colorRef;

				VkSubpassDependency dependency{};
				dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
				dependency.dstSubpass = 0;
				dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				dependency.srcAccessMask = 0;
				dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

				VkRenderPassCreateInfo rp{};
				rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
				rp.attachmentCount = 1;
				rp.pAttachments = &color;
				rp.subpassCount = 1;
				rp.pSubpasses = &subpass;
				rp.dependencyCount = 1;
				rp.pDependencies = &dependency;
				return fns.vkCreateRenderPass(device, &rp, nullptr, &renderPass) == VK_SUCCESS;
			}

			bool VulkanDevice::createFrameResources() {
				VkCommandPoolCreateInfo pci{};
				pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
				pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
				pci.queueFamilyIndex = graphicsFamily;
				if (fns.vkCreateCommandPool(device, &pci, nullptr, &commandPool) != VK_SUCCESS) { return false; }

				commandBuffers.resize(kFramesInFlight);
				VkCommandBufferAllocateInfo ai{};
				ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				ai.commandPool = commandPool;
				ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				ai.commandBufferCount = kFramesInFlight;
				if (fns.vkAllocateCommandBuffers(device, &ai, commandBuffers.data()) != VK_SUCCESS) { return false; }

				imageAvailable.resize(kFramesInFlight);
				inFlightFences.resize(kFramesInFlight);
				VkSemaphoreCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
				VkFenceCreateInfo fi{}; fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
				for (uint32_t i = 0; i < kFramesInFlight; ++i) {
					if (fns.vkCreateSemaphore(device, &si, nullptr, &imageAvailable[i]) != VK_SUCCESS) { return false; }
					if (fns.vkCreateFence(device, &fi, nullptr, &inFlightFences[i]) != VK_SUCCESS) { return false; }
				}
				return true;
			}

			bool VulkanDevice::createSwapchain(int a_width, int a_height) {
				VkSurfaceCapabilitiesKHR caps{};
				fns.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);

				if (caps.currentExtent.width != UINT32_MAX) {
					swapchainExtent = caps.currentExtent;
				} else {
					swapchainExtent.width = std::clamp(static_cast<uint32_t>(a_width), caps.minImageExtent.width, caps.maxImageExtent.width);
					swapchainExtent.height = std::clamp(static_cast<uint32_t>(a_height), caps.minImageExtent.height, caps.maxImageExtent.height);
				}
				if (swapchainExtent.width == 0 || swapchainExtent.height == 0) {
					return true; // minimized; leave swapchain null, beginFrame() will skip until resized.
				}

				// Present mode: FIFO is always available; prefer MAILBOX when vsync is off.
				VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
				if (!swapDesc.vsync) {
					uint32_t modeCount = 0;
					fns.vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, nullptr);
					std::vector<VkPresentModeKHR> modes(modeCount);
					if (modeCount) { fns.vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, modes.data()); }
					for (auto m : modes) { if (m == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = m; break; } }
				}

				uint32_t imageCount = std::max(caps.minImageCount + 1, swapDesc.imageCount);
				imageCount = std::max(imageCount, caps.minImageCount);
				if (caps.maxImageCount > 0) { imageCount = std::min(imageCount, caps.maxImageCount); }

				VkSwapchainCreateInfoKHR sci{};
				sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
				sci.surface = surface;
				sci.minImageCount = imageCount;
				sci.imageFormat = swapchainFormat;
				sci.imageColorSpace = swapchainColorSpace;
				sci.imageExtent = swapchainExtent;
				sci.imageArrayLayers = 1;
				sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
				uint32_t familyIndices[] = { graphicsFamily, presentFamily };
				if (graphicsFamily != presentFamily) {
					sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
					sci.queueFamilyIndexCount = 2;
					sci.pQueueFamilyIndices = familyIndices;
				} else {
					sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
				}
				sci.preTransform = caps.currentTransform;
				sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
				sci.presentMode = presentMode;
				sci.clipped = VK_TRUE;
				sci.oldSwapchain = VK_NULL_HANDLE;
				if (fns.vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS) { return false; }

				uint32_t actualCount = 0;
				fns.vkGetSwapchainImagesKHR(device, swapchain, &actualCount, nullptr);
				swapchainImages.resize(actualCount);
				fns.vkGetSwapchainImagesKHR(device, swapchain, &actualCount, swapchainImages.data());

				swapchainViews.resize(actualCount);
				framebuffers.resize(actualCount);
				renderFinished.resize(actualCount);
				imagesInFlight.assign(actualCount, VK_NULL_HANDLE);
				VkSemaphoreCreateInfo semInfo{}; semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
				for (uint32_t i = 0; i < actualCount; ++i) {
					VkImageViewCreateInfo vci{};
					vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
					vci.image = swapchainImages[i];
					vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
					vci.format = swapchainFormat;
					vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					vci.subresourceRange.levelCount = 1;
					vci.subresourceRange.layerCount = 1;
					if (fns.vkCreateImageView(device, &vci, nullptr, &swapchainViews[i]) != VK_SUCCESS) { return false; }

					VkFramebufferCreateInfo fbi{};
					fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
					fbi.renderPass = renderPass;
					fbi.attachmentCount = 1;
					fbi.pAttachments = &swapchainViews[i];
					fbi.width = swapchainExtent.width;
					fbi.height = swapchainExtent.height;
					fbi.layers = 1;
					if (fns.vkCreateFramebuffer(device, &fbi, nullptr, &framebuffers[i]) != VK_SUCCESS) { return false; }

					if (fns.vkCreateSemaphore(device, &semInfo, nullptr, &renderFinished[i]) != VK_SUCCESS) { return false; }
				}
				return true;
			}

			void VulkanDevice::destroySwapchain() {
				if (!device) { return; }
				for (VkFramebuffer fb : framebuffers) { if (fb) { fns.vkDestroyFramebuffer(device, fb, nullptr); } }
				for (VkImageView v : swapchainViews) { if (v) { fns.vkDestroyImageView(device, v, nullptr); } }
				for (VkSemaphore s : renderFinished) { if (s) { fns.vkDestroySemaphore(device, s, nullptr); } }
				framebuffers.clear();
				swapchainViews.clear();
				renderFinished.clear();
				imagesInFlight.clear();
				swapchainImages.clear();
				if (swapchain) { fns.vkDestroySwapchainKHR(device, swapchain, nullptr); swapchain = VK_NULL_HANDLE; }
			}

			bool VulkanDevice::recreateSwapchain() {
				if (!device) { return false; }
				fns.vkDeviceWaitIdle(device);
				fns.vkQueueWaitIdle(presentQueue); // ensure no present still references a renderFinished semaphore we destroy
				destroySwapchain();
				int w = 0, h = 0;
				SDL_Vulkan_GetDrawableSize(window, &w, &h);
				return createSwapchain(w, h);
			}

			void VulkanDevice::shutdown() {
				if (device) { fns.vkDeviceWaitIdle(device); }

				// Drain any leaked GPU resources.
				if (device) {
					buffers.forEachLive([&](BufferRes &b) {
						if (b.buffer) { fns.vkDestroyBuffer(device, b.buffer, nullptr); }
						if (b.memory) { fns.vkFreeMemory(device, b.memory, nullptr); }
					});
					textures.forEachLive([&](TextureRes &t) {
						if (t.view) { fns.vkDestroyImageView(device, t.view, nullptr); }
						if (t.image) { fns.vkDestroyImage(device, t.image, nullptr); }
						if (t.memory) { fns.vkFreeMemory(device, t.memory, nullptr); }
					});
					samplers.forEachLive([&](SamplerRes &s) {
						if (s.sampler) { fns.vkDestroySampler(device, s.sampler, nullptr); }
					});
				}
				buffers.clear(); textures.clear(); samplers.clear();

				destroySwapchain();

				if (device) {
					if (renderPass) { fns.vkDestroyRenderPass(device, renderPass, nullptr); renderPass = VK_NULL_HANDLE; }
					for (VkSemaphore s : imageAvailable) { if (s) { fns.vkDestroySemaphore(device, s, nullptr); } }
					for (VkFence f : inFlightFences) { if (f) { fns.vkDestroyFence(device, f, nullptr); } }
					imageAvailable.clear();
					inFlightFences.clear();
					if (commandPool) { fns.vkDestroyCommandPool(device, commandPool, nullptr); commandPool = VK_NULL_HANDLE; }
					fns.vkDestroyDevice(device, nullptr);
					device = VK_NULL_HANDLE;
				}
				if (instance) {
					if (debugMessenger && pfnDestroyDebug) { pfnDestroyDebug(instance, debugMessenger, nullptr); debugMessenger = VK_NULL_HANDLE; }
					if (surface) { fns.vkDestroySurfaceKHR(instance, surface, nullptr); surface = VK_NULL_HANDLE; }
					fns.vkDestroyInstance(instance, nullptr);
					instance = VK_NULL_HANDLE;
				}
				if (libraryLoaded) { SDL_Vulkan_UnloadLibrary(); libraryLoaded = false; }
			}

			// =====================================================================
			// frame / pass
			// =====================================================================
			void VulkanDevice::beginFrame() {
				if (!device || swapchain == VK_NULL_HANDLE) { return; }
				fns.vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

				VkResult r = fns.vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable[currentFrame], VK_NULL_HANDLE, &acquiredImage);
				if (r == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return; }
				if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { return; }

				if (imagesInFlight[acquiredImage] != VK_NULL_HANDLE) {
					fns.vkWaitForFences(device, 1, &imagesInFlight[acquiredImage], VK_TRUE, UINT64_MAX);
				}
				imagesInFlight[acquiredImage] = inFlightFences[currentFrame];
				fns.vkResetFences(device, 1, &inFlightFences[currentFrame]);

				fns.vkResetCommandBuffer(commandBuffers[currentFrame], 0);
				VkCommandBufferBeginInfo bi{};
				bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				fns.vkBeginCommandBuffer(commandBuffers[currentFrame], &bi);
				frameActive = true;
			}

			void VulkanDevice::beginSwapchainPass(const float a_clearColor[4]) {
				if (!frameActive) { return; }
				VkClearValue clear{};
				clear.color.float32[0] = a_clearColor[0];
				clear.color.float32[1] = a_clearColor[1];
				clear.color.float32[2] = a_clearColor[2];
				clear.color.float32[3] = a_clearColor[3];

				VkRenderPassBeginInfo rp{};
				rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				rp.renderPass = renderPass;
				rp.framebuffer = framebuffers[acquiredImage];
				rp.renderArea.offset = { 0, 0 };
				rp.renderArea.extent = swapchainExtent;
				rp.clearValueCount = 1;
				rp.pClearValues = &clear;
				fns.vkCmdBeginRenderPass(commandBuffers[currentFrame], &rp, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport vp{};
				vp.x = 0; vp.y = 0;
				vp.width = static_cast<float>(swapchainExtent.width);
				vp.height = static_cast<float>(swapchainExtent.height);
				vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
				fns.vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &vp);
				VkRect2D sc{ { 0, 0 }, swapchainExtent };
				fns.vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &sc);
			}

			void VulkanDevice::beginDefaultPass(const float a_clearColor[4]) {
				beginSwapchainPass(a_clearColor);
			}

			void VulkanDevice::beginPass(BoundRenderTarget a_target, const RenderTargetDesc &a_clearInfo) {
				require<ResourceException>(!a_target.valid(),
					"VulkanDevice::beginPass to an offscreen render target not implemented yet (Phase 3)");
				const float black[4] = { 0, 0, 0, 0 };
				beginSwapchainPass(a_clearInfo.color.empty() ? black : a_clearInfo.color[0].clearColor);
			}

			void VulkanDevice::setViewport(const Viewport &a_viewport) {
				if (!frameActive) { return; }
				VkViewport vp{};
				vp.x = a_viewport.x; vp.y = a_viewport.y;
				vp.width = a_viewport.width; vp.height = a_viewport.height;
				vp.minDepth = a_viewport.minDepth; vp.maxDepth = a_viewport.maxDepth;
				fns.vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &vp);
			}
			void VulkanDevice::setScissor(const Rect &a_rect) {
				if (!frameActive) { return; }
				VkRect2D sc{};
				sc.offset = { a_rect.x, a_rect.y };
				sc.extent = { static_cast<uint32_t>(a_rect.width), static_cast<uint32_t>(a_rect.height) };
				fns.vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &sc);
			}

			void VulkanDevice::endPass() {
				if (!frameActive) { return; }
				fns.vkCmdEndRenderPass(commandBuffers[currentFrame]);
			}

			void VulkanDevice::endFrame() {
				if (!frameActive) { return; }
				frameActive = false;
				VkCommandBuffer cmd = commandBuffers[currentFrame];
				fns.vkEndCommandBuffer(cmd);

				VkSemaphore waitSem = imageAvailable[currentFrame];
				VkSemaphore signalSem = renderFinished[acquiredImage];
				VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

				VkSubmitInfo si{};
				si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				si.waitSemaphoreCount = 1;
				si.pWaitSemaphores = &waitSem;
				si.pWaitDstStageMask = &waitStage;
				si.commandBufferCount = 1;
				si.pCommandBuffers = &cmd;
				si.signalSemaphoreCount = 1;
				si.pSignalSemaphores = &signalSem;
				fns.vkQueueSubmit(graphicsQueue, 1, &si, inFlightFences[currentFrame]);

				VkPresentInfoKHR pi{};
				pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
				pi.waitSemaphoreCount = 1;
				pi.pWaitSemaphores = &signalSem;
				pi.swapchainCount = 1;
				pi.pSwapchains = &swapchain;
				pi.pImageIndices = &acquiredImage;
				VkResult r = fns.vkQueuePresentKHR(presentQueue, &pi);
				if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) { recreateSwapchain(); }

				currentFrame = (currentFrame + 1) % kFramesInFlight;
			}

			void VulkanDevice::onSurfaceResized(int, int) {
				recreateSwapchain();
			}

			// =====================================================================
			// helpers
			// =====================================================================
			uint32_t VulkanDevice::findMemoryType(uint32_t a_typeBits, VkMemoryPropertyFlags a_props) const {
				for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
					if ((a_typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & a_props) == a_props) {
						return i;
					}
				}
				require<ResourceException>(false, "VulkanDevice: no compatible memory type found");
				return 0;
			}

			bool VulkanDevice::oneTimeSubmit(const std::function<void(VkCommandBuffer)> &a_record) {
				VkCommandBufferAllocateInfo ai{};
				ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				ai.commandPool = commandPool;
				ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				ai.commandBufferCount = 1;
				VkCommandBuffer cmd = VK_NULL_HANDLE;
				if (fns.vkAllocateCommandBuffers(device, &ai, &cmd) != VK_SUCCESS) { return false; }

				VkCommandBufferBeginInfo bi{};
				bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				fns.vkBeginCommandBuffer(cmd, &bi);
				a_record(cmd);
				fns.vkEndCommandBuffer(cmd);

				VkSubmitInfo si{};
				si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				si.commandBufferCount = 1;
				si.pCommandBuffers = &cmd;
				fns.vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE);
				fns.vkQueueWaitIdle(graphicsQueue);
				fns.vkFreeCommandBuffers(device, commandPool, 1, &cmd);
				return true;
			}

			void VulkanDevice::transitionImage(VkCommandBuffer a_cmd, VkImage a_image, VkImageLayout a_old, VkImageLayout a_new,
			                                    uint32_t a_baseMip, uint32_t a_mipCount) {
				auto accessFor = [](VkImageLayout l) -> VkAccessFlags {
					switch (l) {
					case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:     return VK_ACCESS_TRANSFER_WRITE_BIT;
					case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:     return VK_ACCESS_TRANSFER_READ_BIT;
					case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return VK_ACCESS_SHADER_READ_BIT;
					case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					default:                                       return 0;
					}
				};
				VkImageMemoryBarrier barrier{};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.oldLayout = a_old;
				barrier.newLayout = a_new;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = a_image;
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barrier.subresourceRange.baseMipLevel = a_baseMip;
				barrier.subresourceRange.levelCount = a_mipCount;
				barrier.subresourceRange.layerCount = 1;
				barrier.srcAccessMask = accessFor(a_old);
				barrier.dstAccessMask = accessFor(a_new);
				// One-time uploads are not hot; a full-pipeline barrier keeps correctness simple.
				fns.vkCmdPipelineBarrier(a_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				                         0, 0, nullptr, 0, nullptr, 1, &barrier);
			}

			// =====================================================================
			// resources: buffers
			// =====================================================================
			BoundBuffer VulkanDevice::createBuffer(BufferUsage a_usage, BufferUpdateHint, const void *a_data, size_t a_bytes) {
				VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
				switch (a_usage) {
				case BufferUsage::Vertex:  usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; break;
				case BufferUsage::Index:   usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT; break;
				case BufferUsage::Uniform: usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; break;
				}
				const VkDeviceSize size = std::max<VkDeviceSize>(a_bytes, 1);

				VkBufferCreateInfo bci{};
				bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				bci.size = size;
				bci.usage = usage;
				bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

				BufferRes res{};
				res.size = size;
				require<ResourceException>(fns.vkCreateBuffer(device, &bci, nullptr, &res.buffer) == VK_SUCCESS, "VulkanDevice::createBuffer: vkCreateBuffer failed");

				VkMemoryRequirements req{};
				fns.vkGetBufferMemoryRequirements(device, res.buffer, &req);
				VkMemoryAllocateInfo mai{};
				mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				mai.allocationSize = req.size;
				// Host-visible+coherent, persistently mapped: correct for every hint (a device-local + staging fast path is later).
				mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				require<ResourceException>(fns.vkAllocateMemory(device, &mai, nullptr, &res.memory) == VK_SUCCESS, "VulkanDevice::createBuffer: vkAllocateMemory failed");
				fns.vkBindBufferMemory(device, res.buffer, res.memory, 0);
				fns.vkMapMemory(device, res.memory, 0, VK_WHOLE_SIZE, 0, &res.mapped);
				if (a_data && a_bytes) { std::memcpy(res.mapped, a_data, a_bytes); }
				return buffers.create(res);
			}
			void VulkanDevice::updateBuffer(BoundBuffer a_handle, const void *a_data, size_t a_bytes, size_t a_offset) {
				BufferRes *b = buffers.get(a_handle);
				require<ResourceException>(b != nullptr, "VulkanDevice::updateBuffer called with a stale/null buffer handle");
				require<ResourceException>(a_offset + a_bytes <= b->size, "VulkanDevice::updateBuffer write out of range");
				std::memcpy(static_cast<char*>(b->mapped) + a_offset, a_data, a_bytes);
			}
			void VulkanDevice::destroyBuffer(BoundBuffer a_handle) {
				BufferRes b;
				if (!buffers.remove(a_handle, b)) { return; }
				if (b.buffer) { fns.vkDestroyBuffer(device, b.buffer, nullptr); }
				if (b.memory) { fns.vkFreeMemory(device, b.memory, nullptr); } // implicitly unmaps
			}

			// =====================================================================
			// resources: textures
			// =====================================================================
			BoundTexture VulkanDevice::createTexture(const TextureDesc &a_desc, const void *a_initialPixels) {
				require<ResourceException>(a_desc.dimension == TextureDimension::Tex2D,
					"VulkanDevice::createTexture currently supports only Tex2D (arrays/cube/3D arrive with the 3D path)");

				TextureRes res{};
				res.format = toVkFormat(a_desc.format);
				res.pixelFormat = a_desc.format;
				res.width = a_desc.width;
				res.height = a_desc.height;
				res.mipLevels = a_desc.mipLevels;
				if (a_desc.generateMips) {
					const int maxDim = std::max(a_desc.width, a_desc.height);
					uint32_t levels = 1;
					for (int d = maxDim; d > 1; d >>= 1) { ++levels; }
					res.mipLevels = std::max<uint32_t>(levels, 1);
				}
				res.mipLevels = std::max<uint32_t>(res.mipLevels, 1);

				VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

				VkImageCreateInfo ici{};
				ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				ici.imageType = VK_IMAGE_TYPE_2D;
				ici.format = res.format;
				ici.extent = { static_cast<uint32_t>(a_desc.width), static_cast<uint32_t>(a_desc.height), 1 };
				ici.mipLevels = res.mipLevels;
				ici.arrayLayers = 1;
				ici.samples = VK_SAMPLE_COUNT_1_BIT;
				ici.tiling = VK_IMAGE_TILING_OPTIMAL;
				ici.usage = usage;
				ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				require<ResourceException>(fns.vkCreateImage(device, &ici, nullptr, &res.image) == VK_SUCCESS, "VulkanDevice::createTexture: vkCreateImage failed");

				VkMemoryRequirements req{};
				fns.vkGetImageMemoryRequirements(device, res.image, &req);
				VkMemoryAllocateInfo mai{};
				mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				mai.allocationSize = req.size;
				mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				require<ResourceException>(fns.vkAllocateMemory(device, &mai, nullptr, &res.memory) == VK_SUCCESS, "VulkanDevice::createTexture: vkAllocateMemory failed");
				fns.vkBindImageMemory(device, res.image, res.memory, 0);

				VkImageViewCreateInfo vci{};
				vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				vci.image = res.image;
				vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
				vci.format = res.format;
				vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				vci.subresourceRange.levelCount = res.mipLevels;
				vci.subresourceRange.layerCount = 1;
				require<ResourceException>(fns.vkCreateImageView(device, &vci, nullptr, &res.view) == VK_SUCCESS, "VulkanDevice::createTexture: vkCreateImageView failed");

				BoundTexture handle{};
				if (a_initialPixels) {
					const VkDeviceSize uploadBytes = static_cast<VkDeviceSize>(a_desc.width) * a_desc.height * bytesPerPixel(a_desc.format);
					VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE; void *stagingMap = nullptr;
					VkBufferCreateInfo sbi{};
					sbi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
					sbi.size = uploadBytes;
					sbi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
					sbi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
					fns.vkCreateBuffer(device, &sbi, nullptr, &staging);
					VkMemoryRequirements sreq{};
					fns.vkGetBufferMemoryRequirements(device, staging, &sreq);
					VkMemoryAllocateInfo smai{};
					smai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
					smai.allocationSize = sreq.size;
					smai.memoryTypeIndex = findMemoryType(sreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
					fns.vkAllocateMemory(device, &smai, nullptr, &stagingMem);
					fns.vkBindBufferMemory(device, staging, stagingMem, 0);
					fns.vkMapMemory(device, stagingMem, 0, uploadBytes, 0, &stagingMap);
					std::memcpy(stagingMap, a_initialPixels, static_cast<size_t>(uploadBytes));
					fns.vkUnmapMemory(device, stagingMem);

					const bool single = (res.mipLevels == 1);
					oneTimeSubmit([&](VkCommandBuffer cmd) {
						transitionImage(cmd, res.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, res.mipLevels);
						VkBufferImageCopy region{};
						region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						region.imageSubresource.mipLevel = 0;
						region.imageSubresource.layerCount = 1;
						region.imageExtent = { static_cast<uint32_t>(a_desc.width), static_cast<uint32_t>(a_desc.height), 1 };
						fns.vkCmdCopyBufferToImage(cmd, staging, res.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
						if (single) {
							transitionImage(cmd, res.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1);
						}
					});
					// Multi-mip stays in TRANSFER_DST until generateMips() blits the chain and finalizes the layout.
					res.layout = single ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

					fns.vkDestroyBuffer(device, staging, nullptr);
					fns.vkFreeMemory(device, stagingMem, nullptr);

					handle = textures.create(res);
					if (!single) { generateMips(handle); }
				} else {
					// No pixels: still make it samplable so a shader can read a blank texture safely.
					oneTimeSubmit([&](VkCommandBuffer cmd) {
						transitionImage(cmd, res.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, res.mipLevels);
					});
					res.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					handle = textures.create(res);
				}
				return handle;
			}

			void VulkanDevice::updateTexture(BoundTexture a_handle, int a_mip, const Rect &a_rect, const void *a_pixels) {
				TextureRes *t = textures.get(a_handle);
				require<ResourceException>(t != nullptr, "VulkanDevice::updateTexture called with a stale/null texture handle");
				const VkDeviceSize uploadBytes = static_cast<VkDeviceSize>(a_rect.width) * a_rect.height * bytesPerPixel(t->pixelFormat);

				VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE; void *stagingMap = nullptr;
				VkBufferCreateInfo sbi{};
				sbi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				sbi.size = uploadBytes;
				sbi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
				sbi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				fns.vkCreateBuffer(device, &sbi, nullptr, &staging);
				VkMemoryRequirements sreq{};
				fns.vkGetBufferMemoryRequirements(device, staging, &sreq);
				VkMemoryAllocateInfo smai{};
				smai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				smai.allocationSize = sreq.size;
				smai.memoryTypeIndex = findMemoryType(sreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				fns.vkAllocateMemory(device, &smai, nullptr, &stagingMem);
				fns.vkBindBufferMemory(device, staging, stagingMem, 0);
				fns.vkMapMemory(device, stagingMem, 0, uploadBytes, 0, &stagingMap);
				std::memcpy(stagingMap, a_pixels, static_cast<size_t>(uploadBytes));
				fns.vkUnmapMemory(device, stagingMem);

				const VkImageLayout current = t->layout;
				oneTimeSubmit([&](VkCommandBuffer cmd) {
					transitionImage(cmd, t->image, current, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(a_mip), 1);
					VkBufferImageCopy region{};
					region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					region.imageSubresource.mipLevel = static_cast<uint32_t>(a_mip);
					region.imageSubresource.layerCount = 1;
					region.imageOffset = { a_rect.x, a_rect.y, 0 };
					region.imageExtent = { static_cast<uint32_t>(a_rect.width), static_cast<uint32_t>(a_rect.height), 1 };
					fns.vkCmdCopyBufferToImage(cmd, staging, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
					transitionImage(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, static_cast<uint32_t>(a_mip), 1);
				});
				// Keep the tracked layout accurate so a later update/readback uses the right oldLayout.
				t->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				fns.vkDestroyBuffer(device, staging, nullptr);
				fns.vkFreeMemory(device, stagingMem, nullptr);
			}

			void VulkanDevice::generateMips(BoundTexture a_handle) {
				TextureRes *t = textures.get(a_handle);
				require<ResourceException>(t != nullptr, "VulkanDevice::generateMips called with a stale/null texture handle");
				// Nothing to do for a 1-level image, or one already finalized to shader-read.
				if (t->mipLevels <= 1 || t->layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) { return; }
				// Contract (from createTexture): every level in TRANSFER_DST, level 0 holding the source image.
				oneTimeSubmit([&](VkCommandBuffer cmd) {
					int32_t mipW = t->width, mipH = t->height;
					for (uint32_t i = 1; i < t->mipLevels; ++i) {
						// Source level (i-1) is in TRANSFER_DST: move to SRC, blit down into i, then retire to shader-read.
						transitionImage(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, i - 1, 1);

						VkImageBlit blit{};
						blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						blit.srcSubresource.mipLevel = i - 1;
						blit.srcSubresource.layerCount = 1;
						blit.srcOffsets[1] = { mipW, mipH, 1 };
						blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						blit.dstSubresource.mipLevel = i;
						blit.dstSubresource.layerCount = 1;
						blit.dstOffsets[1] = { std::max(1, mipW / 2), std::max(1, mipH / 2), 1 };
						fns.vkCmdBlitImage(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

						transitionImage(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, i - 1, 1);
						if (mipW > 1) { mipW /= 2; }
						if (mipH > 1) { mipH /= 2; }
					}
					// The last level is still TRANSFER_DST.
					transitionImage(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, t->mipLevels - 1, 1);
				});
				t->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}

			void VulkanDevice::destroyTexture(BoundTexture a_handle) {
				TextureRes t;
				if (!textures.remove(a_handle, t)) { return; }
				if (t.view) { fns.vkDestroyImageView(device, t.view, nullptr); }
				if (t.image) { fns.vkDestroyImage(device, t.image, nullptr); }
				if (t.memory) { fns.vkFreeMemory(device, t.memory, nullptr); }
			}

			void VulkanDevice::readTexture(BoundTexture a_handle, std::vector<uint8_t> &a_out) {
				TextureRes *t = textures.get(a_handle);
				require<ResourceException>(t != nullptr, "VulkanDevice::readTexture called with a stale/null texture handle");
				const VkDeviceSize bytes = static_cast<VkDeviceSize>(t->width) * t->height * bytesPerPixel(t->pixelFormat);
				a_out.resize(static_cast<size_t>(bytes));

				VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE; void *stagingMap = nullptr;
				VkBufferCreateInfo sbi{};
				sbi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				sbi.size = bytes;
				sbi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
				sbi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				fns.vkCreateBuffer(device, &sbi, nullptr, &staging);
				VkMemoryRequirements sreq{};
				fns.vkGetBufferMemoryRequirements(device, staging, &sreq);
				VkMemoryAllocateInfo smai{};
				smai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				smai.allocationSize = sreq.size;
				smai.memoryTypeIndex = findMemoryType(sreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				fns.vkAllocateMemory(device, &smai, nullptr, &stagingMem);
				fns.vkBindBufferMemory(device, staging, stagingMem, 0);

				const VkImageLayout current = t->layout;
				oneTimeSubmit([&](VkCommandBuffer cmd) {
					transitionImage(cmd, t->image, current, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 1);
					VkBufferImageCopy region{};
					region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					region.imageSubresource.mipLevel = 0;
					region.imageSubresource.layerCount = 1;
					region.imageExtent = { static_cast<uint32_t>(t->width), static_cast<uint32_t>(t->height), 1 };
					fns.vkCmdCopyImageToBuffer(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);
					transitionImage(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1);
				});

				fns.vkMapMemory(device, stagingMem, 0, bytes, 0, &stagingMap);
				std::memcpy(a_out.data(), stagingMap, static_cast<size_t>(bytes));
				fns.vkUnmapMemory(device, stagingMem);
				fns.vkDestroyBuffer(device, staging, nullptr);
				fns.vkFreeMemory(device, stagingMem, nullptr);
			}

			// =====================================================================
			// resources: samplers
			// =====================================================================
			BoundSampler VulkanDevice::createSampler(const SamplerDesc &a_desc) {
				VkSamplerCreateInfo sci{};
				sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
				sci.magFilter = toVkFilter(a_desc.mag);
				sci.minFilter = toVkFilter(a_desc.min);
				sci.mipmapMode = toVkMipmapMode(a_desc.mip);
				sci.addressModeU = toVkAddress(a_desc.u);
				sci.addressModeV = toVkAddress(a_desc.v);
				sci.addressModeW = toVkAddress(a_desc.w);
				sci.mipLodBias = a_desc.lodBias;
				sci.anisotropyEnable = (a_desc.maxAnisotropy > 1.0f) ? VK_TRUE : VK_FALSE;
				sci.maxAnisotropy = a_desc.maxAnisotropy;
				sci.minLod = 0.0f;
				sci.maxLod = VK_LOD_CLAMP_NONE;
				sci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

				SamplerRes res{};
				require<ResourceException>(fns.vkCreateSampler(device, &sci, nullptr, &res.sampler) == VK_SUCCESS, "VulkanDevice::createSampler: vkCreateSampler failed");
				return samplers.create(res);
			}

			// =====================================================================
			// Phase-3 stubs (need SPIR-V shaders, descriptor sets and the DrawList batcher)
			// =====================================================================
			BoundShaderModule VulkanDevice::createShaderModule(const ShaderModuleDesc&) {
				require<ResourceException>(false, "VulkanDevice::createShaderModule not implemented yet (Phase 3: SPIR-V pipeline cache)");
				return {};
			}
			BoundPipeline VulkanDevice::createPipeline(const PipelineDesc&) {
				require<ResourceException>(false, "VulkanDevice::createPipeline not implemented yet (Phase 3)");
				return {};
			}
			void VulkanDevice::destroyPipeline(BoundPipeline) {
				require<ResourceException>(false, "VulkanDevice::destroyPipeline not implemented yet (Phase 3)");
			}
			BoundRenderTarget VulkanDevice::createRenderTarget(const RenderTargetDesc&) {
				require<ResourceException>(false, "VulkanDevice::createRenderTarget not implemented yet (Phase 3)");
				return {};
			}
			void VulkanDevice::destroyRenderTarget(BoundRenderTarget) {
				require<ResourceException>(false, "VulkanDevice::destroyRenderTarget not implemented yet (Phase 3)");
			}
			BoundUniformSet VulkanDevice::createUniformSet(BoundPipeline) {
				require<ResourceException>(false, "VulkanDevice::createUniformSet not implemented yet (Phase 3)");
				return {};
			}
			void VulkanDevice::setUniform(BoundUniformSet, const std::string&, const float*, uint32_t) {
				require<ResourceException>(false, "VulkanDevice::setUniform not implemented yet (Phase 3)");
			}
			void VulkanDevice::setUniformMatrix(BoundUniformSet, const std::string&, const float*) {
				require<ResourceException>(false, "VulkanDevice::setUniformMatrix not implemented yet (Phase 3)");
			}
			void VulkanDevice::setTexture(BoundUniformSet, const std::string&, BoundTexture, BoundSampler) {
				require<ResourceException>(false, "VulkanDevice::setTexture not implemented yet (Phase 3)");
			}
			void VulkanDevice::draw(const DrawItem&) {
				require<ResourceException>(false, "VulkanDevice::draw not implemented yet (Phase 3: DrawList batcher)");
			}

		} // namespace

		std::unique_ptr<Device> Device::makeVulkan() {
			return std::make_unique<VulkanDevice>();
		}

	} // namespace Render
} // namespace MV

#else  // !__has_include(<vulkan/vulkan.h>)

namespace MV {
	namespace Render {
		// Vulkan headers absent at compile time: makeVulkan() reports "unavailable" and Draw2D
		// keeps using the GL/headless backend.
		std::unique_ptr<Device> Device::makeVulkan() { return nullptr; }
	} // namespace Render
} // namespace MV

#endif // __has_include(<vulkan/vulkan.h>)
