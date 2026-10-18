#version 450
#include "../common/bindings.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_source;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params { vec4 size; } u;
void main() {
    ivec2 sourceSize=ivec2(u.size.xy);
    ivec2 start=ivec2(gl_FragCoord.xy)*2;
    float alpha=0.0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        ivec2 p=start+ivec2(x,y);
        if(all(lessThan(p,sourceSize)))alpha=max(alpha,texelFetch(u_source,p,0).a);
    }
    o_color=vec4(0.0,0.0,0.0,alpha);
}
