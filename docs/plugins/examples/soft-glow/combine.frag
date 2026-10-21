// Brilho suave, passe 2 de 2: soma o brilho de volta à imagem.
//
// `bloom` é a entrada nomeada declarada em "inputs" (o target do passe 1).

vec4 aurea_main(vec2 uv) {
    vec4 base = texture(aurea_source, uv);
    vec4 glow = texture(bloom, uv) * intensity;
    vec3 rgb = base.rgb + glow.rgb * tint.rgb;
    // O brilho também cobre o que era transparente em volta da camada.
    float a = clamp(base.a + glow.a * (1.0 - base.a), 0.0, 1.0);
    return vec4(rgb, a);
}
