#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D src;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D lens;
layout(set=0,binding=AUREA_TEX2) uniform sampler2D matte;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap,texel,p0,p1,p2,p3,color;
    vec4 lensRegion,matteRegion,crop,modes;
    mat4 compFromLayer;
} p;
vec2 compPoint(vec2 point) { vec4 q=p.compFromLayer*vec4(point,0,1);return q.xy/max(abs(q.w),1e-6); }
float brightness(vec2 point) {
    vec2 q=p.p3.x>.5?compPoint(point):point;
    vec4 value=texture(lens,(q-p.lensRegion.xy)/max(p.lensRegion.zw,vec2(.001)));
    vec3 enc=aurea_linear_to_srgb(max(unpremultiply(value).rgb,vec3(0)));
    return dot(enc,vec3(.2126,.7152,.0722))*value.a;
}
float maskAt(vec2 point) {
    if(p.p3.y==0.0) return 1.0;
    float mask=0.0;
    if(p.p3.y>0.0) {
        vec4 value=texture(matte,(compPoint(point)-p.matteRegion.xy)/max(p.matteRegion.zw,vec2(.001)));
        mask=p.p3.w>.5?value.a:dot(aurea_linear_to_srgb(max(unpremultiply(value).rgb,vec3(0))),vec3(.2126,.7152,.0722))*value.a;
    }
    return clamp(p.p3.z>.5?1.0-mask:mask,0.0,1.0);
}
vec2 refracted(vec2 point) {
    vec2 stepPx=1.0/max(p.texel.zw,vec2(.001));
    vec2 g=vec2(brightness(point+vec2(stepPx.x,0))-brightness(point-vec2(stepPx.x,0)),brightness(point+vec2(0,stepPx.y))-brightness(point-vec2(0,stepPx.y)))/(2.0*stepPx);
    float c=cos(p.p0.y),s=sin(p.p0.y);
    g=mat2(c,s,-s,c)*g;
    // Differentiate brightness, rather than interpreting R/G as displacement.
    return point-g*p.p0.zw*p.p0.x*(p.modes.y*p.modes.y*.05)*maskAt(point);
}
float wrapCoordinate(float q,int mode) {
    if(mode==1) return fract(q);
    if(mode==2) return 1.0-abs(mod(q,2.0)-1.0);
    return q;
}
vec4 sampleSource(vec2 point) {
    vec2 size=p.crop.zw-p.crop.xy;
    if(any(lessThanEqual(size,vec2(0)))) return vec4(0);
    vec2 q=(point-p.crop.xy)/size;
    if((int(p.p2.x)==0 && (q.x<0.0||q.x>1.0))||(int(p.p2.y)==0 && (q.y<0.0||q.y>1.0))) return vec4(0);
    q=vec2(wrapCoordinate(q.x,int(p.p2.x)),wrapCoordinate(q.y,int(p.p2.y)));
    vec2 pixel=clamp(p.crop.xy+q*size,p.crop.xy+min(vec2(.5),size*.5),p.crop.zw-min(vec2(.5),size*.5));
    vec4 value=texture(src,(pixel-p.p1.xy)/p.p1.zw);
    if(p.p2.w>.5 && p.p2.w<1.5) { value.rgb=unpremultiply(value).rgb;value.a=1.0; }
    return value;
}
void main() {
    vec2 point=p.p1.xy+v_uv*p.p1.zw;
    vec2 warped=refracted(point);
    if(p.p2.z<.5) { o_color=sampleSource(warped);return; }
    // Integrate the warped pixel footprint when it contracts the input.
    vec2 dx=dFdx(warped)*.25,dy=dFdy(warped)*.25;
    o_color=(sampleSource(warped-dx-dy)+sampleSource(warped+dx-dy)+sampleSource(warped-dx+dy)+sampleSource(warped+dx+dy))*.25;
}
