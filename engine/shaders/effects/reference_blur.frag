#version 450
#include "../common/bindings.glsl"
#include "reference_blur.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D source;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D zeroMoment;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D firstMoment;
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D secondMoment;

vec4 bilinearSource(vec2 uv) {
    ivec2 size = textureSize(source, 0);
    vec2 position = clamp(uv * vec2(size) - .5, vec2(0), vec2(size - 1));
    ivec2 first = ivec2(floor(position));
    ivec2 next = min(first + 1, size - 1);
    vec2 fraction = fract(position);
    vec4 upper = mix(texelFetch(source, first, 0), texelFetch(source, ivec2(next.x, first.y), 0), fraction.x);
    vec4 lower = mix(texelFetch(source, ivec2(first.x, next.y), 0), texelFetch(source, next, 0), fraction.x);
    return mix(upper, lower, fraction.y);
}

vec4 nodeValue(int node, int span, int line, int position, BlurSegment segment) {
    int power = int(p.control.x);
    int start = node * span - power;
    vec4 m0, m1, m2;
    if (span == 1) {
        m0 = blurFetch(source, start, line, false);
        m1 = m2 = vec4(0);
    } else {
        ivec2 at = blurPoint(node - 1, line);
        m0 = texelFetch(zeroMoment, at, 0);
        m1 = texelFetch(firstMoment, at, 0);
        m2 = texelFetch(secondMoment, at, 0);
    }
    float origin = segment.range.w * (float(start - position) - segment.range.z) / segment.polynomial.w;
    float scale = segment.range.w * float(span) / segment.polynomial.w;
    vec3 q = segment.polynomial.xyz;
    return ((q.z * origin + q.y) * origin + q.x) * m0 + (q.y + 2.0 * q.z * origin) * scale * m1 +
           q.z * scale * scale * m2;
}
vec4 intervalValue(int low, int high, int line, int position, BlurSegment segment) {
    int left = low + int(p.control.x), right = high + 1 + int(p.control.x), span = 1;
    vec4 result = vec4(0);
    while (left < right) {
        if ((left & 1) != 0)
            result += nodeValue(left++, span, line, position, segment);
        if ((right & 1) != 0)
            result += nodeValue(--right, span, line, position, segment);
        left /= 2;
        right /= 2;
        span *= 2;
    }
    return result;
}
float intervalWeight(int low, int high, BlurSegment segment) {
    if (high < low)
        return 0.0;
    float count = float(high - low + 1);
    float first = segment.range.w * (float(low) - segment.range.z) / segment.polynomial.w;
    float last = segment.range.w * (float(high) - segment.range.z) / segment.polynomial.w;
    float mean = .5 * (first + last);
    float variance = (count * count - 1.0) / (12.0 * segment.polynomial.w * segment.polynomial.w);
    vec3 q = segment.polynomial.xyz;
    return count * (q.x + q.y * mean + q.z * (mean * mean + variance));
}
vec4 applyMoments(int position, int line) {
    vec4 result = vec4(0);
    int length = blurAxisLength();
    for (int index = 0; index < 24; ++index) {
        if (index >= int(p.control.y))
            break;
        BlurSegment segment = p.segments[index];
        int low = int(segment.range.x), high = int(segment.range.y);
        int first = max(position + low, 0), last = min(position + high, length - 1);
        if (first <= last)
            result += intervalValue(first, last, line, position, segment);
        if (p.settings.z > .5) {
            result +=
                intervalWeight(low, min(high, -position - 1), segment) * blurFetch(source, 0, line, true);
            result += intervalWeight(max(low, length - position), high, segment) *
                      blurFetch(source, length - 1, line, true);
        }
    }
    return result;
}
vec4 rasterMoment(ivec2 pixel) {
    int power = int(p.control.x), level = int(p.control.z), moment = int(p.control.w);
    int node = (p.settings.x < .5 ? pixel.x : pixel.y) + 1;
    int line = p.settings.x < .5 ? pixel.y : pixel.x;
    if (node < level || node >= 2 * level) {
        if (moment == 0)
            return texelFetch(zeroMoment, pixel, 0);
        if (moment == 1)
            return texelFetch(firstMoment, pixel, 0);
        return texelFetch(secondMoment, pixel, 0);
    }
    int left = node * 2, right = left + 1;
    vec4 l0, l1, l2, r0, r1, r2;
    if (left >= power) {
        l0 = blurFetch(source, left - power, line, false);
        r0 = blurFetch(source, right - power, line, false);
        l1 = r1 = l2 = r2 = vec4(0);
    } else {
        ivec2 a = blurPoint(left - 1, line), b = blurPoint(right - 1, line);
        l0 = texelFetch(zeroMoment, a, 0);
        r0 = texelFetch(zeroMoment, b, 0);
        l1 = texelFetch(firstMoment, a, 0);
        r1 = texelFetch(firstMoment, b, 0);
        l2 = texelFetch(secondMoment, a, 0);
        r2 = texelFetch(secondMoment, b, 0);
    }
    if (moment == 0)
        return l0 + r0;
    if (moment == 1)
        return .5 * (l1 + r0 + r1);
    return .25 * (l2 + r0 + 2.0 * r1 + r2);
}
void main() {
    int stage = int(p.settings.w + .5);
    if (stage == BLUR_COPY) {
        vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
        // RGBA32F linear filtering is optional on mobile. Explicit four-point
        // interpolation also covers that hardware without changing precision.
        vec4 color = bilinearSource(uv);
        vec2 coverage = clamp(min(uv, 1.0 - uv) * p.edge.xy + .5, 0.0, 1.0);
        o_color = p.settings.z > .5 ? color : color * coverage.x * coverage.y;
        return;
    }
    ivec2 pixel = ivec2(v_uv * p.sizes.zw);
    if (stage == BLUR_RASTER_MOMENTS) {
        o_color = rasterMoment(pixel);
        return;
    }
    int position = (p.settings.x < .5 ? pixel.x : pixel.y) +
                   int(p.settings.x < .5 ? p.geometry.z - p.geometry.x : p.geometry.w - p.geometry.y);
    int line = p.settings.x < .5 ? pixel.y : pixel.x;
    if (stage == BLUR_APPLY_MOMENTS) {
        o_color = applyMoments(position, line);
        return;
    }
    vec4 sum = blurFetch(source, position, line, p.settings.z > .5) * p.weights.x;
    for (int i = 1; i <= 3; ++i) {
        if (i > int(p.control.z))
            break;
        sum += (blurFetch(source, position - i, line, p.settings.z > .5) +
                blurFetch(source, position + i, line, p.settings.z > .5)) *
               p.weights[i];
    }
    o_color = sum;
}
