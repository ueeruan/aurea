#version 450
#include "../common/bindings.glsl"

layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D source;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap, texel, red, green, blue, encoding, color;
} p;

float decode(float x, int curve) {
    if (curve == 0) return x;
    if (curve == 4) {
        if (x <= 0.155251141552511) return (x - 0.0729055341958355) / 10.5402377416545;
        return exp2(clamp(x * 17.52 - 9.72, -32.0, 15.999295));
    }
    float a = abs(x);
    if (curve == 1) return sign(x) * (a <= 0.04045 ? a / 12.92 : pow((a + 0.055) / 1.055, 2.4));
    float alpha = curve == 2 ? 1.099 : 1.09929682680944;
    float beta = curve == 2 ? 0.018 : 0.018053968510807;
    return sign(x) * (a <= 4.5 * beta ? a / 4.5 : pow((a + alpha - 1.0) / alpha, 1.0 / 0.45));
}

float encode(float x, int curve) {
    if (curve == 0) return x;
    if (curve == 4) {
        if (x <= 0.0078125) return 10.5402377416545 * x + 0.0729055341958355;
        return (log2(x) + 9.72) / 17.52;
    }
    float a = abs(x);
    if (curve == 1) return sign(x) * (a <= 0.0031308 ? 12.92 * a : 1.055 * pow(a, 1.0 / 2.4) - 0.055);
    float alpha = curve == 2 ? 1.099 : 1.09929682680944;
    float beta = curve == 2 ? 0.018 : 0.018053968510807;
    return sign(x) * (a <= beta ? a * 4.5 : alpha * pow(a, 0.45) - (alpha - 1.0));
}

void main() {
    vec4 sampleColor = texture(source, v_uv);
    if (sampleColor.a <= 0.0) {
        o_color = vec4(0.0);
        return;
    }
    vec3 straight = sampleColor.rgb / sampleColor.a;
    int fromCurve = int(p.encoding.x), toCurve = int(p.encoding.y);
    vec3 linear = vec3(decode(straight.r, fromCurve), decode(straight.g, fromCurve), decode(straight.b, fromCurve));
    vec3 moved = vec3(dot(p.red.xyz, linear), dot(p.green.xyz, linear), dot(p.blue.xyz, linear));
    vec3 result = vec3(encode(moved.r, toCurve), encode(moved.g, toCurve), encode(moved.b, toCurve));
    vec3 rgb = mix(straight, result, p.encoding.z) * sampleColor.a;
    o_color = vec4(clamp(rgb, vec3(-65504.0), vec3(65504.0)), sampleColor.a);
}
