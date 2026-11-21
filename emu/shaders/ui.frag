#version 450
/* menus (ui.c): a premultiplied RGBA canvas stretched over the whole picture */
layout(set = 0, binding = 3) uniform sampler2D ui;
layout(push_constant) uniform PC { vec2 tsize; vec2 uvmax; } pc;
layout(location = 0) out vec4 o_color;
void main() { o_color = texture(ui, gl_FragCoord.xy / pc.tsize * pc.uvmax); }
