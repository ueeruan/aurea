#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D src;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap,texel,p0,p1,p2,p3,color,background,region;
} p;
uint hash32(uint x) {x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
float random(ivec2 cell,uint salt) {return float(hash32(uint(cell.x)^hash32(uint(cell.y))^hash32(uint(p.p1.w))^salt)&0xffffffu)/16777215.0;}
// Original 5x7 block lettering. Row masks are drawn here, not an extracted atlas.
uint glyphRow(int glyph,int row) {
    const uint rows[112]=uint[112](
        14u,17u,19u,21u,25u,17u,14u,4u,12u,4u,4u,4u,4u,14u,
        14u,17u,1u,2u,4u,8u,31u,30u,1u,1u,14u,1u,1u,30u,
        2u,6u,10u,18u,31u,2u,2u,31u,16u,16u,30u,1u,1u,30u,
        14u,16u,16u,30u,17u,17u,14u,31u,1u,2u,4u,8u,8u,8u,
        14u,17u,17u,14u,17u,17u,14u,14u,17u,17u,15u,1u,1u,14u,
        14u,17u,17u,31u,17u,17u,17u,30u,17u,17u,30u,17u,17u,30u,
        15u,16u,16u,16u,16u,16u,15u,30u,17u,17u,17u,17u,17u,30u,
        31u,16u,16u,30u,16u,16u,31u,17u,27u,21u,21u,17u,17u,17u);
    return rows[clamp(glyph,0,15)*7+clamp(row,0,6)];
}
float pattern(int mode,vec2 uv,float tone,ivec2 cell) {
    vec2 q=uv-.5;
    if(tone<=0.0) return 0.0;
    if(mode==1) {
        ivec2 pixel=ivec2(floor(uv*vec2(6,8)));
        if(pixel.x>=5||pixel.y>=7) return 0.0;
        int glyph=int(floor(tone*15.0));
        return float((glyphRow(glyph,pixel.y)>>uint(4-pixel.x))&1u);
    }
    if(mode==2) return step(abs(q.y),tone*.5);
    if(mode==3) return step(length(q),sqrt(tone/3.14159265));
    if(mode==4) return step(abs(q.x),tone*.5);
    if(mode==5) return step(random(cell*8+ivec2(uv*8),19u),tone);
    if(mode==6) return step(abs(length(q)-.28),tone*.18);
    if(mode==7) return step(max(abs(q.x),abs(q.y)),sqrt(tone)*.5);
    if(mode==8) return step(abs(q.x+q.y)*.5,tone*.5);
    if(mode==9) return step(abs(q.x-q.y)*.5,tone*.5);
    // Nested hatching adds detail as tone increases, rather than a flat mosaic.
    float a=step(abs(q.x),tone*.22),b=step(abs(q.y),max(0.0,tone-.25)*.3);
    float c=step(abs(q.x-q.y)*.5,max(0.0,tone-.5)*.4);
    float d=step(abs(q.x+q.y)*.5,max(0.0,tone-.75)*.6);
    return max(max(a,b),max(c,d));
}
void main() {
    vec2 point=p.region.xy+v_uv*p.region.zw;
    vec2 grid=(point-p.p2.xy)/p.p0.xy;
    ivec2 cell=ivec2(floor(grid));
    vec2 center=(vec2(cell)+.5)*p.p0.xy+p.p2.xy;
    vec4 value=texture(src,(center-p.region.xy)/p.region.zw);
    vec3 encoded=aurea_linear_to_srgb(max(unpremultiply(value).rgb,vec3(0)));
    int channel=int(p.p2.z);
    float tone=dot(encoded,vec3(.299,.587,.114));
    if(channel==1) tone=(encoded.r+encoded.g+encoded.b)/3.0;
    if(channel>=2&&channel<=4) tone=encoded[channel-2];
    if(channel==5) tone=dot(encoded,vec3(.2126,.7152,.0722));
    tone=(tone-.5)*p.p0.w+.5+p.p0.z;
    if(p.p3.x>.5) tone=1.0-tone;
    tone=clamp(tone+(random(cell,3u)-.5)*p.p1.z,0.0,1.0);
    tone=floor(tone*p.p1.y+.5)/p.p1.y;
    int mode=int(p.p1.x);
    if(p.p3.z>.5) mode=int(floor(random(cell,7u)*9.999));
    float ink=pattern(mode,fract(grid),tone,cell);
    vec4 fg=p.color;
    if(p.p2.w>.5) fg.rgb=unpremultiply(value).rgb;
    vec4 bg=p.p3.y>.5?vec4(0):p.background;
    fg.rgb*=fg.a;bg.rgb*=bg.a;
    o_color=mix(bg,fg,ink)*value.a;
}
