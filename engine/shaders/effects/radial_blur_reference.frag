#version 450
// Desfoque radial (referência do AE). Comportamento descrito em
// RadialBlurReferenceEffect.cpp; tudo em pixels inteiros da camada.
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_source;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_phases;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    ivec4 world;    // largura, altura, centro x/y 16.16
    ivec4 control;  // tipo, valor inteiro, teto de amostras, deslocamento do zoom
    ivec4 zoom;     // escala 16.16 nas pontas, semente, fases válidas
    uvec4 jitter;
    vec4 raster;    // origem da região em px da camada, texels por px
} p;

vec4 texel(ivec2 pixel) {
    ivec2 size = textureSize(u_source, 0);
    return texelFetch(u_source, clamp(pixel, ivec2(0), size - 1), 0);
}

// Pixel da camada (inteiro) + fração em 1/32 -> bilinear no raster de trabalho.
vec4 layer_sample(ivec2 base, vec2 fraction) {
    vec2 position = (vec2(base) + fraction - p.raster.xy) * p.raster.zw;
    ivec2 origin = ivec2(floor(position));
    vec2 weight = position - vec2(origin);
    vec4 top = mix(texel(origin), texel(origin + ivec2(1, 0)), weight.x);
    vec4 bottom = mix(texel(origin + ivec2(0, 1)), texel(origin + ivec2(1, 1)), weight.x);
    return mix(top, bottom, weight.y);
}

// floor(r * s / 65536) sem 64 bits: s = alto * 65536 + baixo.
int fix_mul(int r, int s) {
    int high = s >> 16;
    uint low = uint(s & 0xffff);
    return r * high + int((uint(r) * low) >> 16u);
}

int start_phase(ivec2 pixel) {
    if (p.zoom.w == 0) {
        uint h = uint(pixel.x) * 0x9e3779b1u ^ uint(pixel.y) * 0x85ebca77u;
        h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12;
        return int(h & 1023u);
    }
    vec4 packed = texelFetch(u_phases, ivec2(pixel.x >> 3, pixel.y), 0);
    int channel = (pixel.x & 7) >> 1;
    float value = channel == 0 ? packed.x : channel == 1 ? packed.y : channel == 2 ? packed.z : packed.w;
    int pair = int(value + 0.5);
    return (pixel.x & 1) == 1 ? pair >> 10 : pair & 1023;
}

void main() {
    ivec2 rasterPixel = ivec2(floor(v_uv * vec2(textureSize(u_source, 0))));
    vec2 layerPoint = (vec2(rasterPixel) + 0.5) / p.raster.zw + p.raster.xy;
    ivec2 world = p.world.xy;
    ivec2 pixel = clamp(ivec2(floor(layerPoint)), ivec2(0), world - 1);
    ivec2 center = p.world.zw;
    ivec2 centerQuarter = center >> 14;
    ivec2 centerPixel = center >> 16;

    uint counter = uint(p.zoom.z) + uint(pixel.y * world.x + pixel.x) + 1u;
    uint slot = counter & 31u;
    int jitter = int((p.jitter[slot >> 3] >> ((slot & 7u) * 4u)) & 15u);
    ivec2 quarter = centerQuarter - pixel * 4;
    int maxRadius = int(sqrt(float(world.x * world.x + world.y * world.y)) * 4.0);
    int radius = min(int(length(vec2(quarter)) + 0.5) + jitter, maxRadius - 2);

    int samples;
    if (p.control.x == 0) {
        samples = (radius * p.control.y) >> 8;
    } else {
        samples = abs(fix_mul(radius, p.zoom.y) - fix_mul(radius, p.zoom.x)) >> p.control.w;
    }
    samples = clamp(samples, 4, p.control.z);
    int stepSize = 1024 / samples;
    int index = start_phase(pixel);
    vec2 delta = vec2(centerPixel - pixel);
    vec2 origin = vec2(centerPixel);
    ivec2 limit = (world - 2) << 16;
    vec4 sum = vec4(0.0);
    for (int i = 0; i < samples; ++i) {
        index &= 1023;
        vec2 source;
        if (p.control.x == 0) {
            float angle = float(p.control.y * (index - 512)) / -58368.0;
            float s = sin(angle), c = cos(angle);
            source = origin - vec2(delta.x * c - delta.y * s, delta.x * s + delta.y * c);
        } else {
            float scale = float((index - 512) * p.control.y) / 204800.0 + 1.0;
            source = origin - delta * scale;
        }
        index += stepSize;
        ivec2 fixedPoint = clamp(ivec2(source * 65536.0), ivec2(0), limit);
        vec2 fraction = vec2((fixedPoint >> 11) & 31) / 32.0;
        sum += layer_sample(fixedPoint >> 16, fraction);
    }
    o_color = sum / float(samples);
}
