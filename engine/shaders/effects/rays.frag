#version 450
// Integrate native thresholded radiance over each authored radial segment.
// Sparse endpoint taps used to miss one-pixel emitters at full resolution.
#include "../common/bindings.glsl"
#include "common.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0; // intensity, length, threshold, decay
    vec4 p1; // output light UV, authored segments, knee
    vec4 p2; // keep source, hue, mode (1 extract / 2 cached / 0 direct), actual backend capability
    vec4 p3;
    vec4 color;
} p;

vec3 nativeRadiance(ivec2 xy) {
    ivec2 size = textureSize(u_tex0, 0);
    if (any(lessThan(xy, ivec2(0))) || any(greaterThanEqual(xy, size))) return vec3(0.0);
    vec4 sampleColor = unpremultiply(texelFetch(u_tex0, xy, 0));
    vec3 rgb = max(sampleColor.rgb, vec3(0.0));
    float knee = max(p.p1.w, 1e-4);
    float gate = smoothstep(clamp(p.p0.z, 0.0, 1.0) - knee,
                            clamp(p.p0.z, 0.0, 1.0) + knee,
                            aurea_luma(aurea_linear_to_srgb(rgb)));
    return rgb * (gate * sampleColor.a);
}

vec3 fieldRadiance(ivec2 xy, ivec2 size, bool padded) {
    if (any(lessThan(xy,ivec2(0))) || any(greaterThanEqual(xy,size))) return vec3(0.0);
    return padded ? texelFetch(u_tex1,xy+ivec2(1),0).rgb : nativeRadiance(xy);
}

vec3 pointRadiance(vec2 uv, vec2 size, bool padded) {
    vec2 point=uv*size-.5;
    ivec2 cell=ivec2(floor(point));vec2 f=fract(point);
    ivec2 dimensions=ivec2(size);
    return mix(mix(fieldRadiance(cell,dimensions,padded),fieldRadiance(cell+ivec2(1,0),dimensions,padded),f.x),
               mix(fieldRadiance(cell+ivec2(0,1),dimensions,padded),fieldRadiance(cell+ivec2(1,1),dimensions,padded),f.x),f.y);
}

// Exact integral of the bilinear native field over each crossed texel cell.
// All four coefficients are nonnegative. No sparse quadrature or differences
// between approximate prefix sums can lose an emitter or create negative light.
vec3 integrateCells(vec2 uv,vec2 path,vec2 size,bool padded) {
    vec2 point=uv*size-.5, delta=path*size;
    ivec2 direction=ivec2(sign(delta));
    ivec2 cell=ivec2(floor(point));
    if (direction.x<0 && point.x==floor(point.x)) --cell.x;
    if (direction.y<0 && point.y==floor(point.y)) --cell.y;
    ivec2 dimensions=ivec2(size);
    float t=0.0;vec3 integral=vec3(0.0);
    int bound=int(ceil(abs(delta.x))+ceil(abs(delta.y)))+4;
    for(int iteration=0;iteration<bound && t<1.0;++iteration) {
        float tx=direction.x==0?2.0:(float(cell.x+(direction.x>0?1:0))-point.x)/delta.x;
        float ty=direction.y==0?2.0:(float(cell.y+(direction.y>0?1:0))-point.y)/delta.y;
        float stop=min(1.0,min(tx,ty));
        if(stop>t) {
            vec2 a=clamp(point+delta*t-vec2(cell),0.0,1.0);
            vec2 b=clamp(point+delta*stop-vec2(cell),0.0,1.0);
            vec2 d=b-a, mean=(a+b)*.5;
            float product=a.x*a.y+(a.x*d.y+a.y*d.x)*.5+d.x*d.y/3.0;
            vec4 coefficients=max(vec4(1.0-mean.x-mean.y+product,mean.x-product,mean.y-product,product),vec4(0.0));
            coefficients/=max(dot(coefficients,vec4(1.0)),1e-12);
            integral+=(fieldRadiance(cell,dimensions,padded)*coefficients.x+
                       fieldRadiance(cell+ivec2(1,0),dimensions,padded)*coefficients.y+
                       fieldRadiance(cell+ivec2(0,1),dimensions,padded)*coefficients.z+
                       fieldRadiance(cell+ivec2(1,1),dimensions,padded)*coefficients.w)*(stop-t);
            t=stop;
        }
        // Compare with the same computed boundary, without a tolerance that
        // would skip a short cell on a nearly diagonal ray.
        if(tx<=stop)cell.x+=direction.x;
        if(ty<=stop)cell.y+=direction.y;
    }
    return integral;
}

// Clip to the bilinear support of native texels. Transparent parts of a
// segment retain their fraction of its weight, but need no texture fetches.
bool clipSegment(vec2 start, vec2 step, vec2 size, out float first, out float last) {
    first = 0.0; last = 1.0;
    vec2 low = -.5 / size, high = vec2(1.0) + .5 / size;
    for (int axis = 0; axis < 2; ++axis) {
        if (abs(step[axis]) < 1e-12) {
            if (start[axis] < low[axis] || start[axis] > high[axis]) return false;
        } else {
            float a = (low[axis] - start[axis]) / step[axis];
            float b = (high[axis] - start[axis]) / step[axis];
            first = max(first, min(a,b)); last = min(last, max(a,b));
        }
    }
    return last > first;
}

void main() {
    vec2 sourceSize = vec2(textureSize(u_tex0,0));
    if (p.p2.z > .5 && p.p2.z < 1.5) {
        ivec2 xy = ivec2(floor(v_uv * (sourceSize + 2.0))) - ivec2(1);
        o_color = vec4(nativeRadiance(xy), 0.0);
        return;
    }
    vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec4 src = unpremultiply(texture(u_tex0, inUv));
    vec2 step = (p.p1.xy - v_uv) * clamp(p.p0.y,0.0,1.0) * p.uvMap.xy / max(p.p1.z,1.0);
    bool padded = p.p2.z > 1.5;
    float weight = 1.0, wsum = 0.0;
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 64; ++i) {
        if (float(i) >= p.p1.z) break;
        vec2 start = inUv + float(i) * step;
        float first, last;
        vec3 segment = vec3(0.0);
        if (dot(step,step) < 1e-20) {
            segment = pointRadiance(start, sourceSize, padded);
        } else if (clipSegment(start,step,sourceSize,first,last)) {
            vec2 path = step * (last-first);
            // Hardware anisotropic footprints use driver-specific tap
            // weights; they changed narrow HDR emitters and their alpha.
            // The positive analytic native-cell integral is shared by both
            // paths. Padded radiance avoids repeated threshold/gamma work;
            // the direct path supplies the identical field on demand.
            segment=integrateCells(start+step*first,path,sourceSize,padded)*(last-first);
        }
        acc += segment * weight;
        wsum += weight;
        weight *= 1.0-clamp(p.p0.w,0.0,1.0)*.35;
    }
    acc *= p.p0.x/max(wsum,1e-4);
    vec3 tint = max(p.color.rgb,vec3(0.0));
    vec3 rays = p.p2.x > .5 ? acc*tint : vec3(aurea_luma(acc))*tint;
    if (abs(p.p2.y)>1e-4) {
        float c=cos(radians(p.p2.y)),s=sin(radians(p.p2.y));
        rays=vec3(rays.r*c-rays.g*s,rays.r*s+rays.g*c,rays.b);
    }
    rays=max(rays,vec3(0.0));
    float coverage=clamp(max(rays.r,max(rays.g,rays.b)),0.0,1.0);
    o_color=vec4(src.rgb*src.a+rays,src.a+coverage*(1.0-src.a));
}
