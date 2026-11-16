#ifndef AUREA_REFERENCE_BLUR_GLSL
#define AUREA_REFERENCE_BLUR_GLSL
#define BLUR_COPY 0
#define BLUR_GAUSSIAN 1
#define BLUR_RASTER_MOMENTS 2
#define BLUR_APPLY_MOMENTS 3
#define BLUR_ROLLING 0
#define BLUR_ZERO_FIRST_MOMENTS 1
#define BLUR_SECOND_MOMENT 2
struct BlurSegment {
    vec4 range;
    vec4 polynomial;
};
layout(set = 0, binding = AUREA_PARAMS, std140) uniform ReferenceBlurParams {
    vec4 geometry; // source origin xy, target origin xy, in grid pixels
    vec4 sizes;    // source width/height, target width/height
    vec4 settings; // axis, radius in texels, repeat source edges, operation
    vec4 control;  // tree power of two, segment count, tree level/Gaussian radius, moment
    vec4 uvMap;
    vec4 weights;
    vec4 edge;
#ifndef AUREA_REFERENCE_BLUR_COMPUTE
    BlurSegment segments[24];
#endif
}
p;
ivec2 blurPoint(int position, int line) {
    return p.settings.x < .5 ? ivec2(position, line) : ivec2(line, position);
}
int blurAxisLength() {
    return int(p.settings.x < .5 ? p.sizes.x : p.sizes.y);
}
int blurCrossLength() {
    return int(p.settings.x < .5 ? p.sizes.y : p.sizes.x);
}
vec4 blurFetch(sampler2D source, int position, int line, bool repeatEdges) {
    int length = blurAxisLength();
    if (line < 0 || line >= blurCrossLength())
        return vec4(0);
    if (!repeatEdges && (position < 0 || position >= length))
        return vec4(0);
    return texelFetch(source, blurPoint(clamp(position, 0, length - 1), line), 0);
}
#endif
