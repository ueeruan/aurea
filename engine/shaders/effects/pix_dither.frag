#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D src;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {vec4 uvMap,texel,p0,p1,p2,p3,color,second;} p;
#include "pix_palette.glsl"
int bayer(ivec2 point,int size) {
    int result=0;
    for(int bit=0;bit<3;bit++) {
        if((1<<bit)>=size) break;
        int x=(point.x>>bit)&1,y=(point.y>>bit)&1;
        result=result*4+((x^y)*2+y);
    }
    return result;
}
float threshold(ivec2 point,int method) {
    if(method==12) return (float(bayer(point,2))+.5)/4.0;
    if(method==15) return (float(bayer(point,4))+.5)/16.0;
    if(method==16) return (float(bayer(point,8))+.5)/64.0;
    if(method==13) {const int matrix[9]=int[9](0,7,3,6,5,2,4,1,8);ivec2 q=ivec2(mod(vec2(point),3.0));return (float(matrix[q.y*3+q.x])+.5)/9.0;}
    // Non-power-of-two thresholds are AUREA patterns; vendor phase/ordering
    // has not been measured. They retain their distinct spatial families.
    if(method==14) {ivec2 q=ivec2(mod(vec2(point),vec2(5,3)));return (float(((q.x*3+q.y)+6)%15)+.5)/15.0;}
    if(method==17||method==18) {
        const int matrix[16]=int[16](12,5,6,13,4,0,1,7,11,3,2,8,15,10,9,14);
        ivec2 q=ivec2(mod(vec2(point),4.0));
        if(method==17) return (float(matrix[q.y*4+q.x])+.5)/16.0;
        ivec2 fine=ivec2(mod(vec2(point/4),2.0));
        return (float(matrix[q.y*4+q.x]*4+bayer(fine,2))+.5)/64.0;
    }
    if(method==19) return (float((point.y%2)*2+(point.x%2))+.5)/4.0;
    if(method==20) return (float(point.x%8)+.5)/8.0;
    if(method==21) return (float((point.x%12)*4+(point.y%4))+.5)/48.0;
    if(method==22) return (float((point.x%2)*2+(point.y%2))+.5)/4.0;
    if(method==23) return (float(point.y%8)+.5)/8.0;
    if(method==24) return (float((point.y%12)*4+(point.x%4))+.5)/48.0;
    if(method==25) return (float((point.x+point.y)%5+(point.y%5)*5)+.5)/25.0;
    return .5;
}
void main() {
    vec4 value=texture(src,v_uv);
    vec3 straight=aurea_linear_to_srgb(max(unpremultiply(value).rgb,vec3(0)));
    ivec2 point=ivec2(floor(p.p2.xy+v_uv*p.p2.zw));
    point=ivec2(mod(vec2(point),120.0));
    vec3 encoded=quantize(straight+(threshold(point,int(p.p0.x))-.5)*paletteStep()*p.p0.z);
    o_color=vec4(aurea_srgb_to_linear(encoded)*value.a,value.a);
}
