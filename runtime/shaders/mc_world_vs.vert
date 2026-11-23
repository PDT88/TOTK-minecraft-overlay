#version 450

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_rect;
layout(location = 3) in float in_shade;
layout(location = 4) in uint in_rgba;
layout(location = 5) in uint in_flags;

layout(set = 0, binding = 0, std140) uniform WorldUniforms {
    vec4  vp_rows[4];
    vec4  tint_mul;
    float canvas_w;
    float canvas_h;
    float time_sec;
    uint  ignore_gbuf;
    uint  target_is_linear;
    uint  pad0;
    uint  pad1;
    uint  pad2;
} u;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_rect;
layout(location = 2) out vec4 out_color;
layout(location = 3) out float out_shade;
layout(location = 4) flat out uint out_flags;

vec4 unpack_rgba8(uint c) {
    return vec4(float(c & 0xffu), float((c >> 8) & 0xffu),
                float((c >> 16) & 0xffu), float((c >> 24) & 0xffu)) / 255.0;
}

void main() {
    vec4 wp = vec4(in_pos, 1.0);
    vec4 cg = vec4(dot(u.vp_rows[0], wp),
                   dot(u.vp_rows[1], wp),
                   dot(u.vp_rows[2], wp),
                   dot(u.vp_rows[3], wp));
    /* OpenGL [-w,+w] clip Z -> Vulkan [0,+w] standard-Z depth remap, with Vulkan Y-flip for top-left viewport. */
    gl_Position = vec4(cg.x, -cg.y, 0.5 * cg.z + 0.5 * cg.w, cg.w);
    out_uv      = in_uv;
    out_rect    = in_rect;
    out_color   = unpack_rgba8(in_rgba) * u.tint_mul;
    out_shade   = in_shade;
    out_flags   = in_flags;
}
