// Same ABI as MeshPush. XYZ of the last three columns contain the inverse
// transpose of the model's linear transform. W contains cloud-only controls.
layout(push_constant) uniform CloudPush {
    mat4 model;
    vec4 normalCol0;
    vec4 normalCol1;
    vec4 normalCol2;
} pc;
