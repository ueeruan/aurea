#version 450
#include "common/scene.glsl"
#include "common/hdr.glsl"
#include "common/cloud_push.glsl"

layout(location = 0) in vec3 v_local;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_scene;

float cloud_hash(vec3 p) {
    p = fract(p * 0.1031 + vec3(0.11, 0.37, 0.73));
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float cloud_noise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = mix(cloud_hash(i), cloud_hash(i + vec3(1, 0, 0)), f.x);
    float b = mix(cloud_hash(i + vec3(0, 1, 0)), cloud_hash(i + vec3(1, 1, 0)), f.x);
    float c = mix(cloud_hash(i + vec3(0, 0, 1)), cloud_hash(i + vec3(1, 0, 1)), f.x);
    float d = mix(cloud_hash(i + vec3(0, 1, 1)), cloud_hash(i + vec3(1, 1, 1)), f.x);
    return mix(mix(a, b, f.y), mix(c, d, f.y), f.z);
}

float cloud_envelope(vec3 p) {
    float base = length((p - vec3(0.0, -0.12, 0.0)) / vec3(0.42, 0.20, 0.34));
    float crown = length((p - vec3(-0.08, 0.11, -0.04)) / vec3(0.28, 0.32, 0.27));
    float side = length((p - vec3(0.18, 0.02, 0.07)) / vec3(0.22, 0.25, 0.24));
    return 1.0 - min(base, min(crown, side));
}

float cloud_density(vec3 p, float detail, bool shadow) {
    float edge = 0.5 - max(abs(p.x), max(abs(p.y), abs(p.z)));
    if (edge <= 0.0) return 0.0;
    float core = cloud_envelope(p);
    if (core < -0.32) return 0.0;
    float erosion = cloud_noise(8.0 * p + 17.0) - 0.5;
    if (!shadow) {
        erosion += (cloud_noise(19.0 * p + 31.0) - 0.5) * (0.3 + 0.45 * detail);
        erosion += (cloud_noise(44.0 * p + 43.0) - 0.5) * (0.12 + 0.24 * detail);
    }
    float fill = smoothstep(-0.13, 0.44, core + erosion * (0.46 + 0.35 * detail));
    // Delay the transition to zero at the bounding box to avoid geometric edges.
    return fill * smoothstep(0.0, 0.065, edge) * smoothstep(-0.36, -0.18, p.y);
}

// Ray segment from a point already on or inside the unit box.
float cloud_exit(vec3 p, vec3 ray) {
    vec3 bound = mix(vec3(-0.5), vec3(0.5), greaterThanEqual(ray, vec3(0.0)));
    vec3 safeRay = mix(vec3(-1.0), vec3(1.0), greaterThanEqual(ray, vec3(0.0))) * max(abs(ray), vec3(1e-5));
    vec3 distances = (bound - p) / safeRay;
    return max(0.0, min(distances.x, min(distances.y, distances.z)));
}

void main() {
    mat3 worldToLocal = transpose(mat3(pc.normalCol0.xyz, pc.normalCol1.xyz, pc.normalCol2.xyz));
    vec3 eye = worldToLocal * (u.cameraPos.xyz - pc.model[3].xyz);
    bool orthographic = pc.normalCol1.w > 0.5;
    vec3 cameraForward = -vec3(u.viewProj[0].z, u.viewProj[1].z, u.viewProj[2].z);
    vec3 exitPoint = clamp(v_local, vec3(-0.5), vec3(0.5));
    vec3 fromEye = exitPoint - eye;
    vec3 ray = normalize(orthographic ? worldToLocal * cameraForward : fromEye);
    float distanceInside = cloud_exit(exitPoint, -ray);
    if (!orthographic) distanceInside = min(distanceInside, length(fromEye));
    vec3 entry = exitPoint - distanceInside * ray;
    vec4 clipEntry = u.viewProj * (pc.model * vec4(entry, 1.0));
    float entryOutside = clipEntry.z - clipEntry.w; // reverse-Z near plane
    if (entryOutside > 0.0) {
        vec4 clipExit = u.viewProj * (pc.model * vec4(exitPoint, 1.0));
        float clipped = clamp(entryOutside / max(1e-6, entryOutside - (clipExit.z - clipExit.w)), 0.0, 1.0);
        entry = mix(entry, exitPoint, clipped);
        distanceInside *= 1.0 - clipped;
    }
    float extinction = 15.0 * clamp(u.mr.x, 0.0, 1.0);
    if (extinction < 0.001 || distanceInside < 0.0001) discard;

    vec3 lightDir = normalize(vec3(-0.45, -0.78, -0.32));
    vec3 lightColor = vec3(1.14, 1.02, 0.9);
    float best = 0.0;
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= int(u.lightCount.x)) break;
        vec4 light = u.lightPos[i];
        vec3 toLight = light.w < 0.5 ? light.xyz : light.xyz - pc.model[3].xyz;
        float distanceSquared = dot(toLight, toLight);
        float distanceToLight = sqrt(distanceSquared);
        vec3 toLightDir = distanceToLight > 0.001 ? toLight / distanceToLight : lightDir;
        // The engine packs point/spot intensity in pixel space (scaled by
        // pixels-per-meter squared). Apply inverse-square falloff in the same
        // units. A finite core avoids a singularity inside the cloud.
        float attenuation = light.w < 0.5 ? 1.0 : 1.0 / max(1.0, distanceSquared + u.lightSpot2[i].w);
        if (light.w >= 0.5 && u.lightColor[i].w > 0.0) {
            float r2 = distanceSquared / (u.lightColor[i].w * u.lightColor[i].w);
            float window = clamp(1.0 - r2 * r2, 0.0, 1.0);
            attenuation *= window * window;
        }
        if (light.w > 1.5)
            attenuation *= smoothstep(u.lightSpot[i].w - 0.001,
                                      max(u.lightSpot[i].w + 0.001, u.lightSpot2[i].x),
                                      dot(normalize(u.lightSpot[i].xyz), -toLightDir));
        vec3 candidate = max(u.lightColor[i].rgb, vec3(0.0)) * attenuation;
        float energy = dot(candidate, vec3(0.2126, 0.7152, 0.0722));
        if (energy > best) {
            best = energy;
            lightDir = toLightDir;
            // Keep dense scattering readable under very bright local lights,
            // preserving hue and headroom instead of clipping the volume.
            float peak = max(candidate.r, max(candidate.g, candidate.b));
            lightColor = candidate * (0.48 / (1.0 + max(0.0, peak - 1.5) / 1.5));
        }
    }
    if (u.lightCount.x > 0.5 && best <= 1e-5) lightColor = vec3(0.0);
    bool directLight = max(lightColor.r, max(lightColor.g, lightColor.b)) > 1e-5;
    vec3 sun = normalize(worldToLocal * lightDir);
    float phase = 0.55 + 0.55 * pow(clamp(0.5 + 0.5 * dot(normalize(mat3(pc.model) * ray), lightDir), 0.0, 1.0), 4.0);
    vec3 ambient = vec3(0.22, 0.26, 0.35) * max(0.25, u.envParams.x) + u.lightCount.yzw;
    int steps = clamp(int(pc.normalCol0.w), 16, 48);
    float stepSize = distanceInside / float(steps);
    float detail = clamp(u.mr.y, 0.0, 1.0);
    float transmittance = 1.0;
    vec3 scattered = vec3(0.0);
    vec3 front = entry;
    bool visible = false;
    for (int i = 0; i < 48; ++i) {
        if (i >= steps || transmittance < 0.012) break;
        vec3 p = entry + ((float(i) + 0.5) * stepSize) * ray;
        float sigma = cloud_density(p, detail, false) * extinction;
        if (sigma < 0.001) continue;
        float alpha = 1.0 - exp(-sigma * stepSize);
        if (!visible && alpha > 0.003) { front = p; visible = true; }
        vec3 lighting = ambient;
        if (directLight) {
            float toSun = cloud_exit(p, sun) / 3.0;
            float opticalDepth = cloud_density(p + sun * (0.5 * toSun), detail, true)
                               + cloud_density(p + sun * (1.5 * toSun), detail, true)
                               + cloud_density(p + sun * (2.5 * toSun), detail, true);
            lighting += lightColor * (0.12 + phase * exp(-opticalDepth * toSun * extinction * 0.75));
        }
        scattered += transmittance * alpha * lighting;
        transmittance *= 1.0 - alpha;
    }
    float alpha = (1.0 - transmittance) * clamp(u.baseColor.a, 0.0, 1.0);
    if (alpha < 0.002) discard;
    vec4 clip = u.viewProj * (pc.model * vec4(front, 1.0));
    gl_FragDepth = clamp(clip.z / max(clip.w, 0.00001), 0.0, 1.0);
    vec3 color = scattered * u.baseColor.rgb * u.baseColor.a * u.cameraPos.w;
    o_color = vec4(pc.normalCol2.w > 0.5 ? vec3(0.0) : color, alpha);
    o_scene = vec4(aurea_hdr_encode(color), alpha);
}
