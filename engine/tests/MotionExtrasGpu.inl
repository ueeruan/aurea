// Included in test_gpu.cpp to use its real renderer/readback fixture.
#if defined(AUREA_TEST_VULKAN)
AUREA_TEST(MotionExtrasGpu, LinearGridAndCumulativeRepeatPlaceCopies) {
    AUREA_REQUIRE_GPU();
    for(const char* key:{"aurea.repeat.linear","aurea.repeat.grid","aurea.repeat.basic"}) {
        Scene s(96,96);const auto id=s.solid(12,12,{1,0,0,1},16,16);
        auto& fx=s.add_effect(id,key);fx.params[0].constant=ParamValue::scalar(3);
        fx.params[1].constant=ParamValue::vec2(32,32);
        fx.params[5].constant=ParamValue::scalar(2);
        const auto frame=s.render();
        AUREA_CHECK(frame.v(16,16).x>.95f);
        if(std::string_view(key)=="aurea.repeat.grid") {
            AUREA_CHECK(frame.v(48,16).x>.95f);AUREA_CHECK(frame.v(16,48).x>.95f);
            AUREA_CHECK(frame.v(48,48).x<.01f);
        } else { AUREA_CHECK(frame.v(48,48).x>.95f);AUREA_CHECK(frame.v(80,80).x>.95f); }
        AUREA_CHECK(frame.v(64,32).x<.01f);
    }
}
AUREA_TEST(MotionExtrasGpu, KeyersRemoveTheKeyAndPreserveForeground) {
    AUREA_REQUIRE_GPU();
    for(const char* key:{"aurea.key.chroma_basic","aurea.key.color_luma","aurea.key.chroma"}) {
        Scene s(64,64);auto px=uniform_image(64,64,0,255,0);
        for(u32 y=16;y<48;++y)for(u32 x=16;x<48;++x){auto i=(y*64+x)*4;px.rgba[i]=255;px.rgba[i+1]=0;}
        const auto id=s.image(std::move(px),32,32);auto& fx=s.add_effect(id,key);
        fx.params[0].constant=ParamValue::color(0,1,0,1);
        const auto frame=s.render();
        AUREA_CHECK(frame.v(8,8).y<.01f);AUREA_CHECK(frame.v(32,32).x>.9f);
    }
}
AUREA_TEST(MotionExtrasGpu, RepeatAlongPathFollowsTheUnderlyingGuideInLayerSpace) {
    AUREA_REQUIRE_GPU();
    Scene s(96,96);s.solid(60,60,{0,0,0,1},48,48);
    const auto id=s.solid(8,8,{1,0,0,1},48,48);
    auto& fx=s.add_effect(id,"aurea.repeat.path");fx.params[0].constant=ParamValue::scalar(4);
    const auto frame=s.render();
    for(auto p:{Vec2{18,18},Vec2{78,18},Vec2{78,78},Vec2{18,78}})
        AUREA_CHECK(frame.v(u32(p.x),u32(p.y)).x>.95f);
    AUREA_CHECK(frame.v(48,48).x<.01f);
}
AUREA_TEST(MotionExtrasGpu, SolidMatteAndOffsetUseLayerPixels) {
    AUREA_REQUIRE_GPU();
    {
        Scene s(64,64);auto px=uniform_image(64,64,255,0,0,0);
        for(u32 y=16;y<48;++y)for(u32 x=16;x<48;++x)px.rgba[(y*64+x)*4+3]=255;
        const auto id=s.image(std::move(px),32,32);auto& fx=s.add_effect(id,"aurea.key.solid_matte");
        fx.params[0].constant=ParamValue::color(0,0,1,1);
        const auto frame=s.render();AUREA_CHECK(frame.v(8,8).z>.95f);AUREA_CHECK(frame.v(32,32).x>.95f);
    }
    {
        Scene s(64,64);const auto id=s.image(quadrants(64,64),32,32);
        const auto before=s.render();auto& fx=s.add_effect(id,"aurea.transform.offset");
        fx.params[0].constant=ParamValue::vec2(32,0);const auto after=s.render();
        AUREA_CHECK(near4(before.v(8,8),after.v(40,8),.01f));
        AUREA_CHECK(near4(before.v(40,40),after.v(8,40),.01f));
    }
}
AUREA_TEST(MotionExtrasGpu, EveryNewEffectRendersFinitePixelsAndExportMatchesPreview) {
    AUREA_REQUIRE_GPU();
    for(const char* key:{"aurea.motion.blink","aurea.motion.flicker","aurea.motion.pulse_size",
        "aurea.motion.random_displacement","aurea.motion.random_jitter","aurea.motion.swing_range","aurea.motion.spin",
        "aurea.transform.stretch_axis","aurea.transform.scale_assist","aurea.transform.raster","aurea.distort.squeeze",
        "aurea.distort.fisheye","aurea.repeat.radial","aurea.repeat.scatter","aurea.repeat.path","aurea.key.matte_choker"}) {
        Scene s(96,96);const auto id=s.image(quadrants(64,64),48,48);s.add_effect(id,key);
        const auto preview=s.render(FrameIndex{11});const auto final=s.render(FrameIndex{11},1,true);
        bool finite=true;float difference=0;
        for(usize i=0;i<preview.px.size();++i){finite &= std::isfinite(preview.px[i])&&std::isfinite(final.px[i]);difference=std::max(difference,std::abs(preview.px[i]-final.px[i]));}
        AUREA_CHECK_MSG(finite,key);AUREA_CHECK_MSG(difference<.015f,key);
    }
}
AUREA_TEST(MotionExtrasGpu, LensZeroIsIdentityAndStrengthChangesImage) {
    AUREA_REQUIRE_GPU();
    for(const char* key:{"aurea.distort.squeeze","aurea.distort.fisheye"}) {
        Scene s(96,96);const auto id=s.image(quadrants(64,64),48,48);const auto before=s.render();
        auto& fx=s.add_effect(id,key);fx.params[0].constant=ParamValue::scalar(0);const auto zero=s.render();
        fx.params[0].constant=ParamValue::scalar(70);const auto warped=s.render();
        float identityError=0,change=0;
        for(usize i=0;i<before.px.size();++i){identityError=std::max(identityError,std::abs(before.px[i]-zero.px[i]));change+=std::abs(before.px[i]-warped.px[i]);}
        AUREA_CHECK(identityError<.01f);AUREA_CHECK(change>50);
    }
}
#endif
