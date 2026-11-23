#version 450

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_rect;
layout(location = 2) in vec4 in_color;
layout(location = 3) in float in_shade;
layout(location = 4) flat in uint in_flags;

layout(location = 0) out vec4 out_frag;

layout(set = 0, binding = 0, std140) uniform WorldUniforms {
    vec4  vp_rows[4];
    vec4  tint_mul;
    float canvas_w;
    float canvas_h;
    float time_sec;
    uint  ignore_gbuf;
    uint  target_is_linear;
    uint  shaders_enabled;
    uint  pad1;
    uint  pad2;
} u;

layout(set = 0, binding = 1, std430) readonly buffer EnvLuma {
    vec4 env_luma;
};

layout(set = 0, binding = 2) uniform sampler2D atlas_tex;
layout(set = 0, binding = 3) uniform sampler2D glint_tex;
layout(set = 0, binding = 4) uniform sampler2D gbuf_depth;

vec3 srgb_to_linear(vec3 c) {
    vec3 lo = c / 12.92;
    vec3 hi = pow(max((c + 0.055) / 1.055, vec3(0.0)), vec3(2.4));
    return mix(lo, hi, greaterThan(c, vec3(0.04045)));
}

void main() {
    if (u.ignore_gbuf == 0u && (in_flags & 8u) == 0u) {
        ivec2 gsz = textureSize(gbuf_depth, 0);
        if (gsz.x > 1 && gsz.y > 1) {
            vec2 scale = vec2(float(gsz.x) / max(u.canvas_w, 1.0),
                              float(gsz.y) / max(u.canvas_h, 1.0));
            ivec2 dc = ivec2(clamp(gl_FragCoord.xy * scale, vec2(0.0),
                                   vec2(float(gsz.x - 1), float(gsz.y - 1))));
            float hz = texelFetch(gbuf_depth, dc, 0).r;
            if (gl_FragCoord.z > hz + 0.000002) {
                discard;
            }
        }
    }
    vec2 luv = clamp(in_uv, vec2(0.0005), vec2(0.9995));
    vec2 auv = in_rect.xy + luv * in_rect.zw;
    vec4 tex = texture(atlas_tex, auv);
    if ((in_flags & 2u) != 0u && tex.a < 0.5) {
        discard;
    }
    if (tex.a < 0.01) {
        discard;
    }
    vec4 env = (u.shaders_enabled != 0u) ? env_luma : vec4(1.0);
    vec3 rgb = tex.rgb * in_color.rgb * in_shade;
    if (u.shaders_enabled != 0u) {
        if ((in_flags & 16u) != 0u) {
            float exp_em = mix(env.w, 1.0, 0.70);
            rgb *= mix(env.rgb, vec3(1.0), 0.70) * exp_em;
        } else {
            rgb *= env.rgb * env.w;
        }
    }
    if (u.shaders_enabled != 0u && (in_flags & 1u) != 0u) {
        vec2 guv1 = in_uv * 1.45 + vec2(u.time_sec * 0.14, u.time_sec * 0.28);
        vec2 guv2 = vec2(-in_uv.x, in_uv.y) * 1.15 + vec2(-u.time_sec * 0.11, u.time_sec * 0.21);
        vec3 g1 = texture(glint_tex, guv1).rgb;
        vec3 g2 = texture(glint_tex, guv2).rgb;
        float lum1 = dot(g1, vec3(0.45, 0.15, 0.40));
        float lum2 = dot(g2, vec3(0.45, 0.15, 0.40));
        float band = smoothstep(0.30, 0.88, max(lum1, lum2));
        vec3 glint_col = vec3(0.56, 0.20, 0.94);
        rgb = clamp(rgb * (1.0 - band * 0.14) + glint_col * band * (0.34 + 0.16 * env.w), 0.0, 1.0);
    }
    if ((in_flags & 4u) != 0u) {
        rgb = mix(rgb, vec3(1.0), 0.72);
    }
    if (u.target_is_linear != 0u) {
        rgb = srgb_to_linear(clamp(rgb, 0.0, 1.0));
    }
    out_frag = vec4(rgb, tex.a * in_color.a);
}
