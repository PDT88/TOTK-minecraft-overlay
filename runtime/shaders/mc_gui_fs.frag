#version 450

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_rect;
layout(location = 2) in vec4 in_color;
layout(location = 3) flat in uint in_mode;
layout(location = 4) flat in uint in_flags;

layout(location = 0) out vec4 out_frag;

layout(set = 0, binding = 0, std140) uniform GuiUniforms {
    float gui_w;
    float gui_h;
    float time_sec;
    float atlas_w;
    float atlas_h;
    float hdr_ui_nits;
    uint  shaders_enabled;
    uint  pad2;
} u;

layout(set = 0, binding = 1) uniform sampler2D atlas_tex;
layout(set = 0, binding = 2) uniform sampler2D glint_tex;
layout(set = 0, binding = 3) uniform sampler2D paper_tex;

vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow(max((c + 0.055) / 1.055, vec3(0.0)), vec3(2.4)), greaterThan(c, vec3(0.04045)));
}

vec3 pq_encode(vec3 nits) {
    const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * y) / (1.0 + c3 * y), vec3(m2));
}

vec4 hdr_adjust(vec4 c) {
    if (u.hdr_ui_nits <= 0.0) return c;
    const mat3 to2020 = mat3(0.6274, 0.0691, 0.0164, 0.3293, 0.9195, 0.0880, 0.0433, 0.0114, 0.8956);
    vec3 lin = srgb_to_linear(clamp(c.rgb, 0.0, 1.0));
    return vec4(pq_encode(max(to2020 * lin, vec3(0.0)) * u.hdr_ui_nits), c.a);
}

void main() {
    if (in_mode == 1u) {
        out_frag = hdr_adjust(in_color);
        return;
    }
    if (in_mode == 2u) {
        vec4 p = texture(paper_tex, clamp(in_uv, vec2(0.0), vec2(1.0)));
        if (p.a < 0.01) discard;
        out_frag = hdr_adjust(vec4(p.rgb * in_color.rgb, p.a * in_color.a));
        return;
    }
    vec2 eps = vec2(0.1 / max(u.atlas_w, 1.0), 0.1 / max(u.atlas_h, 1.0));
    vec2 rmin = in_rect.xy + eps;
    vec2 rmax = in_rect.xy + in_rect.zw - eps;
    vec2 auv = clamp(in_rect.xy + in_uv * in_rect.zw, rmin, max(rmin, rmax));
    vec4 tex = texture(atlas_tex, auv);
    vec4 res = tex * in_color;
    if (u.shaders_enabled != 0u && (in_flags & 1u) != 0u && tex.a > 0.05) {
        vec2 guv1 = in_uv * 1.55 + vec2(u.time_sec * 0.18, u.time_sec * 0.30);
        vec2 guv2 = vec2(-in_uv.x, in_uv.y) * 1.20 + vec2(-u.time_sec * 0.13, u.time_sec * 0.22);
        vec3 g1 = texture(glint_tex, guv1).rgb;
        vec3 g2 = texture(glint_tex, guv2).rgb;
        float lum1 = dot(g1, vec3(0.45, 0.15, 0.40));
        float lum2 = dot(g2, vec3(0.45, 0.15, 0.40));
        float band = smoothstep(0.32, 0.88, max(lum1, lum2));
        vec3 glint_col = vec3(0.58, 0.22, 0.95);
        res.rgb = clamp(res.rgb * (1.0 - band * 0.12) + glint_col * band * 0.34 * res.a, 0.0, 1.0);
    }
    if ((in_flags & 4u) != 0u) {
        /* Premultiply crosshair so transparent corners leave destination untouched during 1-dst blend. */
        out_frag = vec4(res.rgb * res.a, res.a);
        return;
    }
    out_frag = hdr_adjust(res);
}
