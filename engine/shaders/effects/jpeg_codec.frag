#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D u_original;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {vec4 uvMap;vec4 texel;vec4 p0;vec4 p1;vec4 p2;vec4 p3;vec4 color;} p;
const float PI=3.14159265359;
const int QT[64]=int[64](16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99);
const int CHROMA_QT[64] = int[64](
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99);
float basis(int f,int x){return .5*(f==0?.70710678118:1.)*cos(PI*(float(x)+.5)*float(f)/8.);}
vec3 rgbYcc(vec3 c){return vec3(dot(c,vec3(.299,.587,.114))-.5,dot(c,vec3(-.168736,-.331264,.5)),dot(c,vec3(.5,-.418688,-.081312)));}
vec3 yccRgb(vec3 c){float y=c.x+.5;return vec3(y+1.402*c.z,y-.344136*c.y-.714136*c.z,y+1.772*c.y);}
vec3 source(ivec2 xy){
    vec2 uv=(vec2(clamp(xy,ivec2(0),ivec2(p.texel.xy)-1))+.5)/p.texel.xy;
    vec3 c=rgbYcc(aurea_linear_to_srgb(unpremultiply(texture(u_original,uv)).rgb));
    if(p.p2.z>.5){
        ivec2 step=ivec2(2,p.p2.z>1.5?2:1);ivec2 origin=(xy/step)*step;vec2 chroma=vec2(0);
        for(int j=0;j<2;++j)for(int i=0;i<2;++i){ivec2 at=origin+ivec2(i,min(j,step.y-1));vec2 v=(vec2(clamp(at,ivec2(0),ivec2(p.texel.xy)-1))+.5)/p.texel.xy;chroma+=rgbYcc(aurea_linear_to_srgb(unpremultiply(texture(u_original,v)).rgb)).yz;}
        c.yz=chroma*.25;
    }return c;
}

vec3 compression_source(ivec2 pixel) {
    vec2 uv = (vec2(clamp(pixel, ivec2(0), ivec2(p.texel.xy) - 1)) + 0.5) / p.texel.xy;
    vec3 straight = unpremultiply(texture(u_original, uv)).rgb;
    vec3 srgb = clamp(aurea_linear_to_srgb(max(straight, vec3(0.0))), 0.0, 1.0);
    vec3 digital = rgbYcc(srgb) + vec3(0.5, 128.0 / 255.0, 128.0 / 255.0);
    return (floor(clamp(digital, 0.0, 1.0) * 255.0 + 0.5) - 128.0) / 255.0;
}

vec3 quantize_compression(vec3 coefficient, ivec2 frequency, ivec2 block) {
    int index = frequency.y * 8 + frequency.x;
    float quality = clamp(p.p0.x, 1.0, 100.0);
    float scale = quality < 50.0 ? 5000.0 / quality : 200.0 - quality * 2.0;
    vec3 q = vec3(float(QT[index]), vec2(float(CHROMA_QT[index])));
    q = clamp(floor((q * scale + 50.0) / 100.0), 1.0, 255.0) / 255.0;
    vec3 ratio = coefficient / q;
    coefficient = sign(ratio) * floor(abs(ratio) + 0.5) * q;

    // Keep each frequency band's independent gain and apply it to luma and
    // chroma with separate amounts. A gain of one is a standard JPEG codec.
    float band = length(vec2(frequency));
    float gain = p.p0.y * (frequency.x > 0 ? p.p0.z : 1.0) * (frequency.y > 0 ? p.p0.w : 1.0);
    gain *= band < 3.0 ? p.p1.x : band < 6.0 ? p.p1.y : p.p1.z;
    coefficient *= max(vec3(0.0), vec3(1.0) + (gain - 1.0) * vec3(p.p1.w, vec2(p.p2.x)));

    if (p.p2.y > 0.0) {
        uint seed = uint(p.p3.y) | (uint(p.p3.z) << 16);
        uvec2 cell = uvec2(block / 8) ^ uvec2(seed, seed * 2654435761u);
        if (aurea_hash(cell) < p.p2.z &&
            aurea_hash(cell ^ uvec2(uint(index) * 31337u, 0x9e3779b9u)) < p.p2.y / 64.0) {
            vec3 damage = vec3(aurea_hash(cell + uvec2(uint(index), 91u)),
                               aurea_hash(cell + uvec2(uint(index), 173u)),
                               aurea_hash(cell + uvec2(uint(index), 263u))) * 2.0 - 1.0;
            coefficient += damage * p.p2.w * q;
        }
    }
    return coefficient;
}

vec4 compression_pass() {
    int stage = int(p.p3.x + 0.5);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 block = (pixel / 8) * 8;
    ivec2 phase = pixel - block;
    vec3 coefficient = vec3(0.0);
    if (stage == 0) {
        bool hasChroma = all(lessThan(pixel * 2, ivec2(p.texel.zw)));
        for (int x = 0; x < 8; ++x) {
            ivec2 at = ivec2(block.x + x, pixel.y);
            float weight = basis(phase.x, x);
            coefficient.x += weight * compression_source(at).x;
            if (hasChroma) {
                ivec2 chroma = at * 2;
                vec2 box = compression_source(chroma).yz +
                           compression_source(chroma + ivec2(1, 0)).yz +
                           compression_source(chroma + ivec2(0, 1)).yz +
                           compression_source(chroma + ivec2(1, 1)).yz;
                coefficient.yz += weight * (box * 0.25);
            }
        }
    } else if (stage == 1) {
        for (int y = 0; y < 8; ++y) {
            coefficient += basis(phase.y, y) * texelFetch(u_tex0, ivec2(pixel.x, block.y + y), 0).rgb;
        }
        coefficient = quantize_compression(coefficient, phase, block);
    } else if (stage == 2) {
        for (int y = 0; y < 8; ++y) {
            coefficient += basis(y, phase.y) * texelFetch(u_tex0, ivec2(pixel.x, block.y + y), 0).rgb;
        }
    } else if (stage == 3) {
        for (int x = 0; x < 8; ++x) {
            coefficient += basis(x, phase.x) * texelFetch(u_tex0, ivec2(block.x + x, pixel.y), 0).rgb;
        }
        coefficient = (floor(clamp(coefficient * 255.0 + 128.0, 0.0, 255.0) + 0.5) - 128.0) / 255.0;
    } else {
        // Hold decoded luma samples on the JPEG pixel grid. Reconstruct the
        // separate 4:2:0 chroma lattice with centered triangle interpolation.
        ivec2 xy = clamp(ivec2(floor(v_uv * p.texel.xy)), ivec2(0), ivec2(p.texel.xy) - 1);
        float luma = texelFetch(u_tex0, xy, 0).x + 128.0 / 255.0 - 0.5;
        vec2 chromaMax = ceil(p.texel.xy * 0.5) - 1.0;
        vec2 chromaAt = clamp((vec2(xy) - 0.5) * 0.5, vec2(0.0), chromaMax);
        vec2 chroma = texture(u_tex0, (chromaAt + 0.5) / p.texel.zw).yz;
        vec3 rgb = clamp(yccRgb(vec3(luma, chroma)), 0.0, 1.0);
        float gray = dot(rgb, vec3(0.3125, 0.5, 0.1875));
        rgb = max(mix(vec3(gray), rgb, p.color.z) * p.color.x + p.color.y, vec3(0.0));
        float alpha = texture(u_original, v_uv).a;
        return premultiply(vec4(aurea_srgb_to_linear(rgb), alpha));
    }
    return vec4(coefficient, 1.0);
}

void main(){
    if (p.color.w > 0.5) {
        o_color = compression_pass();
        return;
    }
    int stage=int(p.p3.x+.5);ivec2 xy=ivec2(gl_FragCoord.xy);ivec2 block=(xy/8)*8;ivec2 f=xy-block;vec3 c=vec3(0);
    if(stage==0){for(int x=0;x<8;++x)c+=basis(f.x,x)*source(ivec2(block.x+x,xy.y));}
    else if(stage==1){
        for(int y=0;y<8;++y)c+=basis(f.y,y)*texelFetch(u_tex0,ivec2(xy.x,block.y+y),0).rgb;
        int index=f.y*8+f.x;float quality=clamp(p.p0.x,1.,100.);float scale=quality<50.?5000./quality:200.-quality*2.;
        float q=clamp(floor((float(QT[index])*scale+50.)/100.),1.,255.);
        if(p.p1.y>.5&&index==int(p.p1.z))q=p.p1.w;
        uint seed=uint(p.p0.w)+uint(max(0.,p.p1.x))*40503u;
        if(aurea_hash(uvec2(uint(index),seed))<p.p2.x/64.)q=1.+floor(aurea_hash(uvec2(uint(index)+777u,seed))*p.p2.y);
        q/=255.;c=round(c/q)*q;
        float r=aurea_hash(uvec2(block)^uvec2(seed+uint(index)*31337u,seed));
        if(float(index)>=p.color.y&&float(index)<=p.color.z&&r<p.p0.y){
            vec3 damage=vec3(aurea_hash(uvec2(block)^uvec2(seed+1u,uint(index))),aurea_hash(uvec2(block)^uvec2(seed+2u,uint(index))),aurea_hash(uvec2(block)^uvec2(seed+3u,uint(index))))*2.-1.;
            c+=damage*p.p0.z*(index==0?4.:.8);
        }
        if(index==0)c+=vec3(aurea_hash(uvec2(block)^uvec2(seed,99u))-.5)*p.p2.w*4.;
    }else if(stage==2){for(int y=0;y<8;++y)c+=basis(y,f.y)*texelFetch(u_tex0,ivec2(xy.x,block.y+y),0).rgb;}
    else{
        for(int x=0;x<8;++x)c+=basis(x,f.x)*texelFetch(u_tex0,ivec2(block.x+x,xy.y),0).rgb;
        vec4 src=texture(u_original,v_uv);vec3 decoded=aurea_srgb_to_linear(clamp(yccRgb(c),0.,1.));
        o_color=mix(src,vec4(decoded*src.a,src.a),clamp(p.color.x,0.,1.));return;
    }
    o_color=vec4(c,1.);
}
