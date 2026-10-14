#version 450
// Own bounded alpha-volume intersection. The2D raster pipeline supplies the
// face; this pass supplies its physicalZ interval and actual scene depth.
#include "../common/bindings.glsl"
layout(push_constant) uniform Push { mat4 clipFromLayer;vec4 region;vec4 uvRect;vec4 params; } pc;
layout(location=0) in vec3 v_clip;
layout(location=0) out vec4 o_color;
layout(location=1) out vec4 o_scene;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D u_tex1;
layout(set=0,binding=AUREA_TEX2) uniform sampler2D u_tex2;
layout(set=0,binding=AUREA_TEX3) uniform sampler2D u_tex3;
layout(set=0,binding=AUREA_TEX4) uniform sampler2D u_tex4;
layout(set=0,binding=AUREA_TEX5) uniform sampler2D u_tex5;
layout(set=0,binding=AUREA_TEX6) uniform sampler2D u_tex6;
layout(set=0,binding=AUREA_TEX7) uniform sampler2D u_tex7;
layout(set=0,binding=AUREA_TEX8) uniform sampler2D u_tex8;
// O ambiente da cena, o mesmo dos modelos 3D (pbr.frag).
layout(set=0,binding=AUREA_TEX9) uniform samplerCube u_irradiance;
layout(set=0,binding=AUREA_TEX10) uniform samplerCube u_prefilter;
layout(set=0,binding=AUREA_TEX11) uniform sampler2D u_brdf;
const int MAX_LIGHTS=8;
layout(set=0,binding=AUREA_PARAMS,std140) uniform VolumeLights {
    mat4 worldFromLayer;vec4 cameraPos;vec4 ambient;vec4 lightCount;
    vec4 lightPos[MAX_LIGHTS];vec4 lightColor[MAX_LIGHTS];vec4 lightSpot[MAX_LIGHTS];vec4 lightSpot2[MAX_LIGHTS];
    mat4 layerFromClip;mat4 layerFromWorld;
    vec4 box;vec4 volume;vec4 side;vec4 sizes;
    vec4 envParams;   // x = intensidade, y = tem IBL, z = mips do pré-filtrado, w = rotação (rad)
    vec4 skyColor;vec4 groundColor;   // ambiente analítico (sem IBL)
    vec4 material;    // x = reflexo do ambiente, y = metal, z = rugosidade
} u;

vec4 fetch_source(ivec2 p) {
    if(any(lessThan(p,ivec2(0)))||any(greaterThanEqual(p,ivec2(u.sizes.xy))))return vec4(0.0);
    return texelFetch(u_tex0,p,0);
}
vec4 source(vec2 p) {
    // A solid's existing optimized source is one texel covering the whole
    // layer rectangle. Its alpha is uniform, not a bilinear tent.
    if(all(equal(ivec2(u.sizes.xy),ivec2(1))))return texelFetch(u_tex0,ivec2(0),0);
    vec2 texel=(p-u.box.xy)*u.sizes.xy/u.box.zw-0.5;
    ivec2 cell=ivec2(floor(texel));vec2 f=fract(texel);
    return mix(mix(fetch_source(cell),fetch_source(cell+ivec2(1,0)),f.x),
        mix(fetch_source(cell+ivec2(0,1)),fetch_source(cell+ivec2(1,1)),f.x),f.y);
}
float maximum_read(sampler2D s,ivec2 p) {
    if(any(lessThan(p,ivec2(0)))||any(greaterThanEqual(p,textureSize(s,0))))return 0.0;
    return texelFetch(s,p,0).a;
}
// Constant sampler access supports Vulkan1/ES3.1/Metal without a nonuniform
// indexing extension. Slot8 may be the global max for sources above128.
float maximum(int level,ivec2 p) {
    if(level==0)return maximum_read(u_tex0,p);
    if(level==1)return maximum_read(u_tex1,p);
    if(level==2)return maximum_read(u_tex2,p);
    if(level==3)return maximum_read(u_tex3,p);
    if(level==4)return maximum_read(u_tex4,p);
    if(level==5)return maximum_read(u_tex5,p);
    if(level==6)return maximum_read(u_tex6,p);
    if(level==7)return maximum_read(u_tex7,p);
    return maximum_read(u_tex8,p);
}
float support_max(int level,vec2 texel) {
    float block=exp2(float(level));
    ivec2 lo=ivec2(floor(texel/block)),hi=ivec2(floor((texel+1.0)/block));
    return max(max(maximum(level,lo),maximum(level,ivec2(hi.x,lo.y))),
        max(maximum(level,ivec2(lo.x,hi.y)),maximum(level,hi)));
}
float next_boundary(vec2 p,vec2 velocity,float block) {
    // Both bilinear neighbors must remain inside the proven empty block.
    vec2 steps=vec2(1e30);
    for(int axis=0;axis<2;++axis) {
        float v=velocity[axis];
        if(abs(v)<1e-12)continue;
        for(int neighbor=0;neighbor<2;++neighbor) {
            float x=p[axis]+float(neighbor);
            float edge=(v>0.0?floor(x/block)+1.0:ceil(x/block)-1.0)*block;
            steps[axis]=min(steps[axis],(edge-x)/v);
        }
    }
    return max(min(steps.x,steps.y),1e-5);
}
float first_crossing(float a0,float am,float a1,float threshold) {
    float C=a0-threshold,A=2.0*(a0+a1-2.0*am),B=a1-a0-A;
    if(C>=0.0)return 0.0;
    if(abs(A)<1e-7)return B>0.0?clamp(-C/B,0.0,2.0):2.0;
    float discriminant=B*B-4.0*A*C;
    if(discriminant<0.0)return 2.0;
    float d=sqrt(discriminant),r0=(-B-d)/(2.0*A),r1=(-B+d)/(2.0*A);
    float r=2.0;
    if(r0>=0.0&&r0<=1.0)r=min(r,r0);
    if(r1>=0.0&&r1<=1.0)r=min(r,r1);
    return r;
}
vec3 illumination(vec3 local,vec3 N) {
    vec3 P=(u.worldFromLayer*vec4(local,1.0)).xyz,sum=u.ambient.rgb;
    for(int i=0;i<MAX_LIGHTS;++i) {
        if(i>=int(u.lightCount.x+0.5))break;
        int type=int(u.lightPos[i].w+0.5);vec3 L;float attenuation=1.0;
        if(type==0)L=normalize(u.lightPos[i].xyz);
        else {
            vec3 d=u.lightPos[i].xyz-P;float distance=max(length(d),1e-3);L=d/distance;
            float range=u.lightColor[i].w;
            if(range>0.0){float ratio=distance/range;float w=clamp(1.0-pow(ratio,4.0),0.0,1.0);attenuation=w*w;}
            if(type==2) {
                float c=dot(-L,normalize(u.lightSpot[i].xyz));
                float w=clamp((c-u.lightSpot[i].w)/max(u.lightSpot2[i].x-u.lightSpot[i].w,1e-4),0.0,1.0);
                attenuation*=w*w;
            }
        }
        sum+=u.lightColor[i].rgb*(max(dot(N,L),0.0)*attenuation);
    }
    return sum;
}
// Ambiente: as mesmas fórmulas do pbr.frag (IBL ou céu/chão analítico).
vec2 env_brdf_approx(float NdotV,float roughness) {
    const vec4 c0=vec4(-1.0,-0.0275,-0.572,0.022);
    const vec4 c1=vec4(1.0,0.0425,1.04,-0.04);
    vec4 r=roughness*c0+c1;
    float a004=min(r.x*r.x,exp2(-9.28*NdotV))*r.x+r.y;
    return vec2(-1.04,1.04)*a004+r.zw;
}
vec3 analytic_env(vec3 dir,float blur) {
    float up=-dir.y;
    vec3 horizon=mix(u.groundColor.rgb,u.skyColor.rgb,0.5);
    float k=mix(0.25,1.0,blur);
    return up>=0.0?mix(horizon,u.skyColor.rgb,pow(clamp(up,0.0,1.0),k))
                  :mix(horizon,u.groundColor.rgb,pow(clamp(-up,0.0,1.0),k));
}
vec3 env_dir(vec3 d) {
    d=vec3(d.x,-d.y,-d.z);
    float r=u.envParams.w;
    if(r==0.0)return d;
    float c=cos(r),s=sin(r);
    return vec3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
}
float env_lod(float roughness){float r=clamp(roughness,0.0,1.0);return max(u.envParams.z-1.0,0.0)*r*(1.7-0.7*r);}
// Premultiplied surface: metal tira o difuso; o especular do ambiente soma.
vec4 environment(vec4 color,vec3 local,vec3 N) {
    float amount=u.material.x;
    if(amount<=0.0||color.a<=1e-6)return color;
    vec3 P=(u.worldFromLayer*vec4(local,1.0)).xyz;
    vec3 V=normalize(u.cameraPos.xyz-P);
    if(dot(N,V)<0.0)N=-N;
    float NdotV=clamp(dot(N,V),1e-4,1.0);
    vec3 R=reflect(-V,N);
    float metal=clamp(u.material.y,0.0,1.0),roughness=clamp(u.material.z,0.02,1.0);
    vec3 albedo=color.rgb/color.a;
    vec3 F0=mix(vec3(0.04),albedo,metal);
    vec3 prefiltered;vec2 dfg;
    if(u.envParams.y>0.5) {
        prefiltered=textureLod(u_prefilter,env_dir(R),env_lod(roughness)).rgb;
        dfg=texture(u_brdf,vec2(NdotV,roughness)).rg;
    } else {
        prefiltered=analytic_env(R,roughness);
        dfg=env_brdf_approx(NdotV,roughness);
    }
    vec3 specular=prefiltered*(F0*dfg.x+dfg.y)*u.envParams.x;
    vec3 diffuse=albedo*(1.0-metal*amount);
    return vec4((diffuse+specular*amount)*color.a,color.a);
}
vec3 linear_rgb(vec3 color) {
    return mix(color/12.92,pow((color+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),color));
}
void main() {
    vec2 ndc=v_clip.xy/v_clip.z;
    vec4 nearH=u.layerFromClip*vec4(ndc,1.0,1.0),middleH=u.layerFromClip*vec4(ndc,0.5,1.0);
    if(abs(nearH.w)<1e-12||abs(middleH.w)<1e-12)discard;
    vec3 origin=nearH.xyz/nearH.w,ray=middleH.xyz/middleH.w-origin;
    float rayLength=length(ray);
    if(!(rayLength>1e-10&&rayLength<1e20))discard;
    vec3 direction=ray/rayLength;
    vec3 lower=vec3(u.box.xy,0.0),upper=vec3(u.box.xy+u.box.zw,u.volume.x);
    float begin=0.0,end=1e30;vec3 entryNormal=vec3(0.0);
    for(int axis=0;axis<3;++axis) {
        if(abs(direction[axis])<1e-12) {
            if(origin[axis]<lower[axis]||origin[axis]>upper[axis])discard;
        } else {
            float a=(lower[axis]-origin[axis])/direction[axis],b=(upper[axis]-origin[axis])/direction[axis];
            float first=min(a,b),last=max(a,b);
            if(first>begin){begin=first;entryNormal=vec3(0.0);entryNormal[axis]=direction[axis]>0.0?-1.0:1.0;}
            end=min(end,last);
        }
    }
    if(begin>end||end<0.0)discard;
    float globalAlpha=maximum(int(u.sizes.w)-1,ivec2(0));
    if(globalAlpha<=1e-6)discard;
    float threshold=globalAlpha*0.5;
    float t=begin;vec3 point=origin+direction*t,normal=entryNormal;
    bool cap=abs(entryNormal.z)>0.5,hit=false;
    vec4 color=source(point.xy);
    // Preserve fractional alpha on the front/back raster caps, independently
    // of the wall's half-height contour used for stable geometry.
    if(cap&&color.a>1e-6)hit=true;
    vec2 velocity=direction.xy*u.sizes.xy/u.box.zw;
    int maxLevel=min(int(u.sizes.z)-1,int(u.sizes.w)-2);
    for(int stepIndex=0;stepIndex<512;++stepIndex) {
        if(hit||stepIndex>=int(u.volume.z)||t>=end)break;
        point=origin+direction*t;
        vec2 pixel=(point.xy-u.box.xy)*u.sizes.xy/u.box.zw-0.5;
        int level=0;bool empty=false;
        for(int rank=7;rank>=0;--rank) {
            if(rank>maxLevel)continue;
            if(support_max(rank,pixel)<threshold){level=rank;empty=true;break;}
        }
        float distance=next_boundary(pixel,velocity,exp2(float(level)));
        float nextT=min(end,t+distance);
        if(!empty) {
            float a0=source(point.xy).a,am=source((origin+direction*((t+nextT)*0.5)).xy).a;
            float a1=source((origin+direction*nextT).xy).a;
            float fraction=first_crossing(a0,am,a1,threshold);
            if(fraction<=1.0) {
                t=mix(t,nextT,fraction);point=origin+direction*t;color=source(point.xy);hit=true;cap=false;
                vec2 delta=u.box.zw/u.sizes.xy;
                vec2 gradient=vec2(source(point.xy+vec2(delta.x,0)).a-source(point.xy-vec2(delta.x,0)).a,
                    source(point.xy+vec2(0,delta.y)).a-source(point.xy-vec2(0,delta.y)).a)/delta;
                normal=length(gradient)>1e-8?normalize(vec3(-gradient,0.0)):entryNormal;
            }
        }
        if(!hit)t=nextT+max(1e-5,abs(nextT)*1e-7);
    }
    if(!hit)discard;
    vec4 physical=pc.clipFromLayer*vec4(point,1.0);
    if(physical.w<=0.0||physical.z<0.0||physical.z>physical.w)discard;
    gl_FragDepth=clamp(physical.z/physical.w,0.0,1.0);
    if(dot(normal,normal)<1e-12)normal=-direction;
    vec3 N=normalize(transpose(mat3(u.layerFromWorld))*normal);
    if(!cap) {
        // The wall's coverage follows nearby source alpha, not the0.5 contour
        // crossing itself (which would make every opaque wall translucent).
        vec2 pixel=(point.xy-u.box.xy)*u.sizes.xy/u.box.zw-0.5;
        ivec2 cell=ivec2(floor(pixel));
        float localAlpha=max(max(fetch_source(cell).a,fetch_source(cell+ivec2(1,0)).a),
            max(fetch_source(cell+ivec2(0,1)).a,fetch_source(cell+ivec2(1,1)).a));
        float alpha=localAlpha*u.side.a;
        float shade=mix(1.0,0.25+0.75*max(dot(N,normalize(vec3(-0.4,-0.5,-1.0))),0.0),u.volume.y);
        color=vec4(linear_rgb(u.side.rgb)*shade*alpha,alpha);
    }
    if(u.cameraPos.w>0.5)color.rgb*=illumination(point,N);
    color=environment(color,point,N);
    color*=pc.params.x;
    if(pc.params.y<0.5){if(color.a<0.999)discard;}
    else {if(color.a<=1e-6||color.a>=0.999)discard;}
    o_color=color;o_scene=vec4(0.0,0.0,0.0,color.a);
}
