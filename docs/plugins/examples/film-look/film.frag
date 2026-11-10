// Filme (LUT + grão): exemplo de imagens do pacote lidas com getFile (0.4.6).
// O plugin trabalha em sRGB ("colorSpace": "srgb"): a curva é indexada pelo
// valor sRGB de cada canal, como num editor de imagem.
vec4 aurea_main(vec2 uv) {
    vec4 c = texture(aurea_source, uv);
    if (c.a <= 0.0) return c;
    vec3 graded = vec3(
        texture(getFile(curve), vec2(c.r, 0.5)).r,
        texture(getFile(curve), vec2(c.g, 0.5)).g,
        texture(getFile(curve), vec2(c.b, 0.5)).b);
    vec3 rgb = mix(c.rgb, graded, amount);
    // Grão: o tile de 128 px repetido em px da camada; muda a cada quadro.
    vec2 px = uv * aurea.layer.xy / max(grainSize, 0.5);
    vec2 jitter = vec2(fract(aurea.time.y * 0.6180339), fract(aurea.time.y * 0.7548777)) * 128.0;
    float n = texture(getFile(grainTex), (px + jitter) / 128.0).r - 0.5;
    // Mais grão nos meios-tons (como o filme), quase nada no preto e no branco.
    float l = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb += n * grain * 0.35 * (1.0 - abs(l * 2.0 - 1.0));
    return vec4(clamp(rgb, 0.0, 1.0), c.a);
}
