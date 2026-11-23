#include "mc_metal_render.h"
#include "mc_assets.h"
#include "mc_snapshot.h"
#include "minecraft_overlay.h"

#include <math.h>
#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Constants & Shared Limits
 * ========================================================================= */

#define MC_ATLAS_W           2048u
#define MC_ATLAS_H           1024u
#define MC_MAX_WORLD_QUADS   32768u
#define MC_MAX_GUI_QUADS     16384u
#define MC_MAX_PAPER_QUADS   2048u
#define MC_RING_SLOTS        3
#define MC_ALL_SLOTS         4 /* 0..2 Metal 4 present, 3 classic/screenshot */
#define MC_PAPERDOLL_W       256u
#define MC_PAPERDOLL_H       384u
#define MC_PSO_CACHE_MAX     8
#define MC_WATER_FRAMES      32
#define MC_LAVA_FRAMES       20

/* Vertex flags for 3D geometry */
enum {
    MC_V3D_GLINT       = 1u << 0,
    MC_V3D_CUTOUT      = 1u << 1,
    MC_V3D_FLASH_WHITE = 1u << 2,
    MC_V3D_NO_GBUF     = 1u << 3,
    MC_V3D_EMISSIVE    = 1u << 4
};

/* Vertex modes for 2D GUI geometry */
enum {
    MC_G2D_MODE_ATLAS     = 0,
    MC_G2D_MODE_SOLID     = 1,
    MC_G2D_MODE_PAPERDOLL = 2
};

/* =========================================================================
 * GPU Structs (C11 <-> Metal Shading Language)
 * ========================================================================= */

typedef struct {
    float    pos[3];
    float    uv[2];
    float    rect[4];      /* u0, v0, du, dv in normalized atlas space */
    float    shade;        /* Face lighting + block light factor */
    uint32_t rgba;         /* Packed RGBA8 tint */
    uint16_t flags;        /* MC_V3D_* bitmask */
    uint16_t pad;
} McVert3D;

typedef struct {
    _Alignas(16) float vp_rows[4][4];
    _Alignas(16) float tint_mul[4];
    float    canvas_w;
    float    canvas_h;
    float    time_sec;
    uint32_t ignore_gbuf;
    uint32_t target_is_linear;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
} McWorldUniforms;

typedef struct {
    float    pos[2];       /* Virtual GUI space [0..gui_w, 0..gui_h] */
    float    uv[2];        /* Normalized texcoords in atlas or paperdoll */
    float    rect[4];      /* Atlas subrect [u0, v0, du, dv] */
    uint32_t rgba;         /* Packed RGBA8 */
    uint16_t mode;         /* MC_G2D_MODE_* */
    uint16_t flags;        /* McGuiQuadFlags */
} McVert2D;

typedef struct {
    float gui_w;
    float gui_h;
    float time_sec;
    float atlas_w;
    float atlas_h;
    float pad[3];
} McGuiUniforms;

/* =========================================================================
 * CPU Atlas & Mesh Caches
 * ========================================================================= */

typedef struct {
    float    u0, v0, u1, v1;
    uint16_t w, h;
    uint8_t  loaded;
} McAtlasRect;

typedef struct {
    float x, y, z;
    float nx, ny, nz;
    float u0, v0, u1, v1;
    float w_norm, h_norm;
    float face_shade;
} McExtrudedQuad;

typedef struct {
    uint8_t         built;
    uint16_t        count;
    McExtrudedQuad *quads;
} McExtrudedCache;

typedef struct {
    MTLPixelFormat              fmt;
    id<MTLRenderPipelineState>  pso_opaque;
    id<MTLRenderPipelineState>  pso_blend;
} McWorldPsoEntry;

typedef struct {
    MTLPixelFormat              fmt;
    id<MTLRenderPipelineState>  pso_blend;
    id<MTLRenderPipelineState>  pso_crosshair;
} McGuiPsoEntry;

/* =========================================================================
 * Metal Compiler Shaders (Self-Contained MSL)
 * ========================================================================= */

static const char *const k_mc_msl_source =
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"\n"
"struct McVert3D {\n"
"    packed_float3 pos;\n"
"    packed_float2 uv;\n"
"    packed_float4 rect;\n"
"    float         shade;\n"
"    uint          rgba;\n"
"    ushort        flags;\n"
"    ushort        pad;\n"
"};\n"
"\n"
"struct McWorldUniforms {\n"
"    float4   vp_rows[4];\n"
"    float4   tint_mul;\n"
"    float    canvas_w;\n"
"    float    canvas_h;\n"
"    float    time_sec;\n"
"    uint     ignore_gbuf;\n"
"    uint     target_is_linear;\n"
"    uint     pad0;\n"
"    uint     pad1;\n"
"    uint     pad2;\n"
"};\n"
"\n"
"struct McV3DOut {\n"
"    float4 pos   [[position]];\n"
"    float2 uv;\n"
"    float4 rect;\n"
"    float4 color;\n"
"    float  shade;\n"
"    uint   flags;\n"
"};\n"
"\n"
"static float4 unpack_rgba8(uint c) {\n"
"    return float4(float(c & 0xffu), float((c >> 8) & 0xffu),\n"
"                  float((c >> 16) & 0xffu), float((c >> 24) & 0xffu)) / 255.0;\n"
"}\n"
"\n"
"static float3 srgb_to_linear(float3 c) {\n"
"    float3 lo = c / 12.92;\n"
"    float3 hi = pow(max((c + 0.055) / 1.055, float3(0.0)), float3(2.4));\n"
"    return select(lo, hi, c > float3(0.04045));\n"
"}\n"
"\n"
"static float3 linear_to_srgb(float3 c) {\n"
"    float3 lo = c * 12.92;\n"
"    float3 hi = 1.055 * pow(max(c, float3(0.0)), float3(1.0 / 2.4)) - 0.055;\n"
"    return select(lo, hi, c > float3(0.0031308));\n"
"}\n"
"\n"
"kernel void mc_probe_luma_cs(device float4 *env_out           [[buffer(0)]],\n"
"                             constant McWorldUniforms &u       [[buffer(1)]],\n"
"                             texture2d<float, access::read> sc [[texture(0)]],\n"
"                             uint tid                          [[thread_index_in_threadgroup]]) {\n"
"    threadgroup float4 s_acc[64];\n"
"    uint sw = sc.get_width();\n"
"    uint sh = sc.get_height();\n"
"    float4 val = float4(0.0);\n"
"    if (sw > 4u && sh > 4u && tid < 64u) {\n"
"        uint gx = tid & 7u;\n"
"        uint gy = tid >> 3u;\n"
"        float u0 = 0.14 + (float(gx) / 7.0) * 0.72;\n"
"        float v0 = 0.16 + (float(gy) / 7.0) * 0.56;\n"
"        uint2 px = uint2(clamp(u0 * float(sw), 0.0, float(sw - 1u)),\n"
"                         clamp(v0 * float(sh), 0.0, float(sh - 1u)));\n"
"        float3 c = max(sc.read(px).rgb, float3(0.0));\n"
"        if (u.target_is_linear != 0u) {\n"
"            c = linear_to_srgb(clamp(c, 0.0, 2.0));\n"
"        }\n"
"        c = clamp(c, 0.0, 1.0);\n"
"        float l = dot(c, float3(0.2126, 0.7152, 0.0722));\n"
"        val = float4(c, l);\n"
"    }\n"
"    s_acc[tid] = val;\n"
"    threadgroup_barrier(mem_flags::mem_threadgroup);\n"
"    for (uint s = 32u; s > 0u; s >>= 1u) {\n"
"        if (tid < s) s_acc[tid] += s_acc[tid + s];\n"
"        threadgroup_barrier(mem_flags::mem_threadgroup);\n"
"    }\n"
"    if (tid == 0u) {\n"
"        float4 avg = s_acc[0] / 64.0;\n"
"        float target_exp = clamp(0.32 + avg.w * 1.25, 0.44, 0.92);\n"
"        float mx = max(max(avg.x, avg.y), max(avg.z, 0.001));\n"
"        float3 tint = mix(float3(1.0), clamp(avg.xyz / mx, 0.68, 1.0), 0.18);\n"
"        float4 prev = env_out[0];\n"
"        float4 next = float4(tint, target_exp);\n"
"        if (prev.w >= 0.20 && prev.w <= 1.20) {\n"
"            next = mix(prev, next, 0.18);\n"
"        }\n"
"        env_out[0] = next;\n"
"    }\n"
"}\n"
"\n"
"vertex McV3DOut mc_world_vs(const device McVert3D *verts [[buffer(0)]],\n"
"                            constant McWorldUniforms &u  [[buffer(1)]],\n"
"                            uint vid [[vertex_id]]) {\n"
"    McVert3D v = verts[vid];\n"
"    float4 wp = float4(v.pos, 1.0);\n"
"    float4 cg = float4(dot(u.vp_rows[0], wp),\n"
"                       dot(u.vp_rows[1], wp),\n"
"                       dot(u.vp_rows[2], wp),\n"
"                       dot(u.vp_rows[3], wp));\n"
"    McV3DOut o;\n"
"    /* OpenGL [-w,+w] clip Z -> Metal [0,+w] standard-Z depth remap. */\n"
"    o.pos   = float4(cg.x, cg.y, 0.5 * cg.z + 0.5 * cg.w, cg.w);\n"
"    o.uv    = v.uv;\n"
"    o.rect  = v.rect;\n"
"    o.color = unpack_rgba8(v.rgba) * u.tint_mul;\n"
"    o.shade = v.shade;\n"
"    o.flags = uint(v.flags);\n"
"    return o;\n"
"}\n"
"\n"
"fragment float4 mc_world_fs(McV3DOut in [[stage_in]],\n"
"                            constant McWorldUniforms &u [[buffer(1)]],\n"
"                            const device float4 *env_luma [[buffer(2)]],\n"
"                            texture2d<float> atlas      [[texture(0)]],\n"
"                            texture2d<float> glint_tex  [[texture(1)]],\n"
"                            depth2d<float, access::read> gbuf_depth [[texture(2)]],\n"
"                            sampler samp_nearest        [[sampler(0)]],\n"
"                            sampler samp_repeat         [[sampler(1)]]) {\n"
"    if (u.ignore_gbuf == 0u && (in.flags & 8u) == 0u) {\n"
"        uint gw = gbuf_depth.get_width();\n"
"        uint gh = gbuf_depth.get_height();\n"
"        if (gw > 1u && gh > 1u) {\n"
"            float2 scale = float2(float(gw) / max(u.canvas_w, 1.0),\n"
"                                  float(gh) / max(u.canvas_h, 1.0));\n"
"            uint2 dc = uint2(clamp(in.pos.xy * scale, float2(0.0), float2(float(gw - 1u), float(gh - 1u))));\n"
"            float hz = gbuf_depth.read(dc);\n"
"            if (in.pos.z > hz + 0.000002) {\n"
"                discard_fragment();\n"
"            }\n"
"        }\n"
"    }\n"
"    float2 luv = clamp(in.uv, float2(0.0005), float2(0.9995));\n"
"    float2 auv = in.rect.xy + luv * in.rect.zw;\n"
"    float4 tex = atlas.sample(samp_nearest, auv);\n"
"    if ((in.flags & 2u) != 0u && tex.a < 0.5) {\n"
"        discard_fragment();\n"
"    }\n"
"    if (tex.a < 0.01) {\n"
"        discard_fragment();\n"
"    }\n"
"    float4 env = env_luma[0];\n"
"    float3 rgb = tex.rgb * in.color.rgb * in.shade;\n"
"    if ((in.flags & 16u) != 0u) {\n"
"        float exp_em = mix(env.w, 1.0, 0.70);\n"
"        rgb *= mix(env.rgb, float3(1.0), 0.70) * exp_em;\n"
"    } else {\n"
"        rgb *= env.rgb * env.w;\n"
"    }\n"
"    if ((in.flags & 1u) != 0u) {\n"
"        float2 guv1 = in.uv * 1.45 + float2(u.time_sec * 0.14, u.time_sec * 0.28);\n"
"        float2 guv2 = float2(-in.uv.x, in.uv.y) * 1.15 + float2(-u.time_sec * 0.11, u.time_sec * 0.21);\n"
"        float3 g1 = glint_tex.sample(samp_repeat, guv1).rgb;\n"
"        float3 g2 = glint_tex.sample(samp_repeat, guv2).rgb;\n"
"        float lum1 = dot(g1, float3(0.45, 0.15, 0.40));\n"
"        float lum2 = dot(g2, float3(0.45, 0.15, 0.40));\n"
"        float band = smoothstep(0.30, 0.88, max(lum1, lum2));\n"
"        float3 glint_col = float3(0.56, 0.20, 0.94);\n"
"        rgb = clamp(rgb * (1.0 - band * 0.14) + glint_col * band * (0.34 + 0.16 * env.w), 0.0, 1.0);\n"
"    }\n"
"    if ((in.flags & 4u) != 0u) {\n"
"        rgb = mix(rgb, float3(1.0), 0.72);\n"
"    }\n"
"    if (u.target_is_linear != 0u) {\n"
"        rgb = srgb_to_linear(clamp(rgb, 0.0, 1.0));\n"
"    }\n"
"    return float4(rgb, tex.a * in.color.a);\n"
"}\n"
"\n"
"struct McVert2D {\n"
"    packed_float2 pos;\n"
"    packed_float2 uv;\n"
"    packed_float4 rect;\n"
"    uint          rgba;\n"
"    ushort        mode;\n"
"    ushort        flags;\n"
"};\n"
"\n"
"struct McGuiUniforms {\n"
"    float gui_w;\n"
"    float gui_h;\n"
"    float time_sec;\n"
"    float atlas_w;\n"
"    float atlas_h;\n"
"    float pad0;\n"
"    float pad1;\n"
"    float pad2;\n"
"};\n"
"\n"
"struct McV2DOut {\n"
"    float4 pos   [[position]];\n"
"    float2 uv;\n"
"    float4 rect;\n"
"    float4 color;\n"
"    uint   mode;\n"
"    uint   flags;\n"
"};\n"
"\n"
"vertex McV2DOut mc_gui_vs(const device McVert2D *verts [[buffer(0)]],\n"
"                          constant McGuiUniforms &u   [[buffer(1)]],\n"
"                          uint vid [[vertex_id]]) {\n"
"    McVert2D v = verts[vid];\n"
"    float x = (v.pos.x / max(u.gui_w, 1.0)) * 2.0 - 1.0;\n"
"    float y = 1.0 - (v.pos.y / max(u.gui_h, 1.0)) * 2.0;\n"
"    McV2DOut o;\n"
"    o.pos   = float4(x, y, 0.0, 1.0);\n"
"    o.uv    = v.uv;\n"
"    o.rect  = v.rect;\n"
"    o.color = unpack_rgba8(v.rgba);\n"
"    o.mode  = uint(v.mode);\n"
"    o.flags = uint(v.flags);\n"
"    return o;\n"
"}\n"
"\n"
"fragment float4 mc_gui_fs(McV2DOut in [[stage_in]],\n"
"                          constant McGuiUniforms &u [[buffer(1)]],\n"
"                          texture2d<float> atlas    [[texture(0)]],\n"
"                          texture2d<float> glint    [[texture(1)]],\n"
"                          texture2d<float> paper    [[texture(2)]],\n"
"                          sampler samp_nearest      [[sampler(0)]],\n"
"                          sampler samp_repeat       [[sampler(1)]]) {\n"
"    if (in.mode == 1u) {\n"
"        return in.color;\n"
"    }\n"
"    if (in.mode == 2u) {\n"
"        float4 p = paper.sample(samp_nearest, clamp(in.uv, float2(0.0), float2(1.0)));\n"
"        if (p.a < 0.01) discard_fragment();\n"
"        return float4(p.rgb * in.color.rgb, p.a * in.color.a);\n"
"    }\n"
"    float2 eps = float2(0.1 / max(u.atlas_w, 1.0), 0.1 / max(u.atlas_h, 1.0));\n"
"    float2 rmin = in.rect.xy + eps;\n"
"    float2 rmax = in.rect.xy + in.rect.zw - eps;\n"
"    float2 auv = clamp(in.rect.xy + in.uv * in.rect.zw, rmin, max(rmin, rmax));\n"
"    float4 tex = atlas.sample(samp_nearest, auv);\n"
"    float4 out = tex * in.color;\n"
"    if ((in.flags & 1u) != 0u && tex.a > 0.05) {\n"
"        float2 guv1 = in.uv * 1.55 + float2(u.time_sec * 0.18, u.time_sec * 0.30);\n"
"        float2 guv2 = float2(-in.uv.x, in.uv.y) * 1.20 + float2(-u.time_sec * 0.13, u.time_sec * 0.22);\n"
"        float3 g1 = glint.sample(samp_repeat, guv1).rgb;\n"
"        float3 g2 = glint.sample(samp_repeat, guv2).rgb;\n"
"        float lum1 = dot(g1, float3(0.45, 0.15, 0.40));\n"
"        float lum2 = dot(g2, float3(0.45, 0.15, 0.40));\n"
"        float band = smoothstep(0.32, 0.88, max(lum1, lum2));\n"
"        float3 glint_col = float3(0.58, 0.22, 0.95);\n"
"        out.rgb = clamp(out.rgb * (1.0 - band * 0.12) + glint_col * band * 0.34 * out.a, 0.0, 1.0);\n"
"    }\n"
"    if ((in.flags & 4u) != 0u) {\n"
"        /* Premultiply crosshair so transparent corners leave destination untouched during 1-dst blend. */\n"
"        return float4(out.rgb * out.a, out.a);\n"
"    }\n"
"    return out;\n"
"}\n";

/* =========================================================================
 * Renderer State
 * ========================================================================= */

static struct {
    int                         initialized;
    pthread_mutex_t             mu;
    id<MTLDevice>               dev;
    id<MTL4CommandQueue>        q4;
    id<MTLResidencySet>         res;

    id<MTLLibrary>              lib;
    id<MTLFunction>             vs_world;
    id<MTLFunction>             fs_world;
    id<MTLFunction>             vs_gui;
    id<MTLFunction>             fs_gui;
    id<MTLComputePipelineState> pso_luma;

    id<MTLDepthStencilState>    dss_rw;         /* LessEqual, write YES */
    id<MTLDepthStencilState>    dss_ro;         /* LessEqual, write NO */
    id<MTLDepthStencilState>    dss_none;       /* Always, write NO */

    id<MTLSamplerState>         samp_nearest;
    id<MTLSamplerState>         samp_repeat;

    id<MTLTexture>              atlas_tex;
    id<MTLTexture>              glint_tex;
    id<MTLTexture>              dummy_depth;
    id<MTLTexture>              dummy_rgba;
    McAtlasRect                 rects[MC_TEX_COUNT];
    uint8_t                     font_widths[256];
    uint32_t                    grass_rgba;
    McExtrudedCache             extruded[MC_TEX_COUNT];

    id<MTLBuffer>               quad_ib;

    /* Replay Thread (Pass a + b) triple-buffered resources */
    int                         world_slot;
    id<MTLBuffer>               world_vb[MC_RING_SLOTS];
    id<MTLBuffer>               world_ub_w[MC_RING_SLOTS];
    id<MTLBuffer>               world_ub_vm[MC_RING_SLOTS];
    id<MTL4ArgumentTable>       world_args[MC_RING_SLOTS];
    id<MTL4ArgumentTable>       luma_args[MC_RING_SLOTS];
    id<MTLBuffer>               env_luma_buf;
    id<MTLBuffer>               env_default_buf;
    id<MTLTexture>              mc_depth;
    NSUInteger                  mc_depth_w;
    NSUInteger                  mc_depth_h;
    McRenderSnapshot            replay_snap;

    /* Present Thread (Pass c) 4-slot resources (0..2 Metal4, 3 Classic/Shot) */
    id<MTLBuffer>               gui_vb[MC_ALL_SLOTS];
    id<MTLBuffer>               gui_ub[MC_ALL_SLOTS];
    id<MTL4ArgumentTable>       gui_args[MC_ALL_SLOTS];
    id<MTLTexture>              paper_tex[MC_ALL_SLOTS];
    id<MTLTexture>              paper_depth[MC_ALL_SLOTS];
    id<MTLBuffer>               paper_vb[MC_ALL_SLOTS];
    id<MTLBuffer>               paper_ub[MC_ALL_SLOTS];
    id<MTL4ArgumentTable>       paper_args[MC_ALL_SLOTS];
    McRenderSnapshot            present_snap[MC_ALL_SLOTS];
    uint8_t                     pre_copied[MC_ALL_SLOTS];

    /* Pipeline caches keyed by color target pixelFormat */
    McWorldPsoEntry             world_psos[MC_PSO_CACHE_MAX];
    int                         world_pso_n;
    McGuiPsoEntry               gui_psos[MC_PSO_CACHE_MAX];
    int                         gui_pso_n;
} g_r = {
    .mu = PTHREAD_MUTEX_INITIALIZER
};

/* =========================================================================
 * Residency Helpers
 * ========================================================================= */

static void res_track(id<MTLAllocation> obj) {
    if (!obj || !g_r.res) return;
    pthread_mutex_lock(&g_r.mu);
    [g_r.res addAllocation:obj];
    [g_r.res commit];
    pthread_mutex_unlock(&g_r.mu);
}

static void res_drop(id<MTLAllocation> obj) {
    if (!obj || !g_r.res) return;
    pthread_mutex_lock(&g_r.mu);
    [g_r.res removeAllocation:obj];
    [g_r.res commit];
    pthread_mutex_unlock(&g_r.mu);
}

/* =========================================================================
 * Color & Math Helpers
 * ========================================================================= */

static inline uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

static inline uint32_t tint_rgba_mul(uint32_t c, float mul_rgb, float mul_a) {
    uint32_t r = (uint32_t)fminf(255.0f, fmaxf(0.0f, (float)(c & 0xffu) * mul_rgb));
    uint32_t g = (uint32_t)fminf(255.0f, fmaxf(0.0f, (float)((c >> 8) & 0xffu) * mul_rgb));
    uint32_t b = (uint32_t)fminf(255.0f, fmaxf(0.0f, (float)((c >> 16) & 0xffu) * mul_rgb));
    uint32_t a = (uint32_t)fminf(255.0f, fmaxf(0.0f, (float)((c >> 24) & 0xffu) * mul_a));
    return pack_rgba((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a);
}

static inline uint32_t tint_rgba_blend(uint32_t a, uint32_t b) {
    uint32_t r = ((a & 0xffu) * (b & 0xffu)) / 255u;
    uint32_t g = (((a >> 8) & 0xffu) * ((b >> 8) & 0xffu)) / 255u;
    uint32_t bl = (((a >> 16) & 0xffu) * ((b >> 16) & 0xffu)) / 255u;
    uint32_t al = (((a >> 24) & 0xffu) * ((b >> 24) & 0xffu)) / 255u;
    return pack_rgba((uint8_t)r, (uint8_t)g, (uint8_t)bl, (uint8_t)al);
}

typedef struct { float m[16]; } McMat4; /* row-major */

static McMat4 mat4_identity(void) {
    McMat4 r = {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    return r;
}

static McMat4 mat4_mul(McMat4 a, McMat4 b) {
    McMat4 r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    }
    return r;
}

static McMat4 mat4_translate(float x, float y, float z) {
    McMat4 r = mat4_identity();
    r.m[3] = x; r.m[7] = y; r.m[11] = z;
    return r;
}

static McMat4 mat4_scale(float x, float y, float z) {
    McMat4 r = {{x,0,0,0, 0,y,0,0, 0,0,z,0, 0,0,0,1}};
    return r;
}

static McMat4 mat4_rot_x(float a) {
    float c = cosf(a), s = sinf(a);
    return (McMat4){{1,0,0,0, 0,c,-s,0, 0,s,c,0, 0,0,0,1}};
}

static McMat4 mat4_rot_y(float a) {
    float c = cosf(a), s = sinf(a);
    return (McMat4){{c,0,s,0, 0,1,0,0, -s,0,c,0, 0,0,0,1}};
}

static McMat4 mat4_rot_z(float a) {
    float c = cosf(a), s = sinf(a);
    return (McMat4){{c,-s,0,0, s,c,0,0, 0,0,1,0, 0,0,0,1}};
}

static void mat4_xf_pt(McMat4 m, float x, float y, float z, float out[3]) {
    out[0] = m.m[0] * x + m.m[1] * y + m.m[2]  * z + m.m[3];
    out[1] = m.m[4] * x + m.m[5] * y + m.m[6]  * z + m.m[7];
    out[2] = m.m[8] * x + m.m[9] * y + m.m[10] * z + m.m[11];
}

static McMat4 mat4_perspective_gl(float fov_rad, float aspect, float zn, float zf) {
    float f = 1.0f / tanf(fov_rad * 0.5f);
    McMat4 r = {{0}};
    r.m[0]  = f / fmaxf(aspect, 0.1f);
    r.m[5]  = f;
    r.m[10] = -(zf + zn) / (zf - zn);
    r.m[11] = -(2.0f * zf * zn) / (zf - zn);
    r.m[14] = -1.0f;
    return r;
}

/* =========================================================================
 * Atlas Construction from Real Minecraft Assets
 * ========================================================================= */

static void blit_tex_rgba(uint8_t *dst, uint32_t dst_w, uint32_t dx, uint32_t dy,
                          const McTexture *src) {
    for (int y = 0; y < src->height; y++) {
        uint8_t *drow = dst + ((size_t)(dy + (uint32_t)y) * dst_w + dx) * 4u;
        const uint8_t *srow = src->rgba + ((size_t)y * (size_t)src->width) * 4u;
        memcpy(drow, srow, (size_t)src->width * 4u);
    }
}

typedef struct {
    int   id;
    int   w, h;
} McPackItem;

static int pack_cmp_h(const void *a, const void *b) {
    const McPackItem *pa = (const McPackItem *)a;
    const McPackItem *pb = (const McPackItem *)b;
    if (pb->h != pa->h) return pb->h - pa->h;
    if (pb->w != pa->w) return pb->w - pa->w;
    return pa->id - pb->id;
}

static void build_atlas(void) {
    mc_assets_init(NULL);
    const uint8_t *fw = mc_asset_font_widths();
    memcpy(g_r.font_widths, fw, sizeof g_r.font_widths);

    const McTexture *gcmap = mc_asset_tex(MC_TEX_COLORMAP_GRASS);
    uint32_t gpx = mc_asset_tex_pixel(gcmap, 127, 127);
    g_r.grass_rgba = gpx ? (gpx | 0xFF000000u) : 0xFF6BBD7Cu;

    McPackItem items[MC_TEX_COUNT];
    int nitems = 0;
    for (int i = 0; i < MC_TEX_COUNT; i++) {
        const McTexture *t = mc_asset_tex((McTextureId)i);
        if (t && t->loaded && t->rgba && t->width > 0 && t->height > 0) {
            items[nitems++] = (McPackItem){ i, t->width, t->height };
        }
    }
    qsort(items, (size_t)nitems, sizeof items[0], pack_cmp_h);

    size_t atlas_bytes = (size_t)MC_ATLAS_W * (size_t)MC_ATLAS_H * 4u;
    uint8_t *pixels = (uint8_t *)calloc(1u, atlas_bytes);
    if (!pixels) return;

    const uint32_t pad = 2u;
    uint32_t cx = pad, cy = pad, row_h = 0u;
    for (int i = 0; i < nitems; i++) {
        const McTexture *t = mc_asset_tex((McTextureId)items[i].id);
        uint32_t tw = (uint32_t)t->width, th = (uint32_t)t->height;
        if (cx + tw + pad > MC_ATLAS_W) {
            cx = pad;
            cy += row_h + pad;
            row_h = 0u;
        }
        if (cy + th + pad > MC_ATLAS_H) {
            fprintf(stderr, "[mc-metal] ERROR: atlas overflow at %s (%ux%u)\n",
                    t->rel_path ? t->rel_path : "?", tw, th);
            break;
        }
        blit_tex_rgba(pixels, MC_ATLAS_W, cx, cy, t);
        g_r.rects[items[i].id] = (McAtlasRect){
            .u0 = (float)cx / (float)MC_ATLAS_W,
            .v0 = (float)cy / (float)MC_ATLAS_H,
            .u1 = (float)(cx + tw) / (float)MC_ATLAS_W,
            .v1 = (float)(cy + th) / (float)MC_ATLAS_H,
            .w = (uint16_t)tw,
            .h = (uint16_t)th,
            .loaded = 1
        };
        cx += tw + pad;
        if (th > row_h) row_h = th;
    }

    MTLTextureDescriptor *ad = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:MC_ATLAS_W
                                    height:MC_ATLAS_H
                                 mipmapped:NO];
    ad.usage = MTLTextureUsageShaderRead;
    ad.storageMode = MTLStorageModeShared;
    g_r.atlas_tex = [g_r.dev newTextureWithDescriptor:ad];
    if (g_r.atlas_tex) {
        [g_r.atlas_tex replaceRegion:MTLRegionMake2D(0, 0, MC_ATLAS_W, MC_ATLAS_H)
                         mipmapLevel:0
                           withBytes:pixels
                         bytesPerRow:MC_ATLAS_W * 4u];
        res_track(g_r.atlas_tex);
    }
    free(pixels);

    /* Dedicated repeatable glint texture (128x128) */
    const McTexture *gt = mc_asset_tex(MC_TEX_MISC_GLINT_ITEM);
    if (gt && gt->loaded && gt->rgba) {
        MTLTextureDescriptor *gd = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:(NSUInteger)gt->width
                                        height:(NSUInteger)gt->height
                                     mipmapped:NO];
        gd.usage = MTLTextureUsageShaderRead;
        gd.storageMode = MTLStorageModeShared;
        g_r.glint_tex = [g_r.dev newTextureWithDescriptor:gd];
        if (g_r.glint_tex) {
            [g_r.glint_tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)gt->width, (NSUInteger)gt->height)
                             mipmapLevel:0
                               withBytes:gt->rgba
                             bytesPerRow:(NSUInteger)gt->width * 4u];
            res_track(g_r.glint_tex);
        }
    }

    /* 1x1 dummy textures so Metal 4 slots are always cleanly bound */
    MTLTextureDescriptor *dd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:1 height:1 mipmapped:NO];
    dd.usage = MTLTextureUsageShaderRead;
    dd.storageMode = MTLStorageModePrivate;
    g_r.dummy_depth = [g_r.dev newTextureWithDescriptor:dd];
    if (g_r.dummy_depth) res_track(g_r.dummy_depth);

    MTLTextureDescriptor *drd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:1 height:1 mipmapped:NO];
    drd.usage = MTLTextureUsageShaderRead;
    drd.storageMode = MTLStorageModeShared;
    g_r.dummy_rgba = [g_r.dev newTextureWithDescriptor:drd];
    if (g_r.dummy_rgba) {
        static const uint32_t k_zero = 0u;
        [g_r.dummy_rgba replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&k_zero bytesPerRow:4];
        res_track(g_r.dummy_rgba);
    }
}

/* =========================================================================
 * Pipeline & Depth Stencil Cache
 * ========================================================================= */

static const McWorldPsoEntry *get_world_pso(MTLPixelFormat fmt) {
    pthread_mutex_lock(&g_r.mu);
    for (int i = 0; i < g_r.world_pso_n; i++) {
        if (g_r.world_psos[i].fmt == fmt) {
            const McWorldPsoEntry *e = &g_r.world_psos[i];
            pthread_mutex_unlock(&g_r.mu);
            return e;
        }
    }
    if (g_r.world_pso_n >= MC_PSO_CACHE_MAX || !g_r.vs_world || !g_r.fs_world) {
        pthread_mutex_unlock(&g_r.mu);
        return NULL;
    }

    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = g_r.vs_world;
    d.fragmentFunction = g_r.fs_world;
    d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    d.colorAttachments[0].pixelFormat = fmt;
    d.colorAttachments[0].blendingEnabled = NO;

    NSError *err = nil;
    id<MTLRenderPipelineState> pso_op = [g_r.dev newRenderPipelineStateWithDescriptor:d error:&err];
    if (!pso_op) {
        fprintf(stderr, "[mc-metal] world opaque PSO failed (fmt=%lu): %s\n",
                (unsigned long)fmt, err ? err.localizedDescription.UTF8String : "?");
        pthread_mutex_unlock(&g_r.mu);
        return NULL;
    }

    d.colorAttachments[0].blendingEnabled = YES;
    d.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    d.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    id<MTLRenderPipelineState> pso_bl = [g_r.dev newRenderPipelineStateWithDescriptor:d error:&err];
    if (!pso_bl) {
        fprintf(stderr, "[mc-metal] world blend PSO failed (fmt=%lu): %s\n",
                (unsigned long)fmt, err ? err.localizedDescription.UTF8String : "?");
        pthread_mutex_unlock(&g_r.mu);
        return NULL;
    }

    McWorldPsoEntry *slot = &g_r.world_psos[g_r.world_pso_n++];
    slot->fmt = fmt;
    slot->pso_opaque = pso_op;
    slot->pso_blend = pso_bl;
    pthread_mutex_unlock(&g_r.mu);
    return slot;
}

static const McGuiPsoEntry *get_gui_pso(MTLPixelFormat fmt) {
    pthread_mutex_lock(&g_r.mu);
    for (int i = 0; i < g_r.gui_pso_n; i++) {
        if (g_r.gui_psos[i].fmt == fmt) {
            const McGuiPsoEntry *e = &g_r.gui_psos[i];
            pthread_mutex_unlock(&g_r.mu);
            return e;
        }
    }
    if (g_r.gui_pso_n >= MC_PSO_CACHE_MAX || !g_r.vs_gui || !g_r.fs_gui) {
        pthread_mutex_unlock(&g_r.mu);
        return NULL;
    }

    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = g_r.vs_gui;
    d.fragmentFunction = g_r.fs_gui;
    d.colorAttachments[0].pixelFormat = fmt;
    d.colorAttachments[0].blendingEnabled = YES;
    d.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    d.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    NSError *err = nil;
    id<MTLRenderPipelineState> pso_bl = [g_r.dev newRenderPipelineStateWithDescriptor:d error:&err];
    if (!pso_bl) {
        fprintf(stderr, "[mc-metal] gui blend PSO failed (fmt=%lu): %s\n",
                (unsigned long)fmt, err ? err.localizedDescription.UTF8String : "?");
        pthread_mutex_unlock(&g_r.mu);
        return NULL;
    }

    /* Crosshair invert blend (OneMinusDstColor, OneMinusSrcAlpha). */
    d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOneMinusDestinationColor;
    d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorZero;
    d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;

    id<MTLRenderPipelineState> pso_xh = [g_r.dev newRenderPipelineStateWithDescriptor:d error:&err];
    if (!pso_xh) {
        fprintf(stderr, "[mc-metal] gui crosshair PSO failed (fmt=%lu): %s\n",
                (unsigned long)fmt, err ? err.localizedDescription.UTF8String : "?");
        pthread_mutex_unlock(&g_r.mu);
        return NULL;
    }

    McGuiPsoEntry *slot = &g_r.gui_psos[g_r.gui_pso_n++];
    slot->fmt = fmt;
    slot->pso_blend = pso_bl;
    slot->pso_crosshair = pso_xh;
    pthread_mutex_unlock(&g_r.mu);
    return slot;
}

/* =========================================================================
 * Subrect Mapping Helpers
 * ========================================================================= */

static inline void atlas_subrect(McTextureId tid,
                                 float sx0, float sy0, float sx1, float sy1,
                                 float out_rect[4]) {
    if ((unsigned)tid >= (unsigned)MC_TEX_COUNT || !g_r.rects[tid].loaded) {
        out_rect[0] = out_rect[1] = out_rect[2] = out_rect[3] = 0.0f;
        return;
    }
    McAtlasRect r = g_r.rects[tid];
    float rw = r.u1 - r.u0, rh = r.v1 - r.v0;
    float u0 = r.u0 + sx0 * rw;
    float v0 = r.v0 + sy0 * rh;
    float u1 = r.u0 + sx1 * rw;
    float v1 = r.v0 + sy1 * rh;
    out_rect[0] = u0;
    out_rect[1] = v0;
    out_rect[2] = u1 - u0;
    out_rect[3] = v1 - v0;
}

static inline void atlas_px_rect(McTextureId tid,
                                 float px0, float py0, float pw, float ph,
                                 float out_rect[4]) {
    if ((unsigned)tid >= (unsigned)MC_TEX_COUNT || !g_r.rects[tid].loaded) {
        out_rect[0] = out_rect[1] = out_rect[2] = out_rect[3] = 0.0f;
        return;
    }
    float tw = (float)g_r.rects[tid].w, th = (float)g_r.rects[tid].h;
    atlas_subrect(tid, px0 / tw, py0 / th, (px0 + pw) / tw, (py0 + ph) / th, out_rect);
}

static inline void block_face_tex_rect(McBlockType bt, int face, uint8_t facing,
                                       uint64_t tick, float rect[4],
                                       uint32_t *out_rgba, int *out_cutout,
                                       int *out_emissive) {
    /* face: 0=-X, 1=+X, 2=-Y, 3=+Y, 4=-Z, 5=+Z */
    *out_rgba = 0xFFFFFFFFu;
    *out_cutout = 0;
    *out_emissive = 0;

    static const uint8_t k_front_face[4] = { 4, 5, 0, 1 }; /* N(-Z), S(+Z), W(-X), E(+X) */
    uint8_t front = k_front_face[facing & 3u];
    McTextureId tid = MC_TEX_BLOCK_STONE;

    switch (bt) {
        case MC_BLK_TNT:
            tid = (face == 3) ? MC_TEX_BLOCK_TNT_TOP :
                  (face == 2) ? MC_TEX_BLOCK_TNT_BOTTOM : MC_TEX_BLOCK_TNT_SIDE;
            break;
        case MC_BLK_OAK_LOG:
            tid = (face == 2 || face == 3) ? MC_TEX_BLOCK_OAK_LOG_TOP : MC_TEX_BLOCK_OAK_LOG;
            break;
        case MC_BLK_OAK_PLANKS:
            tid = MC_TEX_BLOCK_OAK_PLANKS;
            break;
        case MC_BLK_GRASS:
            if (face == 3) {
                tid = MC_TEX_BLOCK_GRASS_TOP;
                *out_rgba = g_r.grass_rgba;
            } else if (face == 2) {
                tid = MC_TEX_BLOCK_DIRT;
            } else {
                tid = MC_TEX_BLOCK_GRASS_SIDE;
            }
            break;
        case MC_BLK_DIRT:
            tid = MC_TEX_BLOCK_DIRT;
            break;
        case MC_BLK_STONE:
            tid = MC_TEX_BLOCK_STONE;
            break;
        case MC_BLK_COBBLESTONE:
            tid = MC_TEX_BLOCK_COBBLESTONE;
            break;
        case MC_BLK_OBSIDIAN:
            tid = MC_TEX_BLOCK_OBSIDIAN;
            break;
        case MC_BLK_SAND: tid = MC_TEX_SAND; break;
        case MC_BLK_BED_HEAD:
        case MC_BLK_BED_FOOT:
        case MC_BLK_RED_WOOL: tid = MC_TEX_RED_WOOL; break;
        case MC_BLK_BEDROCK:
            tid = MC_TEX_BLOCK_BEDROCK;
            break;
        case MC_BLK_DIAMOND_BLOCK:
            tid = MC_TEX_BLOCK_DIAMOND_BLOCK;
            break;
        case MC_BLK_GOLD_BLOCK:
            tid = MC_TEX_BLOCK_GOLD_BLOCK;
            break;
        case MC_BLK_GLASS:
        case MC_BLK_GLASS_PANE:
            tid = MC_TEX_BLOCK_GLASS;
            *out_cutout = 1;
            break;
        case MC_BLK_GLOWSTONE:
            tid = MC_TEX_BLOCK_GLOWSTONE;
            *out_emissive = 1;
            break;
        case MC_BLK_CRAFTING_TABLE:
            if (face == 3) tid = MC_TEX_BLOCK_CRAFTING_TABLE_TOP;
            else if (face == 2) tid = MC_TEX_BLOCK_OAK_PLANKS;
            else if (face == (int)front) tid = MC_TEX_BLOCK_CRAFTING_TABLE_FRONT;
            else tid = MC_TEX_BLOCK_CRAFTING_TABLE_SIDE;
            break;
        case MC_BLK_FURNACE:
            if (face == 3 || face == 2) tid = MC_TEX_BLOCK_FURNACE_TOP;
            else if (face == (int)front) tid = MC_TEX_BLOCK_FURNACE_FRONT;
            else tid = MC_TEX_BLOCK_FURNACE_SIDE;
            break;
        case MC_BLK_WATER: {
            uint32_t fr = (uint32_t)((tick / 2u) % MC_WATER_FRAMES);
            float v0 = (float)fr / (float)MC_WATER_FRAMES;
            float v1 = (float)(fr + 1u) / (float)MC_WATER_FRAMES;
            atlas_subrect(MC_TEX_BLOCK_WATER_STILL, 0.0f, v0, 1.0f, v1, rect);
            *out_rgba = pack_rgba(100, 165, 255, 196);
            return;
        }
        case MC_BLK_LAVA: {
            uint32_t fr = (uint32_t)((tick / 3u) % MC_LAVA_FRAMES);
            float v0 = (float)fr / (float)MC_LAVA_FRAMES;
            float v1 = (float)(fr + 1u) / (float)MC_LAVA_FRAMES;
            atlas_subrect(MC_TEX_BLOCK_LAVA_STILL, 0.0f, v0, 1.0f, v1, rect);
            *out_emissive = 1;
            return;
        }
        case MC_BLK_TORCH:
            tid = MC_TEX_BLOCK_TORCH;
            *out_cutout = 1;
            *out_emissive = 1;
            break;
        default:
            tid = MC_TEX_BLOCK_STONE;
            break;
    }
    atlas_subrect(tid, 0.0f, 0.0f, 1.0f, 1.0f, rect);
}

static McBlockType item_to_block_type(McItemType itm) {
    switch (itm) {
        case MC_ITM_TNT:            return MC_BLK_TNT;
        case MC_ITM_OAK_LOG:        return MC_BLK_OAK_LOG;
        case MC_ITM_OAK_PLANKS:     return MC_BLK_OAK_PLANKS;
        case MC_ITM_GRASS_BLOCK:    return MC_BLK_GRASS;
        case MC_ITM_DIRT:           return MC_BLK_DIRT;
        case MC_ITM_STONE:          return MC_BLK_STONE;
        case MC_ITM_COBBLESTONE:    return MC_BLK_COBBLESTONE;
        case MC_ITM_OBSIDIAN:       return MC_BLK_OBSIDIAN;
        case MC_ITM_SAND: return MC_BLK_SAND;
        case MC_ITM_RED_WOOL: return MC_BLK_RED_WOOL;
        case MC_ITM_RED_BED: return MC_BLK_BED_FOOT;
        case MC_ITM_BEDROCK:        return MC_BLK_BEDROCK;
        case MC_ITM_TORCH:          return MC_BLK_TORCH;
        case MC_ITM_DIAMOND_BLOCK:  return MC_BLK_DIAMOND_BLOCK;
        case MC_ITM_GOLD_BLOCK:     return MC_BLK_GOLD_BLOCK;
        case MC_ITM_GLASS:          return MC_BLK_GLASS;
        case MC_ITM_GLASS_PANE:     return MC_BLK_GLASS_PANE;
        case MC_ITM_GLOWSTONE:      return MC_BLK_GLOWSTONE;
        case MC_ITM_CRAFTING_TABLE: return MC_BLK_CRAFTING_TABLE;
        case MC_ITM_FURNACE:        return MC_BLK_FURNACE;
        default:                    return MC_BLK_AIR;
    }
}

static McTextureId item_to_tex_id(McItemType itm, uint8_t bow_pull_stage) {
    switch (itm) {
        case MC_ITM_RAW_BEEF: return MC_TEX_BEEF;
        case MC_ITM_COOKED_BEEF:           return MC_TEX_ITEM_COOKED_BEEF;
        case MC_ITM_DIAMOND_SWORD:         return MC_TEX_ITEM_DIAMOND_SWORD;
        case MC_ITM_IRON_SWORD:            return MC_TEX_ITEM_IRON_SWORD;
        case MC_ITM_NETHERITE_SWORD:       return MC_TEX_ITEM_NETHERITE_SWORD;
        case MC_ITM_DIAMOND_AXE:           return MC_TEX_ITEM_DIAMOND_AXE;
        case MC_ITM_DIAMOND_PICKAXE:       return MC_TEX_ITEM_DIAMOND_PICKAXE;
        case MC_ITM_BOW:
            if (bow_pull_stage == 1) return MC_TEX_ITEM_BOW_PULLING_0;
            if (bow_pull_stage == 2) return MC_TEX_ITEM_BOW_PULLING_1;
            if (bow_pull_stage >= 3) return MC_TEX_ITEM_BOW_PULLING_2;
            return MC_TEX_ITEM_BOW;
        case MC_ITM_CROSSBOW:              return MC_TEX_ITEM_CROSSBOW_ARROW;
        case MC_ITM_ARROW:                 return MC_TEX_ITEM_ARROW;
        case MC_ITM_SHIELD:                return MC_TEX_ENTITY_SHIELD_NOPATTERN;
        case MC_ITM_ELYTRA:                return MC_TEX_ITEM_ELYTRA;
        case MC_ITM_TOTEM_OF_UNDYING:      return MC_TEX_ITEM_TOTEM_OF_UNDYING;
        case MC_ITM_GOLDEN_APPLE:          return MC_TEX_ITEM_GOLDEN_APPLE;
        case MC_ITM_ENCHANTED_GOLDEN_APPLE:return MC_TEX_ITEM_GOLDEN_APPLE;
        case MC_ITM_APPLE:                 return MC_TEX_ITEM_APPLE;
        case MC_ITM_BREAD:                 return MC_TEX_ITEM_BREAD;
        case MC_ITM_FIREWORK_ROCKET:       return MC_TEX_ITEM_FIREWORK_ROCKET;
        case MC_ITM_FLINT_AND_STEEL:       return MC_TEX_ITEM_FLINT_AND_STEEL;
        case MC_ITM_BONE:                  return MC_TEX_ITEM_BONE;
        case MC_ITM_ENDER_PEARL:           return MC_TEX_ITEM_ENDER_PEARL;
        case MC_ITM_WATER_BUCKET:          return MC_TEX_ITEM_WATER_BUCKET;
        case MC_ITM_LAVA_BUCKET:           return MC_TEX_ITEM_LAVA_BUCKET;
        case MC_ITM_EXP_BOTTLE:            return MC_TEX_ITEM_EXPERIENCE_BOTTLE;
        case MC_ITM_DIAMOND_HELMET:        return MC_TEX_ITEM_DIAMOND_HELMET;
        case MC_ITM_DIAMOND_CHESTPLATE:    return MC_TEX_ITEM_DIAMOND_CHESTPLATE;
        case MC_ITM_DIAMOND_LEGGINGS:      return MC_TEX_ITEM_DIAMOND_LEGGINGS;
        case MC_ITM_DIAMOND_BOOTS:         return MC_TEX_ITEM_DIAMOND_BOOTS;
        case MC_ITM_DIAMOND:               return MC_TEX_ITEM_DIAMOND;
        case MC_ITM_EMERALD:               return MC_TEX_ITEM_EMERALD;
        case MC_ITM_IRON_INGOT:            return MC_TEX_ITEM_IRON_INGOT;
        case MC_ITM_GOLD_INGOT:            return MC_TEX_ITEM_GOLD_INGOT;
        case MC_ITM_REDSTONE:              return MC_TEX_ITEM_REDSTONE;
        case MC_ITM_COAL:                  return MC_TEX_ITEM_COAL;
        case MC_ITM_CHARCOAL:              return MC_TEX_ITEM_CHARCOAL;
        case MC_ITM_STICK:                 return MC_TEX_ITEM_STICK;
        case MC_ITM_GUNPOWDER:             return MC_TEX_ITEM_GUNPOWDER;
        case MC_ITM_ROTTEN_FLESH:          return MC_TEX_ITEM_ROTTEN_FLESH;
        case MC_ITM_SPIDER_EYE:            return MC_TEX_ITEM_SPIDER_EYE;
        case MC_ITM_STRING:                return MC_TEX_ITEM_STRING;
        case MC_ITM_SLIME_BALL:            return MC_TEX_ITEM_SLIME_BALL;
        case MC_ITM_BARRIER:               return MC_TEX_ITEM_BARRIER;
        case MC_ITM_TORCH:                 return MC_TEX_BLOCK_TORCH;
        case MC_ITM_GLASS_PANE:            return MC_TEX_BLOCK_GLASS;
        default:                           return MC_TEX_ITEM_BARRIER;
    }
}

/* =========================================================================
 * Cached 3D Extruded Voxel Item Builder (16x16 PNG -> 3D Voxel Mesh)
 * ========================================================================= */

static const McExtrudedCache *get_extruded_item(McTextureId tid) {
    if ((unsigned)tid >= (unsigned)MC_TEX_COUNT) return NULL;
    McExtrudedCache *c = &g_r.extruded[tid];
    if (c->built) return c;

    const McTexture *t = mc_asset_tex(tid);
    c->built = 1;
    if (!t || !t->loaded || !t->rgba || t->width <= 0 || t->height <= 0 ||
        t->width > 32 || t->height > 32) {
        return c;
    }
    int w = t->width, h = t->height;
    /* Worst-case opaque pixels * 6 faces */
    McExtrudedQuad *buf = (McExtrudedQuad *)calloc((size_t)(w * h * 6), sizeof(McExtrudedQuad));
    if (!buf) return c;

    uint16_t n = 0;
    float pw = 1.0f / (float)w, ph = 1.0f / (float)h;
    float half_d = 0.5f * pw;

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            uint32_t p = mc_asset_tex_pixel(t, px, py);
            if (((p >> 24) & 0xffu) < 128u) continue;

            float x0 = (float)px * pw - 0.5f;
            float y1 = 0.5f - (float)py * ph;
            float u0 = ((float)px + 0.1f) * pw;
            float u1 = ((float)px + 0.9f) * pw;
            float v0 = ((float)py + 0.1f) * ph;
            float v1 = ((float)py + 0.9f) * ph;

            /* Front (+Z) and Back (-Z) pixel face */
            buf[n++] = (McExtrudedQuad){ x0, y1 - ph,  half_d, 0,0,1,  u0,v1, u1,v0, pw, ph, 0.96f };
            buf[n++] = (McExtrudedQuad){ x0, y1 - ph, -half_d, 0,0,-1, u1,v1, u0,v0, pw, ph, 0.85f };

            /* Side walls wherever adjacent texel is transparent */
            if (px == 0 || ((mc_asset_tex_pixel(t, px - 1, py) >> 24) & 0xffu) < 128u) {
                buf[n++] = (McExtrudedQuad){ x0, y1 - ph, -half_d, -1,0,0, u0,v1, u1,v0, half_d*2, ph, 0.78f };
            }
            if (px == w - 1 || ((mc_asset_tex_pixel(t, px + 1, py) >> 24) & 0xffu) < 128u) {
                buf[n++] = (McExtrudedQuad){ x0 + pw, y1 - ph, -half_d, 1,0,0, u0,v1, u1,v0, half_d*2, ph, 0.78f };
            }
            if (py == 0 || ((mc_asset_tex_pixel(t, px, py - 1) >> 24) & 0xffu) < 128u) {
                buf[n++] = (McExtrudedQuad){ x0, y1, -half_d, 0,1,0, u0,v0, u1,v1, pw, half_d*2, 1.0f };
            }
            if (py == h - 1 || ((mc_asset_tex_pixel(t, px, py + 1) >> 24) & 0xffu) < 128u) {
                buf[n++] = (McExtrudedQuad){ x0, y1 - ph, -half_d, 0,-1,0, u0,v0, u1,v1, pw, half_d*2, 0.55f };
            }
        }
    }
    c->quads = buf;
    c->count = n;
    return c;
}

/* =========================================================================
 * Dynamic 3D Mesh Builder
 * ========================================================================= */

typedef struct {
    McVert3D *verts;
    uint32_t  quad_count;
    uint32_t  max_quads;
} McMesh3D;

static inline void push_quad_3d(McMesh3D *m,
                                const float p0[3], const float p1[3],
                                const float p2[3], const float p3[3],
                                const float rect[4],
                                float u0, float v0, float u1, float v1,
                                float shade, uint32_t rgba, uint16_t flags) {
    if (m->quad_count >= m->max_quads) return;
    McVert3D *v = m->verts + (size_t)m->quad_count * 4u;
    v[0] = (McVert3D){ {p0[0],p0[1],p0[2]}, {u0,v0}, {rect[0],rect[1],rect[2],rect[3]}, shade, rgba, flags, 0 };
    v[1] = (McVert3D){ {p1[0],p1[1],p1[2]}, {u1,v0}, {rect[0],rect[1],rect[2],rect[3]}, shade, rgba, flags, 0 };
    v[2] = (McVert3D){ {p2[0],p2[1],p2[2]}, {u1,v1}, {rect[0],rect[1],rect[2],rect[3]}, shade, rgba, flags, 0 };
    v[3] = (McVert3D){ {p3[0],p3[1],p3[2]}, {u0,v1}, {rect[0],rect[1],rect[2],rect[3]}, shade, rgba, flags, 0 };
    m->quad_count++;
}

static void push_pane_quad(McMesh3D *m, McMat4 xf, const float q[4][3],
                           const float n[3], const float rect[4],
                           float u0, float u1,
                           float cu0, float cv0, float cu1, float cv1,
                           float shade, int8_t mining_stage,
                           int flash_white, uint16_t extra_flags, int want_alpha_pass) {
    float p[4][3];
    if (!want_alpha_pass) {
        for (int k = 0; k < 4; k++) mat4_xf_pt(xf, q[k][0], q[k][1], q[k][2], p[k]);
        uint16_t flags = extra_flags | MC_V3D_CUTOUT | (flash_white ? MC_V3D_FLASH_WHITE : 0u);
        push_quad_3d(m, p[0], p[1], p[2], p[3], rect, u0, 0.0f, u1, 1.0f,
                     shade, 0xFFFFFFFFu, flags);
    } else if (mining_stage >= 0 && mining_stage <= 9) {
        float drect[4];
        atlas_subrect((McTextureId)(MC_TEX_BLOCK_DESTROY_0 + mining_stage), 0,0,1,1, drect);
        for (int k = 0; k < 4; k++) {
            mat4_xf_pt(xf, q[k][0] + n[0] * 0.0018f,
                           q[k][1] + n[1] * 0.0018f,
                           q[k][2] + n[2] * 0.0018f, p[k]);
        }
        push_quad_3d(m, p[0], p[1], p[2], p[3], drect, cu0, cv0, cu1, cv1,
                     1.0f, 0xD0FFFFFFu, extra_flags);
    }
}

static void emit_glass_pane_xf(McMesh3D *m, McMat4 xf, uint8_t face_mask,
                               float light_val, int8_t mining_stage, int flash_white,
                               uint16_t extra_flags, int want_alpha_pass,
                               const float k_face_shade[6]) {
    const float e = 1.0f / 16.0f, h = 0.5f;
    float r_noside[4], r_cap[4];
    atlas_px_rect(MC_TEX_BLOCK_GLASS, 7.0f, 0.0f, 2.0f, 16.0f, r_noside);
    atlas_px_rect(MC_TEX_BLOCK_GLASS_PANE_TOP, 7.0f, 7.0f, 2.0f, 2.0f, r_cap);

    static const struct {
        uint8_t bit, face;
        float q[4][3], n[3];
    } k_post_sides[4] = {
        { 0, 0, {{-1.0f/16, +0.5f, -1.0f/16}, {-1.0f/16, +0.5f, +1.0f/16}, {-1.0f/16, -0.5f, +1.0f/16}, {-1.0f/16, -0.5f, -1.0f/16}}, {-1, 0, 0} },
        { 1, 1, {{+1.0f/16, +0.5f, +1.0f/16}, {+1.0f/16, +0.5f, -1.0f/16}, {+1.0f/16, -0.5f, -1.0f/16}, {+1.0f/16, -0.5f, +1.0f/16}}, {+1, 0, 0} },
        { 4, 4, {{+1.0f/16, +0.5f, -1.0f/16}, {-1.0f/16, +0.5f, -1.0f/16}, {-1.0f/16, -0.5f, -1.0f/16}, {+1.0f/16, -0.5f, -1.0f/16}}, { 0, 0,-1} },
        { 5, 5, {{-1.0f/16, +0.5f, +1.0f/16}, {+1.0f/16, +0.5f, +1.0f/16}, {+1.0f/16, -0.5f, +1.0f/16}, {-1.0f/16, -0.5f, +1.0f/16}}, { 0, 0,+1} },
    };
    for (int i = 0; i < 4; i++) {
        if (face_mask & (1u << k_post_sides[i].bit)) continue;
        push_pane_quad(m, xf, k_post_sides[i].q, k_post_sides[i].n, r_noside,
                       0.0f, 1.0f,
                       7.0f/16.0f, 0.0f, 9.0f/16.0f, 1.0f,
                       light_val * k_face_shade[k_post_sides[i].face],
                       mining_stage, flash_white, extra_flags, want_alpha_pass);
    }
    if (face_mask & (1u << 3)) {
        const float qt[4][3] = {{-e,+h,-e}, {+e,+h,-e}, {+e,+h,+e}, {-e,+h,+e}};
        const float nt[3] = {0, 1, 0};
        push_pane_quad(m, xf, qt, nt, r_cap, 0.0f, 1.0f,
                       7.0f/16.0f, 7.0f/16.0f, 9.0f/16.0f, 9.0f/16.0f,
                       light_val * k_face_shade[3], mining_stage, flash_white, extra_flags, want_alpha_pass);
    }
    if (face_mask & (1u << 2)) {
        const float qb[4][3] = {{-e,-h,+e}, {+e,-h,+e}, {+e,-h,-e}, {-e,-h,-e}};
        const float nb[3] = {0, -1, 0};
        push_pane_quad(m, xf, qb, nb, r_cap, 0.0f, 1.0f,
                       7.0f/16.0f, 7.0f/16.0f, 9.0f/16.0f, 9.0f/16.0f,
                       light_val * k_face_shade[2], mining_stage, flash_white, extra_flags, want_alpha_pass);
    }

    static const struct {
        uint8_t bit, z_axis;
        int8_t  sign;
        float   px0, py0;
    } k_arms[4] = {
        { 0, 0, -1, 0.0f, 0.0f }, /* -X West */
        { 1, 0, +1, 9.0f, 9.0f }, /* +X East */
        { 4, 1, -1, 9.0f, 0.0f }, /* -Z North */
        { 5, 1, +1, 0.0f, 9.0f }, /* +Z South */
    };
    for (int i = 0; i < 4; i++) {
        if (!(face_mask & (1u << k_arms[i].bit))) continue;
        float a0 = k_arms[i].sign < 0 ? -h : e;
        float a1 = k_arms[i].sign < 0 ? -e : h;
        float r_pane[4], r_edge[4];
        atlas_px_rect(MC_TEX_BLOCK_GLASS, k_arms[i].px0, 0.0f, 7.0f, 16.0f, r_pane);
        atlas_px_rect(MC_TEX_BLOCK_GLASS_PANE_TOP, 7.0f, k_arms[i].py0, 2.0f, 7.0f, r_edge);
        float cu0 = k_arms[i].px0 / 16.0f, cu1 = (k_arms[i].px0 + 7.0f) / 16.0f;
        float cv0 = k_arms[i].py0 / 16.0f, cv1 = (k_arms[i].py0 + 7.0f) / 16.0f;
        if (k_arms[i].z_axis) {
            const float qw[4][3] = {{-e,+h,a0}, {-e,+h,a1}, {-e,-h,a1}, {-e,-h,a0}};
            const float qe[4][3] = {{+e,+h,a1}, {+e,+h,a0}, {+e,-h,a0}, {+e,-h,a1}};
            const float nw[3] = {-1,0,0}, ne[3] = {1,0,0};
            push_pane_quad(m, xf, qw, nw, r_pane, 1.0f, 0.0f, cu1, 0, cu0, 1,
                           light_val * k_face_shade[0],
                           mining_stage, flash_white, extra_flags, want_alpha_pass);
            push_pane_quad(m, xf, qe, ne, r_pane, 0.0f, 1.0f, cu0, 0, cu1, 1,
                           light_val * k_face_shade[1],
                           mining_stage, flash_white, extra_flags, want_alpha_pass);
            if (face_mask & (1u << 3)) {
                const float qt[4][3] = {{-e,+h,a0}, {+e,+h,a0}, {+e,+h,a1}, {-e,+h,a1}};
                const float nt[3] = {0,1,0};
                push_pane_quad(m, xf, qt, nt, r_edge, 0.0f, 1.0f,
                               7.0f/16.0f, cv0, 9.0f/16.0f, cv1,
                               light_val * k_face_shade[3], mining_stage, flash_white, extra_flags, want_alpha_pass);
            }
            if (face_mask & (1u << 2)) {
                const float qb[4][3] = {{-e,-h,a1}, {+e,-h,a1}, {+e,-h,a0}, {-e,-h,a0}};
                const float nb[3] = {0,-1,0};
                push_pane_quad(m, xf, qb, nb, r_edge, 0.0f, 1.0f,
                               7.0f/16.0f, cv0, 9.0f/16.0f, cv1,
                               light_val * k_face_shade[2], mining_stage, flash_white, extra_flags, want_alpha_pass);
            }
        } else {
            const float qn[4][3] = {{a1,+h,-e}, {a0,+h,-e}, {a0,-h,-e}, {a1,-h,-e}};
            const float qs[4][3] = {{a0,+h,+e}, {a1,+h,+e}, {a1,-h,+e}, {a0,-h,+e}};
            const float nn[3] = {0,0,-1}, ns[3] = {0,0,1};
            push_pane_quad(m, xf, qn, nn, r_pane, 1.0f, 0.0f, cu1, 0, cu0, 1,
                           light_val * k_face_shade[4],
                           mining_stage, flash_white, extra_flags, want_alpha_pass);
            push_pane_quad(m, xf, qs, ns, r_pane, 0.0f, 1.0f, cu0, 0, cu1, 1,
                           light_val * k_face_shade[5],
                           mining_stage, flash_white, extra_flags, want_alpha_pass);
            if (face_mask & (1u << 3)) {
                const float qt[4][3] = {{a0,+h,-e}, {a0,+h,+e}, {a1,+h,+e}, {a1,+h,-e}};
                const float nt[3] = {0,1,0};
                push_pane_quad(m, xf, qt, nt, r_edge, 0.0f, 1.0f,
                               cv0, 7.0f/16.0f, cv1, 9.0f/16.0f,
                               light_val * k_face_shade[3], mining_stage, flash_white, extra_flags, want_alpha_pass);
            }
            if (face_mask & (1u << 2)) {
                const float qb[4][3] = {{a1,-h,-e}, {a1,-h,+e}, {a0,-h,+e}, {a0,-h,-e}};
                const float nb[3] = {0,-1,0};
                push_pane_quad(m, xf, qb, nb, r_edge, 0.0f, 1.0f,
                               cv0, 7.0f/16.0f, cv1, 9.0f/16.0f,
                               light_val * k_face_shade[2], mining_stage, flash_white, extra_flags, want_alpha_pass);
            }
        }
    }
}

static void emit_block_cube_xf(McMesh3D *m, McMat4 xf, McBlockType bt,
                               uint8_t face_mask, uint8_t facing, float light_val,
                               int8_t mining_stage, int flash_white,
                               uint64_t tick, uint16_t extra_flags, int want_alpha_pass) {
    static const float k_face_shade[6] = { 0.78f, 0.78f, 0.55f, 1.00f, 0.85f, 0.85f };
    if (bt == MC_BLK_GLASS_PANE) {
        emit_glass_pane_xf(m, xf, face_mask, light_val, mining_stage, flash_white,
                           extra_flags, want_alpha_pass, k_face_shade);
        return;
    }
    if (bt == MC_BLK_BED_FOOT || bt == MC_BLK_BED_HEAD) {
        if (want_alpha_pass) return;
        float angle = facing == 2 ? (float)M_PI_2 : facing == 3 ? -(float)M_PI_2 : facing == 1 ? (float)M_PI : 0;
        McMat4 bed = mat4_mul(xf, mat4_rot_y(angle));
        for (int leg = -1; leg <= 1; leg += 2) {
            McMat4 leg_xf = mat4_mul(bed, mat4_mul(mat4_translate(leg*.40f,-.405f,bt == MC_BLK_BED_HEAD ? .40f : -.40f),mat4_scale(.18f,.19f,.18f)));
            emit_block_cube_xf(m, leg_xf, MC_BLK_OAK_PLANKS, 63, 0, light_val,-1,0,tick,extra_flags,0);
        }
        McMat4 mattress = mat4_mul(bed,mat4_mul(mat4_translate(0,-.125f,0),mat4_scale(1,.375f,1)));
        emit_block_cube_xf(m,mattress,MC_BLK_RED_WOOL,63,0,light_val,mining_stage,flash_white,tick,extra_flags,0);
        float rect[4], p[4][3];
        atlas_px_rect(MC_TEX_RED_BED,6,bt == MC_BLK_BED_HEAD ? 6 : 28,16,16,rect);
        mat4_xf_pt(bed,-.5f,.063f,.5f,p[0]); mat4_xf_pt(bed,.5f,.063f,.5f,p[1]);
        mat4_xf_pt(bed,.5f,.063f,-.5f,p[2]); mat4_xf_pt(bed,-.5f,.063f,-.5f,p[3]);
        push_quad_3d(m,p[0],p[1],p[2],p[3],rect,0,0,1,1,light_val,0xffffffffu,extra_flags);
        return;
    }
    if (bt == MC_BLK_TORCH) {
        if (want_alpha_pass) return;
        /* Authentic 2x10x2 texel torch post resting cleanly on block bottom (-0.5..+0.125) */
        float hw = 1.0f / 16.0f, top_y = -0.5f + 10.0f / 16.0f;
        float rect_side[4], rect_top[4];
        atlas_px_rect(MC_TEX_BLOCK_TORCH, 7.0f, 6.0f, 2.0f, 10.0f, rect_side);
        atlas_px_rect(MC_TEX_BLOCK_TORCH, 7.0f, 6.0f, 2.0f, 2.0f, rect_top);
        static const float s_c[4][4] = {
            {-1, -1, +1, +1}, {+1, -1, +1, -1}, {+1, +1, -1, -1}, {-1, +1, -1, +1}
        };
        for (int f = 0; f < 4; f++) {
            float p0[3], p1[3], p2[3], p3[3];
            float x0 = s_c[f][0] * hw, z0 = s_c[f][1] * hw, x1 = s_c[f][2] * hw, z1 = s_c[f][3] * hw;
            mat4_xf_pt(xf, x0, top_y, z0, p0);
            mat4_xf_pt(xf, x1, top_y, z1, p1);
            mat4_xf_pt(xf, x1, -0.5f, z1, p2);
            mat4_xf_pt(xf, x0, -0.5f, z0, p3);
            push_quad_3d(m, p0, p1, p2, p3, rect_side, 0,0,1,1, 1.0f, 0xFFFFFFFFu, extra_flags);
        }
        float pt0[3], pt1[3], pt2[3], pt3[3];
        mat4_xf_pt(xf, -hw, top_y, -hw, pt0);
        mat4_xf_pt(xf,  hw, top_y, -hw, pt1);
        mat4_xf_pt(xf,  hw, top_y,  hw, pt2);
        mat4_xf_pt(xf, -hw, top_y,  hw, pt3);
        push_quad_3d(m, pt0, pt1, pt2, pt3, rect_top, 0,0,1,1, 1.0f, 0xFFFFFFFFu, extra_flags);
        return;
    }

    int is_translucent = (bt == MC_BLK_WATER);
    float top_h = (bt == MC_BLK_WATER && (face_mask & (1u << 3))) ? 0.375f : 0.5f;

    /* 6 faces: -X, +X, -Y, +Y, -Z, +Z with top-left -> bottom-left UV order */
    static const float k_quads[6][4][3] = {
        { {-0.5f,+1,-0.5f}, {-0.5f,+1,+0.5f}, {-0.5f,-0.5f,+0.5f}, {-0.5f,-0.5f,-0.5f} }, /* -X */
        { {+0.5f,+1,+0.5f}, {+0.5f,+1,-0.5f}, {+0.5f,-0.5f,-0.5f}, {+0.5f,-0.5f,+0.5f} }, /* +X */
        { {-0.5f,-0.5f,+0.5f}, {+0.5f,-0.5f,+0.5f}, {+0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f} }, /* -Y */
        { {-0.5f,+1,-0.5f}, {+0.5f,+1,-0.5f}, {+0.5f,+1,+0.5f}, {-0.5f,+1,+0.5f} }, /* +Y */
        { {+0.5f,+1,-0.5f}, {-0.5f,+1,-0.5f}, {-0.5f,-0.5f,-0.5f}, {+0.5f,-0.5f,-0.5f} }, /* -Z */
        { {-0.5f,+1,+0.5f}, {+0.5f,+1,+0.5f}, {+0.5f,-0.5f,+0.5f}, {-0.5f,-0.5f,+0.5f} }, /* +Z */
    };
    static const float k_norm[6][3] = {
        {-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}
    };

    for (int f = 0; f < 6; f++) {
        if (!(face_mask & (1u << f))) continue;
        float rect[4];
        uint32_t rgba = 0xFFFFFFFFu;
        int cutout = 0, emissive = 0;
        block_face_tex_rect(bt, f, facing, tick, rect, &rgba, &cutout, &emissive);
        float sh = (emissive ? 1.0f : light_val) * k_face_shade[f];
        uint16_t flags = extra_flags | (cutout ? MC_V3D_CUTOUT : 0u) |
                         (emissive ? MC_V3D_EMISSIVE : 0u) |
                         (flash_white ? MC_V3D_FLASH_WHITE : 0u);

        float p[4][3];
        for (int k = 0; k < 4; k++) {
            float ly = (k_quads[f][k][1] > 0.0f) ? top_h : -0.5f;
            mat4_xf_pt(xf, k_quads[f][k][0], ly, k_quads[f][k][2], p[k]);
        }

        if (is_translucent == want_alpha_pass) {
            push_quad_3d(m, p[0], p[1], p[2], p[3], rect, 0,0,1,1, sh, rgba, flags);
            if (!want_alpha_pass && bt == MC_BLK_GRASS && f != 2 && f != 3) {
                /* Grass side Plains tint overlay outset by 0.8mm so no Z-fighting */
                float orect[4];
                atlas_subrect(MC_TEX_BLOCK_GRASS_SIDE_OVERLAY, 0, 0, 1, 1, orect);
                float op[4][3];
                for (int k = 0; k < 4; k++) {
                    mat4_xf_pt(xf,
                               k_quads[f][k][0] + k_norm[f][0] * 0.0008f,
                               ((k_quads[f][k][1] > 0.0f) ? top_h : -0.5f) + k_norm[f][1] * 0.0008f,
                               k_quads[f][k][2] + k_norm[f][2] * 0.0008f,
                               op[k]);
                }
                push_quad_3d(m, op[0], op[1], op[2], op[3], orect, 0,0,1,1,
                             sh, g_r.grass_rgba, flags | MC_V3D_CUTOUT);
            }
        }

        if (want_alpha_pass && mining_stage >= 0 && mining_stage <= 9) {
            float drect[4];
            atlas_subrect((McTextureId)(MC_TEX_BLOCK_DESTROY_0 + mining_stage), 0,0,1,1, drect);
            float dp[4][3];
            for (int k = 0; k < 4; k++) {
                mat4_xf_pt(xf,
                           k_quads[f][k][0] + k_norm[f][0] * 0.0018f,
                           ((k_quads[f][k][1] > 0.0f) ? top_h : -0.5f) + k_norm[f][1] * 0.0018f,
                           k_quads[f][k][2] + k_norm[f][2] * 0.0018f,
                           dp[k]);
            }
            push_quad_3d(m, dp[0], dp[1], dp[2], dp[3], drect, 0,0,1,1,
                         1.0f, 0xD0FFFFFFu, extra_flags);
        }
    }
}

/* Minecraft articulated cuboid with standard BoxUV net on texture tid.
   Box centered in X and Z around local origin; Y spans [y0, y1] (in meters or scaled by unit). */
static void emit_box_uv(McMesh3D *m, McMat4 xf, McTextureId tid,
                        int u0, int v0, int bw, int bh, int bd,
                        float sx, float sy, float sz,
                        float cx, float cy, float cz,
                        int mirror_u, float shade, uint32_t rgba, uint16_t flags) {
    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    float r_top[4], r_bot[4], r_front[4], r_back[4], r_left[4], r_right[4];
    atlas_px_rect(tid, (float)(u0 + bd),            (float)v0,        (float)bw, (float)bd, r_top);
    atlas_px_rect(tid, (float)(u0 + bd + bw),       (float)v0,        (float)bw, (float)bd, r_bot);
    atlas_px_rect(tid, (float)(u0 + bd),            (float)(v0 + bd), (float)bw, (float)bh, r_front);
    atlas_px_rect(tid, (float)(u0 + bd + bw + bd),  (float)(v0 + bd), (float)bw, (float)bh, r_back);
    atlas_px_rect(tid, (float)u0,                   (float)(v0 + bd), (float)bd, (float)bh, r_left);
    atlas_px_rect(tid, (float)(u0 + bd + bw),       (float)(v0 + bd), (float)bd, (float)bh, r_right);

    float ua = mirror_u ? 1.0f : 0.0f, ub = mirror_u ? 0.0f : 1.0f;
    float c[8][3];
    static const float s_k[8][3] = {
        {-1,-1,-1}, {+1,-1,-1}, {+1,+1,-1}, {-1,+1,-1},
        {-1,-1,+1}, {+1,-1,+1}, {+1,+1,+1}, {-1,+1,+1}
    };
    for (int i = 0; i < 8; i++) {
        mat4_xf_pt(xf, cx + s_k[i][0] * hx, cy + s_k[i][1] * hy, cz + s_k[i][2] * hz, c[i]);
    }
    /* Front (+Z): 7(+X? wait +Z front looks from +Z toward -Z: top-left=6(+X,+Y,+Z), top-right=7(-X,+Y,+Z) */
    push_quad_3d(m, c[6], c[7], c[4], c[5], r_front, ua,0,ub,1, shade * 0.96f, rgba, flags);
    push_quad_3d(m, c[3], c[2], c[1], c[0], r_back,  ua,0,ub,1, shade * 0.78f, rgba, flags);
    push_quad_3d(m, c[7], c[3], c[0], c[4], r_right, ua,0,ub,1, shade * 0.84f, rgba, flags);
    push_quad_3d(m, c[2], c[6], c[5], c[1], r_left,  ua,0,ub,1, shade * 0.84f, rgba, flags);
    push_quad_3d(m, c[2], c[3], c[7], c[6], r_top,   ua,0,ub,1, shade * 1.00f, rgba, flags);
    push_quad_3d(m, c[5], c[4], c[0], c[1], r_bot,   ua,0,ub,1, shade * 0.58f, rgba, flags);
}

static void emit_extruded_item_3d(McMesh3D *m, McMat4 xf, McTextureId tid,
                                  float shade, uint32_t rgba, uint16_t flags) {
    const McExtrudedCache *ec = get_extruded_item(tid);
    float rect[4];
    atlas_subrect(tid, 0.0f, 0.0f, 1.0f, 1.0f, rect);

    if (!ec || ec->count == 0) {
        float p0[3], p1[3], p2[3], p3[3];
        mat4_xf_pt(xf, -0.5f,  0.5f, 0.0f, p0);
        mat4_xf_pt(xf,  0.5f,  0.5f, 0.0f, p1);
        mat4_xf_pt(xf,  0.5f, -0.5f, 0.0f, p2);
        mat4_xf_pt(xf, -0.5f, -0.5f, 0.0f, p3);
        push_quad_3d(m, p0, p1, p2, p3, rect, 0,0,1,1, shade, rgba, flags | MC_V3D_CUTOUT);
        return;
    }

    for (uint16_t i = 0; i < ec->count; i++) {
        const McExtrudedQuad *q = &ec->quads[i];
        float ax = q->x, ay = q->y, az = q->z;
        float bx, by, bz, cx, cy, cz, dx, dy, dz;
        if (q->nz != 0.0f) {
            bx = ax + q->w_norm; by = ay;            bz = az;
            cx = ax + q->w_norm; cy = ay + q->h_norm; cz = az;
            dx = ax;             dy = ay + q->h_norm; dz = az;
        } else if (q->nx != 0.0f) {
            bx = ax; by = ay;            bz = az + q->w_norm;
            cx = ax; cy = ay + q->h_norm; cz = az + q->w_norm;
            dx = ax; dy = ay + q->h_norm; dz = az;
        } else {
            bx = ax + q->w_norm; by = ay; bz = az;
            cx = ax + q->w_norm; cy = ay; cz = az + q->h_norm;
            dx = ax;             dy = ay; dz = az + q->h_norm;
        }
        float p0[3], p1[3], p2[3], p3[3];
        mat4_xf_pt(xf, dx, dy, dz, p0);
        mat4_xf_pt(xf, cx, cy, cz, p1);
        mat4_xf_pt(xf, bx, by, bz, p2);
        mat4_xf_pt(xf, ax, ay, az, p3);
        push_quad_3d(m, p0, p1, p2, p3, rect, q->u0, q->v0, q->u1, q->v1,
                     shade * q->face_shade, rgba, flags);
    }
}

static void emit_shield_3d(McMesh3D *m, McMat4 xf, float shade, uint32_t rgba, uint16_t flags) {
    /* 1px = 1/32m: compact vanilla shield plate 12 x 22 x 1 + handle behind */
    float u = 0.032f;
    emit_box_uv(m, xf, MC_TEX_ENTITY_SHIELD_NOPATTERN, 0, 0, 12, 22, 1,
                12.0f * u, 22.0f * u, 1.0f * u, 0.0f, 0.0f, 0.0f, 0, shade, rgba, flags);
    emit_box_uv(m, xf, MC_TEX_ENTITY_SHIELD_NOPATTERN, 26, 0, 2, 6, 6,
                2.0f * u, 6.0f * u, 6.0f * u, 0.0f, 0.0f, -3.5f * u, 0, shade, rgba, flags);
}

static void emit_held_item_3d(McMesh3D *m, McMat4 hand_xf, McItemType itm,
                               int enchanted, uint8_t bow_stage, float scale,
                               uint64_t tick, uint16_t extra_flags) {
    if (itm == MC_ITM_NONE) return;
    uint16_t flags = extra_flags | (enchanted ? MC_V3D_GLINT : 0u);
    if (itm == MC_ITM_SHIELD) {
        McMat4 sx = mat4_mul(hand_xf, mat4_scale(scale, scale, scale));
        emit_shield_3d(m, sx, 1.0f, 0xFFFFFFFFu, flags);
        return;
    }
    McBlockType bt = item_to_block_type(itm);
    if (bt != MC_BLK_AIR && bt != MC_BLK_TORCH && bt != MC_BLK_GLASS_PANE) {
        McMat4 bx = mat4_mul(hand_xf, mat4_mul(mat4_rot_y(0.45f), mat4_scale(scale * 0.42f, scale * 0.42f, scale * 0.42f)));
        emit_block_cube_xf(m, bx, bt, 0x3fu, 0, 1.0f, -1, 0, tick, extra_flags, 0);
        return;
    }
    McTextureId tid = item_to_tex_id(itm, bow_stage);
    McMat4 ix = mat4_mul(hand_xf, mat4_scale(scale * 0.65f, scale * 0.65f, scale * 0.65f));
    emit_extruded_item_3d(m, ix, tid, 1.0f, 0xFFFFFFFFu, flags);
}

/* Java 1.21.11 item/generated and item/handheld display transforms.
 * The extruded mesh is already centered. Model translations are in sixteenths. */
static int uses_sprite_model(McItemType item) {
    McBlockType block = item_to_block_type(item);
    return item != MC_ITM_SHIELD &&
           (block == MC_BLK_AIR || block == MC_BLK_TORCH || block == MC_BLK_GLASS_PANE);
}

static void emit_hand_sprite(McMesh3D *mesh, McMat4 hand, McItemType item, int left,
                             int first, int enchanted, uint8_t stage, float size, float bow_pose, uint16_t flags) {
    int tool = item == MC_ITM_DIAMOND_SWORD || item == MC_ITM_IRON_SWORD ||
               item == MC_ITM_NETHERITE_SWORD || item == MC_ITM_DIAMOND_AXE ||
               item == MC_ITM_DIAMOND_PICKAXE || item == MC_ITM_STICK || item == MC_ITM_BONE;
    float x = 0, y = 3, z = 1, rx = 0, ry = 0, rz = 0, scale = 0.55f;
    if (first) {
        x = 1.13f; y = 3.2f; z = 1.13f; ry = -90; rz = 25; scale = 0.68f;
        if (item == MC_ITM_CROSSBOW) { rx = -90; ry = 0; rz = left ? 35 : -55; }
        else if (!left && item == MC_ITM_BOW) { ry += 56.0f * bow_pose; rz -= 16.0f * bow_pose; }
        else if (left && (tool || item == MC_ITM_BOW)) { ry = 90; rz = -25; }
    } else if (tool) {
        y = 4; z = 0.5f; ry = left ? 90 : -90; rz = left ? -55 : 55; scale = 0.85f;
    } else if (item == MC_ITM_BOW) {
        x = -1; y = -2; z = 2.5f; rx = -80.0f * (1.0f - bow_pose); ry = left ? -280 : 260;
        rz = (left ? 1.0f : -1.0f) * (40.0f - 28.0f * bow_pose); scale = 0.9f;
    } else if (item == MC_ITM_CROSSBOW) {
        x = 2; y = 0.1f; z = -3; rx = -90; rz = left ? 30 : -60; scale = 0.9f;
    }
    float side = left ? -1.0f : 1.0f, rad = (float)M_PI / 180.0f;
    McMat4 xf = mat4_mul(hand, mat4_translate(side*x/16, y/16, z/16));
    xf = mat4_mul(xf, mat4_rot_x(rx*rad));
    xf = mat4_mul(xf, mat4_rot_y(side*ry*rad));
    xf = mat4_mul(xf, mat4_rot_z(side*rz*rad));
    scale *= size;
    emit_extruded_item_3d(mesh, mat4_mul(xf, mat4_scale(scale,scale,scale)),
                          item_to_tex_id(item,stage), 1, 0xffffffffu,
                          flags | (enchanted ? MC_V3D_GLINT : 0));
}

static McMat4 third_person_grip(McMat4 arm, float u, int left) {
    McMat4 xf = mat4_mul(arm, mat4_translate((left ? 1 : -1)*u, -10*u, 2*u));
    xf = mat4_mul(xf, mat4_rot_x((float)M_PI_2));
    return mat4_mul(xf, mat4_rot_y((float)M_PI));
}

/* =========================================================================
 * Articulated Entity & Player Models (Steve, Creeper, Zombie, Skeleton)
 * ========================================================================= */

/* Use the equipped texture's cutout on both surfaces and its own leading-edge strip. */
static void emit_elytra_wing(McMesh3D *m, McMat4 xf, float sx, float sy, float sz,
                             float cx, float cy, float cz, int mirror_u,
                             float shade, uint32_t rgba, uint16_t flags) {
    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    float r_wing[4], r_rim[4];
    atlas_px_rect(MC_TEX_ENTITY_ELYTRA, 36.0f, 2.0f, 10.0f, 20.0f, r_wing);
    atlas_px_rect(MC_TEX_ENTITY_ELYTRA, 34.0f, 2.0f, 2.0f, 20.0f, r_rim);

    float ua = mirror_u ? 1.0f : 0.0f, ub = mirror_u ? 0.0f : 1.0f;
    float c[8][3];
    static const float s_k[8][3] = {
        {-1,-1,-1}, {+1,-1,-1}, {+1,+1,-1}, {-1,+1,-1},
        {-1,-1,+1}, {+1,-1,+1}, {+1,+1,+1}, {-1,+1,+1}
    };
    for (int i = 0; i < 8; i++) {
        mat4_xf_pt(xf, cx + s_k[i][0] * hx, cy + s_k[i][1] * hy, cz + s_k[i][2] * hz, c[i]);
    }
    /* Both back (-Z) and front (+Z) share identical X->U alignment so wing silhouette matches on both sides */
    push_quad_3d(m, c[3], c[2], c[1], c[0], r_wing, ua, 0, ub, 1, shade * 0.94f, rgba, flags);
    push_quad_3d(m, c[7], c[6], c[5], c[4], r_wing, ua, 0, ub, 1, shade * 0.86f, rgba, flags);
    const int edge = mirror_u ? 1 : 0;
    push_quad_3d(m, c[3-edge], c[7-edge], c[4+edge], c[edge],
                 r_rim, 0, 0, 1, 1, shade * 0.92f, rgba, flags);
}

static void emit_steve_model(McMesh3D *m, McMat4 root_xf, const McPlayerSnap *p,
                             uint64_t tick, float main_scale, float off_scale, uint16_t extra_flags) {
    const float u = 1.8f / 32.0f; /* 0.05625 m per Minecraft model pixel */
    uint32_t rgba = p->hurt_flash ? pack_rgba(255, 110, 110, 255) : 0xFFFFFFFFu;
    float open = fmaxf(0.0f, fminf(1.0f, p->elytra_open));
    float flight = open * open * (3.0f - 2.0f * open);
    float sw = sinf(p->walk_phase) * 0.65f * p->walk_amp * (1.0f - flight);
    float crouch_drop = p->crouching ? -0.14f * (1.0f - flight) : 0.0f;
    float lean_x = p->crouching ? 0.36f * (1.0f - flight) : 0.0f;

    McMat4 body_base = mat4_mul(root_xf, mat4_translate(0.0f, crouch_drop, 0.0f));
    float body_pitch = ((float)M_PI * 0.5f - p->elytra_pitch) * flight;
    McMat4 flight_pose = mat4_mul(mat4_rot_x(body_pitch), mat4_rot_y(p->elytra_bank * flight));
    body_base = mat4_mul(body_base,
        mat4_mul(mat4_translate(0, 16.0f * u, 0),
                 mat4_mul(flight_pose, mat4_translate(0, -16.0f * u, 0))));

    McMat4 torso_piv = mat4_mul(body_base,
        mat4_mul(mat4_translate(0, 12.0f * u, 0), mat4_rot_x(lean_x)));

    /* Legs (pivot at hip y = 12u) */
    McMat4 rleg_xf = mat4_mul(body_base, mat4_mul(mat4_translate(-2.0f * u, 12.0f * u, 0), mat4_rot_x(-sw)));
    McMat4 lleg_xf = mat4_mul(body_base, mat4_mul(mat4_translate( 2.0f * u, 12.0f * u, 0), mat4_rot_x( sw)));
    emit_box_uv(m, rleg_xf, MC_TEX_ENTITY_STEVE, 0, 16, 4, 12, 4, 4*u, 12*u, 4*u, 0, -6*u, 0, 0, 1.0f, rgba, extra_flags);
    emit_box_uv(m, lleg_xf, MC_TEX_ENTITY_STEVE, 16, 48, 4, 12, 4, 4*u, 12*u, 4*u, 0, -6*u, 0, 0, 1.0f, rgba, extra_flags);

    /* Torso (center y = 18u inside torso_piv) */
    emit_box_uv(m, torso_piv, MC_TEX_ENTITY_STEVE, 16, 16, 8, 12, 4, 8*u, 12*u, 4*u, 0, 6*u, 0, 0, 1.0f, rgba, extra_flags);

    /* Head (pivot at neck y = 12u in torso_piv -> 24u total) */
    McMat4 head_xf = mat4_mul(torso_piv,
        mat4_mul(mat4_translate(0, 12.0f * u, 0), mat4_rot_x(-p->pitch * 0.7f - flight * 0.4f)));
    emit_box_uv(m, head_xf, MC_TEX_ENTITY_STEVE, 0, 0, 8, 8, 8, 8*u, 8*u, 8*u, 0, 4*u, 0, 0, 1.0f, rgba, extra_flags);
    emit_box_uv(m, head_xf, MC_TEX_ENTITY_STEVE, 32, 0, 8, 8, 8, 8.6f*u, 8.6f*u, 8.6f*u, 0, 4*u, 0, 0, 1.0f, rgba, extra_flags | MC_V3D_CUTOUT);

    /* Keep the bow arm extended while the string hand moves toward the face. */
    float raise = p->bow_raise * p->bow_raise * (3.0f - 2.0f * p->bow_raise);
    float aim_pitch = fmaxf(-0.9f, fminf(0.9f, p->pitch));
    float r_arm_idle = sw + 0.14f * flight - sinf(p->swing_arm * (float)M_PI) * 1.65f;
    float l_arm_idle = -sw + 0.14f * flight - p->shield_block * 0.75f;
    float r_arm_rot = r_arm_idle * (1.0f - raise) + (-1.43f - aim_pitch) * raise;
    float l_arm_rot = l_arm_idle * (1.0f - raise) +
        (-1.12f - aim_pitch - p->bow_pull * 0.38f + p->bow_release * 0.18f) * raise;
    McMat4 rarm_xf = mat4_mul(torso_piv, mat4_mul(mat4_translate(-6*u, 12*u, 0),
                                 mat4_mul(mat4_rot_y(0.22f * raise), mat4_rot_x(r_arm_rot))));
    McMat4 larm_xf = mat4_mul(torso_piv, mat4_mul(mat4_translate(6*u, 12*u, 0),
                                 mat4_mul(mat4_rot_y((-0.42f - p->bow_pull * 0.45f) * raise),
                                          mat4_rot_x(l_arm_rot))));
    emit_box_uv(m, rarm_xf, MC_TEX_ENTITY_STEVE, 40, 16, 4, 12, 4, 4*u, 12*u, 4*u, 0, -6*u, 0, 0, 1.0f, rgba, extra_flags);
    emit_box_uv(m, larm_xf, MC_TEX_ENTITY_STEVE, 32, 48, 4, 12, 4, 4*u, 12*u, 4*u, 0, -6*u, 0, 0, 1.0f, rgba, extra_flags);

    /* Real Minecraft Humanoid Diamond Armor Layers (armor_diamond_1.png & armor_diamond_2.png) */
    uint16_t armor_flags = extra_flags | MC_V3D_CUTOUT | MC_V3D_GLINT;
    if (p->has_helmet) {
        emit_box_uv(m, head_xf, MC_TEX_ARMOR_DIAMOND_1, 0, 0, 8, 8, 8,
                    9.0f*u, 9.0f*u, 9.0f*u, 0, 4.1f*u, 0, 0, 1.0f, 0xFFFFFFFFu, armor_flags);
    }
    if (p->has_chestplate) {
        emit_box_uv(m, torso_piv, MC_TEX_ARMOR_DIAMOND_1, 16, 16, 8, 12, 4,
                    8.9f*u, 12.4f*u, 4.9f*u, 0, 6.0f*u, 0, 0, 1.0f, 0xFFFFFFFFu, armor_flags);
        emit_box_uv(m, rarm_xf, MC_TEX_ARMOR_DIAMOND_1, 40, 16, 4, 12, 4,
                    4.8f*u, 12.2f*u, 4.8f*u, 0, -5.9f*u, 0, 0, 1.0f, 0xFFFFFFFFu, armor_flags);
        emit_box_uv(m, larm_xf, MC_TEX_ARMOR_DIAMOND_1, 40, 16, 4, 12, 4,
                    4.8f*u, 12.2f*u, 4.8f*u, 0, -5.9f*u, 0, 1, 1.0f, 0xFFFFFFFFu, armor_flags);
    }
    if (p->has_leggings) {
        emit_box_uv(m, torso_piv, MC_TEX_ARMOR_DIAMOND_2, 16, 16, 8, 12, 4,
                    8.5f*u, 12.1f*u, 4.5f*u, 0, 6.0f*u, 0, 0, 1.0f, 0xFFFFFFFFu, armor_flags);
        emit_box_uv(m, rleg_xf, MC_TEX_ARMOR_DIAMOND_2, 0, 16, 4, 12, 4,
                    4.4f*u, 12.0f*u, 4.4f*u, 0, -6.0f*u, 0, 0, 1.0f, 0xFFFFFFFFu, armor_flags);
        emit_box_uv(m, lleg_xf, MC_TEX_ARMOR_DIAMOND_2, 0, 16, 4, 12, 4,
                    4.4f*u, 12.0f*u, 4.4f*u, 0, -6.0f*u, 0, 1, 1.0f, 0xFFFFFFFFu, armor_flags);
    }
    if (p->has_boots) {
        emit_box_uv(m, rleg_xf, MC_TEX_ARMOR_DIAMOND_1, 0, 16, 4, 12, 4,
                    4.9f*u, 12.3f*u, 4.9f*u, 0, -6.1f*u, 0, 0, 1.0f, 0xFFFFFFFFu, armor_flags);
        emit_box_uv(m, lleg_xf, MC_TEX_ARMOR_DIAMOND_1, 0, 16, 4, 12, 4,
                    4.9f*u, 12.3f*u, 4.9f*u, 0, -6.1f*u, 0, 1, 1.0f, 0xFFFFFFFFu, armor_flags);
    }

    if (p->has_elytra || open > 0.01f) {
        const float rad = (float)M_PI / 180.0f;
        const float dive = fminf(1.0f, fmaxf(0.0f, -p->elytra_pitch * 1.6f));
        const float folded_roll = p->crouching ? 29.0f : 15.0f;
        const float folded_pitch = p->crouching ? 36.0f : 15.0f;
        const float roll = (folded_roll + (82.0f - 19.0f*dive - folded_roll)*flight) * rad;
        const float pitch = (folded_pitch + (23.0f - 7.0f*dive - folded_pitch)*flight) * rad;
        const float shoulder_drop = p->crouching ? 2.0f * (1.0f - flight) : 0.0f;

        for (int side = -1; side <= 1; side += 2) {
            McMat4 wing = mat4_mul(torso_piv,
                mat4_translate(side * 5.0f * u, (12.0f - shoulder_drop) * u, -2.25f * u));
            /* Pitch the whole spread so the panels stay visible from behind in flight. */
            wing = mat4_mul(wing, mat4_mul(mat4_rot_x(pitch), mat4_rot_z(side * roll)));
            /* The cutout extends back toward the spine from the shoulder hinge. */
            emit_elytra_wing(m, wing, 10*u, 20*u, 2*u, -side*5*u, -10*u, -u,
                             side > 0, 1.0f, rgba, extra_flags | MC_V3D_CUTOUT);
        }
    }

    /* Held items in 3rd-person hands */
    if (p->mainhand_item != MC_ITM_NONE) {
        McMat4 mh_xf = mat4_mul(rarm_xf,
            mat4_mul(mat4_translate(0.0f, -11.0f * u, 2.5f * u),
                     mat4_mul(mat4_rot_x(0.85f), mat4_rot_y((float)M_PI * 0.5f))));
        uint8_t bs = p->bow_pull_stage;
        if (uses_sprite_model(p->mainhand_item))
            emit_hand_sprite(m, third_person_grip(rarm_xf,u,0), p->mainhand_item, 0, 0, p->mainhand_enchanted, bs, main_scale, raise, extra_flags);
        else
            emit_held_item_3d(m, mh_xf, (McItemType)p->mainhand_item, p->mainhand_enchanted, bs, 0.72f * main_scale, tick, extra_flags);
    }
    if (p->offhand_item != MC_ITM_NONE && raise < 0.25f) {
        McMat4 oh_xf = mat4_mul(larm_xf,
            mat4_mul(mat4_translate(1.5f * u, -8.5f * u, 3.0f * u),
                     mat4_mul(mat4_rot_y(p->shield_block > 0.1f ? -0.35f : 1.15f), mat4_rot_x(0.15f))));
        if (uses_sprite_model(p->offhand_item))
            emit_hand_sprite(m, third_person_grip(larm_xf,u,1), p->offhand_item, 1, 0, 0, 0, off_scale, 0.0f, extra_flags);
        else
            emit_held_item_3d(m, oh_xf, (McItemType)p->offhand_item, 0, 0, 0.75f * off_scale, tick, extra_flags);
    }
}

static void emit_mob_model(McMesh3D *m, const McMobSnap *mb, uint64_t tick) {
    const float u = 1.8f / 32.0f;
    float sw = sinf(mb->walk_phase) * 0.75f * mb->walk_amp;
    uint32_t rgba = mb->hurt_flash ? pack_rgba(255, 95, 95, 255) : 0xFFFFFFFFu;
    uint16_t flags = mb->fuse_flash ? MC_V3D_FLASH_WHITE : 0u;
    McMat4 root = mat4_mul(mat4_translate(mb->x, mb->y, mb->z), mat4_rot_y(mb->yaw));

    if (mb->kind == MC_MOB_WOLF) {
        emit_box_uv(m,root,MC_TEX_WOLF,18,14,6,6,9,.38f,.38f,.58f,0,.50f,0,0,1,rgba,flags);
        emit_box_uv(m,root,MC_TEX_WOLF,0,0,6,6,4,.36f,.36f,.27f,0,.70f,.32f,0,1,rgba,flags);
        emit_box_uv(m,root,MC_TEX_WOLF,0,10,3,3,4,.18f,.16f,.21f,0,.63f,.51f,0,1,rgba,flags);
        for (int side=-1;side<=1;side+=2) {
            emit_box_uv(m,root,MC_TEX_WOLF,16,14,2,2,1,.12f,.13f,.08f,side*.12f,.91f,.30f,0,1,rgba,flags);
            for (int end=-1;end<=1;end+=2) {
                McMat4 leg=mat4_mul(root,mat4_mul(mat4_translate(side*.13f,.34f,end*.22f),mat4_rot_x(side*end*sw)));
                emit_box_uv(m,leg,MC_TEX_WOLF,0,18,2,8,2,.12f,.34f,.12f,0,-.17f,0,0,1,rgba,flags);
            }
        }
        McMat4 tail=mat4_mul(root,mat4_mul(mat4_translate(0,.60f,-.28f),mat4_rot_x(1.05f)));
        emit_box_uv(m,tail,MC_TEX_WOLF,9,18,2,8,2,.13f,.38f,.13f,0,-.18f,0,0,1,rgba,flags);
        return;
    }
    if (mb->kind == MC_MOB_CREEPER) {
        float swell_xz = 1.0f + mb->swell * 0.32f;
        float swell_y  = 1.0f - mb->swell * 0.10f;
        McMat4 cx = mat4_mul(root, mat4_scale(swell_xz, swell_y, swell_xz));
        /* Legs (4x6x4, staggered front/back Z=±4u, X=±2u, pivot y=6u) */
        McMat4 fl = mat4_mul(cx, mat4_mul(mat4_translate( 2*u, 6*u,  4*u), mat4_rot_x( sw)));
        McMat4 fr = mat4_mul(cx, mat4_mul(mat4_translate(-2*u, 6*u,  4*u), mat4_rot_x(-sw)));
        McMat4 bl = mat4_mul(cx, mat4_mul(mat4_translate( 2*u, 6*u, -4*u), mat4_rot_x(-sw)));
        McMat4 br = mat4_mul(cx, mat4_mul(mat4_translate(-2*u, 6*u, -4*u), mat4_rot_x( sw)));
        emit_box_uv(m, fl, MC_TEX_ENTITY_CREEPER, 0, 16, 4, 6, 4, 4*u, 6*u, 4*u, 0, -3*u, 0, 0, 1.0f, rgba, flags);
        emit_box_uv(m, fr, MC_TEX_ENTITY_CREEPER, 0, 16, 4, 6, 4, 4*u, 6*u, 4*u, 0, -3*u, 0, 0, 1.0f, rgba, flags);
        emit_box_uv(m, bl, MC_TEX_ENTITY_CREEPER, 0, 16, 4, 6, 4, 4*u, 6*u, 4*u, 0, -3*u, 0, 0, 1.0f, rgba, flags);
        emit_box_uv(m, br, MC_TEX_ENTITY_CREEPER, 0, 16, 4, 6, 4, 4*u, 6*u, 4*u, 0, -3*u, 0, 0, 1.0f, rgba, flags);
        /* Body (8x12x4 at y=6..18u) & Head (8x8x8 at y=18..26u) */
        emit_box_uv(m, cx, MC_TEX_ENTITY_CREEPER, 16, 16, 8, 12, 4, 8*u, 12*u, 4*u, 0, 12*u, 0, 0, 1.0f, rgba, flags);
        McMat4 hx = mat4_mul(cx, mat4_mul(mat4_translate(0, 18*u, 0), mat4_rot_x(-mb->pitch * 0.7f)));
        emit_box_uv(m, hx, MC_TEX_ENTITY_CREEPER, 0, 0, 8, 8, 8, 8*u, 8*u, 8*u, 0, 4*u, 0, 0, 1.0f, rgba, flags);
        return;
    }

    if (mb->kind == MC_MOB_ZOMBIE) {
        McMat4 rl = mat4_mul(root, mat4_mul(mat4_translate(-2*u, 12*u, 0), mat4_rot_x(-sw)));
        McMat4 ll = mat4_mul(root, mat4_mul(mat4_translate( 2*u, 12*u, 0), mat4_rot_x( sw)));
        emit_box_uv(m, rl, MC_TEX_ENTITY_ZOMBIE, 0, 16, 4, 12, 4, 4*u, 12*u, 4*u, 0, -6*u, 0, 0, 1.0f, rgba, flags);
        emit_box_uv(m, ll, MC_TEX_ENTITY_ZOMBIE, 0, 16, 4, 12, 4, 4*u, 12*u, 4*u, 0, -6*u, 0, 1, 1.0f, rgba, flags);
        emit_box_uv(m, root, MC_TEX_ENTITY_ZOMBIE, 16, 16, 8, 12, 4, 8*u, 12*u, 4*u, 0, 18*u, 0, 0, 1.0f, rgba, flags);
        McMat4 hx = mat4_mul(root, mat4_mul(mat4_translate(0, 24*u, 0), mat4_rot_x(-mb->pitch * 0.7f)));
        emit_box_uv(m, hx, MC_TEX_ENTITY_ZOMBIE, 0, 0, 8, 8, 8, 8*u, 8*u, 8*u, 0, 4*u, 0, 0, 1.0f, rgba, flags);
        /* Zombie arms raised forward (-85 deg) */
        float arm_lift = -1.48f + sinf(mb->swing_arm * (float)M_PI) * 0.9f;
        McMat4 ra = mat4_mul(root, mat4_mul(mat4_translate(-6*u, 22*u, 0), mat4_rot_x(arm_lift + sw * 0.12f)));
        McMat4 la = mat4_mul(root, mat4_mul(mat4_translate( 6*u, 22*u, 0), mat4_rot_x(arm_lift - sw * 0.12f)));
        emit_box_uv(m, ra, MC_TEX_ENTITY_ZOMBIE, 40, 16, 4, 12, 4, 4*u, 12*u, 4*u, 0, -5*u, 0, 0, 1.0f, rgba, flags);
        emit_box_uv(m, la, MC_TEX_ENTITY_ZOMBIE, 40, 16, 4, 12, 4, 4*u, 12*u, 4*u, 0, -5*u, 0, 1, 1.0f, rgba, flags);
        return;
    }

    /* MC_MOB_SKELETON (64x32 skinny 2x12x2 bones + held bow) */
    McMat4 rl = mat4_mul(root, mat4_mul(mat4_translate(-2*u, 12*u, 0), mat4_rot_x(-sw)));
    McMat4 ll = mat4_mul(root, mat4_mul(mat4_translate( 2*u, 12*u, 0), mat4_rot_x( sw)));
    emit_box_uv(m, rl, MC_TEX_ENTITY_SKELETON, 0, 16, 2, 12, 2, 2*u, 12*u, 2*u, 0, -6*u, 0, 0, 1.0f, rgba, flags);
    emit_box_uv(m, ll, MC_TEX_ENTITY_SKELETON, 0, 16, 2, 12, 2, 2*u, 12*u, 2*u, 0, -6*u, 0, 1, 1.0f, rgba, flags);
    emit_box_uv(m, root, MC_TEX_ENTITY_SKELETON, 16, 16, 8, 12, 4, 8*u, 12*u, 4*u, 0, 18*u, 0, 0, 1.0f, rgba, flags);
    McMat4 hx = mat4_mul(root, mat4_mul(mat4_translate(0, 24*u, 0), mat4_rot_x(-mb->pitch * 0.7f)));
    emit_box_uv(m, hx, MC_TEX_ENTITY_SKELETON, 0, 0, 8, 8, 8, 8*u, 8*u, 8*u, 0, 4*u, 0, 0, 1.0f, rgba, flags);
    McMat4 ra = mat4_mul(root, mat4_mul(mat4_translate(-5*u, 22*u, 0), mat4_rot_x(-1.40f + sw * 0.1f)));
    McMat4 la = mat4_mul(root, mat4_mul(mat4_translate( 5*u, 22*u, 0), mat4_rot_x(-1.40f - sw * 0.1f)));
    emit_box_uv(m, ra, MC_TEX_ENTITY_SKELETON, 40, 16, 2, 12, 2, 2*u, 12*u, 2*u, 0, -5*u, 0, 0, 1.0f, rgba, flags);
    emit_box_uv(m, la, MC_TEX_ENTITY_SKELETON, 40, 16, 2, 12, 2, 2*u, 12*u, 2*u, 0, -5*u, 0, 1, 1.0f, rgba, flags);
    McMat4 bow_xf = mat4_mul(ra, mat4_mul(mat4_translate(0, -10*u, 2*u), mat4_rot_x(1.0f)));
    emit_held_item_3d(m, bow_xf, MC_ITM_BOW, 0, 0, 0.7f, tick, 0);
}

/* =========================================================================
 * Pass (a) World & Pass (b) Viewmodel Scene Builders
 * ========================================================================= */

static void build_world_scene(const McRenderSnapshot *s, McMesh3D *opaque_m, McMesh3D *blend_m) {
    /* 1. Placed Blocks */
    for (int i = 0; i < s->block_count && i < MC_SNAP_MAX_BLOCKS; i++) {
        const McBlockSnap *b = &s->blocks[i];
        if (b->block_type == MC_BLK_AIR) continue;
        float light_val = 0.25f + ((float)(b->light & 15u) / 15.0f) * 0.75f;
        McMat4 bxf = mat4_translate(b->x, b->y, b->z);
        emit_block_cube_xf(opaque_m, bxf, (McBlockType)b->block_type,
                           b->face_mask, b->facing, light_val,
                           b->mining_stage, b->flash_white, s->tick, 0, 0);
        emit_block_cube_xf(blend_m, bxf, (McBlockType)b->block_type,
                           b->face_mask, b->facing, light_val,
                           b->mining_stage, b->flash_white, s->tick, 0, 1);
    }

    /* 2. Target Block Wireframe (12 crisp black edges in alpha pass) */
    if (s->has_target) {
        float tx = s->target_x, ty = s->target_y, tz = s->target_z;
        float r = 0.503f, th = 0.005f;
        float srect[4] = {0};
        atlas_px_rect(MC_TEX_FONT_ASCII, 0, 0, 1, 1, srect);
        static const float k_edges[12][6] = {
            /* bottom 4 */
            {-1,-1,-1,  1,-1,-1}, { 1,-1,-1,  1,-1, 1}, { 1,-1, 1, -1,-1, 1}, {-1,-1, 1, -1,-1,-1},
            /* top 4 */
            {-1, 1,-1,  1, 1,-1}, { 1, 1,-1,  1, 1, 1}, { 1, 1, 1, -1, 1, 1}, {-1, 1, 1, -1, 1,-1},
            /* vertical 4 */
            {-1,-1,-1, -1, 1,-1}, { 1,-1,-1,  1, 1,-1}, { 1,-1, 1,  1, 1, 1}, {-1,-1, 1, -1, 1, 1}
        };
        /* Draw slim dark wireframe strips facing camera */
        float cx = s->cam_right[0] * th, cy = s->cam_right[1] * th, cz = s->cam_right[2] * th;
        float ux = s->cam_up[0] * th,    uy = s->cam_up[1] * th,    uz = s->cam_up[2] * th;
        for (int e = 0; e < 12; e++) {
            float ax = tx + k_edges[e][0] * r, ay = ty + k_edges[e][1] * r, az = tz + k_edges[e][2] * r;
            float bx = tx + k_edges[e][3] * r, by = ty + k_edges[e][4] * r, bz = tz + k_edges[e][5] * r;
            float p0[3] = { ax - cx - ux, ay - cy - uy, az - cz - uz };
            float p1[3] = { bx - cx - ux, by - cy - uy, bz - cz - uz };
            float p2[3] = { bx + cx + ux, by + cy + uy, bz + cz + uz };
            float p3[3] = { ax + cx + ux, ay + cy + uy, az + cz + uz };
            /* Note: use particle flame/explosion solid center or white/black tint */
            float orect[4]; atlas_subrect(MC_TEX_BLOCK_OBSIDIAN, 0.25f, 0.25f, 0.5f, 0.5f, orect);
            push_quad_3d(blend_m, p0, p1, p2, p3, orect, 0,0,1,1, 0.0f, 0xB0000000u, 0);
        }
    }

    /* 3. Mobs */
    for (int i = 0; i < s->mob_count && i < MC_SNAP_MAX_MOBS; i++) {
        if (!s->mobs[i].active) continue;
        emit_mob_model(opaque_m, &s->mobs[i], s->tick);
    }

    /* 4. Primed TNT */
    for (int i = 0; i < s->primed_tnt_count && i < MC_SNAP_MAX_PRIMED_TNT; i++) {
        const McPrimedTntSnap *pt = &s->primed_tnt[i];
        if (!pt->active) continue;
        float swell = 1.0f + (pt->fuse_progress > 0.85f ? (pt->fuse_progress - 0.85f) * 1.4f : 0.0f);
        McMat4 txf = mat4_mul(mat4_translate(pt->x, pt->y, pt->z), mat4_scale(swell, swell, swell));
        emit_block_cube_xf(opaque_m, txf, MC_BLK_TNT, 0x3fu, 0, 1.0f, -1, pt->flash_white, s->tick, 0, 0);
    }

    /* 5. Dropped Items */
    for (int i = 0; i < s->dropped_count && i < MC_SNAP_MAX_DROPPED; i++) {
        const McDroppedItemSnap *di = &s->dropped[i];
        if (!di->active) continue;
        float bob = 0.06f * sinf(s->time_sec * 3.2f + (float)i);
        McMat4 dxf = mat4_mul(mat4_translate(di->x, di->y + 0.22f + bob, di->z), mat4_rot_y(di->spin_yaw));
        uint16_t df = di->enchanted ? MC_V3D_GLINT : 0u;
        if (di->is_block && di->block_type != MC_BLK_AIR) {
            emit_block_cube_xf(opaque_m, mat4_mul(dxf, mat4_scale(0.28f, 0.28f, 0.28f)),
                               (McBlockType)di->block_type, 0x3fu, 0, 1.0f, -1, 0, s->tick, df, 0);
        } else {
            McTextureId tid = item_to_tex_id((McItemType)di->item_type, 0);
            emit_extruded_item_3d(opaque_m, mat4_mul(dxf, mat4_scale(0.38f, 0.38f, 0.38f)),
                                  tid, 1.0f, 0xFFFFFFFFu, df);
        }
    }

    /* 6. Projectiles (Arrows & Firework Rockets) */
    for (int i = 0; i < s->projectile_count && i < MC_SNAP_MAX_PROJECTILES; i++) {
        const McProjectileSnap *pr = &s->projectiles[i];
        if (!pr->active) continue;
        float spd = sqrtf(pr->vx*pr->vx + pr->vy*pr->vy + pr->vz*pr->vz);
        float dx = spd > 1e-4f ? pr->vx / spd : 0.0f;
        float dy = spd > 1e-4f ? pr->vy / spd : 0.0f;
        float dz = spd > 1e-4f ? pr->vz / spd : 1.0f;
        float yaw = atan2f(dx, dz);
        float pitch = asinf(fmaxf(-1.0f, fminf(1.0f, -dy)));
        McMat4 axf = mat4_mul(mat4_translate(pr->x, pr->y, pr->z),
                              mat4_mul(mat4_rot_y(yaw), mat4_rot_x(pitch)));
        if (pr->kind == MC_PROJ_FIREWORK) {
            emit_extruded_item_3d(opaque_m, mat4_mul(axf, mat4_scale(0.45f, 0.45f, 0.45f)),
                                  MC_TEX_ITEM_FIREWORK_ROCKET, 1.0f, 0xFFFFFFFFu, 0);
        } else {
            float rshaft[4]; atlas_px_rect(MC_TEX_ENTITY_ARROW, 0, 0, 32, 5, rshaft);
            float p0[3], p1[3], p2[3], p3[3];
            mat4_xf_pt(axf, 0,  0.07f, -0.45f, p0); mat4_xf_pt(axf, 0,  0.07f, 0.45f, p1);
            mat4_xf_pt(axf, 0, -0.07f,  0.45f, p2); mat4_xf_pt(axf, 0, -0.07f,-0.45f, p3);
            push_quad_3d(opaque_m, p0, p1, p2, p3, rshaft, 0,0,1,1, 1.0f, 0xFFFFFFFFu, MC_V3D_CUTOUT);
            mat4_xf_pt(axf, -0.07f, 0, -0.45f, p0); mat4_xf_pt(axf, -0.07f, 0, 0.45f, p1);
            mat4_xf_pt(axf,  0.07f, 0,  0.45f, p2); mat4_xf_pt(axf,  0.07f, 0,-0.45f, p3);
            push_quad_3d(opaque_m, p0, p1, p2, p3, rshaft, 0,0,1,1, 1.0f, 0xFFFFFFFFu, MC_V3D_CUTOUT);
        }
    }

    /* 7. 3rd-Person Player + Elytra */
    if (s->view_mode != MC_VIEW_FIRST_PERSON && s->player.valid) {
        McMat4 pxf = mat4_mul(mat4_translate(s->player.pos[0], s->player.pos[1], s->player.pos[2]),
                              mat4_rot_y(s->player.yaw));
        emit_steve_model(opaque_m, pxf, &s->player, s->tick, s->mainhand_scale, s->offhand_scale, 0);
    }

    /* 8. Camera-Facing Particles */
    for (int i = 0; i < s->particle_count && i < MC_SNAP_MAX_PARTICLES; i++) {
        const McParticleSnap *pt = &s->particles[i];
        if (!pt->active || pt->size <= 0.0f) continue;
        McTextureId tid = MC_TEX_PARTICLE_FLAME;
        switch (pt->kind) {
            case MC_PART_EXPLOSION:
                tid = (McTextureId)(MC_TEX_PARTICLE_EXPLOSION_0 + (pt->frame & 15u));
                break;
            case MC_PART_SMOKE:
                tid = (McTextureId)(MC_TEX_PARTICLE_BIG_SMOKE_0 + (pt->frame % 12u));
                break;
            case MC_PART_FLAME:   tid = MC_TEX_PARTICLE_FLAME; break;
            case MC_PART_CRIT:    tid = MC_TEX_PARTICLE_CRITICAL_HIT; break;
            case MC_PART_SWEEP:
                tid = (McTextureId)(MC_TEX_PARTICLE_SWEEP_0 + (pt->frame & 7u));
                break;
            case MC_PART_GLINT:   tid = MC_TEX_PARTICLE_GLINT; break;
            case MC_PART_GENERIC:
                tid = (McTextureId)(MC_TEX_PARTICLE_GENERIC_0 + (pt->frame & 7u));
                break;
            case MC_PART_BLOCK_CRUMB: tid = MC_TEX_BLOCK_COBBLESTONE; break;
            default: break;
        }
        float rect[4];
        if (pt->kind == MC_PART_BLOCK_CRUMB) atlas_subrect(tid, 0.25f, 0.25f, 0.5f, 0.5f, rect);
        else atlas_subrect(tid, 0.0f, 0.0f, 1.0f, 1.0f, rect);

        float hs = pt->size * 0.5f;
        float rx = s->cam_right[0] * hs, ry = s->cam_right[1] * hs, rz = s->cam_right[2] * hs;
        float ux = s->cam_up[0]    * hs, uy = s->cam_up[1]    * hs, uz = s->cam_up[2]    * hs;
        float p0[3] = { pt->x - rx + ux, pt->y - ry + uy, pt->z - rz + uz };
        float p1[3] = { pt->x + rx + ux, pt->y + ry + uy, pt->z + rz + uz };
        float p2[3] = { pt->x + rx - ux, pt->y + ry - uy, pt->z + rz - uz };
        float p3[3] = { pt->x - rx - ux, pt->y - ry - uy, pt->z - rz - uz };
        uint32_t col = pt->rgba ? pt->rgba : 0xFFFFFFFFu;
        push_quad_3d(blend_m, p0, p1, p2, p3, rect, 0,0,1,1, 1.0f, col, 0);
    }
}

static void build_viewmodel_scene(const McRenderSnapshot *s, McMesh3D *vm_m) {
    if (s->view_mode != MC_VIEW_FIRST_PERSON || s->inv_open != MC_INV_CLOSED) return;
    const McViewmodelSnap *vm = &s->viewmodel;

    float swing = sinf(vm->attack_swing * (float)M_PI);
    float swing_sq = sinf(sqrtf(fmaxf(0.0f, vm->attack_swing)) * (float)M_PI);
    float equip_y = -vm->equip_drop * 0.45f;
    float bow_raise = vm->bow_raise * vm->bow_raise * (3.0f - 2.0f * vm->bow_raise);
    float eat_y = vm->eat_bob * 0.08f * sinf(s->time_sec * 26.0f);

    /* Right-hand held item or empty Steve arm (vanilla 3D 1st-person perspective anchored to bottom-right) */
    McItemType main_itm = (McItemType)vm->mainhand_item;
    if (main_itm == MC_ITM_NONE) {
        McMat4 arm_xf = mat4_mul(
            mat4_translate(0.60f + vm->walk_bob_x - swing_sq * 0.34f,
                           -0.56f + vm->walk_bob_y + equip_y + swing * 0.14f,
                           -0.78f - swing * 0.24f),
            mat4_mul(mat4_rot_y(0.55f + swing_sq * 0.55f),
                     mat4_mul(mat4_rot_x(1.74f - swing * 0.95f), mat4_rot_z(0.28f))));
        emit_box_uv(vm_m, arm_xf, MC_TEX_ENTITY_STEVE, 40, 16, 4, 12, 4,
                    0.22f, 0.68f, 0.22f, 0, 0, 0, 0, 1.0f, 0xFFFFFFFFu, MC_V3D_NO_GBUF);
    } else if (main_itm == MC_ITM_BOW) {
        const float raise = bow_raise;
        float shake = raise * fmaxf(0.0f, vm->bow_pull - 0.65f) * 0.018f * sinf(s->time_sec * 42.0f);
        McMat4 hand = mat4_translate(
            0.47f + vm->walk_bob_x * (1.0f - raise) - raise * 0.16f + shake,
            -0.52f + equip_y + vm->walk_bob_y * (1.0f - raise) + raise * 0.15f + shake,
            -0.83f + raise * vm->bow_pull * 0.11f - vm->bow_release * 0.045f);
        hand = mat4_mul(hand, mat4_rot_x(-vm->bow_release * 0.16f));
        emit_hand_sprite(vm_m, hand, main_itm, 0, 1, vm->mainhand_enchanted,
                         vm->bow_pull_stage, s->mainhand_scale * (1.0f - raise * 0.12f), raise, MC_V3D_NO_GBUF);
    } else {
        McBlockType bt = item_to_block_type(main_itm);
        if (bt != MC_BLK_AIR && bt != MC_BLK_TORCH && bt != MC_BLK_GLASS_PANE) {
            McMat4 bx = mat4_mul(
                mat4_translate(0.54f + vm->walk_bob_x - swing_sq * 0.28f,
                               -0.46f + vm->walk_bob_y + equip_y + swing * 0.10f,
                               -0.82f - swing * 0.18f),
                mat4_mul(mat4_rot_y(0.56f + swing_sq * 0.4f), mat4_rot_x(0.22f - swing * 0.6f)));
            emit_held_item_3d(vm_m, bx, main_itm, vm->mainhand_enchanted, 0, 0.58f * s->mainhand_scale, s->tick, MC_V3D_NO_GBUF);
        } else if (uses_sprite_model(main_itm)) {
            McMat4 hand = mat4_translate(0.47f + vm->walk_bob_x - swing_sq*0.38f,
                                         -0.52f + vm->walk_bob_y + equip_y + eat_y + swing*0.12f,
                                         -0.83f - swing*0.22f);
            hand = mat4_mul(hand, mat4_rot_y(-swing_sq*0.52f));
            hand = mat4_mul(hand, mat4_rot_x(-swing*1.10f));
            emit_hand_sprite(vm_m, hand, main_itm, 0, 1, vm->mainhand_enchanted,
                             vm->bow_pull_stage, s->mainhand_scale, 0.0f, MC_V3D_NO_GBUF);
        } else {
            McMat4 tx = mat4_mul(
                mat4_translate(0.56f + vm->walk_bob_x - swing_sq * 0.38f,
                               -0.46f + vm->walk_bob_y + equip_y + eat_y + swing * 0.12f,
                               -0.74f - swing * 0.22f),
                mat4_mul(mat4_rot_y(-0.88f - swing_sq * 0.52f),
                         mat4_mul(mat4_rot_z(0.22f + swing * 0.45f), mat4_rot_x(-0.18f - swing * 1.10f))));
            emit_held_item_3d(vm_m, tx, main_itm, vm->mainhand_enchanted, vm->bow_pull_stage, 0.72f * s->mainhand_scale, s->tick, MC_V3D_NO_GBUF);
        }
    }

    /* Left-hand offhand item / Shield (tucked cleanly into bottom-left when idle; raised when blocking) */
    const float offhand_left_offset = 0.08f;
    McItemType off_itm = (McItemType)vm->offhand_item;
    const float offhand_lower = 0.7f * bow_raise;
    if (off_itm == MC_ITM_SHIELD) {
        float blk = vm->shield_block;
        McMat4 sh_xf = mat4_mul(
            mat4_translate(-0.50f - offhand_left_offset + blk * 0.28f + vm->walk_bob_x * 0.45f,
                           -0.74f - offhand_lower + blk * 0.26f + vm->walk_bob_y * 0.45f,
                           -0.76f + blk * 0.08f),
            mat4_mul(mat4_rot_y(0.22f - blk * 0.16f),
                     mat4_mul(mat4_rot_z(-0.08f + blk * 0.12f), mat4_rot_x(-0.08f + blk * 0.04f))));
        emit_held_item_3d(vm_m, sh_xf, MC_ITM_SHIELD, vm->offhand_enchanted, 0, 0.96f * s->offhand_scale, s->tick, MC_V3D_NO_GBUF);
    } else if (off_itm != MC_ITM_NONE) {
        McMat4 ox = mat4_mul(
            mat4_translate(-0.54f - offhand_left_offset + vm->walk_bob_x * 0.45f,
                           -0.48f - offhand_lower + vm->walk_bob_y * 0.45f + equip_y,
                           -0.80f),
            mat4_rot_y(0.45f));
        if (uses_sprite_model(off_itm)) {
            McMat4 hand = mat4_translate(-0.47f - offhand_left_offset + vm->walk_bob_x*0.45f,
                                         -0.52f - offhand_lower + vm->walk_bob_y*0.45f + equip_y,-0.83f);
            emit_hand_sprite(vm_m, hand, off_itm, 1, 1, vm->offhand_enchanted, 0, s->offhand_scale, 0.0f, MC_V3D_NO_GBUF);
        } else emit_held_item_3d(vm_m, ox, off_itm, vm->offhand_enchanted, 0, 0.50f * s->offhand_scale, s->tick, MC_V3D_NO_GBUF);
    }

    /* Totem of Undying center-screen activation pop animation */
    if (vm->totem_pop > 0.01f && vm->totem_pop < 0.99f) {
        float p = vm->totem_pop;
        float sc = 0.25f + sinf(p * (float)M_PI) * 0.72f;
        McMat4 tot_xf = mat4_mul(
            mat4_translate(0.0f, -0.35f + p * 0.55f, -0.70f),
            mat4_mul(mat4_rot_y(p * 10.0f), mat4_scale(sc, sc, sc)));
        emit_extruded_item_3d(vm_m, tot_xf, MC_TEX_ITEM_TOTEM_OF_UNDYING, 1.0f, 0xFFFFFFFFu, MC_V3D_NO_GBUF);
    }
}

/* =========================================================================
 * Private Depth / Target Allocation Helpers
 * ========================================================================= */

static void ensure_mc_depth(NSUInteger w, NSUInteger h) {
    if (g_r.mc_depth && g_r.mc_depth_w == w && g_r.mc_depth_h == h) return;
    if (g_r.mc_depth) res_drop(g_r.mc_depth);
    MTLTextureDescriptor *d = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:w height:h mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget;
    d.storageMode = MTLStorageModePrivate;
    g_r.mc_depth = [g_r.dev newTextureWithDescriptor:d];
    g_r.mc_depth_w = w;
    g_r.mc_depth_h = h;
    if (g_r.mc_depth) res_track(g_r.mc_depth);
}

static void ensure_paperdoll_targets(int slot) {
    if (g_r.paper_tex[slot] && g_r.paper_depth[slot]) return;
    MTLTextureDescriptor *cd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:MC_PAPERDOLL_W
                                    height:MC_PAPERDOLL_H
                                 mipmapped:NO];
    cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    cd.storageMode = MTLStorageModePrivate;
    g_r.paper_tex[slot] = [g_r.dev newTextureWithDescriptor:cd];
    if (g_r.paper_tex[slot]) res_track(g_r.paper_tex[slot]);

    MTLTextureDescriptor *dd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                     width:MC_PAPERDOLL_W
                                    height:MC_PAPERDOLL_H
                                 mipmapped:NO];
    dd.usage = MTLTextureUsageRenderTarget;
    dd.storageMode = MTLStorageModePrivate;
    g_r.paper_depth[slot] = [g_r.dev newTextureWithDescriptor:dd];
    if (g_r.paper_depth[slot]) res_track(g_r.paper_depth[slot]);
}

/* =========================================================================
 * Pass (a) + (b) Replay Entry Point (Metal 4)
 * ========================================================================= */

static inline int pixel_fmt_is_linear_output(MTLPixelFormat fmt) {
    switch (fmt) {
        case MTLPixelFormatRG11B10Float:
        case MTLPixelFormatRGBA16Float:
        case MTLPixelFormatRGBA32Float:
        case MTLPixelFormatRGB10A2Unorm:
        case MTLPixelFormatBGR10A2Unorm:
        case MTLPixelFormatRGBA8Unorm_sRGB:
        case MTLPixelFormatBGRA8Unorm_sRGB:
            return 1;
        default:
            return 0;
    }
}

void mc_metal_render_world_m4(id<MTL4CommandBuffer> cb,
                              id<MTLTexture> canvas,
                              id<MTLTexture> gbuf_depth) {
    if (!g_r.initialized || !cb || !canvas || !runtime_mc_active()) return;
    mc_snapshot_copy(&g_r.replay_snap);
    const McRenderSnapshot *s = &g_r.replay_snap;
    if (!s->active || !s->cam_valid) return;

    const McWorldPsoEntry *psos = get_world_pso(canvas.pixelFormat);
    if (!psos) return;

    ensure_mc_depth(canvas.width, canvas.height);
    if (!g_r.mc_depth) return;

    int slot = g_r.world_slot;
    g_r.world_slot = (g_r.world_slot + 1) % MC_RING_SLOTS;

    McVert3D *vb = (McVert3D *)g_r.world_vb[slot].contents;
    if (!vb) return;

    McMesh3D opaque_m = { vb, 0, MC_MAX_WORLD_QUADS / 2u };
    McMesh3D blend_m  = { vb + (MC_MAX_WORLD_QUADS / 2u) * 4u, 0, MC_MAX_WORLD_QUADS / 4u };
    McMesh3D vm_m     = { vb + (MC_MAX_WORLD_QUADS * 3u / 4u) * 4u, 0, MC_MAX_WORLD_QUADS / 4u };

    if (s->mode == 1) {
        build_world_scene(s, &opaque_m, &blend_m);
        build_viewmodel_scene(s, &vm_m);
    }

    if (opaque_m.quad_count == 0 && blend_m.quad_count == 0 && vm_m.quad_count == 0) {
        return;
    }

    uint32_t target_lin = (uint32_t)pixel_fmt_is_linear_output(canvas.pixelFormat);

    McWorldUniforms *uw = (McWorldUniforms *)g_r.world_ub_w[slot].contents;
    memcpy(uw->vp_rows, s->cam_vp, sizeof(uw->vp_rows));
    uw->ignore_gbuf = gbuf_depth ? 0u : 1u;
    uw->tint_mul[0] = uw->tint_mul[1] = uw->tint_mul[2] = uw->tint_mul[3] = 1.0f;
    uw->canvas_w = (float)canvas.width;
    uw->canvas_h = (float)canvas.height;
    uw->time_sec = s->time_sec;
    uw->target_is_linear = target_lin;

    McWorldUniforms *uvm = (McWorldUniforms *)g_r.world_ub_vm[slot].contents;
    McMat4 vm_proj = mat4_perspective_gl(70.0f * (float)M_PI / 180.0f,
                                         (float)canvas.width / fmaxf((float)canvas.height, 1.0f),
                                         0.05f, 10.0f);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) uvm->vp_rows[r][c] = vm_proj.m[r * 4 + c];
    }
    uvm->tint_mul[0] = uvm->tint_mul[1] = uvm->tint_mul[2] = uvm->tint_mul[3] = 1.0f;
    uvm->canvas_w = (float)canvas.width;
    uvm->canvas_h = (float)canvas.height;
    uvm->time_sec = s->time_sec;
    uvm->ignore_gbuf = 1u;
    uvm->target_is_linear = target_lin;

    /* Probe Hyrule ambient luminance & tint from canvas before opening render pass on it. */
    if (g_r.pso_luma && g_r.env_luma_buf && g_r.luma_args[slot] &&
        (canvas.usage & MTLTextureUsageShaderRead) != 0 &&
        canvas.width > 16u && canvas.height > 16u) {
        id<MTL4ComputeCommandEncoder> ce = [cb computeCommandEncoder];
        if (ce) {
            id<MTL4ArgumentTable> la = g_r.luma_args[slot];
            [la setAddress:g_r.env_luma_buf.gpuAddress atIndex:0];
            [la setAddress:g_r.world_ub_w[slot].gpuAddress atIndex:1];
            [la setTexture:canvas.gpuResourceID atIndex:0];
            [ce barrierAfterQueueStages:MTLStageAll
                           beforeStages:MTLStageDispatch
                      visibilityOptions:MTL4VisibilityOptionDevice];
            [ce setComputePipelineState:g_r.pso_luma];
            [ce setArgumentTable:la];
            [ce dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
            [ce barrierAfterStages:MTLStageDispatch
                 beforeQueueStages:MTLStageFragment
                 visibilityOptions:MTL4VisibilityOptionDevice];
            [ce endEncoding];
        }
    }

    id<MTLBuffer> env_buf = g_r.env_luma_buf ? g_r.env_luma_buf : g_r.env_default_buf;
    id<MTL4ArgumentTable> args = g_r.world_args[slot];
    id<MTLTexture> depth_tex = gbuf_depth ? gbuf_depth : g_r.dummy_depth;
    if (env_buf) [args setAddress:env_buf.gpuAddress atIndex:2];
    [args setTexture:g_r.atlas_tex.gpuResourceID atIndex:0];
    [args setTexture:g_r.glint_tex.gpuResourceID atIndex:1];
    [args setTexture:depth_tex.gpuResourceID atIndex:2];
    [args setSamplerState:g_r.samp_nearest.gpuResourceID atIndex:0];
    [args setSamplerState:g_r.samp_repeat.gpuResourceID atIndex:1];

    MTLViewport vp = { 0.0, 0.0, (double)canvas.width, (double)canvas.height, 0.0, 1.0 };

    /* Pass (a): World 3D depth-tested against both gbuf_depth & mc_depth */
    if (opaque_m.quad_count > 0 || blend_m.quad_count > 0) {
        MTL4RenderPassDescriptor *pd = [MTL4RenderPassDescriptor new];
        pd.colorAttachments[0].texture = canvas;
        pd.colorAttachments[0].loadAction = MTLLoadActionLoad;
        pd.colorAttachments[0].storeAction = MTLStoreActionStore;
        pd.depthAttachment.texture = g_r.mc_depth;
        pd.depthAttachment.loadAction = MTLLoadActionClear;
        pd.depthAttachment.clearDepth = 1.0;
        pd.depthAttachment.storeAction = MTLStoreActionStore;

        id<MTL4RenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:pd];
        if (en) {
            [en barrierAfterQueueStages:MTLStageAll
                           beforeStages:MTLStageVertex | MTLStageFragment
                      visibilityOptions:MTL4VisibilityOptionDevice];
            [en setViewport:vp];
            [en setCullMode:MTLCullModeNone];
            [args setAddress:g_r.world_ub_w[slot].gpuAddress atIndex:1];
            [en setArgumentTable:args atStages:MTLRenderStageVertex | MTLRenderStageFragment];

            if (opaque_m.quad_count > 0) {
                [args setAddress:g_r.world_vb[slot].gpuAddress atIndex:0];
                [en setRenderPipelineState:psos->pso_opaque];
                [en setDepthStencilState:g_r.dss_rw];
                [en drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                               indexCount:opaque_m.quad_count * 6u
                                indexType:MTLIndexTypeUInt32
                              indexBuffer:g_r.quad_ib.gpuAddress
                        indexBufferLength:g_r.quad_ib.length];
            }
            if (blend_m.quad_count > 0) {
                uint64_t off = (uint64_t)(MC_MAX_WORLD_QUADS / 2u) * 4u * sizeof(McVert3D);
                [args setAddress:g_r.world_vb[slot].gpuAddress + off atIndex:0];
                [en setRenderPipelineState:psos->pso_blend];
                [en setDepthStencilState:g_r.dss_ro];
                [en drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                               indexCount:blend_m.quad_count * 6u
                                indexType:MTLIndexTypeUInt32
                              indexBuffer:g_r.quad_ib.gpuAddress
                        indexBufferLength:g_r.quad_ib.length];
            }
            [en barrierAfterStages:MTLStageVertex | MTLStageFragment
                 beforeQueueStages:MTLStageAll
                 visibilityOptions:MTL4VisibilityOptionDevice];
            [en endEncoding];
        }
    }

    /* Pass (b): 1st-Person Viewmodel (clears ONLY mc_depth to 1.0, never clips walls) */
    if (vm_m.quad_count > 0) {
        MTL4RenderPassDescriptor *pd = [MTL4RenderPassDescriptor new];
        pd.colorAttachments[0].texture = canvas;
        pd.colorAttachments[0].loadAction = MTLLoadActionLoad;
        pd.colorAttachments[0].storeAction = MTLStoreActionStore;
        pd.depthAttachment.texture = g_r.mc_depth;
        pd.depthAttachment.loadAction = MTLLoadActionClear;
        pd.depthAttachment.clearDepth = 1.0;
        pd.depthAttachment.storeAction = MTLStoreActionDontCare;

        id<MTL4RenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:pd];
        if (en) {
            [en barrierAfterQueueStages:MTLStageAll
                           beforeStages:MTLStageVertex | MTLStageFragment
                      visibilityOptions:MTL4VisibilityOptionDevice];
            [en setViewport:vp];
            [en setCullMode:MTLCullModeNone];
            uint64_t off = (uint64_t)(MC_MAX_WORLD_QUADS * 3u / 4u) * 4u * sizeof(McVert3D);
            [args setAddress:g_r.world_vb[slot].gpuAddress + off atIndex:0];
            [args setAddress:g_r.world_ub_vm[slot].gpuAddress atIndex:1];
            [en setArgumentTable:args atStages:MTLRenderStageVertex | MTLRenderStageFragment];
            [en setRenderPipelineState:psos->pso_opaque];
            [en setDepthStencilState:g_r.dss_rw];
            [en drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                           indexCount:vm_m.quad_count * 6u
                            indexType:MTLIndexTypeUInt32
                          indexBuffer:g_r.quad_ib.gpuAddress
                    indexBufferLength:g_r.quad_ib.length];
            [en barrierAfterStages:MTLStageVertex | MTLStageFragment
                 beforeQueueStages:MTLStageAll
                 visibilityOptions:MTL4VisibilityOptionDevice];
            [en endEncoding];
        }
    }
}

/* =========================================================================
 * Pass (c) Pre-step: Offscreen Z-buffered 3D Paperdoll Steve
 * ========================================================================= */

static uint32_t prepare_paperdoll_verts(int slot, const McRenderSnapshot *s) {
    if (!s->active || s->mode != 1 || s->inv_open == MC_INV_CLOSED ||
        s->inv_paperdoll_w <= 0.0f || s->inv_paperdoll_h <= 0.0f) {
        return 0;
    }
    ensure_paperdoll_targets(slot);
    if (!g_r.paper_tex[slot] || !g_r.paper_depth[slot] || !g_r.paper_vb[slot]) return 0;

    McVert3D *vb = (McVert3D *)g_r.paper_vb[slot].contents;
    McMesh3D pm = { vb, 0, MC_MAX_PAPER_QUADS };

    float pd_cx = s->inv_paperdoll_x + s->inv_paperdoll_w * 0.5f;
    float pd_cy = s->inv_paperdoll_y + s->inv_paperdoll_h * 0.48f;
    float gw = s->gui_w > 0.0f ? s->gui_w : 480.0f;
    float gh = s->gui_h > 0.0f ? s->gui_h : 270.0f;
    float look_dx = (s->cursor_nx * gw - pd_cx) / 90.0f;
    float look_dy = (s->cursor_ny * gh - pd_cy) / 90.0f;
    float yaw = fmaxf(-0.85f, fminf(0.85f, -look_dx)) + 0.25f;
    float pitch = fmaxf(-0.45f, fminf(0.45f, -look_dy));

    McPlayerSnap doll = s->player;
    doll.valid = 1;
    doll.yaw = yaw;
    doll.pitch = pitch;
    doll.elytra_open = 0.0f;
    doll.crouching = 0;

    McMat4 model = mat4_mul(mat4_translate(0.0f, -0.90f, -3.15f), mat4_rot_y(yaw));
    emit_steve_model(&pm, model, &doll, s->tick, s->mainhand_scale, s->offhand_scale, MC_V3D_NO_GBUF);

    McWorldUniforms *ub = (McWorldUniforms *)g_r.paper_ub[slot].contents;
    McMat4 proj = mat4_perspective_gl(36.0f * (float)M_PI / 180.0f,
                                      (float)MC_PAPERDOLL_W / (float)MC_PAPERDOLL_H,
                                      0.2f, 10.0f);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) ub->vp_rows[r][c] = proj.m[r * 4 + c];
    }
    ub->tint_mul[0] = ub->tint_mul[1] = ub->tint_mul[2] = ub->tint_mul[3] = 1.0f;
    ub->canvas_w = (float)MC_PAPERDOLL_W;
    ub->canvas_h = (float)MC_PAPERDOLL_H;
    ub->time_sec = s->time_sec;
    ub->ignore_gbuf = 1u;
    ub->target_is_linear = 0u;
    return pm.quad_count;
}

void mc_metal_render_gui_pre_m4(id<MTL4CommandBuffer> cb, int slot) {
    if (!g_r.initialized || !cb || slot < 0 || slot >= MC_ALL_SLOTS || !runtime_mc_active()) return;
    mc_snapshot_copy(&g_r.present_snap[slot]);
    g_r.pre_copied[slot] = 1u;
    const McRenderSnapshot *s = &g_r.present_snap[slot];
    uint32_t quads = prepare_paperdoll_verts(slot, s);
    if (quads == 0) return;

    const McWorldPsoEntry *psos = get_world_pso(MTLPixelFormatRGBA8Unorm);
    if (!psos) return;

    id<MTL4ArgumentTable> args = g_r.paper_args[slot];
    [args setAddress:g_r.paper_vb[slot].gpuAddress atIndex:0];
    [args setAddress:g_r.paper_ub[slot].gpuAddress atIndex:1];
    if (g_r.env_default_buf) [args setAddress:g_r.env_default_buf.gpuAddress atIndex:2];
    [args setTexture:g_r.atlas_tex.gpuResourceID atIndex:0];
    [args setTexture:g_r.glint_tex.gpuResourceID atIndex:1];
    [args setTexture:g_r.dummy_depth.gpuResourceID atIndex:2];
    [args setSamplerState:g_r.samp_nearest.gpuResourceID atIndex:0];
    [args setSamplerState:g_r.samp_repeat.gpuResourceID atIndex:1];

    MTL4RenderPassDescriptor *pd = [MTL4RenderPassDescriptor new];
    pd.colorAttachments[0].texture = g_r.paper_tex[slot];
    pd.colorAttachments[0].loadAction = MTLLoadActionClear;
    pd.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    pd.colorAttachments[0].storeAction = MTLStoreActionStore;
    pd.depthAttachment.texture = g_r.paper_depth[slot];
    pd.depthAttachment.loadAction = MTLLoadActionClear;
    pd.depthAttachment.clearDepth = 1.0;
    pd.depthAttachment.storeAction = MTLStoreActionDontCare;

    id<MTL4RenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:pd];
    if (!en) return;
    [en setViewport:(MTLViewport){0, 0, (double)MC_PAPERDOLL_W, (double)MC_PAPERDOLL_H, 0.0, 1.0}];
    [en setCullMode:MTLCullModeNone];
    [en setArgumentTable:args atStages:MTLRenderStageVertex | MTLRenderStageFragment];
    [en setRenderPipelineState:psos->pso_opaque];
    [en setDepthStencilState:g_r.dss_rw];
    [en drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                   indexCount:quads * 6u
                    indexType:MTLIndexTypeUInt32
                  indexBuffer:g_r.quad_ib.gpuAddress
            indexBufferLength:g_r.quad_ib.length];
    [en barrierAfterStages:MTLStageVertex | MTLStageFragment
         beforeQueueStages:MTLStageAll
         visibilityOptions:MTL4VisibilityOptionDevice];
    [en endEncoding];
}

void mc_metal_render_gui_pre_m3(id<MTLCommandBuffer> cb, int slot) {
    if (!g_r.initialized || !cb || slot < 0 || slot >= MC_ALL_SLOTS || !runtime_mc_active()) return;
    mc_snapshot_copy(&g_r.present_snap[slot]);
    g_r.pre_copied[slot] = 1u;
    const McRenderSnapshot *s = &g_r.present_snap[slot];
    uint32_t quads = prepare_paperdoll_verts(slot, s);
    if (quads == 0) return;

    const McWorldPsoEntry *psos = get_world_pso(MTLPixelFormatRGBA8Unorm);
    if (!psos) return;

    MTLRenderPassDescriptor *pd = [MTLRenderPassDescriptor renderPassDescriptor];
    pd.colorAttachments[0].texture = g_r.paper_tex[slot];
    pd.colorAttachments[0].loadAction = MTLLoadActionClear;
    pd.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    pd.colorAttachments[0].storeAction = MTLStoreActionStore;
    pd.depthAttachment.texture = g_r.paper_depth[slot];
    pd.depthAttachment.loadAction = MTLLoadActionClear;
    pd.depthAttachment.clearDepth = 1.0;
    pd.depthAttachment.storeAction = MTLStoreActionDontCare;

    id<MTLRenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:pd];
    if (!en) return;
    [en setViewport:(MTLViewport){0, 0, (double)MC_PAPERDOLL_W, (double)MC_PAPERDOLL_H, 0.0, 1.0}];
    [en setCullMode:MTLCullModeNone];
    [en setVertexBuffer:g_r.paper_vb[slot] offset:0 atIndex:0];
    [en setVertexBuffer:g_r.paper_ub[slot] offset:0 atIndex:1];
    [en setFragmentBuffer:g_r.paper_ub[slot] offset:0 atIndex:1];
    if (g_r.env_default_buf) [en setFragmentBuffer:g_r.env_default_buf offset:0 atIndex:2];
    [en setFragmentTexture:g_r.atlas_tex atIndex:0];
    [en setFragmentTexture:g_r.glint_tex atIndex:1];
    [en setFragmentTexture:g_r.dummy_depth atIndex:2];
    [en setFragmentSamplerState:g_r.samp_nearest atIndex:0];
    [en setFragmentSamplerState:g_r.samp_repeat atIndex:1];
    [en setRenderPipelineState:psos->pso_opaque];
    [en setDepthStencilState:g_r.dss_rw];
    [en drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                   indexCount:quads * 6u
                    indexType:MTLIndexTypeUInt32
                  indexBuffer:g_r.quad_ib
            indexBufferOffset:0];
    [en endEncoding];
}

/* =========================================================================
 * Pass (c) Native-Res 2D GUI Geometry Builders
 * ========================================================================= */

typedef struct {
    McVert2D *verts;
    uint32_t  quad_count;
    uint32_t  max_quads;
    uint32_t  xhair_start;
    uint32_t  xhair_count;
} McMesh2D;

static inline void push_quad_2d(McMesh2D *m,
                                float x, float y, float w, float h,
                                const float rect[4],
                                float u0, float v0, float u1, float v1,
                                uint32_t rgba, uint16_t mode, uint16_t flags) {
    if (m->quad_count >= m->max_quads || w <= 0.0f || h <= 0.0f) return;
    McVert2D *v = m->verts + (size_t)m->quad_count * 4u;
    v[0] = (McVert2D){ {x,     y},     {u0,v0}, {rect[0],rect[1],rect[2],rect[3]}, rgba, mode, flags };
    v[1] = (McVert2D){ {x + w, y},     {u1,v0}, {rect[0],rect[1],rect[2],rect[3]}, rgba, mode, flags };
    v[2] = (McVert2D){ {x + w, y + h}, {u1,v1}, {rect[0],rect[1],rect[2],rect[3]}, rgba, mode, flags };
    v[3] = (McVert2D){ {x,     y + h}, {u0,v1}, {rect[0],rect[1],rect[2],rect[3]}, rgba, mode, flags };
    m->quad_count++;
}

static inline void push_ptquad_2d(McMesh2D *m,
                                  const float p0[2], const float p1[2],
                                  const float p2[2], const float p3[2],
                                  const float rect[4],
                                  float u0, float v0, float u1, float v1,
                                  uint32_t rgba, uint16_t mode, uint16_t flags) {
    if (m->quad_count >= m->max_quads) return;
    McVert2D *v = m->verts + (size_t)m->quad_count * 4u;
    v[0] = (McVert2D){ {p0[0], p0[1]}, {u0,v0}, {rect[0],rect[1],rect[2],rect[3]}, rgba, mode, flags };
    v[1] = (McVert2D){ {p1[0], p1[1]}, {u1,v0}, {rect[0],rect[1],rect[2],rect[3]}, rgba, mode, flags };
    v[2] = (McVert2D){ {p2[0], p2[1]}, {u1,v1}, {rect[0],rect[1],rect[2],rect[3]}, rgba, mode, flags };
    v[3] = (McVert2D){ {p3[0], p3[1]}, {u0,v1}, {rect[0],rect[1],rect[2],rect[3]}, rgba, mode, flags };
    m->quad_count++;
}

static void gui_emit_nine_slice(McMesh2D *m, const McGuiQuad *q) {
    McTextureId tid = (McTextureId)q->tex_id;
    float tw = (tid < MC_TEX_COUNT && g_r.rects[tid].loaded) ? (float)g_r.rects[tid].w : 100.0f;
    float th = (tid < MC_TEX_COUNT && g_r.rects[tid].loaded) ? (float)g_r.rects[tid].h : 100.0f;
    float base_rect[4];
    atlas_subrect(tid, q->u0, q->v0, q->u1, q->v1, base_rect);

    float sw = tw * (q->u1 - q->u0);
    float sh = th * (q->v1 - q->v0);
    float b = (float)(q->aux > 0 ? q->aux : 9);
    float bx = fminf(b, fminf(q->w * 0.48f, sw * 0.48f));
    float by = fminf(b, fminf(q->h * 0.48f, sh * 0.48f));
    if (sw <= 1.0f || sh <= 1.0f) return;

    float ux = bx / sw, uy = by / sh;
    float xs[4] = { q->x, q->x + bx, q->x + q->w - bx, q->x + q->w };
    float ys[4] = { q->y, q->y + by, q->y + q->h - by, q->y + q->h };
    float us[4] = { 0.0f, ux, 1.0f - ux, 1.0f };
    float vs[4] = { 0.0f, uy, 1.0f - uy, 1.0f };
    uint32_t col = q->rgba_tint ? q->rgba_tint : 0xFFFFFFFFu;

    for (int ry = 0; ry < 3; ry++) {
        for (int rx = 0; rx < 3; rx++) {
            float qw = xs[rx + 1] - xs[rx], qh = ys[ry + 1] - ys[ry];
            if (qw <= 0.0f || qh <= 0.0f) continue;
            push_quad_2d(m, xs[rx], ys[ry], qw, qh, base_rect,
                         us[rx], vs[ry], us[rx + 1], vs[ry + 1],
                         col, MC_G2D_MODE_ATLAS, q->flags);
        }
    }
}

static void gui_emit_iso_block(McMesh2D *m, float x, float y, float w, float h,
                               McBlockType bt, uint32_t rgba_tint, uint16_t flags,
                               uint64_t tick) {
    uint32_t tint = rgba_tint ? rgba_tint : 0xFFFFFFFFu;
    if (bt == MC_BLK_TORCH || bt == MC_BLK_GLASS_PANE) {
        float rect[4];
        atlas_subrect(bt == MC_BLK_TORCH ? MC_TEX_BLOCK_TORCH : MC_TEX_BLOCK_GLASS, 0, 0, 1, 1, rect);
        push_quad_2d(m, x, y, w, h, rect, 0,0,1,1, tint, MC_G2D_MODE_ATLAS, flags);
        return;
    }

    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float sc = fminf(w, h) * 0.57f;
    const float c45 = 0.70710678f, c30 = 0.8660254f, s30 = 0.5f;

    float pts[8][2];
    static const float s_k[8][3] = {
        {-0.5f,+0.5f,-0.5f}, {+0.5f,+0.5f,-0.5f}, {+0.5f,+0.5f,+0.5f}, {-0.5f,+0.5f,+0.5f}, /* top 0..3 */
        {-0.5f,-0.5f,-0.5f}, {+0.5f,-0.5f,-0.5f}, {+0.5f,-0.5f,+0.5f}, {-0.5f,-0.5f,+0.5f}  /* bot 4..7 */
    };
    for (int i = 0; i < 8; i++) {
        float extent = bt == MC_BLK_BED_FOOT ? 1.4f : 1.0f;
        float height = bt == MC_BLK_BED_FOOT ? 0.55f : 1.0f;
        float rx = (s_k[i][0] - s_k[i][2]*extent) * c45;
        float rz = (s_k[i][0] + s_k[i][2]*extent) * c45;
        pts[i][0] = cx + sc * rx;
        pts[i][1] = cy - sc * (s_k[i][1] * height * c30 - rz * s30);
    }

    /* Top (+Y), Left-Front (+Z), Right-Front (+X) */
    static const int k_faces[3] = { 3, 5, 1 };
    static const int k_idx[3][4] = {
        {0, 1, 2, 3},
        {3, 2, 6, 7},
        {2, 1, 5, 6}
    };
    static const float k_sh[3] = { 1.00f, 0.80f, 0.60f };

    for (int f = 0; f < 3; f++) {
        float rect[4];
        uint32_t frgba = 0xFFFFFFFFu;
        int cutout = 0, emissive = 0;
        block_face_tex_rect(bt, k_faces[f], 1, tick, rect, &frgba, &cutout, &emissive);
        if (bt == MC_BLK_BED_FOOT && f==0) atlas_px_rect(MC_TEX_RED_BED,6,6,16,16,rect);
        (void)cutout;
        uint32_t col = tint_rgba_mul(tint_rgba_blend(tint, frgba), emissive ? 1.0f : k_sh[f], 1.0f);
        push_ptquad_2d(m, pts[k_idx[f][0]], pts[k_idx[f][1]], pts[k_idx[f][2]], pts[k_idx[f][3]],
                       rect, 0,0,1,1, col, MC_G2D_MODE_ATLAS, flags);
        if (bt == MC_BLK_GRASS && f != 0) {
            float orect[4];
            atlas_subrect(MC_TEX_BLOCK_GRASS_SIDE_OVERLAY, 0, 0, 1, 1, orect);
            uint32_t gcol = tint_rgba_mul(tint_rgba_blend(tint, g_r.grass_rgba), k_sh[f], 1.0f);
            push_ptquad_2d(m, pts[k_idx[f][0]], pts[k_idx[f][1]], pts[k_idx[f][2]], pts[k_idx[f][3]],
                           orect, 0,0,1,1, gcol, MC_G2D_MODE_ATLAS, flags);
        }
    }
}

static uint32_t mc_section_color(char code, uint8_t alpha) {
    switch (code) {
        case '0': return pack_rgba(  0,   0,   0, alpha);
        case '1': return pack_rgba(  0,   0, 170, alpha);
        case '2': return pack_rgba(  0, 170,   0, alpha);
        case '3': return pack_rgba(  0, 170, 170, alpha);
        case '4': return pack_rgba(170,   0,   0, alpha);
        case '5': return pack_rgba(170,   0, 170, alpha);
        case '6': return pack_rgba(255, 170,   0, alpha);
        case '7': return pack_rgba(170, 170, 170, alpha);
        case '8': return pack_rgba( 85,  85,  85, alpha);
        case '9': return pack_rgba( 85,  85, 255, alpha);
        case 'a': return pack_rgba( 85, 255,  85, alpha);
        case 'b': return pack_rgba( 85, 255, 255, alpha);
        case 'c': return pack_rgba(255,  85,  85, alpha);
        case 'd': return pack_rgba(255,  85, 255, alpha);
        case 'e': return pack_rgba(255, 255,  85, alpha);
        case 'f': return pack_rgba(255, 255, 255, alpha);
        default:  return pack_rgba(255, 255, 255, alpha);
    }
}

static void gui_emit_text(McMesh2D *m, const McGuiText *t) {
    if (!t->text[0]) return;
    float rect[4];
    atlas_subrect(MC_TEX_FONT_ASCII, 0.0f, 0.0f, 1.0f, 1.0f, rect);

    float sc = t->scale > 0.0f ? t->scale : 1.0f;
    int px_w = mc_asset_measure_text(t->text);
    float total_w = (float)px_w * sc;
    float base_x = t->x;
    uint8_t align = t->flags & 3u;
    if (align == MC_GUI_TEXT_CENTER) base_x -= total_w * 0.5f;
    else if (align == MC_GUI_TEXT_RIGHT) base_x -= total_w;

    uint32_t base_col = t->rgba ? t->rgba : 0xFFFFFFFFu;
    uint8_t base_a = (uint8_t)((base_col >> 24) & 0xffu);
    uint32_t dark_col = pack_rgba((uint8_t)((base_col & 0xffu) >> 2),
                                  (uint8_t)(((base_col >> 8) & 0xffu) >> 2),
                                  (uint8_t)(((base_col >> 16) & 0xffu) >> 2), base_a);
    uint32_t blk_col = pack_rgba(0, 0, 0, base_a);

    int n_passes = (t->flags & MC_GUI_TEXT_OUTLINE4) ? 5 : ((t->flags & MC_GUI_TEXT_SHADOW) ? 2 : 1);
    for (int pass = 0; pass < n_passes; pass++) {
        float ox = 0.0f, oy = 0.0f;
        int is_fg = (pass == n_passes - 1);
        uint32_t cur_col = base_col;
        if (!is_fg) {
            if (t->flags & MC_GUI_TEXT_OUTLINE4) {
                static const float k_ox[4] = {-1, 1, 0, 0};
                static const float k_oy[4] = { 0, 0,-1, 1};
                ox = k_ox[pass] * sc;
                oy = k_oy[pass] * sc;
                cur_col = blk_col;
            } else {
                ox = sc; oy = sc;
                cur_col = dark_col;
            }
        }

        float cx = base_x + ox;
        float cy = t->y + oy;
        const unsigned char *p = (const unsigned char *)t->text;
        while (*p) {
            unsigned char c = *p++;
            if (c == 0xC2u && *p == 0xA7u && p[1]) {
                char code = (char)p[1];
                p += 2;
                if (is_fg) cur_col = mc_section_color(code, base_a);
                continue;
            }
            if (c >= 0x80u) {
                while ((*p & 0xC0u) == 0x80u) p++;
                cx += 6.0f * sc;
                continue;
            }
            int col_i = (int)(c & 15u);
            int row_i = (int)(c >> 4);
            float u0 = (float)(col_i * 8) / 128.0f;
            float v0 = (float)(row_i * 8) / 128.0f;
            float u1 = u0 + 8.0f / 128.0f;
            float v1 = v0 + 8.0f / 128.0f;
            if (c != ' ') {
                push_quad_2d(m, cx, cy, 8.0f * sc, 8.0f * sc, rect,
                             u0, v0, u1, v1, cur_col, MC_G2D_MODE_ATLAS, 0);
            }
            cx += (float)g_r.font_widths[c] * sc;
        }
    }
}

static void gui_emit_fallback_hud(McMesh2D *m, const McRenderSnapshot *s,
                                  float gw, float gh) {
    float rect[4];
    /* Centered Crosshair */
    if (s->inv_open == MC_INV_CLOSED) {
        atlas_subrect(MC_TEX_GUI_CROSSHAIR, 0,0,1,1, rect);
        m->xhair_start = m->quad_count;
        push_quad_2d(m, gw * 0.5f - 7.5f, gh * 0.5f - 7.5f, 15.0f, 15.0f, rect,
                     0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, MC_GUI_FLAG_CROSSHAIR);
        m->xhair_count = m->quad_count - m->xhair_start;
    }

    float hx = gw * 0.5f - 91.0f, hy = gh - 22.0f;
    atlas_subrect(MC_TEX_GUI_HOTBAR, 0,0,1,1, rect);
    push_quad_2d(m, hx, hy, 182.0f, 22.0f, rect, 0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, 0);

    int sel = (s->selected_slot >= 0 && s->selected_slot < 9) ? s->selected_slot : 0;
    atlas_subrect(MC_TEX_GUI_HOTBAR_SELECTION, 0,0,1,1, rect);
    push_quad_2d(m, hx - 1.0f + (float)sel * 20.0f, hy - 1.0f, 24.0f, 23.0f, rect,
                 0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, 0);

    if (s->offhand.item_id != MC_ITM_NONE) {
        atlas_subrect(MC_TEX_GUI_HOTBAR_OFFHAND_LEFT, 0,0,1,1, rect);
        push_quad_2d(m, hx - 29.0f, hy - 1.0f, 29.0f, 24.0f, rect, 0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, 0);
    }

    /* XP bar & level */
    float xpy = hy - 7.0f;
    atlas_subrect(MC_TEX_GUI_XP_BG, 0,0,1,1, rect);
    push_quad_2d(m, hx, xpy, 182.0f, 5.0f, rect, 0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, 0);
    if (s->xp_prog > 0.001f) {
        float xp = fminf(1.0f, s->xp_prog);
        atlas_subrect(MC_TEX_GUI_XP_PROGRESS, 0,0,1,1, rect);
        push_quad_2d(m, hx, xpy, 182.0f * xp, 5.0f, rect, 0,0,xp,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, 0);
    }
    if (s->xp_level > 0) {
        McGuiText xt = {0};
        xt.x = gw * 0.5f; xt.y = xpy - 6.0f; xt.scale = 1.0f;
        xt.rgba = pack_rgba(128, 255, 32, 255);
        xt.flags = MC_GUI_TEXT_CENTER | MC_GUI_TEXT_OUTLINE4;
        snprintf(xt.text, sizeof xt.text, "%d", s->xp_level);
        gui_emit_text(m, &xt);
    }

    /* Hearts & Food */
    float stat_y = hy - 17.0f;
    int max_h = (s->max_hearts_half > 0 ? s->max_hearts_half : 20) / 2;
    for (int i = 0; i < max_h && i < 10; i++) {
        float px = hx + (float)i * 8.0f;
        atlas_subrect(MC_TEX_GUI_HEART_CONTAINER, 0,0,1,1, rect);
        push_quad_2d(m, px, stat_y, 9.0f, 9.0f, rect, 0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, 0);
        int hp_rem = s->hearts_half - i * 2;
        if (hp_rem >= 2) {
            atlas_subrect(MC_TEX_GUI_HEART_FULL, 0,0,1,1, rect);
            push_quad_2d(m, px, stat_y, 9.0f, 9.0f, rect, 0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, 0);
        } else if (hp_rem == 1) {
            atlas_subrect(MC_TEX_GUI_HEART_HALF, 0,0,1,1, rect);
            push_quad_2d(m, px, stat_y, 9.0f, 9.0f, rect, 0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, 0);
        }
    }
    for (int i = 0; i < 10; i++) {
        float px = hx + 182.0f - (float)(i + 1) * 8.0f - 1.0f;
        atlas_subrect(MC_TEX_GUI_FOOD_EMPTY, 0,0,1,1, rect);
        push_quad_2d(m, px, stat_y, 9.0f, 9.0f, rect, 0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, 0);
        int fd_rem = s->hunger - i * 2;
        if (fd_rem >= 2) {
            atlas_subrect(MC_TEX_GUI_FOOD_FULL, 0,0,1,1, rect);
            push_quad_2d(m, px, stat_y, 9.0f, 9.0f, rect, 0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, 0);
        } else if (fd_rem == 1) {
            atlas_subrect(MC_TEX_GUI_FOOD_HALF, 0,0,1,1, rect);
            push_quad_2d(m, px, stat_y, 9.0f, 9.0f, rect, 0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, 0);
        }
    }

    /* Hotbar Items + Stack Counts */
    for (int i = 0; i < 9; i++) {
        const McItemStack *st = &s->hotbar[i];
        if (st->item_id == MC_ITM_NONE || st->count == 0) continue;
        float ix = hx + 3.0f + (float)i * 20.0f, iy = hy + 3.0f;
        uint16_t ifl = st->enchanted ? MC_GUI_FLAG_GLINT : 0u;
        McBlockType bt = item_to_block_type((McItemType)st->item_id);
        if (bt != MC_BLK_AIR) {
            gui_emit_iso_block(m, ix, iy, 16.0f, 16.0f, bt, 0xFFFFFFFFu, ifl, s->tick);
        } else {
            atlas_subrect(item_to_tex_id((McItemType)st->item_id, 0), 0,0,1,1, rect);
            push_quad_2d(m, ix, iy, 16.0f, 16.0f, rect, 0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_ATLAS, ifl);
        }
        if (st->count > 1) {
            McGuiText ct = {0};
            ct.x = ix + 16.0f; ct.y = iy + 9.0f; ct.scale = 1.0f;
            ct.rgba = 0xFFFFFFFFu;
            ct.flags = MC_GUI_TEXT_RIGHT | MC_GUI_TEXT_SHADOW;
            snprintf(ct.text, sizeof ct.text, "%u", (unsigned)st->count);
            gui_emit_text(m, &ct);
        }
    }
}

static void build_gui_mesh(int slot, const McRenderSnapshot *s, McMesh2D *m,
                           float *out_gw, float *out_gh) {
    float gw = s->gui_w > 0.0f ? s->gui_w : 480.0f;
    float gh = s->gui_h > 0.0f ? s->gui_h : 270.0f;
    *out_gw = gw;
    *out_gh = gh;

    if (s->gui_quad_count == 0 && s->gui_text_count == 0) {
        if (s->active && s->mode == 1) {
            gui_emit_fallback_hud(m, s, gw, gh);
        }
        return;
    }

    /* Emit crosshair (invert-blended) in its own contiguous range, plus layered quads/texts. */
    int paperdoll_drawn = 0;
    for (uint8_t layer = 0; layer < 8; layer++) {
        for (int i = 0; i < s->gui_quad_count && i < MC_SNAP_MAX_GUI_QUADS; i++) {
            const McGuiQuad *q = &s->gui_quads[i];
            if (q->layer != layer || (q->flags & MC_GUI_FLAG_CROSSHAIR)) continue;
            uint32_t col = q->rgba_tint ? q->rgba_tint : 0xFFFFFFFFu;
            if (q->kind == MC_GUI_QUAD_SOLID) {
                float zero[4] = {0};
                push_quad_2d(m, q->x, q->y, q->w, q->h, zero, 0,0,1,1, col, MC_G2D_MODE_SOLID, 0);
            } else if (q->kind == MC_GUI_QUAD_ISO_BLOCK) {
                gui_emit_iso_block(m, q->x, q->y, q->w, q->h, (McBlockType)q->aux, col, q->flags, s->tick);
            } else if (q->flags & MC_GUI_FLAG_NINE_SLICE) {
                gui_emit_nine_slice(m, q);
            } else {
                float rect[4];
                atlas_subrect((McTextureId)q->tex_id, q->u0, q->v0, q->u1, q->v1, rect);
                push_quad_2d(m, q->x, q->y, q->w, q->h, rect, 0,0,1,1, col, MC_G2D_MODE_ATLAS, q->flags);
            }
        }
        for (int i = 0; i < s->gui_text_count && i < MC_SNAP_MAX_GUI_TEXTS; i++) {
            if (s->gui_texts[i].layer == layer) gui_emit_text(m, &s->gui_texts[i]);
        }
        if (layer == 5 && !paperdoll_drawn && s->inv_open != MC_INV_CLOSED &&
            s->inv_paperdoll_w > 0.0f && s->inv_paperdoll_h > 0.0f && g_r.paper_tex[slot]) {
            float dummy[4] = {0, 0, 1, 1};
            push_quad_2d(m, s->inv_paperdoll_x, s->inv_paperdoll_y,
                         s->inv_paperdoll_w, s->inv_paperdoll_h,
                         dummy, 0,0,1,1, 0xFFFFFFFFu, MC_G2D_MODE_PAPERDOLL, 0);
            paperdoll_drawn = 1;
        }
    }

    m->xhair_start = m->quad_count;
    for (int i = 0; i < s->gui_quad_count && i < MC_SNAP_MAX_GUI_QUADS; i++) {
        const McGuiQuad *q = &s->gui_quads[i];
        if (!(q->flags & MC_GUI_FLAG_CROSSHAIR)) continue;
        float rect[4];
        atlas_subrect((McTextureId)q->tex_id, q->u0, q->v0, q->u1, q->v1, rect);
        uint32_t col = q->rgba_tint ? q->rgba_tint : 0xFFFFFFFFu;
        push_quad_2d(m, q->x, q->y, q->w, q->h, rect, 0,0,1,1, col, MC_G2D_MODE_ATLAS, q->flags);
    }
    m->xhair_count = m->quad_count - m->xhair_start;
}

/* =========================================================================
 * Pass (c) Present-Thread Draw (Metal 4 + Classic Metal)
 * ========================================================================= */

static int prepare_gui_slot(int slot, MTLPixelFormat fmt, McMesh2D *out_m,
                            const McGuiPsoEntry **out_psos) {
    if (!g_r.initialized || slot < 0 || slot >= MC_ALL_SLOTS || !runtime_mc_active()) return 0;
    if (!g_r.pre_copied[slot]) mc_snapshot_copy(&g_r.present_snap[slot]);
    g_r.pre_copied[slot] = 0u;
    const McRenderSnapshot *s = &g_r.present_snap[slot];
    if (!s->active) return 0;

    *out_psos = get_gui_pso(fmt);
    if (!*out_psos || !g_r.gui_vb[slot] || !g_r.gui_ub[slot]) return 0;

    McMesh2D m = { (McVert2D *)g_r.gui_vb[slot].contents, 0, MC_MAX_GUI_QUADS, 0, 0 };
    float gw = 480.0f, gh = 270.0f;
    build_gui_mesh(slot, s, &m, &gw, &gh);
    if (m.quad_count == 0) return 0;

    McGuiUniforms *ub = (McGuiUniforms *)g_r.gui_ub[slot].contents;
    ub->gui_w = gw;
    ub->gui_h = gh;
    ub->time_sec = s->time_sec;
    ub->atlas_w = (float)MC_ATLAS_W;
    ub->atlas_h = (float)MC_ATLAS_H;
    *out_m = m;
    return 1;
}

void mc_metal_render_gui_m4(id<MTL4RenderCommandEncoder> en,
                            id<MTL4ArgumentTable> restore_args,
                            id<MTLRenderPipelineState> restore_pso,
                            id<MTLSamplerState> restore_samp,
                            id<MTLTexture> dst,
                            MTLViewport vp,
                            int slot) {
    if (!en || !dst) return;
    McMesh2D m = {0};
    const McGuiPsoEntry *psos = NULL;
    if (!prepare_gui_slot(slot, dst.pixelFormat, &m, &psos)) return;

    id<MTL4ArgumentTable> args = g_r.gui_args[slot];
    id<MTLTexture> paper = g_r.paper_tex[slot] ? g_r.paper_tex[slot] : g_r.dummy_rgba;
    [args setAddress:g_r.gui_vb[slot].gpuAddress atIndex:0];
    [args setAddress:g_r.gui_ub[slot].gpuAddress atIndex:1];
    [args setTexture:g_r.atlas_tex.gpuResourceID atIndex:0];
    [args setTexture:g_r.glint_tex.gpuResourceID atIndex:1];
    [args setTexture:paper.gpuResourceID atIndex:2];
    [args setSamplerState:g_r.samp_nearest.gpuResourceID atIndex:0];
    [args setSamplerState:g_r.samp_repeat.gpuResourceID atIndex:1];

    [en setViewport:vp];
    [en setCullMode:MTLCullModeNone];
    [en setArgumentTable:args atStages:MTLRenderStageVertex | MTLRenderStageFragment];

    if (m.xhair_start > 0) {
        [en setRenderPipelineState:psos->pso_blend];
        [en drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                       indexCount:m.xhair_start * 6u
                        indexType:MTLIndexTypeUInt32
                      indexBuffer:g_r.quad_ib.gpuAddress
                indexBufferLength:g_r.quad_ib.length];
    }
    if (m.xhair_count > 0) {
        [args setAddress:g_r.gui_vb[slot].gpuAddress + (uint64_t)m.xhair_start * 4u * sizeof(McVert2D) atIndex:0];
        [en setRenderPipelineState:psos->pso_crosshair];
        [en drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                       indexCount:m.xhair_count * 6u
                        indexType:MTLIndexTypeUInt32
                      indexBuffer:g_r.quad_ib.gpuAddress
                indexBufferLength:g_r.quad_ib.length];
    }

    if (restore_args) [en setArgumentTable:restore_args atStages:MTLRenderStageFragment];
    if (restore_pso)  [en setRenderPipelineState:restore_pso];
    if (restore_args && restore_samp) [restore_args setSamplerState:restore_samp.gpuResourceID atIndex:0];
}

void mc_metal_render_gui_m3(id<MTLRenderCommandEncoder> en,
                            id<MTLRenderPipelineState> restore_pso,
                            id<MTLSamplerState> restore_samp,
                            id<MTLTexture> dst,
                            MTLViewport vp,
                            int slot) {
    if (!en || !dst) return;
    McMesh2D m = {0};
    const McGuiPsoEntry *psos = NULL;
    if (!prepare_gui_slot(slot, dst.pixelFormat, &m, &psos)) return;

    id<MTLTexture> paper = g_r.paper_tex[slot] ? g_r.paper_tex[slot] : g_r.dummy_rgba;
    [en setViewport:vp];
    [en setCullMode:MTLCullModeNone];
    [en setVertexBuffer:g_r.gui_ub[slot] offset:0 atIndex:1];
    [en setFragmentBuffer:g_r.gui_ub[slot] offset:0 atIndex:1];
    [en setFragmentTexture:g_r.atlas_tex atIndex:0];
    [en setFragmentTexture:g_r.glint_tex atIndex:1];
    [en setFragmentTexture:paper atIndex:2];
    [en setFragmentSamplerState:g_r.samp_nearest atIndex:0];
    [en setFragmentSamplerState:g_r.samp_repeat atIndex:1];

    if (m.xhair_start > 0) {
        [en setVertexBuffer:g_r.gui_vb[slot] offset:0 atIndex:0];
        [en setRenderPipelineState:psos->pso_blend];
        [en drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                       indexCount:m.xhair_start * 6u
                        indexType:MTLIndexTypeUInt32
                      indexBuffer:g_r.quad_ib
                indexBufferOffset:0];
    }
    if (m.xhair_count > 0) {
        [en setVertexBuffer:g_r.gui_vb[slot] offset:(NSUInteger)m.xhair_start * 4u * sizeof(McVert2D) atIndex:0];
        [en setRenderPipelineState:psos->pso_crosshair];
        [en drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                       indexCount:m.xhair_count * 6u
                        indexType:MTLIndexTypeUInt32
                      indexBuffer:g_r.quad_ib
                indexBufferOffset:0];
    }

    if (restore_pso)  [en setRenderPipelineState:restore_pso];
    if (restore_samp) [en setFragmentSamplerState:restore_samp atIndex:0];
}

/* =========================================================================
 * One-Time Renderer Initialization
 * ========================================================================= */

void mc_metal_render_init(id<MTLDevice> dev, id<MTL4CommandQueue> q4) {
    if (g_r.initialized || !dev) return;
    g_r.dev = dev;
    g_r.q4 = q4;

    NSError *err = nil;
    g_r.res = [dev newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:&err];
    if (g_r.res && q4) {
        [q4 addResidencySet:g_r.res];
    }

    NSString *src = [NSString stringWithUTF8String:k_mc_msl_source];
    g_r.lib = [dev newLibraryWithSource:src options:nil error:&err];
    if (!g_r.lib) {
        fprintf(stderr, "[mc-metal] FATAL shader compile failed: %s\n",
                err ? err.localizedDescription.UTF8String : "?");
        return;
    }
    g_r.vs_world = [g_r.lib newFunctionWithName:@"mc_world_vs"];
    g_r.fs_world = [g_r.lib newFunctionWithName:@"mc_world_fs"];
    g_r.vs_gui   = [g_r.lib newFunctionWithName:@"mc_gui_vs"];
    g_r.fs_gui   = [g_r.lib newFunctionWithName:@"mc_gui_fs"];
    id<MTLFunction> fn_luma = [g_r.lib newFunctionWithName:@"mc_probe_luma_cs"];
    if (fn_luma) {
        g_r.pso_luma = [dev newComputePipelineStateWithFunction:fn_luma error:&err];
    }

    MTLDepthStencilDescriptor *ds = [MTLDepthStencilDescriptor new];
    ds.depthCompareFunction = MTLCompareFunctionLessEqual;
    ds.depthWriteEnabled = YES;
    g_r.dss_rw = [dev newDepthStencilStateWithDescriptor:ds];
    ds.depthWriteEnabled = NO;
    g_r.dss_ro = [dev newDepthStencilStateWithDescriptor:ds];
    ds.depthCompareFunction = MTLCompareFunctionAlways;
    g_r.dss_none = [dev newDepthStencilStateWithDescriptor:ds];

    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterNearest;
    sd.magFilter = MTLSamplerMinMagFilterNearest;
    sd.mipFilter = MTLSamplerMipFilterNotMipmapped;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    g_r.samp_nearest = [dev newSamplerStateWithDescriptor:sd];

    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeRepeat;
    sd.tAddressMode = MTLSamplerAddressModeRepeat;
    g_r.samp_repeat = [dev newSamplerStateWithDescriptor:sd];

    build_atlas();

    /* Static quad index buffer covering up to MC_MAX_WORLD_QUADS */
    size_t ib_bytes = (size_t)MC_MAX_WORLD_QUADS * 6u * sizeof(uint32_t);
    g_r.quad_ib = [dev newBufferWithLength:ib_bytes options:MTLResourceStorageModeShared];
    if (g_r.quad_ib) {
        uint32_t *idx = (uint32_t *)g_r.quad_ib.contents;
        for (uint32_t q = 0; q < MC_MAX_WORLD_QUADS; q++) {
            uint32_t b = q * 4u;
            idx[q * 6u + 0u] = b + 0u;
            idx[q * 6u + 1u] = b + 1u;
            idx[q * 6u + 2u] = b + 2u;
            idx[q * 6u + 3u] = b + 2u;
            idx[q * 6u + 4u] = b + 3u;
            idx[q * 6u + 5u] = b + 0u;
        }
        res_track(g_r.quad_ib);
    }

    MTL4ArgumentTableDescriptor *atd = [MTL4ArgumentTableDescriptor new];
    atd.maxBufferBindCount = 4;
    atd.maxTextureBindCount = 4;
    atd.maxSamplerStateBindCount = 2;

    g_r.env_luma_buf = [dev newBufferWithLength:sizeof(float) * 4u options:MTLResourceStorageModeShared];
    if (g_r.env_luma_buf) {
        float *p = (float *)g_r.env_luma_buf.contents;
        p[0] = 1.0f; p[1] = 1.0f; p[2] = 1.0f; p[3] = 0.85f;
        res_track(g_r.env_luma_buf);
    }
    g_r.env_default_buf = [dev newBufferWithLength:sizeof(float) * 4u options:MTLResourceStorageModeShared];
    if (g_r.env_default_buf) {
        float *p = (float *)g_r.env_default_buf.contents;
        p[0] = 1.0f; p[1] = 1.0f; p[2] = 1.0f; p[3] = 0.92f;
        res_track(g_r.env_default_buf);
    }

    for (int i = 0; i < MC_RING_SLOTS; i++) {
        g_r.world_vb[i] = [dev newBufferWithLength:(size_t)MC_MAX_WORLD_QUADS * 4u * sizeof(McVert3D)
                                           options:MTLResourceStorageModeShared];
        g_r.world_ub_w[i] = [dev newBufferWithLength:sizeof(McWorldUniforms)
                                             options:MTLResourceStorageModeShared];
        g_r.world_ub_vm[i] = [dev newBufferWithLength:sizeof(McWorldUniforms)
                                              options:MTLResourceStorageModeShared];
        if (g_r.world_vb[i]) res_track(g_r.world_vb[i]);
        if (g_r.world_ub_w[i]) res_track(g_r.world_ub_w[i]);
        if (g_r.world_ub_vm[i]) res_track(g_r.world_ub_vm[i]);
        if (q4) {
            g_r.world_args[i] = [dev newArgumentTableWithDescriptor:atd error:&err];
            g_r.luma_args[i]  = [dev newArgumentTableWithDescriptor:atd error:&err];
        }
    }

    for (int i = 0; i < MC_ALL_SLOTS; i++) {
        g_r.gui_vb[i] = [dev newBufferWithLength:(size_t)MC_MAX_GUI_QUADS * 4u * sizeof(McVert2D)
                                         options:MTLResourceStorageModeShared];
        g_r.gui_ub[i] = [dev newBufferWithLength:sizeof(McGuiUniforms)
                                         options:MTLResourceStorageModeShared];
        g_r.paper_vb[i] = [dev newBufferWithLength:(size_t)MC_MAX_PAPER_QUADS * 4u * sizeof(McVert3D)
                                           options:MTLResourceStorageModeShared];
        g_r.paper_ub[i] = [dev newBufferWithLength:sizeof(McWorldUniforms)
                                           options:MTLResourceStorageModeShared];
        if (g_r.gui_vb[i]) res_track(g_r.gui_vb[i]);
        if (g_r.gui_ub[i]) res_track(g_r.gui_ub[i]);
        if (g_r.paper_vb[i]) res_track(g_r.paper_vb[i]);
        if (g_r.paper_ub[i]) res_track(g_r.paper_ub[i]);
        if (q4 && i < MC_RING_SLOTS) {
            g_r.gui_args[i]   = [dev newArgumentTableWithDescriptor:atd error:&err];
            g_r.paper_args[i] = [dev newArgumentTableWithDescriptor:atd error:&err];
        }
    }

    g_r.initialized = 1;
    fprintf(stderr, "[mc-metal] initialized real-asset 3D+GUI Metal renderer (atlas %ux%u)\n",
            MC_ATLAS_W, MC_ATLAS_H);
}
