#version 450
#extension GL_GOOGLE_include_directive : require
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel; // input layer region
    vec4 p0;    // fold origin, normal
    vec4 p1;    // radius, back opacity, projected light X, lighting amount
    vec4 p2;    // light Z, mix
    vec4 p3;    // output pixel footprint in layer coordinates
    vec4 color; // paper tint; alpha controls the tint instead of filling holes
} p;

const float PI = 3.14159265358979323846;

vec4 source_at(vec2 point) {
    return textureLod(u_tex0, (point - p.texel.xy) / p.texel.zw, 0.0);
}

vec4 over(vec4 foreground, vec4 background) {
    return foreground + background * (1.0 - foreground.a);
}

vec4 paper_at(float along, float across, float angle) {
    vec2 n = p.p0.zw;
    vec2 point = p.p0.xy + n * along + vec2(-n.y, n.x) * across;
    vec4 ink = source_at(point);
    bool back = angle > PI * 0.5;
    float facing = back ? -1.0 : 1.0;
    float diffuse = max(0.0, facing * (sin(angle) * p.p1.z + cos(angle) * p.p2.x));
    // Normalize to the undeformed face: no dark seam where angle reaches zero.
    float lighting = mix(1.0, (0.25 + 0.75 * diffuse) / (0.25 + 0.75 * p.p2.x), p.p1.w);
    if (back) {
        // Never turn a transparent image or a text glyph's hole into opaque paper.
        ink.rgb = mix(ink.rgb, p.color.rgb * ink.a, p.color.a);
        ink *= p.p1.y;
    }
    ink.rgb *= lighting;
    return ink;
}

vec4 folded_at(vec2 point, vec4 flatSource) {
    vec2 n = p.p0.zw;
    vec2 relative = point - p.p0.xy;
    float d = dot(relative, n);
    float across = dot(relative, vec2(-n.y, n.x));
    float radius = p.p1.x;
    vec4 result = vec4(0.0);
    if (d <= 0.0) {
        // The flat original lies under the returned sheet.
        result = flatSource;
        result = over(paper_at(PI * radius - d, across, PI), result);
    } else if (d < radius) {
        float angle = asin(clamp(d / radius, 0.0, 1.0));
        result = paper_at(radius * angle, across, angle);
        result = over(paper_at(radius * (PI - angle), across, PI - angle), result);
    }
    return result;
}

void main() {
    vec2 point = p.texel.xy + (v_uv * p.uvMap.xy + p.uvMap.zw) * p.texel.zw;
    vec4 original = source_at(point);
    // Fixed four-sample coverage smooths the cylinder silhouette, fold and
    // transparent source edges. The work does not grow with curl radius.
    vec2 quarter = p.p3.xy * 0.25;
    vec4 folded = (folded_at(point + vec2(-quarter.x, -quarter.y), original)
                 + folded_at(point + vec2( quarter.x, -quarter.y), original)
                 + folded_at(point + vec2(-quarter.x,  quarter.y), original)
                 + folded_at(point + vec2( quarter.x,  quarter.y), original)) * 0.25;
    o_color = mix(original, folded, p.p2.y);
}
