#version 460 core
#include <flutter/runtime_effect.glsl>
uniform vec2 uSize;
uniform float uThreshold;
uniform float uSoftness;
uniform float uTint;
uniform vec3 uColor;
uniform sampler2D uImage;
out vec4 fragColor;
void main() {
  vec4 source = texture(uImage, FlutterFragCoord().xy / uSize);
  // Threshold straight color: antialiased edges must not get a dark fringe.
  vec3 straight = source.a > .0001 ? source.rgb / source.a : vec3(0.0);
  float luminance = dot(straight, vec3(.2126, .7152, .0722));
  float softness = max(uSoftness, .002);
  float mask = smoothstep(uThreshold - softness * .5,
                          uThreshold + softness * .5, luminance);
  vec3 light = mix(source.rgb, vec3(luminance * source.a) * uColor, uTint) * mask;
  fragColor = vec4(light, max(light.r, max(light.g, light.b)));
}
