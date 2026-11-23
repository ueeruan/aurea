#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D tex1;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap,texel,p0,p1,p2,p3,color;
    vec4 rays[8],colors[15],mapTypes,region,size,circle;
} p;

const vec3 scatterLuma = vec3(.3125,.5,.1875);

vec4 scatterDecode(vec4 value) {
    return premultiply(vec4(aurea_srgb_to_linear(max(unpremultiply(value).rgb,vec3(0.0))),value.a));
}

vec3 traceEdgeField() {
    vec2 offset = (p.circle.xy - v_uv) * p.circle.z;
    vec3 sum = vec3(0.0);
    float total = 0.0;
    const int taps = 48;
    bool sharedLength = abs(p.rays[0].x-p.rays[0].y) + abs(p.rays[0].x-p.rays[0].z) < 1e-6;
    for (int i = 0; i < taps; ++i) {
        float t = (float(i) + .5) / float(taps);
        float weight = mix(1.0-t,t,p.circle.w);
        if (sharedLength) {
            sum += vec3(texture(tex0,v_uv+offset*(t*p.rays[0].x)).r) * weight;
        } else {
            sum += vec3(texture(tex0,v_uv+offset*(t*p.rays[0].x)).r,
                        texture(tex0,v_uv+offset*(t*p.rays[0].y)).r,
                        texture(tex0,v_uv+offset*(t*p.rays[0].z)).r) * weight;
        }
        total += weight;
    }
    vec2 delta = v_uv - p.circle.xy;
    float angle = (abs(delta.x)+abs(delta.y)>1e-6) ? atan(delta.y,delta.x) : 0.0;
    float modulation = 1.0 + .16*p.p1.x*sin(angle*p.p1.y + 4.0*aurea_fbm(delta*14.0,3,.5));
    return sum * max(modulation,0.0) / max(total,1e-6);
}

vec3 leakLobe(vec2 point, vec2 center, vec2 radius, float angle, float seed, vec3 inner, vec3 outer) {
    vec2 delta = point - center;
    float cs = cos(angle), sn = sin(angle);
    vec2 plane = vec2(dot(delta, vec2(cs, sn)), dot(delta, vec2(-sn, cs))) / radius;
    float grain = aurea_fbm(plane * 2.0 + vec2(seed, 7.0), 3, 0.57);
    float dist = max(0.0, length(plane) + (grain - .5) * .47);
    float shade = smoothstep(0.0, 1.15, dist);
    return mix(inner, outer, shade) * exp2(-1.65 * dist * dist);
}

vec4 filmLightLeak(vec4 src) {
    vec2 point = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 size = vec2(p.p1.z, p.p1.z * p.p1.w);
    vec2 center = p.p1.xy;
    float seed = p.p2.z * 29.0;
    vec3 leak = leakLobe(point, center + vec2(-.65, -.09), size * vec2(.53, .86), -.12, seed,
                        vec3(.33, .26, .08), vec3(.18, .12, .046));
    leak += leakLobe(point, center + vec2(.21, .10), size * vec2(.67, .92), -.27, seed + 11.0,
                     vec3(.29, .067, .028), vec3(.074, .056, .038));
    leak += leakLobe(point, center + vec2(.61, .29 + .12 * p.p2.w),
                     size * vec2(.45, .54), .32, seed + 31.0, vec3(.24, .18, .14), vec3(.09, .048, .06));
    // Periodic, continuous at the boundary, and independent of frame order.
    float t = p.p2.x * (6.28318530718 / 128.0);
    float pulse = .7 * sin(97.0 * t + seed) + .3 * sin(53.0 * t + seed * .81);
    leak *= p.p0.x * max(0.0, 1.0 + pulse * p.p2.y) * aurea_linear_to_srgb(max(p.color.rgb, vec3(0.0)));
    float coverage = clamp(max(leak.r, max(leak.g, leak.b)) * p.p0.z, 0.0, 1.0);
    if (p.p0.w > 1.5) return scatterDecode(vec4(leak, coverage));
    vec3 encoded = aurea_linear_to_srgb(max(unpremultiply(src).rgb, vec3(0.0))) * src.a * p.p0.y;
    vec3 color = p.p0.w > .5 ? encoded + leak :
        encoded + clamp(leak, 0.0, 1.0) * (1.0 - clamp(encoded, 0.0, 1.0));
    return scatterDecode(vec4(color, src.a + coverage * (1.0 - src.a)));
}

vec4 scatterLight() {
    int kind = int(p.p3.w+.5);
    int stage = int(p.p3.x+.5);
    vec4 source = texture(tex0,v_uv);
    if (stage == 7) return filmLightLeak(source);
    if (stage == 0) {
        vec3 signal = aurea_linear_to_srgb(max(unpremultiply(source).rgb,vec3(0.0)));
        return vec4(signal*source.a,source.a);
    }
    if (stage == 1) {
        if (kind == 3) {
            vec2 stepUV = p.texel.xy;
            vec2 dx = vec2(stepUV.x,0.0), dy = vec2(0.0,stepUV.y);
            vec2 first = .5*stepUV, last = 1.0-first;
            vec3 gx = texture(tex0,clamp(v_uv+dx,first,last)).rgb - texture(tex0,clamp(v_uv-dx,first,last)).rgb;
            vec3 gy = texture(tex0,clamp(v_uv+dy,first,last)).rgb - texture(tex0,clamp(v_uv-dy,first,last)).rgb;
            float edge = clamp(length(vec2(dot(gx,scatterLuma),dot(gy,scatterLuma)))*p.p0.x*.5,0.0,1.0);
            return vec4(vec3(edge),edge);
        }
        vec3 color = unpremultiply(source).rgb;
        vec3 gateColor = max(color-aurea_linear_to_srgb(max(p.color.rgb,vec3(0.0))),vec3(0.0));
        float peak = max(max(gateColor.r,gateColor.g),gateColor.b);
        peak = mix(peak,source.a,p.p0.y);
        float gate = max(0.0,peak-p.p0.x) / max(.04,1.0-p.p0.x);
        vec3 emitter = kind == 2 ? mix(vec3(1.0),color,.3) : color;
        vec3 energy = mix(emitter,vec3(1.0),p.p0.y) * gate * source.a;
        return vec4(energy,max(max(energy.r,energy.g),energy.b));
    }
    if (stage == 2) {
        if (kind == 3) {
            vec3 energy = traceEdgeField();
            return vec4(energy,max(max(energy.r,energy.g),energy.b));
        }
        vec3 weights0 = vec3(p.colors[0].x,p.colors[1].x,p.colors[2].x);
        vec3 weights1 = vec3(p.colors[0].y,p.colors[1].y,p.colors[2].y);
        vec3 weights2 = vec3(p.colors[0].z,p.colors[1].z,p.colors[2].z);
        vec2 stepUV = p.rays[0].xy;
        vec3 energy = weights0*source.rgb
            + weights1*(texture(tex0,v_uv+stepUV).rgb+texture(tex0,v_uv-stepUV).rgb)
            + weights2*(texture(tex0,v_uv+2.0*stepUV).rgb+texture(tex0,v_uv-2.0*stepUV).rgb);
        return vec4(energy,max(max(energy.r,energy.g),energy.b));
    }
    if (stage == 3) return source*p.p1.x+texture(tex1,v_uv)*p.p1.y;
    if (stage == 5) {
        vec3 energy = source.rgb * (p.p0.y>.5 ? p.p0.x : 1.0);
        float alpha = p.p0.y<.5 ? 1.0 : clamp(max(max(energy.r,energy.g),energy.b),0.0,1.0);
        return scatterDecode(vec4(energy,alpha));
    }
    if (stage == 6) {
        // Disperse the two halves of each ray independently so highlights emit
        // a spectrum along the ray instead of shifting the entire input.
        vec2 redOffset = p.rays[1].xy * p.p2.x;
        vec2 greenOffset = p.rays[1].xy * p.p2.y;
        vec2 blueOffset = p.rays[1].xy * p.p2.z;
        vec3 energy = 0.5 * vec3(
            texture(tex0, v_uv + redOffset).r + texture(tex0, v_uv - redOffset).r,
            texture(tex0, v_uv + greenOffset).g + texture(tex0, v_uv - greenOffset).g,
            texture(tex0, v_uv + blueOffset).b + texture(tex0, v_uv - blueOffset).b);
        return vec4(energy, max(energy.r, max(energy.g, energy.b)));
    }

    vec3 energy = texture(tex1,v_uv).rgb * p.p0.x;
    if (kind == 1) energy *= vec3(2.25,1.8,1.5);
    if (kind == 4) energy *= 1.8;
    vec3 tint = aurea_linear_to_srgb(max(p.color.rgb,vec3(0.0)));
    energy = max(mix(vec3(dot(energy,scatterLuma)),energy,p.p1.y),vec3(0.0))*tint;
    source *= p.p0.y;
    source.rgb *= p.p0.z;
    energy *= mix(1.0,1.0-source.a,p.p1.x);
    float coverage = clamp(max(max(energy.r,energy.g),energy.b)*p.p0.w,0.0,1.0);
    vec3 signal = source.rgb + clamp(energy,0.0,1.0)*(1.0-clamp(source.rgb,0.0,1.0));
    return scatterDecode(vec4(signal,source.a+coverage*(1.0-source.a)));
}

vec3 mapColor(int index,float t) {
    int count=int(p.mapTypes[index]);
    if(count==0) return p.colors[index*5].rgb;
    float position=clamp(t,0.0,1.0)*float(count==1?2:4);
    int a=int(floor(position)),b=min(a+1,count==1?2:4);
    if(count==1) {a*=2;b*=2;}
    return mix(p.colors[index*5+a].rgb,p.colors[index*5+b].rgb,fract(position));
}
float extractBright(vec2 uv) {
    vec4 value=texture(tex0,uv);
    vec3 encoded=aurea_linear_to_srgb(max(unpremultiply(value).rgb,vec3(0)));
    int channel=int(p.p2.w);
    float tone=dot(encoded,vec3(.2126,.7152,.0722));
    if(channel==0) tone=(max(encoded.r,max(encoded.g,encoded.b))+min(encoded.r,min(encoded.g,encoded.b)))*.5;
    if(channel>=2&&channel<=4) tone=encoded[channel-2];
    if(channel==5) tone=value.a;
    float soft=max(.0001,p.p0.w);
    return max(0.0,tone-p.p0.z)*smoothstep(p.p0.z-soft,p.p0.z+soft,tone)*value.a;
}
void main() {
    if (p.p3.w>.5) { o_color=scatterLight(); return; }
    int mode=int(p.p3.x);
    vec2 point=p.region.xy+v_uv*p.region.zw;
    if(mode==0) {
        vec2 uv=v_uv*p.uvMap.xy+p.uvMap.zw;
        vec2 offset=p.texel.xy*.25;
        float light=.25*(extractBright(uv+vec2(-offset.x,-offset.y))+
                         extractBright(uv+vec2( offset.x,-offset.y))+
                         extractBright(uv+vec2(-offset.x, offset.y))+
                         extractBright(uv+vec2( offset.x, offset.y)));
        if(p.p3.z>.5) light*=1.0-smoothstep(max(0.0,p.circle.z-p.circle.w),p.circle.z+max(.0001,p.circle.w),length(point-p.circle.xy));
        o_color=vec4(vec3(light),light);return;
    }
    if(mode==1) {
        int ray=int(p.p3.y);vec4 settings=p.rays[ray];
        vec3 sum=vec3(0);
        if(settings.z>0.001) {
            // Gauss-like concentration near the origin, long one-sided tail.
            // Quadrature and attenuation are independently authored estimates.
            for(int i=0;i<48;i++) {
                float t=(float(i)+.5)/48.0;
                float distance=t*t*settings.z;
                vec2 uv=v_uv-settings.xy*distance/p.region.zw;
                float intensity=texture(tex0,uv).r;
                float shimmer=1.0+p.p1.x*sin(6.2831853*(p.p1.z+t*p.p1.y+float(ray)*.173)+dot(point/p.size.xy,vec2(3.1,4.7)));
                sum+=mapColor(clamp(int(settings.w),0,2),t)*intensity*exp(-4.0*t)*max(0.0,shimmer)*2.0*t;
            }
            sum*=p.p0.y*settings.z/48.0;
        }
        vec4 previous=ray==0?vec4(0):texture(tex1,v_uv);
        sum+=previous.rgb;
        o_color=vec4(sum,clamp(max(sum.r,max(sum.g,sum.b)),0.0,1.0));return;
    }
    vec4 source=texture(tex0,v_uv*p.uvMap.xy+p.uvMap.zw)*p.p2.x;
    vec4 glow=texture(tex1,v_uv)*p.p2.y;
    vec3 result;
    int blend=int(p.p2.z);
    if(blend==1) result=source.rgb+glow.rgb;
    else if(blend==2) result=glow.rgb+source.rgb*(1.0-glow.a);
    else if(blend==3) result=max(source.rgb,glow.rgb);
    else result=source.rgb+glow.rgb-source.rgb*clamp(glow.rgb,0.0,1.0);
    o_color=vec4(result,clamp(source.a+glow.a-source.a*glow.a,0.0,1.0));
}
