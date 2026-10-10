namespace rays_footprint_reference {
struct CapabilityScope {
    GPUCapabilities& caps;
    float saved;
    explicit CapabilityScope(bool direct):caps(const_cast<GPUCapabilities&>(gpu().backend.capabilities())),saved(caps.maxSamplerAnisotropy) {
        if(direct)caps.maxSamplerAnisotropy=1;
    }
    ~CapabilityScope(){caps.maxSamplerAnisotropy=saved;}
};
struct Field {
    u32 w,h;std::vector<Vec3> radiance;
    explicit Field(const FloatImage& source,float threshold,float knee):w(source.width),h(source.height),radiance(static_cast<usize>(w)*h) {
        for(u32 y=0;y<h;++y)for(u32 x=0;x<w;++x) {
            const auto p=source.v(x,y);
            const float alpha=p.w;
            const Vec3 rgb=alpha>1e-6f?Vec3{std::max(0.f,p.x/alpha),std::max(0.f,p.y/alpha),std::max(0.f,p.z/alpha)}:Vec3{};
            auto srgb=[](double c){return c<=.0031308?12.92*c:1.055*std::pow(c,1./2.4)-.055;};
            const double luma=.2126*srgb(rgb.x)+.7152*srgb(rgb.y)+.0722*srgb(rgb.z);
            const double t=std::clamp((luma-(threshold-knee))/(2*knee),0.,1.);
            radiance[static_cast<usize>(y)*w+x]=rgb*static_cast<float>(t*t*(3-2*t)*alpha);
        }
    }
    Vec3 at(int x,int y)const{return x>=0&&y>=0&&x<static_cast<int>(w)&&y<static_cast<int>(h)?radiance[static_cast<usize>(y)*w+x]:Vec3{};}
    Vec3 sample(double x,double y)const {
        const double px=x-.5,py=y-.5;const int ix=static_cast<int>(std::floor(px)),iy=static_cast<int>(std::floor(py));
        const float fx=static_cast<float>(px-ix),fy=static_cast<float>(py-iy);
        return (at(ix,iy)*(1-fx)+at(ix+1,iy)*fx)*(1-fy)+(at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*fx)*fy;
    }
    // Independent dense midpoint quadrature: no GPU footprint/subdivision,
    // sampler capability, segment clipping or padded texture mapping reused.
    Vec3 ray(double x,double y,Vec2 center,u32 count,float decay,float intensity)const {
        constexpr u32 dense=256;
        const double dx=(center.x*w-x)*.9/count,dy=(center.y*h-y)*.9/count;
        Vec3 sum{};double weights=0,weight=1;
        for(u32 i=0;i<count;++i) {
            Vec3 segment{};
            for(u32 j=0;j<dense;++j) {
                const double t=i+(j+.5)/dense;
                segment=segment+sample(x+dx*t,y+dy*t);
            }
            sum=sum+segment*static_cast<float>(weight/dense);weights+=weight;weight*=1-decay*.35;
        }
        return sum*static_cast<float>(intensity/weights);
    }
};
void configure(EffectInstance& fx,Vec2 center,u32 n,float decay,float threshold) {
    fx.params[0].constant=ParamValue::scalar(8);fx.params[1].constant=ParamValue::scalar(90);
    fx.params[2].constant=ParamValue::scalar(threshold*100);fx.params[3].constant=ParamValue::scalar(decay*100);
    fx.params[4].constant=ParamValue::vec2(center.x,center.y);fx.params[5].constant=ParamValue::scalar(static_cast<float>(n));
    fx.params[6].constant=ParamValue::scalar(1);fx.params[7].constant=ParamValue::boolean(true);
}
void compare(const FloatImage& actual,const FloatImage& source,const Field& field,Vec2 center,u32 n,float decay,const char* name) {
    double gpuEnergy=0,referenceEnergy=0,maxError=0;u32 expectedLit=0,missed=0;
    // Regular positions both near and far from the light, several beam angles.
    for(u32 y=5;y+5<actual.height;y+=7)for(u32 x=5;x+5<actual.width;x+=5) {
        const auto a=actual.v(x,y),base=source.v(x,y);
        const Vec3 ray=field.ray(x+.5,y+.5,center,n,decay,8);
        const double expected=ray.x;
        const double observed=a.x-base.x;
        maxError=std::max(maxError,std::fabs(observed-expected));
        gpuEnergy+=std::max(0.,observed);referenceEnergy+=expected;
        if(expected>.004){++expectedLit;if(observed<expected*.2)++missed;}
        AUREA_CHECK(std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z)&&std::isfinite(a.w));
        const float coverage=std::clamp(std::max({ray.x,ray.y,ray.z}),0.f,1.f);
        AUREA_CHECK(std::fabs(a.w-(base.w+coverage*(1-base.w)))<.035f);
    }
    std::printf("    Rays %s N%u density%ux%u CPUdense: max=%.7f energy=%.7f/%.7f missing=%u/%u cap=%.1f\n",name,n,actual.width,actual.height,maxError,gpuEnergy,referenceEnergy,missed,expectedLit,gpu().backend.capabilities().maxSamplerAnisotropy);
    AUREA_CHECK(expectedLit>10);AUREA_CHECK_EQ(missed,0u);
    AUREA_CHECK(referenceEnergy>0);AUREA_CHECK(std::fabs(gpuEnergy/referenceEnergy-1)<.12);
    AUREA_CHECK(maxError<.035); // HDR fixture below has an independent larger bound.
}
}

AUREA_TEST(RaysFootprintGpu, NativeAndForcedDirectIntegrateThinEmitterAcrossAngles) {
    AUREA_REQUIRE_GPU();using namespace rays_footprint_reference;
    for(bool direct:{false,true}) {
        CapabilityScope capabilities(direct);
        Scene scene(256,144);scene.comp->set_transparent_background(true);
        auto pixels=uniform_image(256,144,0,0,0,255);
        for(u32 y=16;y<128;++y){auto* p=&pixels.rgba[(y*256+132)*4];p[0]=p[1]=p[2]=255;}
        const auto layer=scene.image(std::move(pixels),128,72);
        auto& fx=scene.add_effect(layer,effect_keys::kRays);fx.enabled=false;
        const auto source=scene.render(FrameIndex{0},1,true);fx.enabled=true;
        Field field(source,.0f,.01f);const Vec2 center{.1f,.3f};
        for(u32 count:{8u,32u,64u})for(float decay:{0.f,.65f}) {
            configure(fx,center,count,decay,0);
            compare(scene.render(FrameIndex{0},1,true),source,field,center,count,decay,direct?"direct":"anisotropic");
        }
    }
}

AUREA_TEST(RaysFootprintGpu, HdrAlphaThresholdAndHalfDensityUseNativeInputGrid) {
    AUREA_REQUIRE_GPU();using namespace rays_footprint_reference;
    for(bool direct:{false,true})for(u32 den:{1u,2u}) {
        CapabilityScope capabilities(direct);
        Scene scene(128,96);scene.comp->set_transparent_background(true);
        auto pixels=uniform_image(128,96,0,0,0,0);
        // Horizontal and diagonal thin emitters; hidden bright transparent RGB
        // must not emit, and threshold must precede spatial interpolation.
        for(u32 x=12;x<116;++x){auto* p=&pixels.rgba[(52*128+x)*4];p[0]=255;p[1]=128;p[2]=64;p[3]=128;}
        for(u32 x=0;x<128;++x){auto* p=&pixels.rgba[(12*128+x)*4];p[0]=p[1]=p[2]=255;}
        const auto layer=scene.image(std::move(pixels),64,48);
        auto& exposure=scene.add_effect(layer,effect_keys::kExposure);exposure.params[0].constant=ParamValue::scalar(3);
        auto& fx=scene.add_effect(layer,effect_keys::kRays);fx.enabled=false;
        const auto source=scene.render(FrameIndex{0},den,true);fx.enabled=true;
        const Vec2 center{.7f,.1f};Field field(source,.7f,.01f);
        configure(fx,center,32,.65f,.7f);
        const auto actual=scene.render(FrameIndex{0},den,true);
        double actualEnergy=0,expectedEnergy=0,maxError=0;u32 checks=0;
        for(u32 y=1;y+1<actual.height;y+=3)for(u32 x=1;x+1<actual.width;x+=3) {
            const auto a=actual.v(x,y),base=source.v(x,y);const auto ray=field.ray(x+.5,y+.5,center,32,.65f,8);
            maxError=std::max(maxError,static_cast<double>(std::fabs(a.x-base.x-ray.x)));
            actualEnergy+=std::max(0.f,a.x-base.x);expectedEnergy+=ray.x;++checks;
            const float alpha=base.w+std::clamp(std::max({ray.x,ray.y,ray.z}),0.f,1.f)*(1-base.w);
            AUREA_CHECK(std::fabs(a.w-alpha)<.05f);
        }
        std::printf("    Rays HDR alpha den%u direct%d max=%.7f energy=%.7f/%.7f checks=%u\n",den,direct,maxError,actualEnergy,expectedEnergy,checks);
        AUREA_CHECK(expectedEnergy>0);AUREA_CHECK(std::fabs(actualEnergy/expectedEnergy-1)<.12);AUREA_CHECK(maxError<.12);
    }
}
