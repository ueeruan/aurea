#version 450
// Native bounded error-diffusion reconstruction, inspired by David Van Brink's
// Omino Diffusion and the documented directional sampling in Omino Diffusion+.
// No frame history: preview, reverse seek and export evaluate the same frame.
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0; // mix, error weight, angle radians, reach / layer width
    vec4 p1; // sample count, stripe width in layer pixels, palette, falloff
    vec4 colorA;
    vec4 colorB;
    vec4 colorC;
    vec4 colorD;
} p;

vec3 quantize(vec3 color) {
    int palette = int(p.p1.z + .5);
    if (palette == 0) return vec3(dot(color, vec3(.2126,.7152,.0722)) >= .5 ? 1.0 : 0.0);
    if (palette < 4) {
        float intervals = float(palette);
        return floor(clamp(color, 0.0, 1.0)*intervals+.5)/intervals;
    }
    vec3 colors[4] = vec3[4](aurea_linear_to_srgb(clamp(p.colorA.rgb,0.0,1.0)),aurea_linear_to_srgb(clamp(p.colorB.rgb,0.0,1.0)),aurea_linear_to_srgb(clamp(p.colorC.rgb,0.0,1.0)),aurea_linear_to_srgb(clamp(p.colorD.rgb,0.0,1.0)));
    vec3 best = colors[0]; float distance = dot(color-best,color-best);
    for (int i=1;i<4;++i) {
        vec3 delta = color-colors[i]; float candidate = dot(delta,delta);
        if (candidate < distance) { best=colors[i]; distance=candidate; }
    }
    return best;
}

void main() {
    vec2 uv = v_uv*p.uvMap.xy+p.uvMap.zw;
    vec4 original = texture(u_tex0,uv);
    if (original.a <= 0.00001 || p.p0.x <= 0.0) { o_color=original; return; }
    vec2 density = max(p.texel.zw,vec2(.00001));
    vec2 pixelSize = p.texel.xy*density;
    vec2 point = uv/pixelSize;
    vec2 axis = vec2(cos(p.p0.z),sin(p.p0.z));
    vec2 normal = vec2(-axis.y,axis.x);
    float width = max(p.p1.y,1.0);
    float perpendicular = dot(point,normal);
    point += normal*((floor(perpendicular/width)+.5)*width-perpendicular);
    int steps = clamp(int(p.p1.x+.5),1,64);
    float reach = max(p.p0.w,0.0)/pixelSize.x;
    vec3 error = vec3(0), mapped = quantize(aurea_linear_to_srgb(max(original.rgb/original.a,vec3(0))));
    for (int i=0;i<64;++i) {
        if (i>=steps) break;
        float remaining = steps>1 ? float(steps-1-i)/float(steps-1) : 0.0;
        vec2 sampleUV = (point-axis*(remaining*reach))*pixelSize;
        if (any(lessThan(sampleUV,vec2(0))) || any(greaterThan(sampleUV,vec2(1)))) { error=vec3(0); continue; }
        vec4 sampleColor = unpremultiply(texture(u_tex0,sampleUV));
        if (sampleColor.a <= .00001) { error=vec3(0); continue; }
        vec3 corrected = aurea_linear_to_srgb(max(sampleColor.rgb,vec3(0))) + error;
        mapped = quantize(corrected);
        float weight = mix(p.p0.y,1.0,clamp(p.p1.w*remaining,0.0,1.0));
        // Exaggerated feedback is intentional; bound it to prevent infinities.
        error = clamp((corrected-mapped)*weight*sampleColor.a,vec3(-8),vec3(8));
    }
    vec4 processed = vec4(aurea_srgb_to_linear(clamp(mapped,0.0,1.0))*original.a,original.a);
    o_color = mix(original,processed,clamp(p.p0.x,0.0,1.0));
}
