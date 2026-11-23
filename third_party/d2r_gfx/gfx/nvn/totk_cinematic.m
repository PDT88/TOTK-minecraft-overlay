// TOTK: Cinematic post-processing & depth-aware HDR shader pipeline.
// Stage 1 (pre-UI, linear HDR + G-buffer depth, right before TOTK's compose shader 0ac9a54938974396):
//   - Quarter-res true-HDR bloom & warm filmic halation (extract + separable anamorphic Gaussian blur)
//   - Metric-scale depth-aware Screen-Space Ambient Occlusion (8-tap rotated spiral, zero far-plane halos)
//   - Depth-aware atmospheric clarity (reduces milky mid-range haze while preserving distant aerial depth)
//   - Subtle luminance micro-clarity (unsharp mask, clamped)
//   - Filmic shadow toe, warm-highlight / cool-shadow split toning, natural foliage/sky sculpting
// Stage 2 (final present pass, after UI/upscale):
//   - Gentle optical cinema-lens vignette
//   - Organic luma-weighted 35mm film grain
//   - Optional 2.39:1 cinema letterbox bars (preset 3)
#import <Metal/Metal.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "totk_cinematic.h"

#define CLOG(...) do { fprintf(stderr, "[dm] [nvn-mtl] [cinematic] " __VA_ARGS__); fputc('\n', stderr); } while (0)

#define CIN_SLOTS 16
#define CIN_OLD   (CIN_SLOTS * 3)

typedef struct {
    float    size[2];
    float    inv_size[2];
    float    bloom_size[2];
    float    bloom_inv[2];
    float    near_plane;
    float    far_plane;
    float    mode;
    float    frame_f;
    uint32_t has_depth;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
} CinParams;

static struct {
    id<MTLDevice> dev;
    id<MTLResidencySet> rs;
    id<MTLLibrary> lib;
    id<MTLComputePipelineState> ps_extract;
    id<MTLComputePipelineState> ps_blur_h;
    id<MTLComputePipelineState> ps_blur_v;
    id<MTLComputePipelineState> ps_grade;
    id<MTLSamplerState> samp_lin;
    id<MTLTexture> out_tex;
    id<MTLTexture> bloom_a;
    id<MTLTexture> bloom_b;
    id<MTLTexture> dummy_depth;
    NSUInteger w, h, bw, bh;
    id<MTL4ArgumentTable> args_ext[CIN_SLOTS];
    id<MTL4ArgumentTable> args_bh[CIN_SLOTS];
    id<MTL4ArgumentTable> args_bv[CIN_SLOTS];
    id<MTL4ArgumentTable> args_grd[CIN_SLOTS];
    id<MTLBuffer> par[CIN_SLOTS];
    int slot;
    int failed;
    float near_p, far_p;
    int cam_ok;
    uint64_t frame, encoded_frame;
    __unsafe_unretained id<MTLTexture> last_src;
    struct { id obj; uint64_t frame; } old[CIN_OLD];
    int nold;
    // Present pass PSO cache (separate mutex because present runs on present/shot thread)
    pthread_mutex_t pres_mu;
    id<MTLRenderPipelineState> pres_pso[4];
    MTLPixelFormat pres_fmt[4];
    int pres_n;
} g = { .pres_mu = PTHREAD_MUTEX_INITIALIZER };

static _Atomic int g_mode = -1;

const char *cinematic_mode_name(int mode) {
    static const char *const names[CIN_NMODES] = {
        "off",
        "cinematic (balanced filmic HDR)",
        "golden cinema (warm sunlight & anamorphic glow)",
        "dramatic widescreen (deep contrast & 2.39:1 bars)"
    };
    return (mode >= 0 && mode < CIN_NMODES) ? names[mode] : "?";
}

int cinematic_mode(void) {
    int m = atomic_load_explicit(&g_mode, memory_order_relaxed);
    if (m < 0) {
        const char *e = getenv("TOTK_CINEMATIC");
        m = (e && *e) ? atoi(e) : CIN_OFF;
        if (m < 0 || m >= CIN_NMODES) m = CIN_BALANCED;
        atomic_store(&g_mode, m);
        if (m) CLOG("TOTK_CINEMATIC=%d: %s", m, cinematic_mode_name(m));
    }
    return m;
}

int cinematic_enabled(void) { return cinematic_mode() > 0; }

void cinematic_set(int mode) {
    if (mode < 0 || mode >= CIN_NMODES) mode = CIN_OFF;
    atomic_store(&g_mode, mode);
    CLOG("mode -> %d: %s", mode, cinematic_mode_name(mode));
}

void cinematic_cycle(void) {
    int next = (cinematic_mode() + 1) % CIN_NMODES;
    cinematic_set(next);
}

void cinematic_note_camera(const float *cam, int n) {
    if (n < 64) return;
    if (cam[56] > 0.0f && cam[57] > cam[56]) {
        g.near_p = cam[56];
        g.far_p  = cam[57];
        g.cam_ok = 1;
    }
}

void cinematic_frame_end(void) {
    g.frame++;
    g.cam_ok = 0;
}

id<MTLResidencySet> cinematic_residency(void) { return g.rs; }

static void keep_old(id obj) {
    if (!obj) return;
    int dropped = 0;
    for (int i = 0; i < g.nold; i++) {
        if (g.frame - g.old[i].frame >= CIN_SLOTS) {
            if ([g.old[i].obj conformsToProtocol:@protocol(MTLAllocation)]) {
                [g.rs removeAllocation:g.old[i].obj];
                dropped = 1;
            }
            g.old[i] = g.old[--g.nold];
            i--;
        }
    }
    if (dropped) [g.rs commit];
    if (g.nold < CIN_OLD) g.old[g.nold++] = (__typeof__(g.old[0])){ obj, g.frame };
}

static id<MTLTexture> mk_tex(MTLPixelFormat fmt, NSUInteger w, NSUInteger h, MTLTextureUsage u) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h mipmapped:NO];
    d.usage = u;
    d.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [g.dev newTextureWithDescriptor:d];
    if (t && g.rs) [g.rs addAllocation:t];
    return t;
}

static const char *const k_cinematic_msl =
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"\n"
"struct CinParams {\n"
"    float2 size;\n"
"    float2 inv_size;\n"
"    float2 bloom_size;\n"
"    float2 bloom_inv;\n"
"    float  near_plane;\n"
"    float  far_plane;\n"
"    float  mode;\n"
"    float  frame_f;\n"
"    uint   has_depth;\n"
"    uint   pad0;\n"
"    uint   pad1;\n"
"    uint   pad2;\n"
"};\n"
"\n"
"static inline float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }\n"
"\n"
"static inline float linearize_depth(float d, float n, float f) {\n"
"    float denom = max(f - d * (f - n), 1e-5);\n"
"    return (n * f) / denom;\n"
"}\n"
"\n"
"// Pass 1: Quarter-res linear HDR bright-pass extraction with soft knee + warm halation bias\n"
"kernel void cin_bloom_extract(texture2d<float, access::sample> src   [[texture(0)]],\n"
"                              texture2d<float, access::write>  dst   [[texture(1)]],\n"
"                              sampler                          samp  [[sampler(0)]],\n"
"                              constant CinParams              &p     [[buffer(0)]],\n"
"                              uint2                            gid   [[thread_position_in_grid]]) {\n"
"    if (gid.x >= uint(p.bloom_size.x) || gid.y >= uint(p.bloom_size.y)) return;\n"
"    float2 uv = (float2(gid) + 0.5) * p.bloom_inv;\n"
"    float2 o  = p.inv_size * 1.25;\n"
"    // 4-tap box downsample to suppress sub-pixel fireflies\n"
"    float3 c0 = max(src.sample(samp, uv + float2(-o.x, -o.y)).rgb, float3(0.0));\n"
"    float3 c1 = max(src.sample(samp, uv + float2( o.x, -o.y)).rgb, float3(0.0));\n"
"    float3 c2 = max(src.sample(samp, uv + float2(-o.x,  o.y)).rgb, float3(0.0));\n"
"    float3 c3 = max(src.sample(samp, uv + float2( o.x,  o.y)).rgb, float3(0.0));\n"
"    // Karis-style weighted average to prevent HDR firefly shimmering\n"
"    float w0 = 1.0 / (1.0 + luma(c0));\n"
"    float w1 = 1.0 / (1.0 + luma(c1));\n"
"    float w2 = 1.0 / (1.0 + luma(c2));\n"
"    float w3 = 1.0 / (1.0 + luma(c3));\n"
"    float3 c = (c0 * w0 + c1 * w1 + c2 * w2 + c3 * w3) / max(w0 + w1 + w2 + w3, 1e-4);\n"
"    c = min(c, float3(6.0));\n"
"    float br = max(max(c.r, c.g), c.b);\n"
"    float thresh = (p.mode > 1.5 && p.mode < 2.5) ? 0.68 : 0.82;\n"
"    float knee   = 0.45;\n"
"    float soft   = clamp(br - thresh + knee, 0.0, 2.0 * knee);\n"
"    soft = (soft * soft) / max(4.0 * knee, 1e-4);\n"
"    float contrib = max(soft, br - thresh) / max(br, 1e-4);\n"
"    float3 bloom = c * clamp(contrib, 0.0, 4.0);\n"
"    // Subtle warm filmic halation tint on bright highlights\n"
"    float3 hal_tint = (p.mode > 1.5 && p.mode < 2.5) ? float3(1.12, 0.99, 0.84) : float3(1.06, 0.99, 0.92);\n"
"    dst.write(float4(bloom * hal_tint, 1.0), gid);\n"
"}\n"
"\n"
"// Pass 2: Horizontal wide separable Gaussian blur (slightly wider for subtle anamorphic cinema character)\n"
"kernel void cin_bloom_blur_h(texture2d<float, access::sample> src  [[texture(0)]],\n"
"                             texture2d<float, access::write>  dst  [[texture(1)]],\n"
"                             sampler                          samp [[sampler(0)]],\n"
"                             constant CinParams              &p    [[buffer(0)]],\n"
"                             uint2                            gid  [[thread_position_in_grid]]) {\n"
"    if (gid.x >= uint(p.bloom_size.x) || gid.y >= uint(p.bloom_size.y)) return;\n"
"    float2 uv = (float2(gid) + 0.5) * p.bloom_inv;\n"
"    float anam = (p.mode > 1.5 && p.mode < 2.5) ? 2.15 : 1.65;\n"
"    float dx = p.bloom_inv.x * anam;\n"
"    const float w[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };\n"
"    const float o[5] = { 0.0, 1.384615, 3.230769, 5.15, 7.2 };\n"
"    float3 acc = src.sample(samp, uv).rgb * w[0];\n"
"    for (int i = 1; i < 5; i++) {\n"
"        acc += src.sample(samp, uv + float2( dx * o[i], 0.0)).rgb * w[i];\n"
"        acc += src.sample(samp, uv + float2(-dx * o[i], 0.0)).rgb * w[i];\n"
"    }\n"
"    dst.write(float4(acc, 1.0), gid);\n"
"}\n"
"\n"
"// Pass 3: Vertical separable Gaussian blur\n"
"kernel void cin_bloom_blur_v(texture2d<float, access::sample> src  [[texture(0)]],\n"
"                             texture2d<float, access::write>  dst  [[texture(1)]],\n"
"                             sampler                          samp [[sampler(0)]],\n"
"                             constant CinParams              &p    [[buffer(0)]],\n"
"                             uint2                            gid  [[thread_position_in_grid]]) {\n"
"    if (gid.x >= uint(p.bloom_size.x) || gid.y >= uint(p.bloom_size.y)) return;\n"
"    float2 uv = (float2(gid) + 0.5) * p.bloom_inv;\n"
"    float dy = p.bloom_inv.y * 1.15;\n"
"    const float w[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };\n"
"    const float o[5] = { 0.0, 1.384615, 3.230769, 5.15, 7.2 };\n"
"    float3 acc = src.sample(samp, uv).rgb * w[0];\n"
"    for (int i = 1; i < 5; i++) {\n"
"        acc += src.sample(samp, uv + float2(0.0,  dy * o[i])).rgb * w[i];\n"
"        acc += src.sample(samp, uv + float2(0.0, -dy * o[i])).rgb * w[i];\n"
"    }\n"
"    dst.write(float4(acc, 1.0), gid);\n"
"}\n"
"\n"
"// Pass 4: Full-res pre-UI linear HDR cinematic grade + metric depth AO + atmospheric clarity + micro-contrast\n"
"kernel void cin_scene_grade(texture2d<float, access::sample> src   [[texture(0)]],\n"
"                            texture2d<float, access::sample> bloom [[texture(1)]],\n"
"                            depth2d<float, access::read>     depth [[texture(2)]],\n"
"                            texture2d<float, access::write>  dst   [[texture(3)]],\n"
"                            sampler                          samp  [[sampler(0)]],\n"
"                            constant CinParams              &p     [[buffer(0)]],\n"
"                            uint2                            gid   [[thread_position_in_grid]]) {\n"
"    if (gid.x >= uint(p.size.x) || gid.y >= uint(p.size.y)) return;\n"
"    float2 uv = (float2(gid) + 0.5) * p.inv_size;\n"
"    float3 col = max(src.sample(samp, uv).rgb, float3(0.0));\n"
"    float3 blm = max(bloom.sample(samp, uv).rgb, float3(0.0));\n"
"\n"
"    // ---- 1. Metric Depth-Aware Contact AO + Geometry-Only Micro-Clarity ----\n"
"    float ao = 1.0;\n"
"    float geom_mask = 0.0;\n"
"    if (p.has_depth != 0u) {\n"
"        uint dw = depth.get_width();\n"
"        uint dh = depth.get_height();\n"
"        if (dw > 16u && dh > 16u) {\n"
"            float2 dscale = float2(float(dw), float(dh)) * p.inv_size;\n"
"            int2 dc = int2(clamp(float2(gid) * dscale, float2(0.0), float2(float(dw - 1u), float(dh - 1u))));\n"
"            float d0 = depth.read(uint2(dc));\n"
"            float z0 = linearize_depth(d0, p.near_plane, p.far_plane);\n"
"            geom_mask = 1.0 - smoothstep(0.990, 0.996, d0);\n"
"\n"
"            // Contact AO only on nearby geometry (< 45m) so distant sky/fog/LOD volumes are never affected\n"
"            if (d0 < 0.996 && z0 < 45.0) {\n"
"                // Interleaved gradient noise rotation per pixel (smooth, no stipple)\n"
"                float ang = fract(52.9829189 * fract(dot(float2(gid), float2(0.06711056, 0.00583715)))) * 6.2831853;\n"
"                float sa = sin(ang), ca = cos(ang);\n"
"                float rad_px = clamp(38.0 / max(z0, 1.8), 2.5, 15.0);\n"
"                float occ = 0.0;\n"
"                const float2 taps[8] = {\n"
"                    float2( 0.35,  0.00), float2(-0.25,  0.42), float2(-0.18, -0.58), float2( 0.62, -0.22),\n"
"                    float2( 0.55,  0.65), float2(-0.82,  0.18), float2(-0.45, -0.82), float2( 0.95,  0.28)\n"
"                };\n"
"                float max_dz = clamp(0.40 + z0 * 0.04, 0.40, 1.8);\n"
"                float min_dz = clamp(0.03 + z0 * 0.003, 0.03, 0.18);\n"
"                for (int i = 0; i < 8; i++) {\n"
"                    float2 r = float2(taps[i].x * ca - taps[i].y * sa, taps[i].x * sa + taps[i].y * ca) * rad_px;\n"
"                    int2 sc = clamp(dc + int2(round(r)), int2(0), int2(int(dw) - 1, int(dh) - 1));\n"
"                    float zs = linearize_depth(depth.read(uint2(sc)), p.near_plane, p.far_plane);\n"
"                    float dz = z0 - zs; // positive means neighbor is closer -> occludes center\n"
"                    if (dz > min_dz && dz < max_dz) {\n"
"                        float falloff = 1.0 - smoothstep(min_dz, max_dz, dz);\n"
"                        occ += falloff;\n"
"                    }\n"
"                }\n"
"                float dist_fade = 1.0 - smoothstep(18.0, 45.0, z0);\n"
"                float ao_strength = (p.mode > 2.5) ? 0.52 : ((p.mode > 1.5) ? 0.42 : 0.44);\n"
"                ao = 1.0 - clamp((occ / 8.0) * ao_strength * dist_fade, 0.0, 0.52);\n"
"            }\n"
"        }\n"
"    }\n"
"\n"
"    // Subtle luminance micro-clarity on real geometry only (never sharpens sky/volumetric-fog gradients)\n"
"    if (geom_mask > 0.01) {\n"
"        float2 px = p.inv_size;\n"
"        float l_c = luma(col);\n"
"        float l_n = 0.25 * (luma(src.sample(samp, uv + float2( px.x, 0.0)).rgb) +\n"
"                            luma(src.sample(samp, uv + float2(-px.x, 0.0)).rgb) +\n"
"                            luma(src.sample(samp, uv + float2( 0.0, px.y)).rgb) +\n"
"                            luma(src.sample(samp, uv + float2( 0.0,-px.y)).rgb));\n"
"        float detail = clamp((l_c - l_n) * 0.26, -0.05 * l_c - 0.006, 0.07 * l_c + 0.010) * geom_mask;\n"
"        col = max(col * (1.0 + detail / max(l_c, 0.05)), float3(0.0));\n"
"    }\n"
"\n"
"    // Apply contact AO with subtle cool ambient shadow tint (never muddy black)\n"
"    float3 ao_tint = mix(float3(0.72, 0.82, 0.96), float3(1.0), ao);\n"
"    col *= mix(float3(ao), ao_tint * ao, 0.42);\n"
"\n"
"    // ---- 2. Add True Linear-HDR Filmic Bloom & Warm Halation ----\n"
"    float bloom_str = (p.mode > 1.5 && p.mode < 2.5) ? 0.34 : ((p.mode > 2.5) ? 0.22 : 0.15);\n"
"    col += blm * bloom_str;\n"
"\n"
"    dst.write(float4(col, 1.0), gid);\n"
"}\n"
"\n"
"// Final Present Shader (post-tonemap filmic cinema grade: deep shadow toe, ACES-inspired filmic curve,\n"
"// warm-sun / cool-shadow split tone, painterly foliage & sky sculpting, micro-clarity, optical vignette, 35mm grain)\n"
"struct CinPresV { float4 p [[position]]; float2 uv; };\n"
"vertex CinPresV cin_present_vs(uint id [[vertex_id]]) {\n"
"    float2 uv = float2((id << 1) & 2, id & 2);\n"
"    CinPresV o;\n"
"    o.p = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
"    o.uv = uv;\n"
"    return o;\n"
"}\n"
"struct CinPresU { float4 rect; float4 edge; float4 cin; };\n"
"// cin.x = mode (1..3), cin.y = frame_seed, cin.z = aspect, cin.w = letterbox (0 or 1)\n"
"static inline float3 cin_srgb_to_lin(float3 c) { return pow(max(c, float3(0.0)), float3(2.2)); }\n"
"static inline float3 cin_lin_to_srgb(float3 c) { return pow(max(c, float3(0.0)), float3(1.0 / 2.2)); }\n"
"\n"
"static inline float3 cin_filmic_curve(float3 x, float toe, float contrast, float exposure) {\n"
"    x = max(x * exposure, float3(0.0));\n"
"    float l = dot(x, float3(0.2126, 0.7152, 0.0722));\n"
"    float l_toe = pow(max(l, 1e-5), toe);\n"
"    x *= clamp(l_toe / max(l, 1e-5), 0.45, 1.45);\n"
"    float3 a = x * (2.51 * x + 0.03);\n"
"    float3 b = x * (2.43 * x + 0.59) + 0.14;\n"
"    float3 mapped = clamp(a / max(b, float3(1e-5)), 0.0, 1.0);\n"
"    return mix(clamp(x, 0.0, 1.0), mapped, contrast);\n"
"}\n"
"\n"
"fragment float4 cin_present_fs(CinPresV in [[stage_in]],\n"
"                               texture2d<float> t [[texture(0)]],\n"
"                               sampler s [[sampler(0)]],\n"
"                               constant CinPresU &u [[buffer(0)]]) {\n"
"    // Optional 2.39:1 cinema letterbox bars (preset 3)\n"
"    if (u.cin.w > 0.5) {\n"
"        float target_ar = 2.39;\n"
"        float cur_ar = max(u.cin.z, 1.0);\n"
"        if (cur_ar < target_ar) {\n"
"            float bar = 0.5 * (1.0 - cur_ar / target_ar);\n"
"            if (in.uv.y < bar || in.uv.y > 1.0 - bar) return float4(0.0, 0.0, 0.0, 1.0);\n"
"        }\n"
"    }\n"
"    float2 suv = clamp(in.uv * u.rect.zw + u.rect.xy, u.edge.xy, u.edge.zw);\n"
"    float4 c = t.sample(s, suv);\n"
"    float mode = u.cin.x;\n"
"    if (mode > 0.5) {\n"
"        float3 srgb = clamp(c.rgb, 0.0, 1.0);\n"
"\n"
"        // 1. Edge-gated micro-clarity (crisp textures/characters, leaves smooth sky & fog gradients untouched)\n"
"        float2 px = float2(1.0 / max(float(t.get_width()), 1.0), 1.0 / max(float(t.get_height()), 1.0));\n"
"        float3 n0 = t.sample(s, clamp(suv + float2( px.x, 0.0), u.edge.xy, u.edge.zw)).rgb;\n"
"        float3 n1 = t.sample(s, clamp(suv + float2(-px.x, 0.0), u.edge.xy, u.edge.zw)).rgb;\n"
"        float3 n2 = t.sample(s, clamp(suv + float2( 0.0, px.y), u.edge.xy, u.edge.zw)).rgb;\n"
"        float3 n3 = t.sample(s, clamp(suv + float2( 0.0,-px.y), u.edge.xy, u.edge.zw)).rgb;\n"
"        float3 blur4 = 0.25 * (n0 + n1 + n2 + n3);\n"
"        float l_c = dot(srgb, float3(0.2126, 0.7152, 0.0722));\n"
"        float l_b = dot(blur4, float3(0.2126, 0.7152, 0.0722));\n"
"        float diff = l_c - l_b;\n"
"        float edge_gate = smoothstep(0.012, 0.045, abs(diff));\n"
"        float sharp = clamp(diff * 0.38 * edge_gate, -0.045, 0.045);\n"
"        srgb = clamp(srgb + sharp, 0.0, 1.0);\n"
"\n"
"        // Convert to linear for physically clean cinema color grading\n"
"        float3 lin = cin_srgb_to_lin(srgb);\n"
"\n"
"        // 2. Gentle shadow floor cleanup (low enough not to exaggerate faint volumetric fog boundaries)\n"
"        float black_cut = (mode > 2.5) ? 0.010 : ((mode > 1.5) ? 0.006 : 0.006);\n"
"        lin = max(lin - float3(black_cut), float3(0.0)) / (1.0 - black_cut);\n"
"\n"
"        // 3. Filmic ACES-inspired contrast & shadow toe per preset\n"
"        float toe      = (mode > 2.5) ? 1.22 : ((mode > 1.5) ? 1.15 : 1.19);\n"
"        float contrast = (mode > 2.5) ? 0.70 : ((mode > 1.5) ? 0.56 : 0.62);\n"
"        float exposure = (mode > 2.5) ? 1.14 : ((mode > 1.5) ? 1.22 : 0.98);\n"
"        lin = cin_filmic_curve(lin, toe, contrast, exposure);\n"
"\n"
"        // 4. Painterly Hyrule color sculpting: deeper, richer emerald grass & darker dramatic cinema sky\n"
"        float g_dom = clamp((lin.g - max(lin.r, lin.b)) / max(lin.g, 0.04), 0.0, 1.0);\n"
"        // Also catch warm sunlit yellow-green grass (where G and R are both high, B is low)\n"
"        float yg_grass = clamp((min(lin.g, lin.r * 1.18) - lin.b) / max(lin.g, 0.05), 0.0, 1.0) * smoothstep(0.0, 0.12, lin.g - lin.r * 0.78);\n"
"        float grass_m = max(g_dom, yg_grass * 0.85);\n"
"        float b_dom = clamp((lin.b - max(lin.r, lin.g)) / max(lin.b, 0.04), 0.0, 1.0);\n"
"        if (mode < 1.5) {\n"
"            // Mode 1: noticeably deeper, richer grass & darker dramatic sky (purely color-based, zero depth seams)\n"
"            lin.r *= 1.0 - 0.22 * grass_m;\n"
"            lin.g *= 1.0 - 0.16 * grass_m;\n"
"            lin.b *= 1.0 - 0.06 * grass_m;\n"
"            lin.r *= 1.0 - 0.24 * b_dom;\n"
"            lin.g *= 1.0 - 0.18 * b_dom;\n"
"            lin.b *= 1.0 - 0.10 * b_dom;\n"
"        } else {\n"
"            lin.r *= 1.0 - ((mode > 1.5 && mode < 2.5) ? 0.05 : 0.09) * g_dom;\n"
"            lin.g *= 1.0 - 0.04 * g_dom;\n"
"            lin.b *= 1.0 + 0.05 * g_dom;\n"
"            lin.b *= 1.0 + 0.08 * b_dom;\n"
"            lin.r *= 1.0 - 0.05 * b_dom;\n"
"        }\n"
"\n"
"        // 5. Cinema split-toning: warm sunlit highlights, cool slate-teal shadows\n"
"        float lum = dot(lin, float3(0.2126, 0.7152, 0.0722));\n"
"        float hi_w = smoothstep(0.22, 0.85, lum);\n"
"        float sh_w = 1.0 - smoothstep(0.01, 0.28, lum);\n"
"        float3 hi_tint = (mode > 1.5 && mode < 2.5) ? float3(1.13, 1.02, 0.83)\n"
"                       : ((mode > 2.5) ? float3(1.06, 1.00, 0.92) : float3(1.06, 1.00, 0.92));\n"
"        float3 sh_tint = (mode > 2.5) ? float3(0.84, 0.96, 1.14)\n"
"                       : ((mode > 1.5) ? float3(0.92, 0.96, 1.06) : float3(0.87, 0.96, 1.10));\n"
"        lin *= mix(float3(1.0), hi_tint, hi_w * 0.80);\n"
"        lin *= mix(float3(1.0), sh_tint, sh_w * 0.82);\n"
"\n"
"        // 6. Subtle filmic vibrance (boosts muted tones, protects bright saturated highlights)\n"
"        float cmax = max(lin.r, max(lin.g, lin.b));\n"
"        float cmin = min(lin.r, min(lin.g, lin.b));\n"
"        float sat = (cmax - cmin) / max(cmax, 1e-4);\n"
"        float vib = (mode > 1.5 && mode < 2.5) ? 0.24 : ((mode > 2.5) ? 0.06 : 0.16);\n"
"        float l_v = dot(lin, float3(0.2126, 0.7152, 0.0722));\n"
"        lin = max(mix(float3(l_v), lin, 1.0 + vib * (1.0 - sat * 0.65)), float3(0.0));\n"
"\n"
"        float3 rgb = cin_lin_to_srgb(clamp(lin, 0.0, 1.0));\n"
"\n"
"        // 7. Soft optical cinema-lens vignette\n"
"        float2 d = (in.uv - 0.5) * float2(1.15, 1.0);\n"
"        float r2 = dot(d, d);\n"
"        float vig_str = (mode > 2.5) ? 0.42 : ((mode > 1.5) ? 0.26 : 0.24);\n"
"        float vig = 1.0 - smoothstep(0.14, 0.88, r2) * vig_str;\n"
"        rgb *= vig;\n"
"\n"
"        // 8. Ultra-fine organic 35mm film grain (peaks in midtones, zero in pure black/white)\n"
"        float2 gpx = in.p.xy + float2(u.cin.y * 13.37, u.cin.y * 7.91);\n"
"        float n = fract(sin(dot(gpx, float2(12.9898, 78.233))) * 43758.5453) - 0.5;\n"
"        float lg = dot(rgb, float3(0.2126, 0.7152, 0.0722));\n"
"        float g_weight = 4.0 * lg * (1.0 - lg);\n"
"        float g_amt = (mode > 2.5) ? 0.024 : 0.015;\n"
"        rgb = clamp(rgb + n * g_amt * g_weight, 0.0, 1.0);\n"
"        c.rgb = rgb;\n"
"    }\n"
"    return c;\n"
"}\n";

static int ensure_lib(id<MTLDevice> dev) {
    if (g.lib) return 1;
    if (g.failed || !dev) return 0;
    NSError *e = nil;
    MTLCompileOptions *o = [MTLCompileOptions new];
    if (@available(macOS 15.0, *)) o.mathMode = MTLMathModeSafe;
    g.lib = [dev newLibraryWithSource:@(k_cinematic_msl) options:o error:&e];
    if (!g.lib) {
        CLOG("shader compile failed: %s", e ? e.localizedDescription.UTF8String : "?");
        g.failed = 1;
        return 0;
    }
    return 1;
}

static int setup_scene(id<MTLDevice> dev) {
    if (g.dev) return 1;
    if (g.failed || !ensure_lib(dev)) return 0;
    NSError *e = nil;
    MTLResidencySetDescriptor *rd = [MTLResidencySetDescriptor new];
    rd.label = @"TOTK Cinematic";
    g.rs = [dev newResidencySetWithDescriptor:rd error:&e];
    id<MTLFunction> f_ext = [g.lib newFunctionWithName:@"cin_bloom_extract"];
    id<MTLFunction> f_bh  = [g.lib newFunctionWithName:@"cin_bloom_blur_h"];
    id<MTLFunction> f_bv  = [g.lib newFunctionWithName:@"cin_bloom_blur_v"];
    id<MTLFunction> f_grd = [g.lib newFunctionWithName:@"cin_scene_grade"];
    g.ps_extract = f_ext ? [dev newComputePipelineStateWithFunction:f_ext error:&e] : nil;
    g.ps_blur_h  = f_bh  ? [dev newComputePipelineStateWithFunction:f_bh  error:&e] : nil;
    g.ps_blur_v  = f_bv  ? [dev newComputePipelineStateWithFunction:f_bv  error:&e] : nil;
    g.ps_grade   = f_grd ? [dev newComputePipelineStateWithFunction:f_grd error:&e] : nil;

    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.supportArgumentBuffers = YES;
    g.samp_lin = [dev newSamplerStateWithDescriptor:sd];

    MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new];
    ad.maxBufferBindCount = 1;
    ad.maxTextureBindCount = 4;
    ad.maxSamplerStateBindCount = 1;
    for (int i = 0; i < CIN_SLOTS && g.rs; i++) {
        g.args_ext[i] = [dev newArgumentTableWithDescriptor:ad error:&e];
        g.args_bh[i]  = [dev newArgumentTableWithDescriptor:ad error:&e];
        g.args_bv[i]  = [dev newArgumentTableWithDescriptor:ad error:&e];
        g.args_grd[i] = [dev newArgumentTableWithDescriptor:ad error:&e];
        g.par[i] = [dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
        if (g.par[i]) [g.rs addAllocation:g.par[i]];
    }
    MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:1 height:1 mipmapped:NO];
    dd.usage = MTLTextureUsageShaderRead;
    dd.storageMode = MTLStorageModePrivate;
    g.dummy_depth = [dev newTextureWithDescriptor:dd];
    if (g.dummy_depth && g.rs) [g.rs addAllocation:g.dummy_depth];

    if (!g.rs || !g.ps_extract || !g.ps_blur_h || !g.ps_blur_v || !g.ps_grade || !g.samp_lin || !g.dummy_depth || !g.par[CIN_SLOTS - 1]) {
        CLOG("scene setup failed: %s", e ? e.localizedDescription.UTF8String : "Metal objects");
        g.failed = 1;
        return 0;
    }
    [g.rs commit];
    g.dev = dev;
    CLOG("initialized pre-UI HDR + depth cinematic shader pipeline");
    return 1;
}

static int ensure_targets(NSUInteger w, NSUInteger h) {
    if (g.out_tex && g.w == w && g.h == h) return 1;
    NSUInteger bw = (w + 3) / 4; if (bw < 16) bw = 16;
    NSUInteger bh = (h + 3) / 4; if (bh < 16) bh = 16;
    keep_old(g.out_tex);
    keep_old(g.bloom_a);
    keep_old(g.bloom_b);
    MTLTextureUsage rw = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    g.out_tex = mk_tex(MTLPixelFormatRGBA16Float, w, h, rw);
    g.bloom_a = mk_tex(MTLPixelFormatRGBA16Float, bw, bh, rw);
    g.bloom_b = mk_tex(MTLPixelFormatRGBA16Float, bw, bh, rw);
    [g.rs commit];
    if (!g.out_tex || !g.bloom_a || !g.bloom_b) {
        CLOG("failed to allocate targets %lux%lu (bloom %lux%lu)",
             (unsigned long)w, (unsigned long)h, (unsigned long)bw, (unsigned long)bh);
        return 0;
    }
    g.w = w; g.h = h; g.bw = bw; g.bh = bh;
    CLOG("targets %lux%lu (RGBA16Float), bloom %lux%lu", (unsigned long)w, (unsigned long)h, (unsigned long)bw, (unsigned long)bh);
    return 1;
}

id<MTLTexture> cinematic_scene_encode(id<MTL4CommandBuffer> cb, id<MTLTexture> src, id<MTLTexture> depth) {
    int mode = cinematic_mode();
    if (!mode || !cb || !src || src.textureType != MTLTextureType2D) return nil;
    if (g.encoded_frame == g.frame + 1 && src == g.last_src && g.out_tex) return g.out_tex;
    if (!setup_scene(src.device) || !ensure_targets(src.width, src.height)) return nil;

    int s = g.slot = (g.slot + 1) % CIN_SLOTS;
    CinParams *par = (CinParams *)g.par[s].contents;
    par->size[0] = (float)g.w;
    par->size[1] = (float)g.h;
    par->inv_size[0] = 1.0f / (float)g.w;
    par->inv_size[1] = 1.0f / (float)g.h;
    par->bloom_size[0] = (float)g.bw;
    par->bloom_size[1] = (float)g.bh;
    par->bloom_inv[0] = 1.0f / (float)g.bw;
    par->bloom_inv[1] = 1.0f / (float)g.bh;
    par->near_plane = g.cam_ok ? g.near_p : 0.1f;
    par->far_plane  = g.cam_ok ? g.far_p  : 25000.0f;
    par->mode       = (float)mode;
    par->frame_f    = (float)(g.frame & 0xFFFFu);
    int use_depth = (depth && depth.pixelFormat == MTLPixelFormatDepth32Float && depth.textureType == MTLTextureType2D && depth.width >= 320);
    par->has_depth  = use_depth ? 1u : 0u;
    par->pad0 = par->pad1 = par->pad2 = 0u;

    id<MTLTexture> dtex = use_depth ? depth : g.dummy_depth;

    [g.args_ext[s] setTexture:src.gpuResourceID atIndex:0];
    [g.args_ext[s] setTexture:g.bloom_a.gpuResourceID atIndex:1];
    [g.args_ext[s] setSamplerState:g.samp_lin.gpuResourceID atIndex:0];
    [g.args_ext[s] setAddress:g.par[s].gpuAddress atIndex:0];

    [g.args_bh[s] setTexture:g.bloom_a.gpuResourceID atIndex:0];
    [g.args_bh[s] setTexture:g.bloom_b.gpuResourceID atIndex:1];
    [g.args_bh[s] setSamplerState:g.samp_lin.gpuResourceID atIndex:0];
    [g.args_bh[s] setAddress:g.par[s].gpuAddress atIndex:0];

    [g.args_bv[s] setTexture:g.bloom_b.gpuResourceID atIndex:0];
    [g.args_bv[s] setTexture:g.bloom_a.gpuResourceID atIndex:1];
    [g.args_bv[s] setSamplerState:g.samp_lin.gpuResourceID atIndex:0];
    [g.args_bv[s] setAddress:g.par[s].gpuAddress atIndex:0];

    [g.args_grd[s] setTexture:src.gpuResourceID atIndex:0];
    [g.args_grd[s] setTexture:g.bloom_a.gpuResourceID atIndex:1];
    [g.args_grd[s] setTexture:dtex.gpuResourceID atIndex:2];
    [g.args_grd[s] setTexture:g.out_tex.gpuResourceID atIndex:3];
    [g.args_grd[s] setSamplerState:g.samp_lin.gpuResourceID atIndex:0];
    [g.args_grd[s] setAddress:g.par[s].gpuAddress atIndex:0];

    id<MTL4ComputeCommandEncoder> e = [cb computeCommandEncoder];
    if (!e) return nil;
    [e barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];

    MTLSize tg = MTLSizeMake(16, 16, 1);
    [e setComputePipelineState:g.ps_extract];
    [e setArgumentTable:g.args_ext[s]];
    [e dispatchThreads:MTLSizeMake(g.bw, g.bh, 1) threadsPerThreadgroup:tg];

    [e barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
    [e setComputePipelineState:g.ps_blur_h];
    [e setArgumentTable:g.args_bh[s]];
    [e dispatchThreads:MTLSizeMake(g.bw, g.bh, 1) threadsPerThreadgroup:tg];

    [e barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
    [e setComputePipelineState:g.ps_blur_v];
    [e setArgumentTable:g.args_bv[s]];
    [e dispatchThreads:MTLSizeMake(g.bw, g.bh, 1) threadsPerThreadgroup:tg];

    [e barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
    [e setComputePipelineState:g.ps_grade];
    [e setArgumentTable:g.args_grd[s]];
    [e dispatchThreads:MTLSizeMake(g.w, g.h, 1) threadsPerThreadgroup:tg];

    [e barrierAfterStages:MTLStageDispatch beforeQueueStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    [e endEncoding];

    g.last_src = src;
    g.encoded_frame = g.frame + 1;
    return g.out_tex;
}

id<MTLRenderPipelineState> cinematic_present_pso(id<MTLDevice> dev, MTLPixelFormat format) {
    pthread_mutex_lock(&g.pres_mu);
    for (int i = 0; i < g.pres_n; i++) {
        if (g.pres_fmt[i] == format) {
            id<MTLRenderPipelineState> pso = g.pres_pso[i];
            pthread_mutex_unlock(&g.pres_mu);
            return pso;
        }
    }
    if (!ensure_lib(dev)) { pthread_mutex_unlock(&g.pres_mu); return nil; }
    NSError *e = nil;
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [g.lib newFunctionWithName:@"cin_present_vs"];
    pd.fragmentFunction = [g.lib newFunctionWithName:@"cin_present_fs"];
    pd.colorAttachments[0].pixelFormat = format;
    id<MTLRenderPipelineState> pso = [dev newRenderPipelineStateWithDescriptor:pd error:&e];
    if (!pso) {
        CLOG("present PSO failed (fmt=%lu): %s", (unsigned long)format, e ? e.localizedDescription.UTF8String : "?");
        pthread_mutex_unlock(&g.pres_mu);
        return nil;
    }
    if (g.pres_n < 4) {
        g.pres_fmt[g.pres_n] = format;
        g.pres_pso[g.pres_n] = pso;
        g.pres_n++;
    }
    pthread_mutex_unlock(&g.pres_mu);
    return pso;
}

void cinematic_present_params(float out4[4], uint64_t frame_index, float dst_w, float dst_h) {
    int mode = cinematic_mode();
    out4[0] = (float)mode;
    out4[1] = (float)(frame_index % 1024u);
    out4[2] = (dst_h > 0.0f) ? (dst_w / dst_h) : (16.0f / 9.0f);
    out4[3] = (mode == CIN_DRAMATIC) ? 1.0f : 0.0f;
}
