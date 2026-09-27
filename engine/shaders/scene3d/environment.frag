#version 450
#include "../common/bindings.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform samplerCube panorama;
layout(std140,set=0,binding=AUREA_PARAMS) uniform Sky { mat4 view; vec4 projection; vec4 light; } u;
void main() {
    vec2 ndc = v_uv*2.0-1.0;
    vec3 world = transpose(mat3(u.view))*normalize(vec3(ndc.x*u.projection.x*u.projection.y,ndc.y*u.projection.y,1.0));
    vec3 d = vec3(world.x,-world.y,-world.z);
    float c=cos(u.light.x), s=sin(u.light.x);
    d=vec3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
    vec3 color = max(textureLod(panorama,d,0.0).rgb*u.light.y*u.light.z,vec3(0));
    // Preserve ordinary panoramas; compress only HDR highlights.
    float peak=max(color.r,max(color.g,color.b));
    if(peak>1.0) color *= (1.0 + log2(peak)*0.05)/peak;
    o_color=vec4(clamp(color,0.0,1.0),1.0);
}
