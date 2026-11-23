// TOTK: Cinematic post-processing & depth-aware HDR shader pipeline (TOTK_CINEMATIC=0..3, key 6).
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CIN_OFF = 0,
    CIN_BALANCED = 1,   // Balanced Filmic HDR (depth AO, dehaze, warm-sun/cool-shadow grade, soft HDR bloom, vignette, fine grain)
    CIN_GOLDEN = 2,     // Golden Cinema (warmer sunlit highlights, richer emerald/amber palette, stronger anamorphic halation)
    CIN_DRAMATIC = 3,   // Dramatic Widescreen (deeper contrast, cooler shadows, stronger lens vignette, 2.39:1 cinema bars)
    CIN_NMODES = 4
};

int cinematic_mode(void);
int cinematic_enabled(void);
void cinematic_set(int mode);
void cinematic_cycle(void);
const char *cinematic_mode_name(int mode);
void cinematic_note_camera(const float *cam, int n);
void cinematic_frame_end(void);

#ifdef __OBJC__
#import <Metal/Metal.h>
id<MTLResidencySet> cinematic_residency(void);
// Pre-UI linear HDR + G-buffer depth pass (runs before TOTK's compose shader 0ac9a54938974396).
// Returns the processed RGBA16Float HDR texture, or nil when off / unavailable.
id<MTLTexture> cinematic_scene_encode(id<MTL4CommandBuffer> cb, id<MTLTexture> src, id<MTLTexture> depth);
// Final present-pass pipeline state (optical vignette, subtle 35mm grain, optional 2.39:1 letterbox).
id<MTLRenderPipelineState> cinematic_present_pso(id<MTLDevice> dev, MTLPixelFormat format);
// Fill the 4 cinematic floats appended to the present uniform buffer (uv[8..11]).
void cinematic_present_params(float out4[4], uint64_t frame_index, float dst_w, float dst_h);
#endif

#ifdef __cplusplus
}
#endif
