// Mira 3D: um anel no plano do nulo (perpendicular ao eixo Z dele), visto pela
// câmera da cena. Sem alvo ligado, o anel fica no centro da composição (z = 0).
vec4 aurea_main(vec2 uv) {
    vec4 base = texture(aurea_source, uv);
    vec3 center = aurea_layer_valid(target) ? aurea_layer_position(target) : vec3(aurea.comp.xy * 0.5, 0.0);
    vec3 normal = aurea_layer_valid(target) ? aurea_layer_axis(target, 2) : vec3(0.0, 0.0, 1.0);
    vec3 ro, rd;
    aurea_ray(uv, ro, rd);
    float facing = dot(rd, normal);
    if (abs(facing) < 1e-5) return base;
    float t = dot(center - ro, normal) / facing;
    if (t <= 0.0) return base;                       // o plano está atrás da câmera
    vec3 hit = ro + rd * t;
    float d = abs(length(hit - center) - radius);
    float ring = smoothstep(width, 0.0, d);
    float halo = glow * exp(-d / max(width * 4.0, 1.0)) * 0.35;
    vec3 light = tint.rgb * (ring + halo);
    return vec4(base.rgb + light * base.a + light * (1.0 - base.a), max(base.a, clamp(ring + halo, 0.0, 1.0)));
}
