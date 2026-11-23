#version 450

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_rect;
layout(location = 3) in uint in_rgba;
layout(location = 4) in uint in_mode;
layout(location = 5) in uint in_flags;

layout(set = 0, binding = 0, std140) uniform GuiUniforms {
    float gui_w;
    float gui_h;
    float time_sec;
    float atlas_w;
    float atlas_h;
    float hdr_ui_nits;
    float pad1;
    float pad2;
} u;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_rect;
layout(location = 2) out vec4 out_color;
layout(location = 3) flat out uint out_mode;
layout(location = 4) flat out uint out_flags;

vec4 unpack_rgba8(uint c) {
    return vec4(float(c & 0xffu), float((c >> 8) & 0xffu),
                float((c >> 16) & 0xffu), float((c >> 24) & 0xffu)) / 255.0;
}

void main() {
    float x = (in_pos.x / max(u.gui_w, 1.0)) * 2.0 - 1.0;
    float y = (in_pos.y / max(u.gui_h, 1.0)) * 2.0 - 1.0;
    gl_Position = vec4(x, y, 0.0, 1.0);
    out_uv      = in_uv;
    out_rect    = in_rect;
    out_color   = unpack_rgba8(in_rgba);
    out_mode    = in_mode;
    out_flags   = in_flags;
}
