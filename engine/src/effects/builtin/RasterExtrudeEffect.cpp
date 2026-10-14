#include "BuiltinEffects.hpp"
#include "aurea/effects/RasterExtrude.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::raster_extrude {
namespace {
f32 finite(f32 x, f32 fallback) noexcept { return std::isfinite(x)?x:fallback; }
bool inverse(const Mat4& matrix, Mat4& result) noexcept {
    f64 a[4][8]{};
    for(u32 row=0;row<4;++row)for(u32 col=0;col<4;++col) {
        const Vec4& v=matrix.col[col];
        a[row][col]=row==0?v.x:row==1?v.y:row==2?v.z:v.w;
        a[row][col+4]=row==col?1:0;
    }
    for(u32 col=0;col<4;++col) {
        u32 pivot=col;
        for(u32 row=col+1;row<4;++row)if(std::abs(a[row][col])>std::abs(a[pivot][col]))pivot=row;
        if(!std::isfinite(a[pivot][col])||std::abs(a[pivot][col])<1e-14)return false;
        if(pivot!=col)for(u32 k=0;k<8;++k)std::swap(a[col][k],a[pivot][k]);
        const f64 divisor=a[col][col];
        for(u32 k=0;k<8;++k)a[col][k]/=divisor;
        for(u32 row=0;row<4;++row)if(row!=col) {
            const f64 scale=a[row][col];
            for(u32 k=0;k<8;++k)a[row][k]-=scale*a[col][k];
        }
    }
    for(u32 col=0;col<4;++col) {
        for(u32 row=0;row<4;++row)if(!std::isfinite(a[row][col+4])||std::abs(a[row][col+4])>1e20)return false;
        result.col[col]={static_cast<f32>(a[0][col+4]),static_cast<f32>(a[1][col+4]),static_cast<f32>(a[2][col+4]),static_cast<f32>(a[3][col+4])};
    }
    return true;
}
}
bool accepts(LayerKind kind) noexcept {
    return kind==LayerKind::Text||kind==LayerKind::Shape||kind==LayerKind::Image
        ||kind==LayerKind::Video||kind==LayerKind::Composition;
}
bool depth_parameter(EffectTypeId type,u32 index) noexcept {
    return (type==effect_type_id(effect_keys::kRasterExtrude)&&index==kDepth)
        ||(type==effect_type_id(effect_keys::kStrokeOutline)&&index==kStrokeDepth);
}
Options options_from(const EffectEval& e) noexcept {
    Options result;
    if(e.effect->type_id()==effect_type_id(effect_keys::kRasterExtrude)) {
        result.depth=std::clamp(finite(e.f(kDepth),0),0.f,kMaxDepth);
        result.sideColor=e.color(kSideColor);
        result.shading=std::clamp(finite(e.f(kShading),.7f),0.f,1.f);
        result.quality=std::min(e.e(kQuality),2u);
        // Instância salva antes destes slots: sem reflexo (o visual de antes).
        if(e.count>kReflection)result.reflection=std::clamp(finite(e.f(kReflection),0),0.f,1.f);
        if(e.count>kMetal)result.metal=std::clamp(finite(e.f(kMetal),0),0.f,1.f);
        if(e.count>kRoughness)result.roughness=std::clamp(finite(e.f(kRoughness),.3f),0.f,1.f);
    } else if(e.effect->type_id()==effect_type_id(effect_keys::kStrokeOutline)) {
        result.depth=e.count>kStrokeDepth?std::clamp(finite(e.f(kStrokeDepth),0),0.f,kMaxDepth):0.f;
        result.sideColor=e.color(1);
    }
    for(f32* v:{&result.sideColor.x,&result.sideColor.y,&result.sideColor.z,&result.sideColor.w})*v=std::clamp(finite(*v,0),0.f,1.f);
    return result;
}
bool update_projection(Volume& volume,const Mat4& clipFromLayer,const Mat4& clipFromWorld,Mat4* worldFromLayer) noexcept {
    if(!inverse(clipFromLayer,volume.layerFromClip))return false;
    volume.layerFromWorld=volume.layerFromClip*clipFromWorld;
    if(worldFromLayer && !inverse(volume.layerFromWorld,*worldFromLayer))return false;
    return true;
}
bool build_volume(EffectBuildContext& ctx,const LayerImage& input,const Options& options,
    const Mat4& clipFromLayer,const Mat4& worldFromLayer,Volume& out) noexcept {
    out={};
    if(!input.valid()||!(options.depth>0)||input.width>8192||input.height>8192
        ||!(input.region.w>0&&input.region.h>0))return false;
    // Zero-scale/singular layers are empty geometry, not pending resources.
    // They must not leave an export waiting for a frame that cannot exist.
    if(!inverse(clipFromLayer,out.layerFromClip)||!inverse(worldFromLayer,out.layerFromWorld))return true;
    out.box={input.region.x,input.region.y,input.region.w,input.region.h};
    const u32 steps[3]={64,192,512};
    out.params={std::clamp(options.depth,0.f,kMaxDepth),options.shading,static_cast<f32>(steps[std::min(options.quality,2u)]),0};
    out.sideColor=options.sideColor;
    out.material={options.reflection,options.metal,options.roughness,0};
    std::array<FGTexture,14> levels{};levels[0]=input.texture;
    u32 count=1,w=input.width,h=input.height;u64 pixels=0;
    while((w>1||h>1)&&count<levels.size()) {
        const u32 nw=std::max(1u,(w+1)/2),nh=std::max(1u,(h+1)/2);
        pixels+=static_cast<u64>(nw)*nh;
        if(pixels>static_cast<u64>(input.width)*input.height+14)return false;
        levels[count]=ctx.texture("raster-extrude-alpha-max",nw,nh);
        const Vec4 size{static_cast<f32>(w),static_cast<f32>(h),0,0};
        if(ctx.fullscreen_pass("raster-extrude-alpha-max",PassStage::Effects,levels[count],ShaderId::effects_raster_alpha_max_frag,
            {PassTexture{levels[count-1],{},CommonSampler::NearestClamp}},&size,sizeof(size))==kInvalidIndex)return false;
        ++count;w=nw;h=nh;
    }
    if(w!=1||h!=1)return false;
    out.count=std::min(count,kTextureSlots);
    for(u32 k=0;k<out.count;++k)out.textures[k]=levels[k];
    if(count>kTextureSlots)out.textures[kTextureSlots-1]=levels[count-1];
    for(u32 k=out.count;k<kTextureSlots;++k)out.textures[k]=out.textures[out.count-1];
    out.sizes={static_cast<f32>(input.width),static_cast<f32>(input.height),static_cast<f32>(count-1),static_cast<f32>(out.count)};
    return true;
}
} // namespace aurea::raster_extrude

namespace aurea::builtin {
namespace {
class RasterExtrude final:public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{effect_keys::kRasterExtrude,"Extrusão raster","3D",EffectClass::Domain};return info;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("depth","Profundidade",32,0,512,kParamAnimatable|kParamPixels,"px");p.typed_range(0,4096);
        p.add_color("side_color","Cor lateral",{.45f,.45f,.45f,1});
        p.add_float("shading","Sombreamento",.7f,0,1,kParamAnimatable);
        static const char* const qualities[]={"Baixa","Média","Alta"};
        p.add_enum("quality","Qualidade",qualities,3,1);
        // Como nos modelos 3D: o HDRI/estúdio da cena reflete na face e nas
        // laterais. Projeto salvo sem o slot fica sem reflexo (visual de antes).
        p.add_float("reflection","Reflexo do ambiente",1,0,1,kParamAnimatable|kParamLegacyZero);
        p.add_float("metal","Metal",0,0,1,kParamAnimatable);
        p.add_float("roughness","Rugosidade",.3f,0,1,kParamAnimatable);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !(e.f(0)>0); }
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat f) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_raster_alpha_max_frag,f));
    }
    Status build(EffectBuildContext&,const EffectEval&,const LayerImage& in,f32,LayerImage& out) const override {
        // The entire raster effect stack supplies the face/alpha. The scene
        // renderer creates a volume from that final image, once per layer.
        out=in;return OkStatus;
    }
};
}
void register_raster_extrude(EffectRegistry& r) { (void)r.add(std::make_unique<RasterExtrude>()); }
} // namespace aurea::builtin
