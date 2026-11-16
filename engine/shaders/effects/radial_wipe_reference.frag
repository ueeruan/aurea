#version 450
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_source;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_mask;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    ivec4 normals;
    ivec4 controls;
} p;

int coverage(ivec2 values) {
    bool start = values.x < p.controls.x;
    bool end = values.y >= p.controls.y;
    return (p.controls.z == 0 ? start || end : start && end) ? 1 : 0;
}

float mask_value() {
    ivec2 pixel = ivec2(floor(v_uv * vec2(textureSize(u_source, 0))));
    ivec2 stepX = p.normals.xz;
    ivec2 stepY = p.normals.yw;
    ivec2 values = stepX * pixel.x + stepY * pixel.y;
    ivec2 corner = values - (stepX >> 1) - (stepY >> 1);
    ivec2 corners[4] = ivec2[4](corner, corner + stepX,
        corner + stepX + stepY, corner + stepY);
    bool boundary = false;
    for (int edge = 0; edge < 4; ++edge) {
        ivec2 first = corners[edge];
        ivec2 second = corners[(edge + 1) % 4];
        boundary = boundary || ((first.x < p.controls.x) != (second.x < p.controls.x))
            || ((first.y < p.controls.y) != (second.y < p.controls.y));
    }
    if (!boundary) return float(coverage(values));
    ivec2 incrementX = stepX / 16;
    ivec2 incrementY = stepY / 16;
    ivec2 rowIncrement = incrementY + 16 * incrementX - stepX;
    int samples = 0;
    for (int y = 0; y < 16; ++y) {
        ivec2 sampleValue = corner + y * rowIncrement;
        for (int x = 0; x < 16; ++x) {
            samples += coverage(sampleValue);
            sampleValue += incrementX;
        }
    }
    // The observed native floating-point path rounds its 256-sample count
    // to binary mask coverage before applying the spatial feather.
    return float((samples + 128) / 256);
}

void main() {
    if (p.controls.w == 1) {
        o_color = texture(u_source, v_uv) * texture(u_mask, v_uv).r;
        return;
    }
    float keep = mask_value();
    o_color = p.controls.w == 0 ? vec4(keep) : texture(u_source, v_uv) * keep;
}
