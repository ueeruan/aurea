#version 450
#include "../common/bindings.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 outRect;
    vec4 inRect;
    vec4 points;
    vec4 control; // mode, displacement/angle, cylinder radius, mix
    vec4 basis;   // direction XY, shading strength
    vec4 footprint; // layer pixels per output texel
    vec4 backColor;
} p;

vec4 source_at(vec2 point) {
    vec2 uv=(point-p.inRect.xy)/p.inRect.zw;
    // Explicit LOD: inverse branches differ per fragment, source has one mip.
    return textureLod(u_tex0,uv,0.0);
}
vec4 material(vec4 c,float cosine) {
    float lighting=mix(1.0,.35+.65*abs(cosine),p.basis.z);
    if(cosine<0.0){c.rgb*=p.backColor.rgb*p.backColor.a;c.a*=p.backColor.a;}
    c.rgb*=lighting;
    return c; // Shading never makes transparent source pixels opaque.
}
vec4 deform_at(vec2 point,out bool deformed) {
    deformed=false;
    vec4 original=source_at(point);
    int mode=int(p.control.x+.5);
    if(mode==0) {
        vec2 span=p.points.zw-p.points.xy;
        float lengthAB=length(span);
        if(lengthAB<1e-5)return original;
        vec2 along=span/lengthAB,normal=vec2(-along.y,along.x);
        float s=dot(point-p.points.xy,along)/lengthAB;
        // Smooth deflection with fixed endpoints and zero end tangents.
        float envelope=s>0.0&&s<1.0?16.0*s*s*(1.0-s)*(1.0-s):0.0;
        deformed=envelope>0.0;
        vec4 curved=source_at(point-normal*(p.control.y*envelope));
        return mix(original,curved,p.control.w);
    }
    vec2 axis=p.basis.xy,side=vec2(-axis.y,axis.x);
    vec2 rel=point-p.points.xy;
    float n=dot(rel,axis),t=dot(rel,side),angle=p.control.y;
    float c=cos(angle);
    vec4 colors[4]; float depths[4];
    for(int i=0;i<4;++i){colors[i]=vec4(0);depths[i]=-1e20;}
    if(n<=0.0){colors[0]=original;depths[0]=0.0;}
    if(mode==1) {
        if(abs(c)>1e-5) {
            float s=n/c;
            if(s>0.0){
                colors[1]=material(source_at(p.points.xy+axis*s+side*t),c);
                depths[1]=s*sign(angle)*max(abs(sin(angle)),1e-6);
            }
        }
    } else {
        float radius=p.control.z,arc=radius*angle;
        if(abs(n)<=radius) {
            float a=asin(clamp(n/radius,-1.0,1.0));
            float branches[2];branches[0]=a;branches[1]=3.14159265359-a;
            for(int i=0;i<2;++i) {
                float phi=branches[i];
                if(phi>0.0&&phi<=angle&&(i==0||abs(phi-a)>1e-5)) {
                    float cosine=cos(phi);
                    colors[i+1]=material(source_at(p.points.xy+axis*(phi*radius)+side*t),cosine);
                    depths[i+1]=radius*(1.0-cosine);
                }
            }
        }
        if(abs(c)>1e-5) {
            float tail=(n-radius*sin(angle))/c;
            if(tail>0.0) {
                colors[3]=material(source_at(p.points.xy+axis*(arc+tail)+side*t),c);
                depths[3]=radius*(1.0-c)+tail*sin(angle);
            }
        }
    }
    // Four sheets at most (fixed, cylinder front/back, tangent tail).
    // Sort back to front, then alpha-over; holes expose the underlying sheet.
    deformed=n>0.0||colors[1].a>0.0||colors[2].a>0.0||colors[3].a>0.0;
    for(int i=0;i<3;++i)for(int j=i+1;j<4;++j)if(depths[j]<depths[i]) {
        float d=depths[i];depths[i]=depths[j];depths[j]=d;
        vec4 v=colors[i];colors[i]=colors[j];colors[j]=v;
    }
    vec4 result=vec4(0);
    for(int i=0;i<4;++i)result=colors[i]+result*(1.0-colors[i].a);
    return mix(original,result,p.control.w);
}
void main() {
    vec2 point=p.outRect.xy+v_uv*p.outRect.zw;
    vec2 d=p.footprint.xy*.25;
    bool a,b,c,e;
    vec4 value=deform_at(point+vec2(-d.x,-d.y),a)+deform_at(point+vec2(d.x,-d.y),b)
                  +deform_at(point+vec2(-d.x,d.y),c)+deform_at(point+vec2(d.x,d.y),e);
    // Supersampling only the curved/folded footprint avoids refiltering sharp
    // source pixels on a stationary sheet (e.g. a one-pixel checkerboard).
    o_color=a||b||c||e?.25*value:source_at(point);
}
