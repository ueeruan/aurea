vec4 aurea_main(vec2 uv) {
    vec4 straight = aurea_unpremultiply(texture(aurea_source, uv));
    vec3 rgb = mix(straight.rgb, paint(tone(aurea_luma(straight.rgb))).rgb, amount);
    vec4 past = aurea_unpremultiply(texture(aurea_history, uv));
    rgb = mix(rgb, past.rgb, echo * past.a);
    rgb *= 1.0 + aurea_audio(0.1) * AUREA_AUDIO_PEAK;
    if (overlay_connected) {
        vec4 top = texture(overlay, uv);
        rgb = mix(rgb, top.rgb, top.a * 0.5);
    }
    return vec4(rgb * straight.a, straight.a);
}
