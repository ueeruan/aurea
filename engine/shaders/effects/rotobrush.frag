#version 450
#include "../common/bindings.glsl"
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D u_tex0;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D u_tex1;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap;vec4 texel;vec4 p0;vec4 p1;vec4 p2;vec4 p3;vec4 color;
} p;
// p0: limite, largura da borda, inverter, mistura
// p1: mostrar máscara, tem máscara, modo de vista (0 final, 1 máscara, 2 sobreposição), contraste
// p2: região da entrada; p3: tamanho da camada, suavizar (texels da máscara), descontaminar
float maskAt(vec2 uv){return texture(u_tex1,clamp(uv,vec2(0),vec2(1))).r;}
void main(){
    vec4 base=texture(u_tex0,v_uv*p.uvMap.xy+p.uvMap.zw);
    if(p.p1.y<.5){o_color=base;return;}
    vec2 point=p.p2.xy+v_uv*p.p2.zw;
    vec2 uv=point/max(p.p3.xy,vec2(1));
    vec2 ts=vec2(1.0)/vec2(textureSize(u_tex1,0));
    float raw=maskAt(uv);
    if(p.p3.z>0.){
        // Suavizar (Refine Edge "Smooth"): média 3x3 no raio pedido.
        float s=0.;vec2 r=ts*p.p3.z;
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)s+=maskAt(uv+vec2(float(x),float(y))*r);
        raw=mix(raw,s/9.,.85);
    }
    // Contraste: a transição da borda fica mais curta.
    float w=max(.001,p.p0.y*(1.-.95*clamp(p.p1.w,0.,1.)));
    float a=smoothstep(p.p0.x-w,p.p0.x+w,raw);
    if(p.p0.z>.5)a=1.-a;
    if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))a=0.;
    vec4 src=base;
    if(p.p3.w>0.&&a>0.&&a<1.){
        // Descontaminar cores da borda: a cor de dentro do objeto, dois
        // texels da máscara para dentro, no lugar da mistura com o fundo.
        vec2 g=vec2(maskAt(uv+vec2(ts.x,0.))-maskAt(uv-vec2(ts.x,0.)),maskAt(uv+vec2(0.,ts.y))-maskAt(uv-vec2(0.,ts.y)));
        if(p.p0.z>.5)g=-g;
        if(dot(g,g)>1e-8){
            vec2 dv=normalize(g)*ts*2.*max(p.p3.xy,vec2(1))/max(p.p2.zw,vec2(1e-5));
            vec4 inner=texture(u_tex0,(v_uv+dv)*p.uvMap.xy+p.uvMap.zw);
            if(inner.a>.001)src.rgb=mix(base.rgb,inner.rgb/inner.a*base.a,clamp(p.p3.w,0.,1.)*(1.-a));
        }
    }
    vec4 cut=src*a;
    if(p.p1.x>.5||(p.p1.z>.5&&p.p1.z<1.5))cut=vec4(vec3(a)*base.a,base.a);
    if(p.p1.z>1.5){
        // Sobreposição (modo de edição): objeto como está, fundo tingido de vermelho.
        vec3 tint=mix(base.rgb,vec3(base.a,0.,0.),.55);
        cut=vec4(mix(tint,base.rgb,a),base.a);
    }
    o_color=mix(base,cut,clamp(p.p0.w,0.,1.));
}
