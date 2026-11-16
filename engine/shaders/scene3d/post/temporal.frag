#version 450
#include "../../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D t_color;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D t_depth;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D t_history;
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D t_history_depth;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    mat4 inverseCurrent;
    mat4 previous;
    vec4 texel;     // xy reciprocal size, zw jitter in UV units
    vec4 options;   // x maximum history weight, y relative depth threshold
} p;

bool finite4(vec4 value) { return !any(isnan(value)) && !any(isinf(value)); }

void main() {
    vec2 uv = v_uv + p.texel.zw;
    vec4 current = textureLod(t_color, uv, 0.0);
    o_color = current;
    float depth = textureLod(t_depth, uv, 0.0).r;
    if (p.options.x <= 0.0 || depth <= 1e-7 || !finite4(current)) return;
    vec4 world = p.inverseCurrent * vec4(uv * 2.0 - 1.0, depth, 1.0);
    if (!finite4(world) || abs(world.w) < 1e-8) return;
    vec4 previousClip = p.previous * vec4(world.xyz / world.w, 1.0);
    if (!finite4(previousClip) || previousClip.w <= 1e-7) return;
    vec3 previousNdc = previousClip.xyz / previousClip.w;
    // Motion vectors use current-to-previous normalized texture coordinates.
    vec2 motion = previousNdc.xy * .5 + .5 - v_uv;
    vec2 previousUv = v_uv + motion;
    vec2 border = p.texel.xy * .5;
    if (any(lessThan(previousUv, border)) || any(greaterThan(previousUv, vec2(1.0) - border))
        || previousNdc.z <= 0.0 || previousNdc.z > 1.0) return;
    float oldDepth = textureLod(t_history_depth, previousUv, 0.0).r;
    float depthThreshold = max(1e-7, p.options.y * max(previousNdc.z, oldDepth));
    if (isnan(oldDepth) || isinf(oldDepth) || oldDepth <= 0.0
        || abs(oldDepth - previousNdc.z) > depthThreshold) return;
    vec4 history = textureLod(t_history, previousUv, 0.0);
    if (!finite4(history)) return;
    vec3 lower = current.rgb, upper = current.rgb;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec3 sampleColor = textureLod(t_color, uv + vec2(x, y) * p.texel.xy, 0.0).rgb;
            lower = min(lower, sampleColor);
            upper = max(upper, sampleColor);
        }
    }
    history.rgb = clamp(history.rgb, lower, upper);
    float delta = abs(dot(current.rgb - history.rgb, vec3(.2126, .7152, .0722)));
    float luminance = max(.05, dot(abs(current.rgb), vec3(.2126, .7152, .0722)));
    float reactive = smoothstep(.05, .35, delta / luminance);
    reactive = max(reactive, smoothstep(.02, .25, abs(current.a - history.a)));
    float weight = p.options.x * (1.0 - reactive);
    // Current coverage remains authoritative for transparent compositing.
    o_color = vec4(mix(current.rgb, history.rgb, weight), current.a);
}
