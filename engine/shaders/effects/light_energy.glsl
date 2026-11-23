#ifndef AUREA_EFFECTS_LIGHT_ENERGY_GLSL
#define AUREA_EFFECTS_LIGHT_ENERGY_GLSL

const vec3 encodedLightLuma = vec3(0.3125, 0.5, 0.1875);

// A single gain for the three channel excesses preserves the source hue.
vec3 hue_preserving_highlights(vec3 source, float threshold, vec3 thresholdColor) {
    vec3 excess = max(source - vec3(threshold) - thresholdColor, vec3(0.0));
    float sourceEnergy = max(dot(source, vec3(1.0)), 1e-6);
    float headroom = max(1.0 - threshold, 1e-4);
    return source * (dot(excess, vec3(1.0)) / (sourceEnergy * headroom));
}
#endif
