#version 450
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
vec4 sampleAt(vec2 uv) {
    // Explicit border treatment is shared with GLES, where border samplers
    // are not guaranteed. Allow half a texel of bilinear edge coverage.
    if (p.p0.w > .5) return texture(u_tex0, clamp(uv, p.texel.xy*.5, 1.0-p.texel.xy*.5));
    vec2 coverage = clamp(min(uv,1.0-uv)/p.texel.xy + .5, 0.0, 1.0);
    return texture(u_tex0, clamp(uv,p.texel.xy*.5,1.0-p.texel.xy*.5))*coverage.x*coverage.y;
}
void main() {
    vec2 uv = v_uv*p.uvMap.xy+p.uvMap.zw;
    float radius = clamp(p.p0.z,0.0,64.0);
    vec4 sum = sampleAt(uv);
    // Symmetric finite kernel; bilinear pairs halve texture reads. Fractional
    // radius weights the last tap instead of jumping between integer kernels.
    for (int i=1; i<=65; i+=2) {
        float a = clamp(radius-float(i)+1.0,0.0,1.0);
        float b = clamp(radius-float(i),0.0,1.0);
        float weight = a+b;
        if (weight <= 0.0) break;
        vec2 offset = p.p0.xy*(float(i)+b/weight);
        sum += (sampleAt(uv+offset)+sampleAt(uv-offset))*weight;
    }
    o_color = sum/(1.0+2.0*radius);
}
