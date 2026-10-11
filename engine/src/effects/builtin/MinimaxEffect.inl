// Included inside StylizeEffects.cpp's anonymous namespace.
// Exact integer-grid extrema. Square uses two axes; diamond uses the two
// parity classes of (x+y,x-y), avoiding a quadratic kernel or r passes.
class Minimax final : public Effect {
public:
    enum : u32 { kRadius = 0, kMode, kShape, kAmount, kChannel };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kMinimax, "Minimax", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const modes[] = {"Dilatar", "Erodir", "Abrir", "Fechar"};
        static const char* const shapes[] = {"Cruz", "Quadrado", "Losango"};
        static const char* const channels[] = {"Luz e transparência", "Transparência", "Cores", "Cores e transparência"};
        p.add_float("radius", "Raio", 4, 0, 120, kParamAnimatable | kParamPixels, "px");
        p.add_enum("mode", "Operação", modes, 4, 0);
        p.add_enum("shape", "Forma", shapes, 3, 1);
        p.add_float("amount", "Intensidade", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_enum("channel", "Comparar por", channels, 4, 0);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kRadius) < .5f || e.f(kAmount) < .01f; }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::max(0.f, e.f(kRadius)) * (e.e(kMode) >= 2 ? 2.f : 1.f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_minimax_frag, work));
    }
    struct Uniforms { EffectUniforms base; Vec4 otherUv; };
    static_assert(sizeof(Uniforms) == 128);

    // Crop only pixels that subsequent stages cannot read, on the source grid.
    // Intermediate axes retain the following axis' halo, including open/close.
    static Rect crop(const Rect& full, const LayerImage& grid, const EffectEval& e, f32 margin) {
        const Rect visible = spread_region(full, 0, 0, e.placement, margin);
        const f32 sx = grid.region.w / grid.width, sy = grid.region.h / grid.height;
        const f32 x0 = std::max(full.x, grid.region.x + std::floor((visible.x-grid.region.x)/sx)*sx);
        const f32 y0 = std::max(full.y, grid.region.y + std::floor((visible.y-grid.region.y)/sy)*sy);
        const f32 x1 = std::min(full.x+full.w, grid.region.x + std::ceil((visible.x+visible.w-grid.region.x)/sx)*sx);
        const f32 y1 = std::min(full.y+full.h, grid.region.y + std::ceil((visible.y+visible.h-grid.region.y)/sy)*sy);
        return {x0,y0,std::max(sx,x1-x0),std::max(sy,y1-y0)};
    }
    static Status pass(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& grid,
                       const LayerImage& input, const LayerImage& other, Rect region,
                       Vec4 kernel, Vec2 offset, u32 kind, bool dilate, f32 amount, LayerImage& out) {
        const f32 sx=grid.region.w/grid.width, sy=grid.region.h/grid.height;
        const f64 wf=std::round(region.w/sx), hf=std::round(region.h/sy);
        // Do not silently shrink the kernel/image when a backend texture limit
        // is reached. The graph reports the same resource failure as other FX.
        if (!std::isfinite(wf) || !std::isfinite(hf) || wf<1 || hf<1 ||
            wf>ctx.max_texture_size() || hf>ctx.max_texture_size()) return Errc::OutOfDeviceMemory;
        const u32 w=static_cast<u32>(wf),h=static_cast<u32>(hf);
        Uniforms u{};
        u.base.uvMap=EffectBuildContext::uv_map(region,input.region);
        u.otherUv=other.valid()?EffectBuildContext::uv_map(region,other.region):u.base.uvMap;
        u.base.texel={sx/input.region.w,sy/input.region.h,0,0};
        u.base.p0={static_cast<f32>(kind),dilate?1.f:0.f,static_cast<f32>(e.e(kChannel)),amount};
        u.base.p1=kernel;u.base.p2={offset.x,offset.y,0,0};
        out={ctx.texture("minimax",w,h),region,w,h};
        if(ctx.fullscreen_pass("minimax",PassStage::Effects,out.texture,ShaderId::effects_minimax_frag,
            {PassTexture{input.texture,{},CommonSampler::LinearBorder},
             PassTexture{other.valid()?other.texture:input.texture,{},CommonSampler::LinearBorder}},
             &u,sizeof(u))==kInvalidIndex) return Errc::PipelineCompileFailed;
        return OkStatus;
    }
    static Status axis(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& grid,
                       const LayerImage& input, Vec2 direction,i32 low,i32 high,Vec2 offset,
                       bool dilate,f32 margin,LayerImage& out) {
        const f32 sx=grid.region.w/grid.width,sy=grid.region.h/grid.height;
        const Vec2 a{offset.x+direction.x*low,offset.y+direction.y*low};
        const Vec2 b{offset.x+direction.x*high,offset.y+direction.y*high};
        Rect region{input.region.x-std::max(a.x,b.x)*sx,input.region.y-std::max(a.y,b.y)*sy,
                    input.region.w+std::fabs(b.x-a.x)*sx,input.region.h+std::fabs(b.y-a.y)*sy};
        region=crop(region,grid,e,margin);
        return pass(ctx,e,grid,input,{},region,{direction.x,direction.y,static_cast<f32>(low),static_cast<f32>(high)},offset,0,dilate,1,out);
    }
    static Status merge(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& grid,
                        const LayerImage& a,const LayerImage& b,bool dilate,f32 margin,LayerImage& out) {
        const f32 x=std::min(a.region.x,b.region.x),y=std::min(a.region.y,b.region.y);
        Rect region{x,y,std::max(a.region.x+a.region.w,b.region.x+b.region.w)-x,
                    std::max(a.region.y+a.region.h,b.region.y+b.region.h)-y};
        return pass(ctx,e,grid,a,b,crop(region,grid,e,margin),{},{},1,dilate,1,out);
    }
    static Status operation(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,
                            i32 radius,bool dilate,f32 margin,LayerImage& out) {
        const u32 shape=e.e(kShape);
        const f32 pixel=std::max(input.region.w/input.width,input.region.h/input.height);
        LayerImage a,b,c,d,winner;
        if(shape==1) {
            if(auto s=axis(ctx,e,input,input,{1,0},-radius,radius,{},dilate,margin+radius*pixel,a);!s.ok())return s;
            if(auto s=axis(ctx,e,input,a,{0,1},-radius,radius,{},dilate,margin,winner);!s.ok())return s;
        } else if(shape==0) {
            if(auto s=axis(ctx,e,input,input,{1,0},-radius,radius,{},dilate,margin,a);!s.ok())return s;
            if(auto s=axis(ctx,e,input,input,{0,1},-radius,radius,{},dilate,margin,b);!s.ok())return s;
            if(auto s=merge(ctx,e,input,a,b,dilate,margin,winner);!s.ok())return s;
        } else {
            const i32 half=radius/2;
            if(auto s=axis(ctx,e,input,input,{1,1},-half,half,{},dilate,margin+half*pixel,a);!s.ok())return s;
            if(auto s=axis(ctx,e,input,a,{1,-1},-half,half,{},dilate,margin,b);!s.ok())return s;
            const i32 low=(radius&1)?-half-1:-half,high=(radius&1)?half:half-1;
            if(low<=high) {
                if(auto s=axis(ctx,e,input,input,{1,1},low,high,{1,0},dilate,margin+std::max(-low,high)*pixel,c);!s.ok())return s;
                if(auto s=axis(ctx,e,input,c,{1,-1},low,high,{},dilate,margin,d);!s.ok())return s;
                if(auto s=merge(ctx,e,input,b,d,dilate,margin,winner);!s.ok())return s;
            } else winner=b;
        }
        // Center wins equal extrema, irrespective of parity/decomposition.
        return pass(ctx,e,input,input,winner,winner.region,{},{},2,dilate,1,out);
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,f32 margin,LayerImage& out) const override {
        if(!input.valid() || !(input.region.w>0) || !(input.region.h>0))return Errc::InvalidArgument;
        const f32 density=std::min(input.texel_scale_x(),input.texel_scale_y());
        const i32 radius=static_cast<i32>(std::floor(std::clamp(e.f(kRadius),0.f,120.f)*density+.5f));
        if(radius<=0){out=input;return OkStatus;}
        const u32 mode=e.e(kMode);LayerImage first,result;
        const f32 nextMargin=mode>=2?radius/density:0.f;
        if(auto s=operation(ctx,e,input,radius,mode==0||mode==3,margin+nextMargin,first);!s.ok())return s;
        if(mode>=2) {
            if(auto s=operation(ctx,e,first,radius,mode==2,margin,result);!s.ok())return s;
        } else result=first;
        const f32 amount=std::clamp(e.f(kAmount)*.01f,0.f,1.f);
        if(amount>=1){out=result;return OkStatus;}
        // Intensity blends the complete morphological operation once.
        return pass(ctx,e,input,input,result,result.region,{},{},3,true,amount,out);
    }
    bool demo_values(EffectInstance&,std::vector<ParamValue>& v)const noexcept override {
        v[kRadius]=ParamValue::scalar(9);v[kMode]=ParamValue::scalar(1);return true;
    }
};
