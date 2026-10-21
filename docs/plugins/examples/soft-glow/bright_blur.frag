// Brilho suave, passe 1 de 2: as partes claras, desfocadas.
//
// Este passe roda em 1/4 da resolução ("scale": 0.25) e escreve na textura
// "bloom". O uv é o mesmo de todas as texturas do plugin: só a resolução muda.

vec4 aurea_main(vec2 uv) {
    // `radius` em px da camada → passo em uv deste passe (7 x 7 amostras).
    vec2 stepUv = aurea_px_to_uv(vec2(radius)) / 3.0;
    vec4 sum = vec4(0.0);
    float weight = 0.0;
    // Laço com limites FIXOS: o compilador do aparelho desenrola, e o custo
    // por pixel não depende de nenhum parâmetro.
    for (int y = -3; y <= 3; ++y) {
        for (int x = -3; x <= 3; ++x) {
            vec2 o = vec2(float(x), float(y));
            float w = exp(-dot(o, o) / 4.5);
            vec4 c = texture(aurea_source, uv + o * stepUv);
            // Limiar no valor de alfa reto (a cor guardada é pré-multiplicada).
            float bright = smoothstep(threshold, threshold + 0.1, aurea_luma(aurea_unpremultiply(c).rgb));
            sum += c * (bright * w);
            weight += w;
        }
    }
    return sum / weight;
}
