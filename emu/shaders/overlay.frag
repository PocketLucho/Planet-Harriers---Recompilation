#version 450
/* Eurasia 2D layers over the 3D image: 640x960 R16_UINT (layer 1 rows 0-479, layer 2 rows 480-959),
 * ABGR1555 with bit 15 = opaque; layer 2 is on top unless ena bit 2 (Eurasia 0x1bc bit 2) puts layer 1 there. */
layout(set = 0, binding = 1) uniform usampler2D layers;
layout(push_constant) uniform PC { uint ena; float scale; float xoff; uint clip; } pc;
layout(location = 0) out vec4 o_color;
vec3 c1555(uint p) { return vec3(float(p & 31u), float((p >> 5) & 31u), float((p >> 10) & 31u)) / 31.0; }
void main() {
    vec2 fc = vec2(gl_FragCoord.x - pc.xoff, gl_FragCoord.y);     /* 2D layers stay 4:3, centred (widescreen) */
    /* widescreen: outside the 4:3 area the edge column is extended (letterbox bands, full-screen backgrounds).
       With "clean edges" (clip bit 0) only the edges marked smooth in clip bits 1-4 (layer * 2 + side) are
       extended; the others stop at the 4:3 border (detailed pictures, sliding texts) */
    bool outside = (pc.clip & 1u) != 0u && (fc.x < 0.0 || fc.x >= 640.0 * pc.scale);
    uint side = fc.x < 0.0 ? 0u : 1u;
    ivec2 q = clamp(ivec2(floor(fc / pc.scale)), ivec2(0), ivec2(639, 479));
    bool l1top = (pc.ena & 4u) != 0u;
    for (int k = 0; k < 2; k++) {
        int l = l1top ? k : 1 - k;
        if ((pc.ena & (1u << l)) == 0u) continue;
        if (outside && (pc.clip & (2u << (uint(l) * 2u + side))) == 0u) continue;   /* clipped: nothing beyond 4:3 */
        uint p = texelFetch(layers, q + ivec2(0, 480 * l), 0).r;
        if ((p & 0x8000u) != 0u) { o_color = vec4(c1555(p), 1.0); return; }
    }
    discard;
}
