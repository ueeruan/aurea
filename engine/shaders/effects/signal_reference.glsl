#ifndef AUREA_SIGNAL_REFERENCE_GLSL
#define AUREA_SIGNAL_REFERENCE_GLSL

layout(set = 0, binding = AUREA_PARAMS, std140) uniform SignalParams {
    vec4 controls[9];
    vec4 raster; // width, height, seconds, stage
    vec4 context; // frame, connection index
} p;

const float signalPi = 3.141592653589793;
const float signalSampleRate = 14318181.818181818;

float setting(int index) {
    return p.controls[index / 4][index % 4];
}

uint signalSeed(uint stream) {
    return uint(setting(0)) ^ uint(max(0.0, p.context.x)) * 40503u
        ^ uint(p.context.y) * 2654435761u ^ stream;
}

float signalRandom(ivec2 pixel, uint stream) {
    return aurea_hash(uvec2(pixel) ^ uvec2(signalSeed(stream), stream * 747796405u));
}

float signalSmoothNoise(vec2 coordinate, uint stream) {
    ivec2 cell = ivec2(floor(coordinate));
    vec2 t = fract(coordinate);
    t = t * t * (3.0 - 2.0 * t);
    float a = mix(signalRandom(cell, stream), signalRandom(cell + ivec2(1, 0), stream), t.x);
    float b = mix(signalRandom(cell + ivec2(0, 1), stream), signalRandom(cell + ivec2(1, 1), stream), t.x);
    return mix(a, b, t.y) * 2.0 - 1.0;
}

int signalCarrierOffset(int row) {
    int shift = int(setting(29));
    int offset = int(setting(30));
    if (shift == 90) offset += row / 2;
    if (shift == 180) offset += row & 2;
    if (shift == 270) offset -= row / 2;
    return offset & 3;
}

vec2 signalCarrierBasis(ivec2 pixel) {
    int phase = (pixel.x + signalCarrierOffset(pixel.y)) & 3;
    if (phase == 0) return vec2(1.0, 0.0);
    if (phase == 1) return vec2(0.0, 1.0);
    if (phase == 2) return vec2(-1.0, 0.0);
    return vec2(0.0, -1.0);
}

float signalLumaCutoff() {
    int mode = clamp(int(setting(10)), 0, 2);
    return mode == 0 ? 2400000.0 : (mode == 1 ? 1900000.0 : 1400000.0);
}

float signalChromaCutoff() {
    int mode = clamp(int(setting(10)), 0, 2);
    return mode == 0 ? 320000.0 : (mode == 1 ? 300000.0 : 280000.0);
}

int signalChromaDelay() {
    int mode = clamp(int(setting(10)), 0, 2);
    return mode == 0 ? 9 : (mode == 1 ? 12 : 14);
}

float signalPole(float cutoff) {
    float angular = 2.0 * signalPi * max(cutoff, 1.0);
    return angular / (signalSampleRate + angular);
}

vec3 signalToRgb(vec3 yiq) {
    return vec3(yiq.x + 0.956 * yiq.y + 0.621 * yiq.z,
                yiq.x - 0.272 * yiq.y - 0.647 * yiq.z,
                yiq.x - 1.106 * yiq.y + 1.703 * yiq.z);
}

#endif
