// Onda RGB — exemplo de plugin "shader" de um passe.
//
// O cabeçalho do AUREA já declarou: aurea_source (a camada), o bloco `aurea`
// (resolução, tempo, camada, região) e uma macro por parâmetro do plugin.json:
// amount, wave, frequency, speed (float), direction (angle, em radianos) e
// tint (color, vec4 linear).

vec4 aurea_main(vec2 uv) {
    // Direção do deslocamento (0 = para a direita).
    vec2 dir = vec2(cos(direction), sin(direction));
    // A onda corre na vertical com o tempo; `wave` = 0 desliga a onda e deixa
    // o deslocamento constante.
    float phase = (uv.y * frequency - aurea.time.x * speed) * 2.0 * AUREA_PI;
    float w = mix(1.0, sin(phase), wave);
    // `amount` está em px da camada: aurea_px_to_uv converte para uv deste
    // passe (vale no preview reduzido e no export em 4K).
    vec2 offset = aurea_px_to_uv(dir * amount * w);
    vec4 r = texture(aurea_source, uv + offset);
    vec4 g = texture(aurea_source, uv);
    vec4 b = texture(aurea_source, uv - offset);
    // Cor linear PRÉ-MULTIPLICADA: cada canal já vem multiplicado pelo alfa
    // da amostra dele; o alfa do resultado é o maior dos três.
    vec4 c = vec4(r.r, g.g, b.b, max(max(r.a, g.a), b.a));
    return vec4(c.rgb * tint.rgb, c.a);
}
