#version 460 core
#include <flutter/runtime_effect.glsl>
precision highp float;
uniform vec2 uSize;
uniform sampler2D uRed;
uniform sampler2D uGreen;
uniform sampler2D uBlue;
out vec4 fragColor;
void main() {
  vec2 uv = FlutterFragCoord().xy / uSize;
  vec4 r = texture(uRed, uv);
  vec4 g = texture(uGreen, uv);
  vec4 b = texture(uBlue, uv);
  // Inputs and output are premultiplied. Keep alpha from the current
  // frame and take each straight color from its own temporal sample.
  vec3 rgb = vec3(r.a > 0.0 ? r.r/r.a : 0.0,
                  g.a > 0.0 ? g.g/g.a : 0.0,
                  b.a > 0.0 ? b.b/b.a : 0.0);
  fragColor = vec4(rgb * g.a, g.a);
}
