#version 450
layout(location = 0) in vec4 a_clip;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_light;
layout(location = 3) in uvec4 a_tex;
layout(location = 4) in vec4 a_fog;
layout(location = 5) in vec4 a_spec;
layout(location = 6) in vec4 a_base;
layout(location = 7) in vec4 a_col1;
layout(location = 0) out vec4 v_light;
layout(location = 1) out vec2 v_uv;
layout(location = 2) flat out uvec4 v_tex;
layout(location = 3) out vec4 v_fog;
layout(location = 4) out vec3 v_spec;
layout(location = 5) out vec3 v_base;
layout(location = 6) out vec3 v_col1;
void main() {
    gl_Position = a_clip;
    v_light = a_light;
    v_uv = a_uv;
    v_tex = a_tex;
    v_fog = a_fog;
    v_spec = a_spec.rgb;
    v_base = a_base.rgb;
    v_col1 = a_col1.rgb;
}
