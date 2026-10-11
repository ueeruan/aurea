#version 450
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D original;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D effected;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 effectMap;
    vec4 size;
} p;
void main() {
    vec4 pixel = v_uv.x < 0.5 ? texture(original, v_uv)
        : texture(effected, v_uv * p.effectMap.xy + p.effectMap.zw);
    vec2 cell = floor(v_uv * p.size.xy / 10.0);
    vec3 backdrop = vec3(mix(0.018, 0.038, mod(cell.x + cell.y, 2.0)));
    pixel = vec4(pixel.rgb + backdrop * (1.0 - pixel.a), 1.0);
    if (abs(v_uv.x - 0.5) < 0.65 / max(p.size.x, 1.0)) pixel = vec4(0.8, 0.8, 0.8, 1.0);
    o_color = pixel;
}
