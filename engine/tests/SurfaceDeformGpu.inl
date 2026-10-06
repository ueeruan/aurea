namespace {
f32 surface_difference(const FloatImage& a,const FloatImage& b) {
    f32 result=0;
    for(usize i=0;i<std::min(a.px.size(),b.px.size());++i)result=std::max(result,std::fabs(a.px[i]-b.px[i]));
    return result;
}
}

AUREA_TEST(SurfaceDeformGpu, ZeroMixAnimationAndTransparentInputAreStable) {
    AUREA_REQUIRE_GPU();
    for(u32 mode=0;mode<3;++mode) {
        const char* keys[]{"aurea.distort.bender","aurea.distort.bend","aurea.distort.curl"};
        Scene scene(192,144);
        scene.comp->set_transparent_background(true);
        auto source=reference_image(96,64);
        // A hole must stay a hole even when a back material is illuminated.
        for(u32 y=22;y<42;++y)for(u32 x=38;x<58;++x) {
            const usize at=(usize(y)*source.width+x)*4;
            for(u32 c=0;c<4;++c)source.rgba[at+c]=0;
        }
        const auto id=scene.image(std::move(source),96,72);
        const auto original=scene.render();
        auto& fx=scene.add_effect(id,keys[mode]);
        AUREA_CHECK_NEAR(surface_difference(original,scene.render()),0,0);
        fx.params[0].constant.v[0]=mode==0?20.f:mode==1?125.f:145.f;
        if(mode==2)fx.params[1].constant.v[0]=12;
        const auto changed=scene.render();
        AUREA_CHECK(surface_difference(original,changed)>.1f);
        u32 visible=0,transparent=0,invalid=0;
        for(usize i=0;i<changed.px.size();i+=4) {
            const f32 a=changed.px[i+3];
            invalid+=!std::isfinite(a)||a<-.001f||a>1.001f;
            for(u32 c=0;c<3;++c)invalid+=!std::isfinite(changed.px[i+c])||changed.px[i+c]<-.001f||changed.px[i+c]>a+.005f;
            visible+=a>.01f;transparent+=a<.001f;
        }
        AUREA_CHECK_EQ(invalid,0u);AUREA_CHECK(visible>100u);AUREA_CHECK(transparent>1000u);
        const u32 mix=mode==0?3:mode==1?5:6;
        fx.params[mix].constant.v[0]=0;
        AUREA_CHECK_NEAR(surface_difference(original,scene.render()),0,0);
        fx.params[mix].constant.v[0]=100;
        scene.comp->layer(id)->tracks.get_or_create(TrackProperty::EffectParam,fx.id,param_track_key(0,0)).set(FrameIndex{0},0);
        scene.comp->layer(id)->tracks.get_or_create(TrackProperty::EffectParam,fx.id,param_track_key(0,0)).set(FrameIndex{10},mode==0?20.f:125.f);
        AUREA_CHECK_NEAR(surface_difference(original,scene.render(FrameIndex{0})),0,0);
        AUREA_CHECK(surface_difference(original,scene.render(FrameIndex{10}))>.1f);
    }
}

AUREA_TEST(SurfaceDeformGpu, CurveKeepsItsEndpointsAndBendHasAColoredTransparentBack) {
    AUREA_REQUIRE_GPU();
    {
        Scene scene(192,144);
        auto gradient=uniform_image(96,64,0,0,80);
        for(u32 y=0;y<64;++y)for(u32 x=0;x<96;++x) {
            gradient.rgba[(usize(y)*96+x)*4]=static_cast<u8>(x*255/95);
            gradient.rgba[(usize(y)*96+x)*4+1]=static_cast<u8>(y*255/63);
        }
        // Smooth coordinate ramp separates the geometric displacement from
        // the expected antialiasing of high-frequency detail on the curve.
        const auto id=scene.image(std::move(gradient),96,72);
        const auto original=scene.render();
        auto& fx=scene.add_effect(id,"aurea.distort.bender");
        fx.params[0].constant.v[0]=20;
        fx.params[1].constant=ParamValue::vec2(.125f,.5f);
        fx.params[2].constant=ParamValue::vec2(.875f,.5f);
        const auto result=scene.render();
        for(u32 x:{54u,138u}) for(u32 y:{60u,72u,80u}) for(u32 c=0;c<3;++c)
            AUREA_CHECK_NEAR(result.at(x,y)[c],original.at(x,y)[c],.015f);
        for(u32 c=0;c<3;++c)AUREA_CHECK_NEAR(result.at(96,92)[c],original.at(96,72)[c],.04f);
        fx.params[2].constant=fx.params[1].constant;
        AUREA_CHECK_NEAR(surface_difference(original,scene.render()),0,0);
    }
    {
        Scene scene(192,144);scene.comp->set_transparent_background(true);
        auto image=uniform_image(96,64,255,255,255);
        // Only the right half exists, so a disappearing folded back cannot be
        // hidden by an opaque stationary front.
        for(u32 y=0;y<64;++y)for(u32 x=0;x<48;++x)for(u32 c=0;c<4;++c)image.rgba[(usize(y)*96+x)*4+c]=0;
        const auto id=scene.image(std::move(image),96,72);
        auto& fx=scene.add_effect(id,"aurea.distort.bend");
        fx.params[0].constant.v[0]=180;fx.params[4].constant.v[0]=0;
        fx.params[3].constant=ParamValue::color(.5f,.5f,.5f,.5f);
        const auto result=scene.render();
        for(u32 c=0;c<3;++c)AUREA_CHECK_NEAR(result.at(80,72)[c],.5f*Color::srgb_to_linear(.5f),.003f);
        AUREA_CHECK_NEAR(result.at(80,72)[3],.5f,.003f);
        fx.params[3].constant=ParamValue::color(1,1,1,0);
        AUREA_CHECK_NEAR(scene.render().at(80,72)[3],0,.001f);
    }
}

AUREA_TEST(SurfaceDeformGpu, TileAndColorChainsMatchPreviewAndExportWithSmallRadii) {
    AUREA_REQUIRE_GPU();
    const char* keys[]{"aurea.distort.bender","aurea.distort.bend","aurea.distort.curl"};
    for(u32 mode=0;mode<3;++mode)for(bool tileFirst:{false,true}) {
        Scene scene(160,90);
        const auto id=scene.image(reference_image(160,90),80,45);
        auto tile=[&]{auto& fx=scene.add_effect(id,effect_keys::kMotionTile);fx.params[motion_tile::kTileWidth].constant.v[0]=50;fx.params[motion_tile::kTileHeight].constant.v[0]=50;fx.params[motion_tile::kMirror].constant.v[0]=1;};
        if(tileFirst)tile();
        auto& fx=scene.add_effect(id,keys[mode]);fx.params[0].constant.v[0]=mode==0?12.f:90.f;
        if(mode==2)fx.params[1].constant.v[0]=1;
        if(mode>0)fx.params[mode==1?2:3].constant.v[0]=37;
        if(!tileFirst)tile();
        scene.add_effect(id,effect_keys::kExposure).params[0].constant.v[0]=-.5f;
        const auto preview=scene.render(FrameIndex{4});
        const auto final=scene.render(FrameIndex{4},1,true);
        AUREA_CHECK_NEAR(surface_difference(preview,final),0,.001f);
        u32 lit=0,finite=0;for(usize i=0;i<final.px.size();i+=4){lit+=final.px[i]+final.px[i+1]+final.px[i+2]>.01f;finite+=std::isfinite(final.px[i]);}
        AUREA_CHECK(lit>100u);AUREA_CHECK_EQ(finite,final.width*final.height);
        AUREA_CHECK_NEAR(surface_difference(final,scene.render(FrameIndex{4},1,true)),0,0);
    }
}
