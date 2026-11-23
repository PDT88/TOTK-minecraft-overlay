#pragma once

#import <Metal/Metal.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize pipelines, pack real Minecraft asset atlases, and register residency set with q4.
   Call once during mtl_init() after g_dev and g_q4 are created. */
void mc_metal_render_init(id<MTLDevice> dev, id<MTL4CommandQueue> q4);

/* Pass (a) + Pass (b): Replay-thread full-resolution 3D Minecraft world + 1st-person viewmodel.
   Draws into `canvas` (MTLLoadActionLoad), depth-testing against Hyrule G-buffer `gbuf_depth`
   in the fragment shader and maintaining a private `canvas`-sized `mc_depth` buffer cleared to
   1.0 before the viewmodel pass. Must be called when no Metal encoder is active on `cb`. */
void mc_metal_render_world_m4(id<MTL4CommandBuffer> cb,
                              id<MTLTexture> canvas,
                              id<MTLTexture> gbuf_depth);

/* Pass (c) Pre-step: Encodes the 3D inventory paperdoll into an offscreen texture before
   present_encode opens its render encoder. Safe no-op when inventory paperdoll is hidden.
   slot: 0..2 for triple-buffered Metal 4 present, 3 for classic/screenshot present. */
void mc_metal_render_gui_pre_m4(id<MTL4CommandBuffer> cb, int slot);
void mc_metal_render_gui_pre_m3(id<MTLCommandBuffer> cb, int slot);

/* Pass (c): Crisp nearest-neighbor native-resolution 2D HUD & inventory pass onto `dst`
   within `vp`. Restores `restore_args` (Fragment), `restore_pso`, and `restore_samp` so
   subsequent host overlays (fps/shader/load boxes) encode unmodified. */
void mc_metal_render_gui_m4(id<MTL4RenderCommandEncoder> en,
                            id<MTL4ArgumentTable> restore_args,
                            id<MTLRenderPipelineState> restore_pso,
                            id<MTLSamplerState> restore_samp,
                            id<MTLTexture> dst,
                            MTLViewport vp,
                            int slot);

void mc_metal_render_gui_m3(id<MTLRenderCommandEncoder> en,
                            id<MTLRenderPipelineState> restore_pso,
                            id<MTLSamplerState> restore_samp,
                            id<MTLTexture> dst,
                            MTLViewport vp,
                            int slot);

#ifdef __cplusplus
}
#endif
