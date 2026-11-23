#pragma once

#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize Vulkan pipelines, descriptor layouts, samplers, quad index buffer,
   and packed Minecraft texture atlases. Idempotent and thread-safe; also called
   lazily on first render call if not called explicitly. */
void mc_vulkan_render_init(void);

/* Release all Vulkan resources created by mc_vulkan_render. Call during device
   shutdown after vkDeviceWaitIdle (e.g. from gfx_window_release_vk). */
void mc_vulkan_render_shutdown(void);

/* Pass (a) + Pass (b): Replay-thread full-resolution 3D Minecraft world + 1st-person viewmodel.
   Draws into `color_image`/`color_view` (VK_ATTACHMENT_LOAD_OP_LOAD, layout VK_IMAGE_LAYOUT_GENERAL),
   depth-testing against Hyrule G-buffer `gbuf_depth_image`/`gbuf_depth_view` (also in
   VK_IMAGE_LAYOUT_GENERAL) in the fragment shader and maintaining a private `width x height`
   `mc_depth` buffer (D32_SFLOAT) cleared to 1.0 before Pass (a) and again before Pass (b).
   Must be called outside any open render pass; leaves `color_image` and `gbuf_depth_image` in
   VK_IMAGE_LAYOUT_GENERAL with a pipeline barrier on return. */
void mc_vulkan_render_world(VkCommandBuffer cb,
                            VkImage color_image,
                            VkImageView color_view,
                            VkFormat color_fmt,
                            uint32_t width,
                            uint32_t height,
                            VkImage gbuf_depth_image,
                            VkImageView gbuf_depth_view);

/* Pass (c-pre): Present-thread offscreen 3D inventory paperdoll render into an offscreen
   RGBA8 VkImage for `slot` (wrapped mod MC_ALL_SLOTS). Must be called outside any open
   render pass before the swapchain GUI render pass begins. Safe no-op when hidden. */
void mc_vulkan_render_gui_pre(VkCommandBuffer cb, uint32_t slot);

/* Pass (c): Crisp nearest-neighbor native-resolution 2D HUD, inventory, crafting, furnace,
   creative screen, tooltips, and text onto `dst_view` (assumed in
   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, extent target_w x target_h) inside
   `viewport_rect = {x, y, w, h}` (or full target if NULL). Opens and closes its own
   dynamic rendering pass with LOAD_OP_LOAD / STORE_OP_STORE. */
void mc_vulkan_render_gui(VkCommandBuffer cb,
                          VkImageView dst_view,
                          VkFormat dst_fmt,
                          uint32_t target_w,
                          uint32_t target_h,
                          const int viewport_rect[4],
                          uint32_t slot);

#ifdef __cplusplus
}
#endif
