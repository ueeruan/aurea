#version 450
#include "../common/bindings.glsl"
struct CopyRows { vec4 x;vec4 y; };
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params { vec4 region;vec4 inputRegion;CopyRows copies[64]; } p;
layout(location=0) out vec2 v_uv;
layout(location=1) out float v_opacity;
void main() {
    const vec2 corners[6]=vec2[](vec2(0,0),vec2(1,0),vec2(0,1),vec2(0,1),vec2(1,0),vec2(1,1));
    int copyIndex=gl_VertexIndex/6; v_uv=corners[gl_VertexIndex%6];
    vec3 local=vec3(p.inputRegion.xy+v_uv*p.inputRegion.zw,1);
    CopyRows m=p.copies[copyIndex]; vec2 point=vec2(dot(m.x.xyz,local),dot(m.y.xyz,local));
    vec2 clip=(point-p.region.xy)/p.region.zw*2.0-1.0;
    gl_Position=vec4(clip,0,1);v_opacity=m.x.w;
}
