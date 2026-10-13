#version 450
// A terminal Motion Tile is a periodic plane, not a finite raster wall. Rays
// are resolved by the fragment shader, including its physical depth/frustum.
layout(push_constant) uniform Push {
    mat4 clipFromLayer; vec4 region; vec4 uvRect; vec4 params;
} pc;
layout(location = 0) out vec3 v_clip;
const vec2 kCorners[6] = vec2[6](vec2(0,0),vec2(1,0),vec2(0,1),vec2(1,0),vec2(1,1),vec2(0,1));
void main() {
    vec2 p = kCorners[gl_VertexIndex] * 2.0 - 1.0;
    gl_Position = vec4(p, 0.0, 1.0);
    v_clip = vec3(p, 1.0);
}
