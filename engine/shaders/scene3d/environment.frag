#version 450
#include "../common/bindings.glsl"
#include "common/hdr.glsl"
layout(location=0) in vec2 v_uv;
// MRT do passe 3D: 0 = 2D de exibição (o céu só tapa o que está atrás), 1 = cena HDR.
layout(location=0) out vec4 o_color;
layout(location=1) out vec4 o_scene;
layout(set=0,binding=AUREA_TEX0) uniform samplerCube panorama;
layout(set=0,binding=AUREA_TEX1) uniform samplerCube blurred;   // especular pré-filtrado (desfoque forte)
// projection = (aspecto, tan(fov/2), escala das derivadas, LOD do especular);
// light = (giro, intensidade, exposição, mistura do especular);
// post = (exposição do grupo, operador de tone map, 1 = saída direta, -).
layout(std140,set=0,binding=AUREA_PARAMS) uniform Sky { mat4 view; vec4 projection; vec4 light; vec4 post; } u;
void main() {
    vec2 ndc = v_uv*2.0-1.0;
    vec3 world = transpose(mat3(u.view))*normalize(vec3(ndc.x*u.projection.x*u.projection.y,ndc.y*u.projection.y,1.0));
    vec3 d = vec3(world.x,-world.y,-world.z);
    float c=cos(u.light.x), s=sin(u.light.x);
    d=vec3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
    // LOD pela pegada real do pixel (derivadas da direção): nem pixelado nem
    // serrilhado. O desfoque escala as derivadas e, forte, cai no especular.
    vec3 bg = textureGrad(panorama,d,dFdx(d)*u.projection.z,dFdy(d)*u.projection.z).rgb;
    if (u.light.w > 0.0) bg = mix(bg, textureLod(blurred,d,u.projection.w).rgb, u.light.w);
    vec3 color = max(bg*u.light.y*u.light.z,vec3(0));
    // Linear HDR, sem teto: o céu passa pela MESMA exposição e tone map dos
    // modelos no pós do grupo (reflexo e fundo casam). Codificado para o
    // resolve do MSAA (ver common/hdr.glsl).
    if (u.post.z > 0.5) {
        // Grupo SÓ de fundo (o panorama no fundo da pilha): um passe de tela
        // cheia, sem MSAA, sem profundidade e sem pós — a mesma exposição e o
        // mesmo tone map do grupo dos modelos, direto aqui.
        o_color=vec4(aurea_tonemap(color*u.post.x,u.post.y),1.0);
        o_scene=vec4(0.0,0.0,0.0,1.0);
        return;
    }
    o_color=vec4(0.0,0.0,0.0,1.0);
    o_scene=vec4(aurea_hdr_encode(color),1.0);
}
