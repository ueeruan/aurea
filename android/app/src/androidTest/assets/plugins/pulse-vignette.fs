/*{
    "DESCRIPTION": "Vinheta que pulsa no tempo (exemplo de importacao ISF no AUREA)",
    "CREDIT": "AUREA (exemplo)",
    "ISFVSN": "2",
    "CATEGORIES": ["Stylize"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "strength", "TYPE": "float", "DEFAULT": 0.6, "MIN": 0.0, "MAX": 1.0, "LABEL": "Strength" },
        { "NAME": "rate", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 8.0, "LABEL": "Pulse rate" },
        { "NAME": "center", "TYPE": "point2D", "DEFAULT": [0.5, 0.5], "MIN": [0.0, 0.0], "MAX": [1.0, 1.0], "LABEL": "Center" },
        { "NAME": "edgeColor", "TYPE": "color", "DEFAULT": [0.0, 0.0, 0.0, 1.0], "LABEL": "Edge color" },
        { "NAME": "invert", "TYPE": "bool", "DEFAULT": false, "LABEL": "Invert" }
    ]
}*/

void main() {
    vec4 src = IMG_NORM_PIXEL(inputImage, isf_FragNormCoord);
    float pulse = 0.5 + 0.5 * sin(TIME * rate * 6.2831853);
    float d = distance(isf_FragNormCoord, center);
    float v = smoothstep(0.2, 0.8, d) * strength * (0.6 + 0.4 * pulse);
    if (invert) v = 1.0 - v;
    gl_FragColor = vec4(mix(src.rgb, edgeColor.rgb, v), src.a);
}
